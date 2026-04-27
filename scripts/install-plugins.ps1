<#
    install-plugins.ps1
    Windows / PowerShell 7+ equivalent of scripts/install-plugins.sh.

    Usage:
        pwsh ./scripts/install-plugins.ps1               # install
        pwsh ./scripts/install-plugins.ps1 -Verify       # verify only
        pwsh ./scripts/install-plugins.ps1 -Force        # re-download

    The default registry repo is public, so no authentication is required.
    If you hit GitHub's anonymous rate limit (60 requests/hour, e.g. on a
    shared CI runner), set $env:GITHUB_TOKEN to any GitHub PAT — public
    repos do not require any specific scopes.

    Requires: PowerShell 7+ (Expand-Archive, Invoke-WebRequest, ConvertFrom-Json)
#>
[CmdletBinding()]
param(
    [switch]$Verify,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$Manifest    = Join-Path $ProjectRoot 'crossx-plugins.json'
$Lock        = Join-Path $ProjectRoot 'crossx-plugins.lock.json'
$PluginsDir  = Join-Path $ProjectRoot 'Plugins'

if (-not (Test-Path $Manifest)) { throw "manifest not found: $Manifest" }
if (-not (Test-Path $Lock)) {
    '{ "plugins": {} }' | Out-File -FilePath $Lock -Encoding utf8
}

$manifestObj = Get-Content $Manifest -Raw | ConvertFrom-Json
$lockObj     = Get-Content $Lock     -Raw | ConvertFrom-Json

if ($manifestObj.registry.type -ne 'github-releases') {
    throw "unsupported registry.type: $($manifestObj.registry.type)"
}
$Owner = $manifestObj.registry.owner
$Repo  = $manifestObj.registry.repo
$Api   = "https://api.github.com/repos/$Owner/$Repo"

function Sha256File([string]$Path) {
    (Get-FileHash -Algorithm SHA256 -Path $Path).Hash.ToLower()
}

function PluginEntries {
    foreach ($prop in $manifestObj.plugins.PSObject.Properties) {
        [pscustomobject]@{ Name = $prop.Name; Value = $prop.Value }
    }
}

# ---------- verify mode -----------------------------------------------------
if ($Verify) {
    $failed = 0
    foreach ($e in PluginEntries) {
        if ($e.Value -isnot [string]) { continue }
        $uplugin = Join-Path $PluginsDir (Join-Path $e.Name "$($e.Name).uplugin")
        if (-not (Test-Path $uplugin)) {
            Write-Host "[fail] $($e.Name): not installed"; $failed = 1; continue
        }
        $actual = (Get-Content $uplugin -Raw | ConvertFrom-Json).VersionName
        $locked = $lockObj.plugins.$($e.Name).version
        if ($actual -ne $e.Value) {
            Write-Host "[fail] $($e.Name): .uplugin=$actual, manifest=$($e.Value)"
            $failed = 1
        } elseif ($locked -and $locked -ne $e.Value) {
            Write-Host "[warn] $($e.Name): lock=$locked, manifest=$($e.Value) (run sdk-install)"
            $failed = 1
        } else {
            Write-Host "[ok]   $($e.Name): $actual"
        }
    }
    exit $failed
}

# ---------- install mode ----------------------------------------------------

New-Item -ItemType Directory -Path $PluginsDir -Force | Out-Null

# Authorization header is attached only when a token is present. Public repos
# can be read anonymously; a token is just a way to bypass the 60/hr
# anonymous rate limit on shared runners.
$headersJson = @{ Accept = 'application/vnd.github+json' }
$headersBin  = @{ Accept = 'application/octet-stream' }
if ($env:GITHUB_TOKEN) {
    $headersJson.Authorization = "Bearer $env:GITHUB_TOKEN"
    $headersBin.Authorization  = "Bearer $env:GITHUB_TOKEN"
}

foreach ($e in PluginEntries) {
    $name = $e.Name

    if ($e.Value -is [pscustomobject]) {
        if ($e.Value.source -eq 'local') {
            Write-Host "[skip] $name: local mode -> $($e.Value.path)"
            continue
        }
        throw "$name: unsupported object value"
    }

    $version = [string]$e.Value
    $tag     = "$name@v$version"
    $asset   = "$name-$version.zip"

    # Resolve asset URL
    try {
        $rel = Invoke-RestMethod -Headers $headersJson -Uri "$Api/releases/tags/$tag"
    } catch {
        throw "$name: tag '$tag' not found on $Owner/$Repo"
    }
    $assetObj = $rel.assets | Where-Object { $_.name -eq $asset } | Select-Object -First 1
    if (-not $assetObj) {
        $avail = ($rel.assets.name -join ', ')
        throw "$name: asset '$asset' not attached to tag '$tag' (available: $avail)"
    }

    # Short-circuit
    $prev = $lockObj.plugins.$name
    $uplugin = Join-Path $PluginsDir (Join-Path $name "$name.uplugin")
    if (-not $Force -and $prev -and $prev.version -eq $version -and $prev.sha256 -and (Test-Path $uplugin)) {
        Write-Host "[ok]   $name@$version (up to date)"
        continue
    }

    # Download
    $tmpzip = [System.IO.Path]::GetTempFileName() + '.zip'
    Write-Host "[get]  $name@$version  <-  $Owner/$Repo  ($asset)"
    Invoke-WebRequest -Headers $headersBin -Uri $assetObj.url -OutFile $tmpzip
    $sha = Sha256File $tmpzip

    # Unpack
    $dest = Join-Path $PluginsDir $name
    if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
    Expand-Archive -Path $tmpzip -DestinationPath $PluginsDir -Force
    Remove-Item $tmpzip -Force

    # Validate
    if (-not (Test-Path $uplugin)) { throw "$name: $name.uplugin missing after extraction" }
    $actual = (Get-Content $uplugin -Raw | ConvertFrom-Json).VersionName
    if ($actual -ne $version) {
        throw "$name: expected VersionName=$version, got $actual"
    }

    # Update lock
    $lockObj.plugins | Add-Member -NotePropertyName $name -NotePropertyValue ([pscustomobject]@{
        version      = $version
        tag          = $tag
        asset        = $asset
        sha256       = $sha
        installed_at = (Get-Date).ToUniversalTime().ToString('o')
    }) -Force
    ($lockObj | ConvertTo-Json -Depth 6) | Out-File -FilePath $Lock -Encoding utf8

    Write-Host "[done] $name@$version  sha256=$($sha.Substring(0,12))..."
}

Write-Host 'All plugins in sync with crossx-plugins.json.'
