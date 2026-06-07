@echo off
setlocal

set PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%

call build.bat
if errorlevel 1 exit /b 1

set DIST=dist-app-v1.3.8
if exist "%DIST%" rmdir /S /Q "%DIST%"
mkdir "%DIST%"

copy /Y build\MemoryGuardian.exe "%DIST%\" >nul
for %%F in (build\*.dll) do copy /Y "%%F" "%DIST%\" >nul
for %%D in (generic imageformats networkinformation platforms styles tls) do (
  if exist "build\%%D" xcopy /E /I /Y "build\%%D" "%DIST%\%%D" >nul
)
copy /Y installer\update.json "%DIST%\update.json" >nul
copy /Y LICENSE "%DIST%\LICENSE" >nul
copy /Y USER_AGREEMENT.md "%DIST%\USER_AGREEMENT.md" >nul
copy /Y USER_AGREEMENT.en.md "%DIST%\USER_AGREEMENT.en.md" >nul
copy /Y README.md "%DIST%\README.ko.md" >nul
copy /Y README.en.md "%DIST%\README.en.md" >nul

if not "%~1"=="" set "ISCC=%~1"
if defined INNO_ISCC set "ISCC=%INNO_ISCC%"
if exist "C:\Users\gfs2\AppData\Local\Programs\Inno Setup 6\ISCC.exe" set "ISCC=C:\Users\gfs2\AppData\Local\Programs\Inno Setup 6\ISCC.exe"

where ISCC.exe >nul 2>nul
if not errorlevel 1 for /f "delims=" %%P in ('where ISCC.exe') do if "%ISCC%"=="" set "ISCC=%%P"

for %%P in (
  "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
  "%ProgramFiles%\Inno Setup 6\ISCC.exe"
  "%LocalAppData%\Programs\Inno Setup 6\ISCC.exe"
  "%UserProfile%\AppData\Local\Programs\Inno Setup 6\ISCC.exe"
  "%ProgramFiles(x86)%\Inno Setup 5\ISCC.exe"
  "%ProgramFiles%\Inno Setup 5\ISCC.exe"
) do (
  if exist %%~P if "%ISCC%"=="" set ISCC=%%~P
)

if "%ISCC%"=="" (
  for /f "usebackq delims=" %%P in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$candidates = @(); $roots = @($env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:LOCALAPPDATA, $env:ProgramData) | Where-Object { $_ }; foreach ($root in $roots) { $candidates += Get-ChildItem -Path $root -Filter ISCC.exe -Recurse -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName }; $cmd = Get-Command ISCC.exe -ErrorAction SilentlyContinue; if ($cmd) { $candidates += $cmd.Source }; $candidates | Select-Object -First 1"`) do if not "%%P"=="" set "ISCC=%%P"
)

if "%ISCC%"=="" (
  echo Inno Setup compiler was not found.
  echo Install Inno Setup 6, then run this file again.
  echo Official site: https://jrsoftware.org/isinfo.php
  echo If it is already installed, run this and send the result:
  echo powershell -NoProfile -Command "Get-ChildItem -Path $env:ProgramFiles,${env:ProgramFiles(x86)},$env:LOCALAPPDATA -Recurse -Filter ISCC.exe -ErrorAction SilentlyContinue ^| Select-Object -ExpandProperty FullName"
  exit /b 2
)

echo Using Inno Setup compiler: "%ISCC%"
"%ISCC%" installer\MemoryGuardian.iss
if errorlevel 1 exit /b 1

echo Built MemoryGuardianSetup.exe with Inno Setup.
endlocal





