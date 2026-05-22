#!/usr/bin/env bash
# ----------------------------------------------------------------------------
#  install-plugins.sh
#  Fetches CROSSx Unreal plugins listed in crossx-plugins.json from GitHub
#  Releases and unpacks them into ./Plugins/.
#
#  Modes:
#    (default)   Install / reconcile to match crossx-plugins.json
#    --verify    Exit 1 if installed .uplugin VersionName does not match lock
#    --force     Force re-download even if sha256 matches
#
#  Requirements: bash >=3.2, curl, jq, unzip, shasum OR sha256sum
#  Env: GITHUB_TOKEN  (OPTIONAL — only needed when pointing the registry at a
#                     private repo. Public release assets are downloaded without
#                     using the GitHub REST API.)
# ----------------------------------------------------------------------------
set -euo pipefail

MODE="install"
FORCE="false"
for arg in "$@"; do
    case "$arg" in
        --verify) MODE="verify" ;;
        --force)  FORCE="true" ;;
        -h|--help)
            sed -n '2,16p' "$0"; exit 0 ;;
        *) echo "unknown arg: $arg" >&2; exit 2 ;;
    esac
done

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MANIFEST="$PROJECT_ROOT/crossx-plugins.json"
LOCK="$PROJECT_ROOT/crossx-plugins.lock.json"
PLUGINS_DIR="$PROJECT_ROOT/Plugins"

# ---------- sanity checks ---------------------------------------------------

for bin in curl jq unzip; do
    command -v "$bin" >/dev/null || { echo "[err] '$bin' is required" >&2; exit 1; }
done

if command -v shasum >/dev/null; then
    SHA_CMD="shasum -a 256"
elif command -v sha256sum >/dev/null; then
    SHA_CMD="sha256sum"
else
    echo "[err] shasum or sha256sum is required" >&2; exit 1
fi

[[ -f "$MANIFEST" ]] || { echo "[err] manifest not found: $MANIFEST" >&2; exit 1; }
[[ -f "$LOCK"     ]] || printf '{\n  "plugins": {}\n}\n' > "$LOCK"

# GITHUB_TOKEN is optional. The default public registry downloads release assets
# directly from github.com, avoiding the GitHub REST API's anonymous rate limit.
# We attach Authorization only when a token is present, mainly for private
# registry repos.
#
# Keep this as a wrapper instead of an optional bash array: macOS still ships
# bash 3.2, where expanding an empty array under `set -u` can fail with
# "unbound variable".
github_curl() {
    if [[ -n "${GITHUB_TOKEN:-}" ]]; then
        curl -H "Authorization: Bearer ${GITHUB_TOKEN}" "$@"
    else
        curl "$@"
    fi
}

OWNER=$(jq -r '.registry.owner' "$MANIFEST")
REPO=$( jq -r '.registry.repo'  "$MANIFEST")
REG_TYPE=$(jq -r '.registry.type' "$MANIFEST")
[[ "$REG_TYPE" == "github-releases" ]] || {
    echo "[err] unsupported registry.type: $REG_TYPE" >&2; exit 1; }

API="https://api.github.com/repos/${OWNER}/${REPO}"

QUEUE="$(mktemp -t crossx-plugin-queue-XXXXXX)"
DESIRED="$(mktemp -t crossx-plugin-desired-XXXXXX)"
PROCESSED="$(mktemp -t crossx-plugin-processed-XXXXXX)"
cleanup() { rm -f "$QUEUE" "$DESIRED" "$PROCESSED"; }
trap cleanup EXIT

# ---------- helpers ---------------------------------------------------------

sha256_of() { $SHA_CMD "$1" | awk '{print $1}'; }

# $1=name  $2=version
plugin_tag()   { printf '%s@v%s' "$1" "$2"; }
# $1=name  $2=version
plugin_asset() { printf '%s-%s.zip' "$1" "$2"; }
# $1=tag   $2=asset
plugin_download_url() {
    printf 'https://github.com/%s/%s/releases/download/%s/%s' "$OWNER" "$REPO" "$1" "$2"
}

