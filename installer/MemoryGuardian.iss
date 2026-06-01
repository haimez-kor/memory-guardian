#define AppName "Memory Guardian"
#define AppVersion "1.1.8"
#define AppPublisher "HAIMEZ"
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
CloseApplications=yes
RestartApplications=no
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
Source: "..\dist-app-v1.1.8\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
Type: files; Name: "{app}\MemoryGuardian.exe"
Type: files; Name: "{app}\*.dll"
Type: files; Name: "{app}\update.json"
Type: files; Name: "{app}\LICENSE"
Type: files; Name: "{app}\USER_AGREEMENT.md"
Type: files; Name: "{app}\USER_AGREEMENT.en.md"
Type: files; Name: "{app}\README.ko.md"
Type: files; Name: "{app}\README.en.md"
Type: filesandordirs; Name: "{app}\generic"
Type: filesandordirs; Name: "{app}\imageformats"
Type: filesandordirs; Name: "{app}\networkinformation"
Type: filesandordirs; Name: "{app}\platforms"
Type: filesandordirs; Name: "{app}\styles"
Type: filesandordirs; Name: "{app}\tls"

[Icons]
Name: "{group}\Memory Guardian"; Filename: "{app}\MemoryGuardian.exe"; WorkingDir: "{app}"
Name: "{group}\Uninstall Memory Guardian"; Filename: "{uninstallexe}"
Name: "{commondesktop}\Memory Guardian"; Filename: "{app}\MemoryGuardian.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{sys}\schtasks.exe"; Parameters: "/Create /F /TN ""Memory Guardian Background Protection"" /SC ONLOGON /RL HIGHEST /TR ""\""{app}\MemoryGuardian.exe\"" --background"""; Flags: runhidden; Tasks: startup
Filename: "{app}\MemoryGuardian.exe"; Description: "Launch Memory Guardian"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{sys}\schtasks.exe"; Parameters: "/Delete /F /TN ""Memory Guardian Background Protection"""; Flags: runhidden; RunOnceId: "RemoveStartupTask"

