param([Parameter(Mandatory = $true)][string]$SourceDir)
$ErrorActionPreference = "Stop"
$global:JanusTestGhState = "invalid"
$global:JanusTestExtracted = $false
$global:JanusTestWarnings = @()

function Invoke-WebRequest {
    param([string]$Uri, [string]$OutFile, [switch]$UseBasicParsing)
    [IO.File]::WriteAllText($OutFile, "fixture")
}
function Get-FileHash {
    param($Algorithm, $Path)
    [pscustomobject]@{ Hash = "fixture" }
}
function Get-Content { param($Path) "fixture  archive" }
function Get-Command {
    param($Name, $ErrorAction)
    if ($global:JanusTestGhState -ne "absent") { [pscustomobject]@{ Name = "gh" } }
}
function gh {
    if ($args[0] -eq "attestation" -and $args[1] -eq "--help") {
        $global:LASTEXITCODE = if ($global:JanusTestGhState -eq "old") { 1 } else { 0 }
    } else {
        $global:LASTEXITCODE = if ($global:JanusTestGhState -eq "invalid") { 1 } else { 0 }
    }
}
function Expand-Archive {
    $global:JanusTestExtracted = $true
    throw "extraction reached"
}
function Write-Warning { param($Message) $global:JanusTestWarnings += $Message }

function Invoke-InstallCase {
    param([string]$Name, [string]$Url, [string]$GhState, [bool]$OptOut)
    $global:JanusTestGhState = $GhState
    $global:JanusTestExtracted = $false
    $global:JanusTestWarnings = @()
    $env:JANUS_DIST_URL = $Url
    $env:JANUS_ALLOW_UNVERIFIED_PRIVATE_MIRROR = if ($OptOut) { "1" } else { $null }
    $failure = $null
    try { & "$SourceDir/scripts/install.ps1" } catch { $failure = $_ }
    [pscustomobject]@{
        Name = $Name
        Failure = $failure
        Extracted = $global:JanusTestExtracted
        Warnings = @($global:JanusTestWarnings)
    }
}

$env:JANUS_VERSION = "test"
$official = "https://github.com/cyril103/janus/releases/download/vtest/janus-test-Windows-AMD64.zip"
$officialVariants = @(
    $official,
    "HTTPS://GITHUB.COM/CYRIL103/JANUS/RELEASES/DOWNLOAD/vtest/archive.zip",
    "https://github.com:443/cyril103/janus/releases/download/vtest/archive.zip",
    "https://user:secret@GitHub.Com/cyril103/janus/releases/download/vtest/archive.zip?download=1"
)
foreach ($ghState in @("absent", "old", "invalid")) {
    $result = Invoke-InstallCase "official-$ghState" $official $ghState $true
    if (-not $result.Failure) { throw "$($result.Name) was accepted" }
    if ($result.Extracted) { throw "$($result.Name) reached extraction" }
}
foreach ($url in $officialVariants) {
    $result = Invoke-InstallCase "official-variant" $url "old" $true
    if (-not $result.Failure) { throw "official URL variant accepted opt-out: $url" }
    if ($result.Extracted) { throw "official URL variant reached extraction: $url" }
}

$private = "https://packages.example.invalid/janus/archive.zip"
$result = Invoke-InstallCase "private-default" $private "old" $false
if (-not $result.Failure) { throw "private mirror silently skipped attestation" }
if ($result.Extracted) { throw "private default reached extraction" }

$result = Invoke-InstallCase "private-optout" $private "old" $true
if (-not $result.Extracted -or $result.Failure.Exception.Message -ne "extraction reached") {
    throw "private opt-out did not reach extraction"
}
if (-not ($result.Warnings -match "private.*unverified|unverified.*private|non vérifié.*privé")) {
    throw "private opt-out was not loudly logged"
}
