#define AppName "Safari Browser"
#define AppVer "1.0.0"
[Setup]
AppId={{8C3F2A51-6B1E-4D7A-9E2F-5A1C0B7D3E91}
AppName={#AppName}
AppVersion={#AppVer}
AppPublisher=Patch
DefaultDirName={autopf}\Safari Browser
DefaultGroupName=Safari Browser
UninstallDisplayIcon={app}\SafariBrowser.exe
OutputDir=installer
OutputBaseFilename=SafariBrowser-Setup-{#AppVer}
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
PrivilegesRequiredOverridesAllowed=dialog
[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"
[Files]
Source: "dist\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
[Icons]
Name: "{group}\Safari Browser"; Filename: "{app}\SafariBrowser.exe"
Name: "{group}\Uninstall Safari Browser"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Safari Browser"; Filename: "{app}\SafariBrowser.exe"; Tasks: desktopicon
[Run]
Filename: "{app}\SafariBrowser.exe"; Description: "Launch Safari Browser"; Flags: nowait postinstall skipifsilent
