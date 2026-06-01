; Inno Setup script for Meketreve OBS Essentials
;
; Installs the plugin into the per-machine OBS plugin directory
;   %ProgramData%\obs-studio\plugins\<name>\
; which OBS 30+ scans on startup. No admin rights or OBS path detection needed.
;
; Values are normally supplied by the CI packaging step via /D defines; the
; fallbacks below let you build the installer locally too.

#ifndef MyAppName
  #define MyAppName "meketreve-obs-essentials"
#endif
#ifndef MyAppVersion
  #define MyAppVersion "1.0.0"
#endif
#ifndef MyAppPublisher
  #define MyAppPublisher "meketreve"
#endif
; SourceDir must point at release\<Config>\<name> (contains bin\ and data\)
#ifndef SourceDir
  #define SourceDir "..\release\RelWithDebInfo\" + MyAppName
#endif
#ifndef OutputDir
  #define OutputDir "..\release"
#endif

[Setup]
AppId={{B7F3A2E1-9C4D-4A6B-8E11-MEKETREVE0001}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL=https://github.com/meketreve/meketreve-obs-essentials
DefaultDirName={commonappdata}\obs-studio\plugins\{#MyAppName}
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
OutputDir={#OutputDir}
OutputBaseFilename={#MyAppName}-{#MyAppVersion}-windows-x64-installer
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
UninstallDisplayName={#MyAppName} {#MyAppVersion}

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
