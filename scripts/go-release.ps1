#Requires -Version 5.1
param(
    [ValidateSet('patch', 'minor', 'major')]
    [string]$Bump = 'patch',
    [string]$Version = '',
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

function Get-RepoRoot {
    $here = if ($PSScriptRoot) { $PSScriptRoot } else { (Get-Location).Path }
    return (git -C $here rev-parse --show-toplevel).Trim()
}

function Read-Version([string]$path) {
    if (-not (Test-Path $path)) { return '0.0.0' }
    $raw = (Get-Content -Path $path -Raw).Trim()
    if ($raw -notmatch '^\d+\.\d+\.\d+$') {
        throw "VERSION must be semver X.Y.Z, got '$raw'"
    }
    return $raw
}

function Get-NextVersion([string]$current, [string]$bump) {
    $parts = $current.Split('.')
    $maj = [int]$parts[0]
    $min = [int]$parts[1]
    $pat = [int]$parts[2]
    switch ($bump) {
        'major' { return "$($maj + 1).0.0" }
        'minor' { return "$maj.$($min + 1).0" }
        default { return "$maj.$min.$($pat + 1)" }
    }
}

function Write-VersionFiles([string]$root, [string]$ver) {
    $utf8 = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText((Join-Path $root 'VERSION'), "$ver`n", $utf8)
    $header = "#pragma once`n`nstatic constexpr const char* FW_VERSION = `"$ver`";`n"
    [System.IO.File]::WriteAllText((Join-Path $root 'src\version.h'), $header, $utf8)
}

$repo = Get-RepoRoot
Set-Location $repo

$versionFile = Join-Path $repo 'VERSION'
$current = Read-Version $versionFile
$existingTags = @(git tag -l 'v*.*.*')

if ($Version) {
    if ($Version -notmatch '^\d+\.\d+\.\d+$') {
        throw "Version must be semver X.Y.Z, got '$Version'"
    }
    $next = $Version
} elseif ($existingTags.Count -eq 0) {
    $next = $current
    if ($next -eq '0.0.0') { $next = '1.0.0' }
} else {
    $next = Get-NextVersion $current $Bump
}

$tag = "v$next"
if ($existingTags -contains $tag) {
    throw "Tag $tag already exists"
}

$pio = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\pio.exe'
if (-not (Test-Path $pio)) {
    $pioCmd = Get-Command pio -ErrorAction SilentlyContinue
    if (-not $pioCmd) { throw 'PlatformIO CLI not found (pio.exe)' }
    $pio = $pioCmd.Source
}

$firmwareSrc = Join-Path $repo '.pio\build\m5stack-cardputer\firmware.bin'

Write-Host "Current VERSION: $current"
Write-Host "Next release:    $tag"
Write-Host "PIO:             $pio"

if ($DryRun) {
    Write-Host 'Dry run - no build, commit, tag, or GitHub release.'
    exit 0
}

$dirty = @(git status --porcelain -- VERSION src platformio.ini lib)
$allowed = @('VERSION', 'src/version.h', 'src\version.h')
$blocked = @($dirty | Where-Object {
    $path = ($_ -replace '^.. ', '').Trim()
    $norm = $path -replace '\\', '/'
    $allowed -notcontains $norm
})
if ($blocked.Count -gt 0) {
    $list = $blocked -join '; '
    throw "Uncommitted firmware files would not match the git tag: $list. Commit or stash them before goRelease."
}

Write-VersionFiles $repo $next

Write-Host 'Building m5stack-cardputer...'
& $pio run -e m5stack-cardputer
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }
if (-not (Test-Path $firmwareSrc)) { throw "Missing $firmwareSrc" }

git add -- VERSION src/version.h
$staged = @(git diff --cached --name-only)
if ($staged.Count -gt 0) {
    git commit -m "Release $tag"
    if ($LASTEXITCODE -ne 0) { throw 'git commit failed' }
} else {
    Write-Host "VERSION already $next - tagging current HEAD"
}

git tag -a $tag -m $tag
if ($LASTEXITCODE -ne 0) { throw "git tag $tag failed" }

git push origin HEAD
if ($LASTEXITCODE -ne 0) { throw 'git push failed' }

git push origin $tag
if ($LASTEXITCODE -ne 0) { throw "git push $tag failed" }

$asset = Join-Path $repo "firmware-$next.bin"
Copy-Item -Path $firmwareSrc -Destination $asset -Force
try {
    gh release create $tag "$asset#firmware.bin" --title $tag --generate-notes
    if ($LASTEXITCODE -ne 0) { throw "gh release create $tag failed" }
} finally {
    Remove-Item -Path $asset -ErrorAction SilentlyContinue
}

$url = gh release view $tag --json url --jq .url
Write-Host "Released $tag"
Write-Host $url
