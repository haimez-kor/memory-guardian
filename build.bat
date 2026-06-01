@echo off
setlocal

set PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%

cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=C:\msys64\mingw64 -DCMAKE_CXX_COMPILER=C:\msys64\mingw64\bin\g++.exe
if errorlevel 1 exit /b 1

cmake --build build --config Release
if errorlevel 1 exit /b 1

windeployqt --release build\MemoryGuardian.exe
if errorlevel 1 exit /b 1

for %%D in (
  libb2-1.dll
  libgcc_s_seh-1.dll
  libstdc++-6.dll
  libwinpthread-1.dll
  libdouble-conversion.dll
  libzstd.dll
  zlib1.dll
  libpcre2-16-0.dll
  libfreetype-6.dll
  libharfbuzz-0.dll
  libpng16-16.dll
  libbz2-1.dll
  libbrotlidec.dll
  libbrotlicommon.dll
  libmd4c.dll
  libicuin78.dll
  libicuuc78.dll
  libicudt78.dll
) do (
  if exist "C:\msys64\mingw64\bin\%%D" copy /Y "C:\msys64\mingw64\bin\%%D" build\ >nul
)

powershell -NoProfile -ExecutionPolicy Bypass -Command "$bin='C:\msys64\mingw64\bin'; $build=(Resolve-Path 'build').Path; $objdump=Join-Path $bin 'objdump.exe'; $copied=1; while($copied -gt 0){ $copied=0; $files=@(); $files += Get-ChildItem $build -Filter *.exe; $files += Get-ChildItem $build -Filter *.dll -Recurse; foreach($f in $files){ $output=& $objdump -p $f.FullName 2>$null; foreach($line in $output){ if($line -match 'DLL Name:\s*(.+)$'){ $dll=$matches[1].Trim(); $target=Join-Path $build $dll; $source=Join-Path $bin $dll; if((Test-Path $source) -and !(Test-Path $target)){ Copy-Item $source $target -Force; $copied++ } } } } }"
if errorlevel 1 exit /b 1

echo Built build\MemoryGuardian.exe
endlocal
