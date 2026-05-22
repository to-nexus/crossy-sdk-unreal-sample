<#
    install-plugins.ps1
    Windows / PowerShell 7+ equivalent of scripts/install-plugins.sh.

    Usage:
        pwsh ./scripts/install-plugins.ps1               # install
        pwsh ./scripts/install-plugins.ps1 -Verify       # verify only
        pwsh ./scripts/install-plugins.ps1 -Force        # re-download

    The default registry repo is public, so no authentication is required.
    Public release assets are downloaded directly from github.com without using
    the GitHub REST API. Set $env:GITHUB_TOKEN only when pointing the registry
    at a private repo.

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

function Plugin-DownloadUrl([string]$Tag, [string]$Asset) {
    "https://github.com/$Owner/$Repo/releases/download/$Tag/$Asset"
}

$queue = [System.Collections.Generic.List[object]]::new()
$desired = @{}
$processed = @{}

function Enqueue-Plugin([string]$Name, [string]$Version, [string]$SourceLabel) {
    if ($desired.ContainsKey($Name)) {
        if ($desired[$Name] -ne $Version) {
            throw "$Name: version conflict ($($desired[$Name]) vs $Version from $SourceLabel)"
        }
        return
    }

    $desired[$Name] = $Version
    $queue.Add([pscustomobject]@{ Name = $Name; Version = $Version })
}

function Enqueue-CrossxDependencies([string]$UpluginPath, [string]$SourceName) {
    if (-not (Test-Path $UpluginPath)) { return }

    $upluginObj = Get-Content $UpluginPath -Raw | ConvertFrom-Json
    if (-not $upluginObj.CrossxDependencies) { return }

    foreach ($dep in $upluginObj.CrossxDependencies.PSObject.Properties) {
        Write-Host "[dep]  $SourceName requires $($dep.Name)@$($dep.Value)"
        Enqueue-Plugin $dep.Name ([string]$dep.Value) "$SourceName.CrossxDependencies"
    }
}

function Seed-ManifestQueue {
    foreach ($prop in $manifestObj.plugins.PSObject.Properties) {
        $name = $prop.Name
        $value = $prop.Value

        if ($value -is [pscustomobject]) {
            if ($value.source -eq 'local') {
                Write-Host "[skip] $name: local mode -> $($value.path)"
                continue
            }
            throw "$name: unsupported object value"
        }

        Enqueue-Plugin $name ([string]$value) 'crossx-plugins.json'
    }
}

function Ensure-LockPluginObject([string]$Name, [object]$Value) {
    $lockObj.plugins | Add-Member -NotePropertyName $Name -NotePropertyValue $Value -Force
}

function Verify-Plugin([string]$Name, [string]$Version) {
    $uplugin = Join-Path $PluginsDir (Join-Path $Name "$Name.uplugin")
    if (-not (Test-Path $uplugin)) {
        Write-Host "[fail] $Name: not installed"
        return $false
    }

    $actual = (Get-Content $uplugin -Raw | ConvertFrom-Json).VersionName
    $locked = $lockObj.plugins.$Name.version
    if ($actual -ne $Version) {
        Write-Host "[fail] $Name: .uplugin=$actual, expected=$Version"
        return $false
    }
    if ($locked -and $locked -ne $Version) {
        Write-Host "[warn] $Name: lock=$locked, expected=$Version (run sdk-install)"
        return $false
    }

    Write-Host "[ok]   $Name: $actual"
    Enqueue-CrossxDependencies $uplugin $Name
    return $true
}

