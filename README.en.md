# Memory Guardian

Memory Guardian is an open-source Windows utility that monitors RAM usage, learns daily usage patterns, and automatically starts memory cleanup when the configured or learned threshold is exceeded.

## Download

- Latest release: https://github.com/haimez-kor/memory-guardian/releases/latest
- Installer: `MemoryGuardianSetup.exe`
- Source code: https://github.com/haimez-kor/memory-guardian
- Checksum file: `SHA256SUMS.txt`

After downloading the installer, you can verify file integrity with:

```powershell
Get-FileHash -Algorithm SHA256 .\MemoryGuardianSetup.exe
```

Compare the result with the value in `SHA256SUMS.txt` or `update.json`.

## Features

- Administrator permission manifest
- Modern GUI for non-technical users
- Optimization score based RAM status
- Commit memory, page file, non-paged pool, and paged pool tracking
- Top RAM processes with daily growth
- Per-process RAM growth CSV saved every 10 minutes
- Leak suspicion status based on process growth and RAM trend
- Automatic RAM cleanup based on memory pressure
- Daily RAM usage report
- Hourly RAM usage tracking
- 1-hour temporary learning and full daily adaptive threshold learning
- Background protection when the window is closed
- System tray menu for reopening, viewing report, or quitting
- Optional Windows startup background task
- SHA-256 update metadata for tamper/corruption checks

## Open Source Notice

This project is open source and licensed under the MIT License. You may use, copy, modify, build, and redistribute it.

If you redistribute the original project, a modified version, or a package that includes part of this project, keep the original copyright and license notice:

```text
Memory Guardian
Copyright (c) 2026 haimez-kor
Licensed under the MIT License.
Original project: https://github.com/haimez-kor/memory-guardian
```

Use of this software is at the user's own responsibility. See [USER_AGREEMENT.en.md](USER_AGREEMENT.en.md), [USER_AGREEMENT.md](USER_AGREEMENT.md), and [LICENSE](LICENSE).

## Build

Build the app:

```bat
build.bat
```

Build the Inno Setup installer:

```bat
build-inno-installer.bat
```

Build the legacy bundled installer:

```bat
package-installer.bat
```
