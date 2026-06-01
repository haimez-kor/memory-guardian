$ErrorActionPreference = "Stop"
$repo = "haimez-kor/memory-guardian"
$version = "v1.2.0"
$displayVersion = $version.TrimStart("v")
$projectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$installer = Join-Path $projectDir "MemoryGuardianSetup.exe"
$checksumFile = Join-Path $projectDir "SHA256SUMS.txt"
$updateFile = Join-Path $projectDir "update.json"

Set-Location $projectDir

if (!(Test-Path $installer)) {
    throw "MemoryGuardianSetup.exe was not found. Run build-inno-installer.bat first."
}

$hash = (Get-FileHash -Algorithm SHA256 $installer).Hash.ToUpperInvariant()
"$hash  MemoryGuardianSetup.exe" | Set-Content -Path $checksumFile -Encoding UTF8

$manifest = [ordered]@{
    version = $displayVersion
    downloadUrl = "https://github.com/$repo/releases/latest/download/MemoryGuardianSetup.exe"
    sha256 = $hash
    checksumUrl = "https://github.com/$repo/releases/latest/download/SHA256SUMS.txt"
    notes = "Adds process detail view with PID, start/current memory, growth amount, MB per hour, and learned status labels such as normal pattern, warning, and leak suspected."
}
$manifest | ConvertTo-Json -Depth 3 | Set-Content -Path $updateFile -Encoding UTF8

Write-Host ""
Write-Host "Memory Guardian GitHub publish start" -ForegroundColor Cyan
Write-Host "Repo: $repo"
Write-Host "Version: $version"
Write-Host "SHA-256: $hash"
Write-Host ""

git status --short --branch
git add -u
git add update.json SHA256SUMS.txt
git diff --cached --quiet
if ($LASTEXITCODE -ne 0) {
    git commit -m "Update release metadata for $version"
}
git push origin main

$notes = @"
## Memory Guardian 1.2.0

- Add commit memory, page file, Non-Paged Pool, and Paged Pool tracking
- Show top RAM processes with today's growth amount
- Save per-process RAM records every 10 minutes
- Add leak suspicion status for process growth and RAM trend
- Add 1-hour temporary learning before the full daily learned threshold
- Request administrator permission when the app starts so memory cleanup can work properly
- Stop repeated cleanup attempts for 10 minutes when RAM does not drop after cleanup
- Improve daily report card spacing to prevent clipped numbers
- Keep reports and learned settings during automatic reinstall updates
- Add process detail view with PID, start memory, current memory, growth amount, and MB per hour
- Label processes as normal pattern, warning, or leak suspected
- Save process growth speed and pattern labels to CSV and daily reports
- Keep SHA-256 update verification metadata for corruption/tamper checks

SHA-256:
$hash
"@

$previousErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
gh release view $version --repo $repo *> $null
$releaseExists = ($LASTEXITCODE -eq 0)
$ErrorActionPreference = $previousErrorActionPreference

if ($releaseExists) {
    Write-Host "Existing release found. Replacing assets." -ForegroundColor Yellow
    gh release upload $version .\MemoryGuardianSetup.exe .\SHA256SUMS.txt --repo $repo --clobber
    gh release edit $version --repo $repo --title "Memory Guardian 1.2.0" --notes $notes --latest
} else {
    Write-Host "Creating a new release." -ForegroundColor Green
    gh release create $version .\MemoryGuardianSetup.exe .\SHA256SUMS.txt --repo $repo --title "Memory Guardian 1.2.0" --notes $notes --latest
}

Write-Host ""
Write-Host "Done." -ForegroundColor Green
Write-Host "https://github.com/haimez-kor/memory-guardian/releases/tag/$version"
Write-Host ""
Read-Host "Press Enter to close"


