$ErrorActionPreference = "Stop"
$repo = "haimez-kor/memory-guardian"
$version = "v1.3.6"
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
    notes = "Shows every readable running process and adds instant process-name/PID search with live result counts."
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
## Memory Guardian 1.3.6

- Show every readable running process instead of only the top 20
- Add instant search by process name or PID
- Show total and filtered process counts
- Keep the selected process when the table refreshes
- Replace the compressed process text list with a scrollable ranked table
- Show real Windows application icons when the executable path is available
- Add RAM usage, growth amount, and pattern columns
- Update the process detail panel when a row is selected
- Separate Process and Activity Log views into tabs
- Add a ? button that explains the optimization score deductions
- Hide process growth speed until at least 10 minutes of observation
- Show long-term trend tooltips with time, RAM, commit, and change amount
- Default new installs to General PC mode and hide the server panel outside Server/Developer mode
- Polish the startup window layout to prevent metric text from overlapping the progress bar
- Move long leak suspicion text onto a wider summary row
- Keep auto-update installs restarting Memory Guardian after completion
- Restart Memory Guardian automatically after silent auto-update installs
- Keep a Finish-screen launch checkbox for normal installer runs
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
- Persist auto/manual threshold, cleanup action, operation mode, and theme after restart
- Make control labels easier to understand
- Add a long-term trend window with 1-hour, 24-hour, 7-day, and 30-day ranges
- Read trend graphs from the 5-minute memory history file
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
    gh release edit $version --repo $repo --title "Memory Guardian 1.3.6" --notes $notes --latest
} else {
    Write-Host "Creating a new release." -ForegroundColor Green
    gh release create $version .\MemoryGuardianSetup.exe .\SHA256SUMS.txt --repo $repo --title "Memory Guardian 1.3.6" --notes $notes --latest
}

Write-Host ""
Write-Host "Done." -ForegroundColor Green
Write-Host "https://github.com/haimez-kor/memory-guardian/releases/tag/$version"
Write-Host ""
Read-Host "Press Enter to close"





