# Memory Guardian User Agreement and Open Source Notice

This document explains the usage terms, redistribution notice, and responsibility scope for Memory Guardian ("the Software"). By installing, running, copying, modifying, or redistributing the Software, you acknowledge that you understand and agree to this notice.

## 1. Open Source Notice

The Software is open source. You may use it for personal, educational, research, commercial, or other purposes. You may inspect, copy, modify, build, and redistribute the source code.

The Software is licensed under the MIT License included in the `LICENSE` file. The MIT License allows use, copy, modification, merge, publication, distribution, sublicensing, and sale of copies, provided that the original copyright notice and license notice are preserved.

## 2. Attribution Requirement

If you redistribute the Software, distribute a modified version, or include part of the code in another project, keep the following notice in a reasonably visible location such as README, help page, license file, about screen, distribution page, or source-code notice:

```text
Memory Guardian
Made by HAIMEZ
Copyright (c) 2026 HAIMEZ
Licensed under the MIT License.
Original project: https://github.com/haimez-kor/memory-guardian
```

Private internal use does not require a separate public notice.

## 3. Modification and Redistribution

You may freely modify the Software and redistribute modified versions as repositories, installers, archives, packages, forks, or as part of another product.

When redistributing a modified version, you should clearly identify what was changed and who distributes the modified package to avoid confusion with the original project. Problems caused by modified or third-party redistributed versions are the responsibility of the modifier, distributor, or user, not the original author.

## 4. User Responsibility

The Software observes Windows memory usage and may attempt memory cleanup according to user settings or learned thresholds. Behavior can vary depending on Windows version, permissions, drivers, running applications, security software, system state, and user configuration.

Users must decide whether to install, run, allow administrator permission, enable startup registration, use memory cleanup, check updates, and change settings at their own responsibility.

## 5. Disclaimer

The Software is provided "as is", without warranty of any kind. The author and contributors do not guarantee that the Software is fit for a particular purpose, always error-free, identical on every system, free from performance issues, or completely secure.

The author and contributors are not liable for direct, indirect, incidental, special, or consequential damages arising from use, misuse, installation failure, update failure, permission configuration, startup registration, memory cleanup behavior, modified versions, or third-party redistributed packages.

## 6. Administrator Permission and Startup

Some installation and cleanup features may require Windows administrator permission. If Windows shows a permission prompt, the user must review it and choose whether to allow it.

The installer may register a Windows startup task so Memory Guardian can run in the background after login. Users may remove that task through Windows Task Scheduler or the included uninstall script.

## 7. Updates and Integrity Verification

Update metadata may include a SHA-256 hash. Users can verify the downloaded installer against `SHA256SUMS.txt` or `update.json` to detect corrupted or modified downloads.

Hash verification is a helpful integrity check, but users should still download files only from trusted sources.

## 8. Third-Party Components

The Software may use Qt, MinGW runtime files, Windows APIs, and other third-party or system components. Those components are governed by their own licenses and policies.

## 9. Original Repository

```text
https://github.com/haimez-kor/memory-guardian
```
