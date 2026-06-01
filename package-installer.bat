@echo off
setlocal

set PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%

call build.bat
if errorlevel 1 exit /b 1

set DIST=dist-app-v1.1.1
if not exist "%DIST%" mkdir "%DIST%"

copy /Y build\MemoryGuardian.exe "%DIST%\" >nul
for %%F in (build\*.dll) do copy /Y "%%F" "%DIST%\" >nul
for %%D in (generic imageformats networkinformation platforms styles tls) do (
  if exist "build\%%D" xcopy /E /I /Y "build\%%D" "%DIST%\%%D" >nul
)
copy /Y installer\update.json "%DIST%\update.json" >nul

if exist installer\app.zip del /Q installer\app.zip
powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%DIST%\*' -DestinationPath 'installer\app.zip' -Force"
if errorlevel 1 exit /b 1

pushd installer
windres installer.rc -O coff -o installer_res.o
if errorlevel 1 exit /b 1
g++ -municode -mwindows -static-libgcc -static-libstdc++ installer.cpp installer_res.o -o ..\MemoryGuardianSetup.exe -lshell32 -luser32
if errorlevel 1 exit /b 1
popd

echo Built MemoryGuardianSetup.exe
endlocal