function Install-Plugin([string]$Name, [string]$Version) {
    $tag   = "$Name@v$Version"
    $asset = "$Name-$Version.zip"
    $uplugin = Join-Path $PluginsDir (Join-Path $Name "$Name.uplugin")

    $prev = $lockObj.plugins.$Name
    if (-not $Force -and $prev -and $prev.version -eq $Version -and $prev.sha256 -and (Test-Path $uplugin)) {
        Write-Host "[ok]   $Name@$Version (up to date)"
        Enqueue-CrossxDependencies $uplugin $Name
        return
    }

    $tmpzip = [System.IO.Path]::GetTempFileName() + '.zip'
    Write-Host "[get]  $Name@$Version  <-  $Owner/$Repo  ($asset)"

    if ($env:GITHUB_TOKEN) {
        try {
            $rel = Invoke-RestMethod -Headers $headersJson -Uri "$Api/releases/tags/$tag"
        } catch {
            if (Test-Path $tmpzip) { Remove-Item $tmpzip -Force }
            throw "$Name: tag '$tag' not found on $Owner/$Repo or token has no access"
        }

        $assetObj = $rel.assets | Where-Object { $_.name -eq $asset } | Select-Object -First 1
        if (-not $assetObj) {
            if (Test-Path $tmpzip) { Remove-Item $tmpzip -Force }
            $avail = ($rel.assets.name -join ', ')
            throw "$Name: asset '$asset' not attached to tag '$tag' (available: $avail)"
        }

        try {
            Invoke-WebRequest -Headers $headersBin -Uri $assetObj.url -OutFile $tmpzip
        } catch {
            if (Test-Path $tmpzip) { Remove-Item $tmpzip -Force }
            throw "$Name: failed to download '$asset' from tag '$tag'"
        }
    } else {
        $downloadUrl = Plugin-DownloadUrl $tag $asset
        try {
            Invoke-WebRequest -Uri $downloadUrl -OutFile $tmpzip
        } catch {
            if (Test-Path $tmpzip) { Remove-Item $tmpzip -Force }
            $statusCode = $_.Exception.Response.StatusCode.value__
            $suffix = if ($statusCode) { " (HTTP $statusCode)" } else { "" }
            throw "$Name: failed to download '$asset' from tag '$tag'$suffix. Check that the release tag and asset name exist on $Owner/$Repo. If the repo is private, set GITHUB_TOKEN to a PAT with repo access. URL: $downloadUrl"
        }
    }
    $sha = Sha256File $tmpzip

    $dest = Join-Path $PluginsDir $Name
    if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }
    Expand-Archive -Path $tmpzip -DestinationPath $PluginsDir -Force
    Remove-Item $tmpzip -Force

    if (-not (Test-Path $uplugin)) { throw "$Name: $Name.uplugin missing after extraction" }
    $actual = (Get-Content $uplugin -Raw | ConvertFrom-Json).VersionName
    if ($actual -ne $Version) {
        throw "$Name: expected VersionName=$Version, got $actual"
    }

    Ensure-LockPluginObject $Name ([pscustomobject]@{
        version      = $Version
        tag          = $tag
        asset        = $asset
        sha256       = $sha
        installed_at = (Get-Date).ToUniversalTime().ToString('o')
    })
    ($lockObj | ConvertTo-Json -Depth 6) | Out-File -FilePath $Lock -Encoding utf8

    Write-Host "[done] $Name@$Version  sha256=$($sha.Substring(0,12))..."
    Enqueue-CrossxDependencies $uplugin $Name
}

# ---------- install / verify ------------------------------------------------

New-Item -ItemType Directory -Path $PluginsDir -Force | Out-Null

# Authorization header is attached only when a token is present. Public release
# assets are downloaded directly and do not use the GitHub REST API.
$headersJson = @{ Accept = 'application/vnd.github+json' }
$headersBin  = @{ Accept = 'application/octet-stream' }
if ($env:GITHUB_TOKEN) {
    $headersJson.Authorization = "Bearer $env:GITHUB_TOKEN"
    $headersBin.Authorization  = "Bearer $env:GITHUB_TOKEN"
}

Seed-ManifestQueue

$failed = 0
$index = 0
while ($index -lt $queue.Count) {
    $entry = $queue[$index]
    $index++

    if ($processed.ContainsKey($entry.Name)) { continue }
    $processed[$entry.Name] = $true

    if ($Verify) {
        if (-not (Verify-Plugin $entry.Name $entry.Version)) { $failed = 1 }
    } else {
        Install-Plugin $entry.Name $entry.Version
    }
}

if ($Verify) { exit $failed }

Write-Host 'All plugins in sync with crossx-plugins.json.'
