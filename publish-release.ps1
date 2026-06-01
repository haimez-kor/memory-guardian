$ErrorActionPreference = "Stop"
$repo = "haimez-kor/memory-guardian"
$version = "v1.1.3"
$projectDir = Split-Path -Parent $MyInvocation.MyCommand.Path

Set-Location $projectDir

Write-Host ""
Write-Host "Memory Guardian GitHub publish start" -ForegroundColor Cyan
Write-Host "Repo: $repo"
Write-Host "Version: $version"
Write-Host ""

git status --short --branch
git push origin main

$notes = @"
## Memory Guardian 1.1.3

- Register background startup protection
- Close button choices: hide window, quit fully, or cancel
- System tray menu: open window, daily report, quit
- Add SHA-256 update verification metadata
- Provide SHA256SUMS.txt to detect corrupt or modified downloads

SHA-256:
7465FD34FFAC302F4CA2F9C8DB6D056AFA1C9AA0C58B971B94F569B0E8E23C63
"@

$previousErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
gh release view $version --repo $repo *> $null
$releaseExists = ($LASTEXITCODE -eq 0)
$ErrorActionPreference = $previousErrorActionPreference

if ($releaseExists) {
    Write-Host "Existing release found. Replacing assets." -ForegroundColor Yellow
    gh release upload $version .\MemoryGuardianSetup.exe .\SHA256SUMS.txt --repo $repo --clobber
    gh release edit $version --repo $repo --title "Memory Guardian 1.1.3" --notes $notes --latest
} else {
    Write-Host "Creating a new release." -ForegroundColor Green
    gh release create $version .\MemoryGuardianSetup.exe .\SHA256SUMS.txt --repo $repo --title "Memory Guardian 1.1.3" --notes $notes --latest
}

Write-Host ""
Write-Host "Done." -ForegroundColor Green
Write-Host "https://github.com/haimez-kor/memory-guardian/releases/tag/$version"
Write-Host ""
Read-Host "Press Enter to close"

