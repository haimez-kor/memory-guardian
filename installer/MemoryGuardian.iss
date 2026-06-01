#define AppName "Memory Guardian"
#define AppVersion "1.1.5"
#define AppPublisher "haimez-kor"
#define AppURL "https://github.com/haimez-kor/memory-guardian"

[Setup]
AppId={{9C899083-3296-4D59-8848-5047DD4E2B2C}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}/releases/latest
DefaultDirName={autopf}\Memory Guardian
DefaultGroupName=Memory Guardian
DisableProgramGroupPage=yes
OutputDir=..
OutputBaseFilename=MemoryGuardianSetup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\MemoryGuardian.exe
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription=Memory Guardian Setup
VersionInfoProductName={#AppName}

[Languages]
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"; LicenseFile: "license-ko.txt"
Name: "english"; MessagesFile: "compiler:Default.isl"; LicenseFile: "license-en.txt"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "startup"; Description: "Start Memory Guardian in the background when Windows starts"; GroupDescription: "Startup"

[Files]
Source: "..\dist-app-v1.1.5\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Memory Guardian"; Filename: "{app}\MemoryGuardian.exe"; WorkingDir: "{app}"
Name: "{group}\Uninstall Memory Guardian"; Filename: "{uninstallexe}"
Name: "{commondesktop}\Memory Guardian"; Filename: "{app}\MemoryGuardian.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{sys}\schtasks.exe"; Parameters: "/Create /F /TN ""Memory Guardian Background Protection"" /SC ONLOGON /RL HIGHEST /TR ""\""{app}\MemoryGuardian.exe\"" --background"""; Flags: runhidden; Tasks: startup
Filename: "{app}\MemoryGuardian.exe"; Description: "Launch Memory Guardian"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\schtasks.exe"; Parameters: "/Delete /F /TN ""Memory Guardian Background Protection"""; Flags: runhidden; RunOnceId: "RemoveStartupTask"
