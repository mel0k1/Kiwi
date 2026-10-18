; Kiwi Windows installer (Inno Setup 6).
; CI invocation: ISCC /DKiwiVersion=<x.y.z> packaging/windows/kiwi.iss
; Paths are relative to this file: it packs the bundle filled by
; scripts/bundle-windows.sh and drops the installer into <repo>/build.

#ifndef KiwiVersion
#define KiwiVersion "0.0.0"
#endif

[Setup]
AppId={{8F4A9C3E-1D2B-4C7E-A5F6-0B9D8E7C6A5B}
AppName=Kiwi
AppVersion={#KiwiVersion}
AppPublisher=mel0k1
AppPublisherURL=https://github.com/mel0k1/Kiwi
AppSupportURL=https://github.com/mel0k1/Kiwi/issues
DefaultDirName={localappdata}\Programs\Kiwi
PrivilegesRequired=lowest
DisableProgramGroupPage=yes
OutputDir=..\..\build
OutputBaseFilename=kiwi-setup-x64
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\kiwi.exe
CloseApplications=no
RestartApplications=no

[Tasks]
Name: "desktop"; Description: "{cm:CreateDesktopIcon}"; \
    GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\..\build\bundle\*"; DestDir: "{app}"; \
    Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{autoprograms}\Kiwi"; Filename: "{app}\kiwi.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\Kiwi"; Filename: "{app}\kiwi.exe"; WorkingDir: "{app}"; Tasks: desktop

[Run]
Filename: "{app}\kiwi.exe"; WorkingDir: "{app}"; \
    Description: "{cm:LaunchProgram,Kiwi}"; Flags: nowait postinstall skipifsilent

; user data (settings.toml, profiles) under %LocalAppData%\kiwi intentionally survives uninstall
