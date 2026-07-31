#ifndef MyAppVersion
  #define MyAppVersion "dev"
#endif
#ifndef PackageSource
  #error PackageSource must point to the deployed Pinloom directory
#endif
#ifndef PackageOutput
  #error PackageOutput must point to the installer output directory
#endif

[Setup]
AppId={{0359A8C7-F68E-4BFD-AFAF-A1DE33413C5A}
AppName=Pinloom
AppVersion={#MyAppVersion}
AppPublisher=Pinloom
DefaultDirName={localappdata}\Programs\Pinloom
DefaultGroupName=Pinloom
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#PackageOutput}
OutputBaseFilename=Pinloom-Setup-x64-{#MyAppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\pinloom_app.exe
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "autostart"; Description: "Start Pinloom when signing in"; GroupDescription: "Startup"; Flags: unchecked

[Files]
Source: "{#PackageSource}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Pinloom"; Filename: "{app}\pinloom_app.exe"
Name: "{autodesktop}\Pinloom"; Filename: "{app}\pinloom_app.exe"; Tasks: desktopicon
Name: "{userstartup}\Pinloom"; Filename: "{app}\pinloom_app.exe"; Parameters: "--hidden"; Tasks: autostart

[Run]
Filename: "{app}\pinloom_app.exe"; Description: "Launch Pinloom"; Flags: nowait postinstall skipifsilent
