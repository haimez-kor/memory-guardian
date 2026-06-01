@echo off
setlocal

set PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%

call build.bat
if errorlevel 1 exit /b 1

set DIST=dist-app-v1.1.4
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

set ISCC=
for %%P in (
  "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
  "%ProgramFiles%\Inno Setup 6\ISCC.exe"
) do (
  if exist %%~P set ISCC=%%~P
)

if "%ISCC%"=="" (
  echo Inno Setup compiler was not found.
  echo Install Inno Setup 6, then run this file again.
  echo Official site: https://jrsoftware.org/isinfo.php
  exit /b 2
)

"%ISCC%" installer\MemoryGuardian.iss
if errorlevel 1 exit /b 1

echo Built MemoryGuardianSetup.exe with Inno Setup.
endlocal