# $1=name  $2=version  $3=source label
enqueue_plugin() {
    name="$1"
    version="$2"
    source_label="$3"

    existing=$(awk -F '\t' -v n="$name" '$1 == n { print $2; exit }' "$DESIRED")
    if [[ -n "$existing" ]]; then
        if [[ "$existing" != "$version" ]]; then
            echo "[err] $name: version conflict ($existing vs $version from $source_label)" >&2
            exit 1
        fi
        return
    fi

    printf '%s\t%s\n' "$name" "$version" >> "$DESIRED"
    printf '%s\t%s\n' "$name" "$version" >> "$QUEUE"
}

# $1=uplugin path  $2=source plugin name
enqueue_crossx_dependencies() {
    uplugin="$1"
    source_name="$2"
    [[ -f "$uplugin" ]] || return

    jq -r '.CrossxDependencies // {} | to_entries[] | "\(.key)\t\(.value)"' "$uplugin" |
    while IFS=$'\t' read -r dep_name dep_version; do
        [[ -z "$dep_name" ]] && continue
        echo "[dep]  $source_name requires $dep_name@$dep_version"
        enqueue_plugin "$dep_name" "$dep_version" "$source_name.CrossxDependencies"
    done
}

seed_manifest_queue() {
    jq -c '.plugins | to_entries[]' "$MANIFEST" | while read -r entry; do
        name=$(  echo "$entry" | jq -r '.key')
        vtype=$( echo "$entry" | jq -r '.value | type')

        if [[ "$vtype" == "object" ]]; then
            src=$(echo "$entry" | jq -r '.value.source // ""')
            if [[ "$src" == "local" ]]; then
                path=$(echo "$entry" | jq -r '.value.path')
                echo "[skip] $name: local mode -> $path (manage manually)"
                continue
            fi
            echo "[err] $name: unsupported object value: $(echo "$entry" | jq -c '.value')" >&2
            exit 1
        fi

        version=$(echo "$entry" | jq -r '.value')
        enqueue_plugin "$name" "$version" "crossx-plugins.json"
    done
}

# $1=name  $2=version
verify_plugin() {
    name="$1"
    want_version="$2"
    uplugin="$PLUGINS_DIR/$name/$name.uplugin"
    if [[ ! -f "$uplugin" ]]; then
        echo "[fail] $name: not installed"
        return 1
    fi

    actual=$(jq -r '.VersionName' "$uplugin")
    locked=$(jq -r --arg n "$name" '.plugins[$n].version // empty' "$LOCK")
    if [[ "$actual" != "$want_version" ]]; then
        echo "[fail] $name: .uplugin=$actual, expected=$want_version"
        return 1
    elif [[ -n "$locked" && "$locked" != "$want_version" ]]; then
        echo "[warn] $name: lock=$locked, expected=$want_version (run sdk-install)"
        return 1
    fi

    echo "[ok]   $name: $actual"
    enqueue_crossx_dependencies "$uplugin" "$name"
    return 0
}

