param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Archive
)

$ErrorActionPreference = "Stop"
$Archive = (Resolve-Path $Archive).Path
$Checksum = "$Archive.sha256"
if (-not (Test-Path $Checksum)) {
    throw "smoke test: checksum not found: $Checksum"
}

$Expected = ((Get-Content $Checksum) -split "\s+")[0]
$Actual = (Get-FileHash -Algorithm SHA256 $Archive).Hash
if ($Actual -ne $Expected) {
    throw "smoke test: SHA-256 verification failed"
}

$Work = Join-Path ([System.IO.Path]::GetTempPath()) "janus-smoke-$([guid]::NewGuid())"
try {
    $PackageDirectory = Join-Path $Work "package"
    New-Item -ItemType Directory -Path $PackageDirectory | Out-Null
    Expand-Archive -Path $Archive -DestinationPath $PackageDirectory
    $PackageRoot = Get-ChildItem -Path $PackageDirectory -Directory |
        Where-Object Name -Like "janus-*" |
        Select-Object -First 1
    if (-not $PackageRoot) {
        throw "smoke test: invalid Janus archive layout"
    }

    $Janus = Join-Path $PackageRoot.FullName "bin\janus.exe"
    $env:HOME = Join-Path $Work "home"
    $env:JANUS_CACHE = Join-Path $Work "cache"
    $env:JANUS_REGISTRY = Join-Path $Work "registry"
    & $Janus --version
    if ($LASTEXITCODE -ne 0) { throw "smoke test: janus --version failed" }

    $Project = Join-Path $Work "hello"
    & $Janus new $Project
    Push-Location $Project
    try {
        & $Janus check
        if ($LASTEXITCODE -ne 0) { throw "smoke test: janus check failed" }
        & $Janus build --release
        if ($LASTEXITCODE -ne 0) { throw "smoke test: release build failed" }
        $Output = & $Janus run
        if ($LASTEXITCODE -ne 0 -or $Output -notmatch "Hello from Janus!") {
            throw "smoke test: generated program did not run"
        }
        Set-Content -Path "tests\basic.janus" -Value @"
def main() : int {
    return 0
}
"@
        & $Janus test
        if ($LASTEXITCODE -ne 0) { throw "smoke test: janus test failed" }
    } finally {
        Pop-Location
    }
    Write-Host "smoke test: packaged Janus toolchain is operational"
} finally {
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $Work
}
