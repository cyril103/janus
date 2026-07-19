param(
    [string]$Version = $(if ($env:JANUS_VERSION) { $env:JANUS_VERSION } else { "0.1.0" })
)

$ErrorActionPreference = "Stop"
$Architecture = switch ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture) {
    "X64" { "AMD64" }
    "Arm64" { "ARM64" }
    default { throw "Architecture non prise en charge: $_" }
}
$Archive = "janus-$Version-Windows-$Architecture.zip"
$BaseUrl = if ($env:JANUS_DIST_SERVER) {
    $env:JANUS_DIST_SERVER
} else {
    "https://github.com/cyril103/janus/releases/download/v$Version"
}
$Url = if ($env:JANUS_DIST_URL) { $env:JANUS_DIST_URL } else { "$BaseUrl/$Archive" }
$JanusHome = if ($env:JANUSUP_HOME) { $env:JANUSUP_HOME } else {
    Join-Path $env:LOCALAPPDATA "Janus"
}
$Temporary = Join-Path ([System.IO.Path]::GetTempPath()) "janus-$([guid]::NewGuid())"

try {
    New-Item -ItemType Directory -Path $Temporary | Out-Null
    $ArchivePath = Join-Path $Temporary $Archive
    Invoke-WebRequest -UseBasicParsing -Uri $Url -OutFile $ArchivePath
    $ChecksumPath = "$ArchivePath.sha256"
    Invoke-WebRequest -UseBasicParsing -Uri "$Url.sha256" -OutFile $ChecksumPath
    $ExpectedHash = ((Get-Content $ChecksumPath) -split "\s+")[0]
    $ActualHash = (Get-FileHash -Algorithm SHA256 $ArchivePath).Hash
    if ($ActualHash -ne $ExpectedHash) { throw "La somme SHA-256 du paquet est invalide" }
    if (Get-Command gh -ErrorAction SilentlyContinue) {
        & gh attestation verify $ArchivePath --repo cyril103/janus
        if ($LASTEXITCODE -ne 0) { throw "La provenance du paquet est invalide" }
    } elseif ($env:JANUS_REQUIRE_ATTESTATION -eq "1") {
        throw "GitHub CLI est nécessaire pour vérifier la provenance"
    } else {
        Write-Warning "Installez GitHub CLI pour vérifier la provenance du paquet"
    }
    $Package = Join-Path $Temporary "package"
    Expand-Archive -Path $ArchivePath -DestinationPath $Package
    $PackageRoot = Join-Path $Package "janus-$Version-Windows-$Architecture"
    $env:JANUSUP_HOME = $JanusHome
    & (Join-Path $PackageRoot "bin\janusup.exe") install $PackageRoot $Version
    if ($LASTEXITCODE -ne 0) { throw "janusup a échoué avec le code $LASTEXITCODE" }

    $Bin = Join-Path $JanusHome "bin"
    $UserPath = [Environment]::GetEnvironmentVariable("Path", "User")
    if (($UserPath -split ";") -notcontains $Bin) {
        [Environment]::SetEnvironmentVariable("Path", "$Bin;$UserPath", "User")
        Write-Host "Le PATH utilisateur a été mis à jour; ouvrez un nouveau terminal."
    }
    Write-Host "Janus $Version est installé."
} finally {
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $Temporary
}