# $1=name  $2=version
install_plugin() {
    name="$1"
    version="$2"
    tag=$(plugin_tag "$name" "$version")
    asset=$(plugin_asset "$name" "$version")

    # Short-circuit if already installed at the same version.
    prev_sha=$(jq -r --arg n "$name" '.plugins[$n].sha256 // empty' "$LOCK")
    prev_ver=$(jq -r --arg n "$name" '.plugins[$n].version // empty' "$LOCK")
    uplugin="$PLUGINS_DIR/$name/$name.uplugin"
    if [[ "$FORCE" != "true" \
          && "$prev_ver" == "$version" \
          && -n "$prev_sha" \
          && -d "$PLUGINS_DIR/$name" \
          && -f "$uplugin" ]]; then
        echo "[ok]   $name@$version (up to date)"
        enqueue_crossx_dependencies "$uplugin" "$name"
        return
    fi

    # 1) Download. Public installs use the stable releases/download URL to
    # avoid GitHub's anonymous REST API limit. Authenticated installs keep the
    # API path so private registry repos remain supported.
    tmpzip="$(mktemp -t crossx-plugin-XXXXXX).zip"
    echo "[get]  $name@$version  <-  ${OWNER}/${REPO}  ($asset)"

    if [[ -n "${GITHUB_TOKEN:-}" ]]; then
        release_json=$(github_curl -fsSL \
            -H "Accept: application/vnd.github+json" \
            "${API}/releases/tags/${tag}") || {
            rm -f "$tmpzip"
            echo "[err] $name: tag '$tag' not found on ${OWNER}/${REPO} or token has no access" >&2
            exit 1; }

        asset_url=$(echo "$release_json" | jq -r --arg a "$asset" '.assets[] | select(.name==$a) | .url')
        [[ -n "$asset_url" && "$asset_url" != "null" ]] || {
            rm -f "$tmpzip"
            echo "[err] $name: asset '$asset' not attached to tag '$tag'" >&2
            echo "      available: $(echo "$release_json" | jq -r '.assets[].name' | paste -sd, -)" >&2
            exit 1; }

        github_curl -fSL --progress-bar \
            -H "Accept: application/octet-stream" \
            "$asset_url" -o "$tmpzip" || {
            rm -f "$tmpzip"
            echo "[err] $name: failed to download '$asset' from tag '$tag'" >&2
            exit 1; }
    else
        download_url=$(plugin_download_url "$tag" "$asset")
        if ! http_code=$(github_curl -sSL -w '%{http_code}' "$download_url" -o "$tmpzip"); then
            rm -f "$tmpzip"
            echo "[err] $name: failed to download '$asset' from tag '$tag'" >&2
            echo "      url: $download_url" >&2
            echo "      If ${OWNER}/${REPO} is private, set GITHUB_TOKEN to a PAT with repo access." >&2
            exit 1
        fi
        if [[ "$http_code" -lt 200 || "$http_code" -ge 300 ]]; then
            rm -f "$tmpzip"
            echo "[err] $name: failed to download '$asset' from tag '$tag' (HTTP $http_code)" >&2
            echo "      url: $download_url" >&2
            echo "      Check that the release tag and asset name exist on ${OWNER}/${REPO}." >&2
            exit 1
        fi
    fi

    sha=$(sha256_of "$tmpzip")

    # 2) Unpack
    rm -rf "$PLUGINS_DIR/$name"
    unzip -q "$tmpzip" -d "$PLUGINS_DIR"
    rm -f "$tmpzip"

    # 3) Validate .uplugin VersionName
    uplugin="$PLUGINS_DIR/$name/$name.uplugin"
    [[ -f "$uplugin" ]] || {
        echo "[err] $name: $name.uplugin missing after extraction" >&2; exit 1; }
    actual=$(jq -r '.VersionName' "$uplugin")
    if [[ "$actual" != "$version" ]]; then
        echo "[err] $name: expected VersionName=$version, got $actual" >&2
        exit 1
    fi

    # 4) Update lock file
    ts="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    tmp="$(mktemp)"
    jq --arg n "$name" --arg v "$version" --arg t "$tag" \
       --arg a "$asset" --arg s "$sha" --arg at "$ts" \
       '.plugins[$n] = { version:$v, tag:$t, asset:$a, sha256:$s, installed_at:$at }' \
       "$LOCK" > "$tmp"
    mv "$tmp" "$LOCK"

    echo "[done] $name@$version  sha256=${sha:0:12}…"
    enqueue_crossx_dependencies "$uplugin" "$name"
}

# ---------- process queue ---------------------------------------------------

seed_manifest_queue

if [[ "$MODE" == "install" ]]; then
    mkdir -p "$PLUGINS_DIR"
fi

failed=0
queue_index=1
while true; do
    queued=$(wc -l < "$QUEUE" | tr -d ' ')
    if [[ "$queue_index" -gt "$queued" ]]; then
        break
    fi

    line=$(sed -n "${queue_index}p" "$QUEUE")
    queue_index=$((queue_index + 1))
    name=${line%%$'\t'*}
    version=${line#*$'\t'}
    [[ -z "$name" ]] && continue

    if grep -Fqx "$name" "$PROCESSED"; then
        continue
    fi
    echo "$name" >> "$PROCESSED"

    if [[ "$MODE" == "verify" ]]; then
        verify_plugin "$name" "$version" || failed=1
    else
        install_plugin "$name" "$version"
    fi
done

if [[ "$MODE" == "verify" ]]; then
    exit $failed
fi

echo "All plugins in sync with crossx-plugins.json."
