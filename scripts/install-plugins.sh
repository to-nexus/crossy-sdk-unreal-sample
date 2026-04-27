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
#  Env: GITHUB_TOKEN  (OPTIONAL — only needed to raise GitHub's anonymous
#                     rate limit (60/hr -> 5000/hr) or to read a private
#                     registry. The default registry repo is public.)
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

# GITHUB_TOKEN is optional: GitHub Releases on a public repo can be read
# anonymously. We attach Authorization only when a token is present, mainly
# to bypass the 60/hr anonymous rate limit on shared CI runners.
AUTH_HEADER_ARG=()
if [[ -n "${GITHUB_TOKEN:-}" ]]; then
    AUTH_HEADER_ARG=(-H "Authorization: Bearer ${GITHUB_TOKEN}")
fi

OWNER=$(jq -r '.registry.owner' "$MANIFEST")
REPO=$( jq -r '.registry.repo'  "$MANIFEST")
REG_TYPE=$(jq -r '.registry.type' "$MANIFEST")
[[ "$REG_TYPE" == "github-releases" ]] || {
    echo "[err] unsupported registry.type: $REG_TYPE" >&2; exit 1; }

API="https://api.github.com/repos/${OWNER}/${REPO}"

# ---------- helpers ---------------------------------------------------------

sha256_of() { $SHA_CMD "$1" | awk '{print $1}'; }

# $1=name  $2=version
plugin_tag()   { printf '%s@v%s' "$1" "$2"; }
# $1=name  $2=version
plugin_asset() { printf '%s-%s.zip' "$1" "$2"; }

# ---------- verify mode -----------------------------------------------------

if [[ "$MODE" == "verify" ]]; then
    failed=0
    while IFS=$'\t' read -r name want_version; do
        [[ -z "$name" ]] && continue
        uplugin="$PLUGINS_DIR/$name/$name.uplugin"
        if [[ ! -f "$uplugin" ]]; then
            echo "[fail] $name: not installed"
            failed=1; continue
        fi
        actual=$(jq -r '.VersionName' "$uplugin")
        locked=$(jq -r --arg n "$name" '.plugins[$n].version // empty' "$LOCK")
        if [[ "$actual" != "$want_version" ]]; then
            echo "[fail] $name: .uplugin=$actual, manifest=$want_version"
            failed=1
        elif [[ -n "$locked" && "$locked" != "$want_version" ]]; then
            echo "[warn] $name: lock=$locked, manifest=$want_version (run sdk-install)"
            failed=1
        else
            echo "[ok]   $name: $actual"
        fi
    done < <(jq -r '.plugins | to_entries[] | select(.value|type=="string") | "\(.key)\t\(.value)"' "$MANIFEST")
    exit $failed
fi

# ---------- install mode ----------------------------------------------------

mkdir -p "$PLUGINS_DIR"

# Process string-version entries only. Object entries (local mode) are skipped.
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
    tag=$(plugin_tag "$name" "$version")
    asset=$(plugin_asset "$name" "$version")

    # 1) Resolve asset URL via Releases API
    release_json=$(curl -fsSL \
        "${AUTH_HEADER_ARG[@]}" \
        -H "Accept: application/vnd.github+json" \
        "${API}/releases/tags/${tag}") || {
        echo "[err] $name: tag '$tag' not found on ${OWNER}/${REPO}" >&2
        echo "      (If you hit a rate limit, set GITHUB_TOKEN to any GitHub PAT — public repos do not require special scopes.)" >&2
        exit 1; }

    asset_url=$(echo "$release_json" | jq -r --arg a "$asset" '.assets[] | select(.name==$a) | .url')
    [[ -n "$asset_url" && "$asset_url" != "null" ]] || {
        echo "[err] $name: asset '$asset' not attached to tag '$tag'" >&2
        echo "      available: $(echo "$release_json" | jq -r '.assets[].name' | paste -sd, -)" >&2
        exit 1; }

    # 2) Short-circuit if already installed at the same version
    prev_sha=$(jq -r --arg n "$name" '.plugins[$n].sha256 // empty' "$LOCK")
    prev_ver=$(jq -r --arg n "$name" '.plugins[$n].version // empty' "$LOCK")
    if [[ "$FORCE" != "true" \
          && "$prev_ver" == "$version" \
          && -n "$prev_sha" \
          && -d "$PLUGINS_DIR/$name" \
          && -f "$PLUGINS_DIR/$name/$name.uplugin" ]]; then
        echo "[ok]   $name@$version (up to date)"
        continue
    fi

    # 3) Download
    tmpzip="$(mktemp -t crossx-plugin-XXXXXX).zip"
    echo "[get]  $name@$version  <-  ${OWNER}/${REPO}  ($asset)"
    curl -fSL --progress-bar \
        "${AUTH_HEADER_ARG[@]}" \
        -H "Accept: application/octet-stream" \
        "$asset_url" -o "$tmpzip"

    sha=$(sha256_of "$tmpzip")

    # 4) Unpack
    rm -rf "$PLUGINS_DIR/$name"
    unzip -q "$tmpzip" -d "$PLUGINS_DIR"
    rm -f "$tmpzip"

    # 5) Validate .uplugin VersionName
    uplugin="$PLUGINS_DIR/$name/$name.uplugin"
    [[ -f "$uplugin" ]] || {
        echo "[err] $name: $name.uplugin missing after extraction" >&2; exit 1; }
    actual=$(jq -r '.VersionName' "$uplugin")
    if [[ "$actual" != "$version" ]]; then
        echo "[err] $name: expected VersionName=$version, got $actual" >&2
        exit 1
    fi

    # 6) Update lock file
    ts="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    tmp="$(mktemp)"
    jq --arg n "$name" --arg v "$version" --arg t "$tag" \
       --arg a "$asset" --arg s "$sha" --arg at "$ts" \
       '.plugins[$n] = { version:$v, tag:$t, asset:$a, sha256:$s, installed_at:$at }' \
       "$LOCK" > "$tmp"
    mv "$tmp" "$LOCK"

    echo "[done] $name@$version  sha256=${sha:0:12}…"
done

echo "All plugins in sync with crossx-plugins.json."
