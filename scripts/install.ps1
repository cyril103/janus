param(
    [string]$Version = $(if ($env:JANUS_VERSION) { $env:JANUS_VERSION } else { "0.11.0" }),
    [string]$ValidateArchivePath,
    [string]$ExpectedRoot
)

$ErrorActionPreference = "Stop"
function Get-ArchiveLimit([UInt64]$Production, [string]$Variable) {
    [UInt64]$candidate = 0
    $text = [Environment]::GetEnvironmentVariable($Variable)
    if ($text -and [UInt64]::TryParse($text, [ref]$candidate) -and $candidate -lt $Production) {
        return $candidate
    }
    return $Production
}
function Test-ToolchainArchive([string]$Path, [string]$Root) {
    $maxEntries = Get-ArchiveLimit 100000 "JANUS_ARCHIVE_TEST_MAX_ENTRIES"
    $maxFile = Get-ArchiveLimit 1073741824 "JANUS_ARCHIVE_TEST_MAX_FILE_SIZE"
    $maxTotal = Get-ArchiveLimit 4294967296 "JANUS_ARCHIVE_TEST_MAX_TOTAL_SIZE"
    Add-Type -AssemblyName System.IO.Compression
    $stream = [IO.File]::OpenRead($Path)
    try {
        $zip = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Read, $false)
        try {
            $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
            $regularPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
            $requiredDirectories = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
            [UInt64]$total = 0
            if ($zip.Entries.Count -eq 0) { throw "unsafe archive: empty archive" }
            if ([UInt64]$zip.Entries.Count -gt $maxEntries) { throw "unsafe archive: too many entries" }
            foreach ($entry in $zip.Entries) {
                $name = $entry.FullName
                if (-not $name -or $name -notmatch '^[\x21-\x7E]+$' -or
                    $name.Contains("\") -or $name.StartsWith("/") -or
                    $name -match '^[A-Za-z]:' -or
                    $name.IndexOfAny([char[]]@(0, 9, 10, 13)) -ge 0) {
                    throw "unsafe archive: absolute or ambiguous entry path"
                }
                $parts = $name.Split('/')
                $normalized = [Collections.Generic.List[string]]::new()
                for ($index = 0; $index -lt $parts.Count; $index++) {
                    $part = $parts[$index]
                    if (-not $part) {
                        if ($index -ne $parts.Count - 1) { throw "unsafe archive: ambiguous separators" }
                        continue
                    }
                    if ($part -eq "." -or $part -eq "..") { throw "unsafe archive: traversing entry path" }
                    if ($part.EndsWith('.') -or $part.IndexOfAny([char[]]'<>:"|?*') -ge 0) {
                        throw "unsafe archive: Windows-ambiguous entry path"
                    }
                    $device = ($part.Split('.')[0]).ToLowerInvariant()
                    if ($device -match '^(con|prn|aux|nul|com[1-9]|lpt[1-9])$') {
                        throw "unsafe archive: reserved Windows entry path"
                    }
                    $normalized.Add($part)
                }
                if ($normalized.Count -eq 0 -or $normalized[0] -cne $Root) {
                    throw "unsafe archive: unexpected archive root"
                }
                $key = [string]::Join('/', $normalized)
                if (-not $seen.Add($key)) { throw "unsafe archive: colliding entry paths" }
                $attributes = [BitConverter]::ToUInt32(
                    [BitConverter]::GetBytes([Int32]$entry.ExternalAttributes), 0)
                $unixType = ($attributes -shr 16) -band 0xF000
                if ($unixType -ne 0 -and $unixType -ne 0x8000 -and $unixType -ne 0x4000) {
                    throw "unsafe archive: link or special entry"
                }
                $isDirectory = $name.EndsWith('/')
                if (-not $isDirectory -and $unixType -eq 0x4000) { throw "unsafe archive: malformed directory" }
                $ancestor = ""
                for ($index = 0; $index -lt $normalized.Count - 1; $index++) {
                    $ancestor = if ($ancestor) { "$ancestor/$($normalized[$index])" } else { $normalized[$index] }
                    if ($regularPaths.Contains($ancestor)) {
                        throw "unsafe archive: file/directory path collision"
                    }
                    [void]$requiredDirectories.Add($ancestor)
                }
                if (-not $isDirectory) {
                    if ($requiredDirectories.Contains($key)) {
                        throw "unsafe archive: file/directory path collision"
                    }
                    [void]$regularPaths.Add($key)
                    [UInt64]$size = $entry.Length
                    if ($size -gt $maxFile) { throw "unsafe archive: entry is too large" }
                    $total += $size
                    if ($total -gt $maxTotal) { throw "unsafe archive: total size limit exceeded" }
                }
            }
        } finally { $zip.Dispose() }
    } finally { $stream.Dispose() }
}
if ($ValidateArchivePath) {
    if (-not $ExpectedRoot) { throw "ExpectedRoot is required" }
    Test-ToolchainArchive $ValidateArchivePath $ExpectedRoot
    return
}
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
$ParsedUrl = [Uri]$Url
$OfficialSource = $ParsedUrl.Scheme.Equals(
        "https", [StringComparison]::OrdinalIgnoreCase) -and
    $ParsedUrl.Host.Equals(
        "github.com", [StringComparison]::OrdinalIgnoreCase) -and
    $ParsedUrl.Port -eq 443 -and
    $ParsedUrl.AbsolutePath.StartsWith(
        "/cyril103/janus/releases/download/",
        [StringComparison]::OrdinalIgnoreCase)
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
    $GhSupportsAttestation = $false
    if (Get-Command gh -ErrorAction SilentlyContinue) {
        & gh attestation --help *> $null
        $GhSupportsAttestation = $LASTEXITCODE -eq 0
    }
    if ($env:JANUS_ALLOW_UNVERIFIED_PRIVATE_MIRROR -eq "1" -and -not $OfficialSource) {
        Write-Warning "Using an unverified private mirror (JANUS_ALLOW_UNVERIFIED_PRIVATE_MIRROR=1)"
    } elseif ($GhSupportsAttestation) {
        & gh attestation verify $ArchivePath --repo cyril103/janus
        if ($LASTEXITCODE -ne 0) { throw "La provenance du paquet est invalide" }
    } else {
        throw "Une version récente de GitHub CLI avec la commande attestation est nécessaire pour vérifier la provenance"
    }
    $Package = Join-Path $Temporary "package"
    Test-ToolchainArchive $ArchivePath "janus-$Version-Windows-$Architecture"
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
