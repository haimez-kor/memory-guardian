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
Remove-Item (Join-Path ([Environment]::GetFolderPath("CommonDesktopDirectory")) "메모리 자동 보호기.lnk") -Force -ErrorAction SilentlyContinue
Remove-Item (Join-Path ([Environment]::GetFolderPath("CommonPrograms")) "Memory Guardian") -Recurse -Force -ErrorAction SilentlyContinue
Start-Process cmd -WindowStyle Hidden -ArgumentList "/c timeout /t 2 >nul & rmdir /s /q `"$installDir`""
'@

Set-Content -Path (Join-Path $installDir "uninstall.ps1") -Value $uninstall -Encoding UTF8

Write-Host "설치가 완료되었습니다: $installDir"
Start-Process $exe
