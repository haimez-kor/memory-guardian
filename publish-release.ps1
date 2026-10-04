$ErrorActionPreference = "Stop"
$repo = "haimez-kor/memory-guardian"
$version = "v1.3.18"
Set-Location -LiteralPath $PSScriptRoot
function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    $result = & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed ($LASTEXITCODE). Publishing stopped." }
    return $result
}
$installer = Join-Path $PSScriptRoot "MemoryGuardianSetup.exe"
$info = (Get-Item -LiteralPath $installer).VersionInfo
$actualVersion = [version]::new($info.FileMajorPart, $info.FileMinorPart, $info.FileBuildPart, $info.FilePrivatePart)
$requestedVersion = [version]$version.TrimStart("v")
$expectedVersion = [version]::new($requestedVersion.Major, $requestedVersion.Minor, $requestedVersion.Build, [Math]::Max(0, $requestedVersion.Revision))
if ($actualVersion -ne $expectedVersion) {
    throw "Installer version mismatch: expected $expectedVersion, found $actualVersion. Build the correct Inno installer first."
}
$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $installer).Hash.ToUpperInvariant()
Invoke-Checked gh @("api", "user", "--jq", ".login") | Out-Host
if ((Invoke-Checked git @("branch", "--show-current")).Trim() -ne "main") { throw "Publish from main." }
if (Invoke-Checked git @("status", "--porcelain")) { throw "Commit reviewed changes first." }
Invoke-Checked git @("-c", "http.sslBackend=openssl", "fetch", "origin", "main") | Out-Host
Invoke-Checked git @("merge-base", "--is-ancestor", "origin/main", "HEAD") | Out-Host
Invoke-Checked git @("-c", "http.sslBackend=openssl", "push", "origin", "main") | Out-Host
$commit = (Invoke-Checked git @("rev-parse", "HEAD")).Trim()
$releases = (Invoke-Checked gh @("release", "list", "--repo", $repo, "--limit", "100", "--json", "tagName,isDraft")) | ConvertFrom-Json
$existing = $releases | Where-Object { $_.tagName -eq $version } | Select-Object -First 1
$verifyDir = Join-Path $PSScriptRoot ("build/release-verify-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $verifyDir | Out-Null
$checksum = Join-Path $verifyDir "SHA256SUMS.txt"
"$hash  MemoryGuardianSetup.exe" | Set-Content -LiteralPath $checksum -Encoding ASCII
if (!$existing) {
    Invoke-Checked gh @("release", "create", $version, $installer, $checksum, "--repo", $repo,
        "--target", $commit, "--draft", "--title", "Memory Guardian 1.3.18",
        "--notes-file", "docs/RELEASE_NOTES_1.3.18.md") | Out-Host
}
$downloadDir = Join-Path $verifyDir "download"
New-Item -ItemType Directory -Path $downloadDir | Out-Null
Invoke-Checked gh @("release", "download", $version, "--repo", $repo, "--dir", $downloadDir,
    "--pattern", "MemoryGuardianSetup.exe", "--pattern", "SHA256SUMS.txt") | Out-Host
$remoteHash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $downloadDir "MemoryGuardianSetup.exe")).Hash
$remoteChecksum = (Get-Content -LiteralPath (Join-Path $downloadDir "SHA256SUMS.txt") -Raw).Trim()
if ($remoteHash -ne $hash -or $remoteChecksum -ne "$hash  MemoryGuardianSetup.exe") {
    throw "Remote assets mismatch. No assets replaced and update metadata not published."
}
if (!$existing -or $existing.isDraft) {
    Invoke-Checked gh @("release", "edit", $version, "--repo", $repo, "--draft=false", "--latest") | Out-Host
}
$release = (Invoke-Checked gh @("release", "view", $version, "--repo", $repo, "--json", "isDraft,url")) | ConvertFrom-Json
if ($release.isDraft) { throw "Release still draft. Update metadata not published." }
# Publish version-pinned metadata only after the uploaded assets have been verified.
$manifest = [ordered]@{
    version = "1.3.18"
    downloadUrl = "https://github.com/$repo/releases/download/$version/MemoryGuardianSetup.exe"
    sha256 = $hash
    checksumUrl = "https://github.com/$repo/releases/download/$version/SHA256SUMS.txt"
    notes = "Private-commit leak analysis, responsive monitoring, accurate metrics, and conservative self-cleanup."
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath "update.json" -Encoding UTF8
Copy-Item -LiteralPath $checksum -Destination "SHA256SUMS.txt" -Force
Invoke-Checked git @("add", "--", "update.json", "SHA256SUMS.txt") | Out-Host
& git diff --cached --quiet
if ($LASTEXITCODE -eq 1) {
    Invoke-Checked git @("commit", "-m", "Publish verified update metadata for $version") | Out-Host
} elseif ($LASTEXITCODE -ne 0) { throw "Cannot inspect staged metadata." }
Invoke-Checked git @("-c", "http.sslBackend=openssl", "push", "origin", "main") | Out-Host
Write-Host "Published and verified: $($release.url)"
