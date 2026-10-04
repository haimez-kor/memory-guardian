$ErrorActionPreference = "Stop"
Set-Location -LiteralPath $PSScriptRoot
$compiler = Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe"
if (!(Test-Path -LiteralPath $compiler)) { throw "Inno Setup 6 not found: $compiler" }
if (!(Test-Path -LiteralPath "dist-app-v1.3.18/MemoryGuardian.exe")) { throw "Prepared app payload is missing." }
& gh api user --jq .login
if ($LASTEXITCODE -ne 0) { throw "GitHub login is required in this Windows account." }
& git diff --cached --quiet
if ($LASTEXITCODE -ne 0) { throw "Review existing staged changes before publishing." }
& $compiler "installer/MemoryGuardian.iss"
if ($LASTEXITCODE -ne 0) { throw "Installer build failed. Nothing uploaded." }
$files = @(
    "CMakeLists.txt", "README.md", "build-inno-installer.bat", "installer/MemoryGuardian.iss",
    "installer/update.json", "package-installer.bat", "publish-release.ps1", "finish-release.ps1",
    "src/main.cpp", "docs/DIAGNOSTIC_LIMITS.md", "docs/RELEASE_NOTES_1.3.18.md", "tests/diagnostics.cpp"
)
& git add -- @files
if ($LASTEXITCODE -ne 0) { throw "Could not stage release source." }
& git diff --cached --quiet
if ($LASTEXITCODE -eq 1) {
    & git commit -m "Prepare v1.3.18 diagnostics and verified publishing"
    if ($LASTEXITCODE -ne 0) { throw "Could not commit release source." }
} elseif ($LASTEXITCODE -ne 0) { throw "Could not inspect staged changes." }
& (Join-Path $PSScriptRoot "publish-release.ps1")
