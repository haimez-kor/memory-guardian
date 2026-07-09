param([switch]$Elevated)

$ErrorActionPreference = "Stop"

function Test-Admin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

$sourceRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$persistentRoot = Join-Path $env:TEMP "MemoryGuardianInstaller"

if (-not (Test-Admin)) {
    New-Item -ItemType Directory -Force $persistentRoot | Out-Null
    Copy-Item (Join-Path $sourceRoot "app.zip") (Join-Path $persistentRoot "app.zip") -Force
    Copy-Item $MyInvocation.MyCommand.Path (Join-Path $persistentRoot "install.ps1") -Force
    Start-Process powershell -Verb RunAs -Wait -ArgumentList @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", "`"$persistentRoot\install.ps1`"",
        "-Elevated"
    )
    exit
}

$installDir = Join-Path $env:ProgramFiles "Memory Guardian"
$extractDir = Join-Path $env:TEMP "MemoryGuardianPayload"

if (Test-Path $extractDir) {
    Remove-Item $extractDir -Recurse -Force
}
New-Item -ItemType Directory -Force $extractDir | Out-Null
New-Item -ItemType Directory -Force $installDir | Out-Null

Expand-Archive -Path (Join-Path $sourceRoot "app.zip") -DestinationPath $extractDir -Force
Copy-Item (Join-Path $extractDir "*") $installDir -Recurse -Force

$exe = Join-Path $installDir "MemoryGuardian.exe"
$shell = New-Object -ComObject WScript.Shell
$taskName = "Memory Guardian Background Protection"

$desktopShortcut = Join-Path ([Environment]::GetFolderPath("CommonDesktopDirectory")) "메모리 자동 보호기.lnk"
$shortcut = $shell.CreateShortcut($desktopShortcut)
$shortcut.TargetPath = $exe
$shortcut.WorkingDirectory = $installDir
$shortcut.IconLocation = $exe
$shortcut.Save()

$startMenuDir = Join-Path ([Environment]::GetFolderPath("CommonPrograms")) "Memory Guardian"
New-Item -ItemType Directory -Force $startMenuDir | Out-Null
$startShortcut = Join-Path $startMenuDir "메모리 자동 보호기.lnk"
$shortcut = $shell.CreateShortcut($startShortcut)
$shortcut.TargetPath = $exe
$shortcut.WorkingDirectory = $installDir
$shortcut.IconLocation = $exe
$shortcut.Save()

$action = New-ScheduledTaskAction -Execute $exe -Argument "--background" -WorkingDirectory $installDir
$trigger = New-ScheduledTaskTrigger -AtLogOn -User "$env:USERDOMAIN\$env:USERNAME"
$principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -AllowStartIfOnBatteries -DisallowStartIfOnBatteries:$false
Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Force | Out-Null

$uninstall = @'
param([switch]$Elevated)

function Test-Admin {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-Admin)) {
    Start-Process powershell -Verb RunAs -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "`"$PSCommandPath`"", "-Elevated")
    exit
}

$installDir = Join-Path $env:ProgramFiles "Memory Guardian"
$taskName = "Memory Guardian Background Protection"
Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
Remove-Item (Join-Path ([Environment]::GetFolderPath("CommonDesktopDirectory")) "메모리 자동 보호기.lnk") -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path ([Environment]::GetFolderPath("CommonPrograms")) "Memory Guardian") -Recurse -Force -ErrorAction SilentlyContinue

$filesToRemove = @(
    "MemoryGuardian.exe",
    "MemoryGuardianQt.exe",
    "update.json",
    "LICENSE",
    "USER_AGREEMENT.md",
    "USER_AGREEMENT.en.md",
    "ERROR_REPORTING.md",
    "README.ko.md",
    "README.en.md"
)

$foldersToRemove = @(
    "docs",
    "generic",
    "imageformats",
    "networkinformation",
    "platforms",
    "styles",
    "tls"
)

Get-ChildItem $installDir -Filter "*.dll" -File -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
foreach ($file in $filesToRemove) {
    Remove-Item (Join-Path $installDir $file) -Force -ErrorAction SilentlyContinue
}
foreach ($folder in $foldersToRemove) {
    Remove-Item (Join-Path $installDir $folder) -Recurse -Force -ErrorAction SilentlyContinue
}

# Keep reports, logs, learned settings, and other user data unless the user removes them manually.
Get-ChildItem $installDir -Force -ErrorAction SilentlyContinue | Where-Object {
    -not $_.PSIsContainer -and
    $_.Name -ne "profile.ini" -and
    $_.Name -ne "memory_history.csv" -and
    $_.Name -ne "process_history.csv"
} | Remove-Item -Force -ErrorAction SilentlyContinue
'@

Set-Content -Path (Join-Path $installDir "uninstall.ps1") -Value $uninstall -Encoding UTF8

Write-Host "설치가 완료되었습니다: $installDir"
Write-Host "PC를 켤 때 자동으로 백그라운드 보호가 시작되도록 등록했습니다."
Write-Host "Windows가 관리자 권한 허용 창을 보여주면 '예'를 눌러주세요."
Start-Process $exe
