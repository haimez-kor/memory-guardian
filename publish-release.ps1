$ErrorActionPreference = "Stop"
$repo = "haimez-kor/memory-guardian"
$version = "v1.1.4"
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
## Memory Guardian 1.1.4

- Register background startup protection
- Close button choices: hide window, quit fully, or cancel
- System tray menu: open window, daily report, quit
- Add SHA-256 update verification metadata
- Provide SHA256SUMS.txt to detect corrupt or modified downloads
- Add Korean and English README/user agreement documents
- Include open-source download and source-code guidance

SHA-256:
BFC824266C5A2FDBD0DE31529DF11F87BE488A35D419E26C24C9316F95AA6F03
"@

$previousErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
gh release view $version --repo $repo *> $null
$releaseExists = ($LASTEXITCODE -eq 0)
$ErrorActionPreference = $previousErrorActionPreference

if ($releaseExists) {
    Write-Host "Existing release found. Replacing assets." -ForegroundColor Yellow
    gh release upload $version .\MemoryGuardianSetup.exe .\SHA256SUMS.txt --repo $repo --clobber
    gh release edit $version --repo $repo --title "Memory Guardian 1.1.4" --notes $notes --latest
} else {
    Write-Host "Creating a new release." -ForegroundColor Green
    gh release create $version .\MemoryGuardianSetup.exe .\SHA256SUMS.txt --repo $repo --title "Memory Guardian 1.1.4" --notes $notes --latest
}

Write-Host ""
Write-Host "Done." -ForegroundColor Green
Write-Host "https://github.com/haimez-kor/memory-guardian/releases/tag/$version"
Write-Host ""
Read-Host "Press Enter to close"


