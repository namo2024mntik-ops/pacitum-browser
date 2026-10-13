#define AppName "Pacitum Browser"
#define AppVer "1.4.0"
#define AppExe "PacitumBrowser.exe"
[Setup]
AppId={{FD3A16C9-4CF0-4B0B-BBEC-A5E3334D7CFE}
AppName={#AppName}
AppVersion={#AppVer}
AppVerName={#AppName} {#AppVer}
AppPublisher=Pacitum
DefaultDirName={autopf}\Pacitum Browser
DefaultGroupName=Pacitum Browser
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
SetupIconFile=resources\pacitum.ico
OutputDir=installer
OutputBaseFilename=PacitumBrowser-Setup-{#AppVer}
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
PrivilegesRequiredOverridesAllowed=dialog
; --- in-place upgrade (same AppId as 1.1.0) ---
UsePreviousAppDir=yes
UsePreviousGroup=yes
UsePreviousTasks=yes
UsePreviousPrivileges=yes
DisableDirPage=auto
DisableProgramGroupPage=auto
DisableReadyPage=no
CloseApplications=yes
CloseApplicationsFilter=*.exe,*.dll
RestartApplications=yes
AppMutex=PacitumBrowserRunning
VersionInfoVersion={#AppVer}
VersionInfoProductName={#AppName}
LZMAUseSeparateProcess=yes
[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"
[InstallDelete]
; stale Qt files from older builds (user data lives in %LOCALAPPDATA%, not here)
Type: filesandordirs; Name: "{app}\translations"
Type: filesandordirs; Name: "{app}\resources\qtwebengine_devtools_resources.pak"

[Files]
Source: "dist\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
[Icons]
Name: "{group}\Pacitum Browser"; Filename: "{app}\{#AppExe}"
Name: "{group}\Uninstall Pacitum Browser"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Pacitum Browser"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon
[Run]
Filename: "{app}\{#AppExe}"; Description: "Launch Pacitum Browser"; Flags: nowait postinstall skipifsilent
