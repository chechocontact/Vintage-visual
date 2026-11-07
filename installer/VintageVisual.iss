; Inno Setup 6 script for Vintage Visual (VST3, Windows 11 x64).
; Build:  ISCC.exe /DAppVersion=0.2.0 /DBuildDir="C:\path\to\VintageVisual_artefacts\Release\VST3" installer\VintageVisual.iss
; The version comes from CMakeLists.txt (single source of truth); CI passes it in.

#ifndef AppVersion
  #define AppVersion "0.2.0"
#endif
#ifndef BuildDir
  #define BuildDir "..\build\VintageVisual_artefacts\Release\VST3"
#endif

#define AppName "Vintage Visual"

[Setup]
; Fixed identifier: lets a new version upgrade the old one. Never change it for this product.
AppId={{8F3B6E52-4C1A-4D8E-9A57-2B6D0C1E7F94}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppName}
VersionInfoVersion={#AppVersion}
; The VST3 bundle goes where every DAW scans: C:\Program Files\Common Files\VST3
DefaultDirName={commoncf64}\VST3\{#AppName}.vst3
DisableDirPage=yes
DisableProgramGroupPage=yes
UsePreviousAppDir=no
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.22000
OutputDir=..\dist
OutputBaseFilename=VintageVisual-{#AppVersion}-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#AppName} (VST3)
#ifexist "..\LICENSE"
LicenseFile=..\LICENSE
#endif

[Files]
; restartreplace: if the DAW still has the old plug-in loaded, Windows swaps it on the next restart.
Source: "{#BuildDir}\{#AppName}.vst3\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs restartreplace uninsrestartdelete
#ifexist "..\LICENSE"
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
#endif
#ifexist "..\README.md"
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
#endif

[Messages]
FinishedLabel=Setup installed {#AppName}. Close and reopen your DAW, then rescan plug-ins if it does not appear (in FL Studio: Options > Manage plugins > Find installed plugins).
