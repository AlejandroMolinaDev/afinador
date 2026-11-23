; NovaPitch — Windows installer (Inno Setup 6).
;
; Build (CI does this):
;   iscc /DAppVersion=0.9.0 /DArtefactsDir=<...>\NovaPitch_artefacts\Release /DDocsDir=<folder with Guia_de_usuario.html>
;        /DOutputDir=<dist> installer\windows\NovaPitch.iss
;
; FROZEN: AppId identifies the product for upgrades and uninstall. Never change it.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef ArtefactsDir
  #define ArtefactsDir "..\..\build\NovaPitch_artefacts\Release"
#endif
#ifndef DocsDir
  #define DocsDir "..\..\build"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

[Setup]
AppId={{237AEEF3-3E15-4C10-9146-21EAD2F85A67}
AppName=NovaPitch
AppVersion={#AppVersion}
AppVerName=NovaPitch {#AppVersion}
AppPublisher=NovaPitch Audio
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\NovaPitch
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir={#OutputDir}
OutputBaseFilename=NovaPitch-{#AppVersion}-Windows-x64-Setup
Compression=lzma2/max
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
MinVersion=10.0
WizardStyle=modern
LicenseFile=..\LICENCIAS.txt
UninstallDisplayName=NovaPitch {#AppVersion}
UninstallDisplayIcon={app}\NovaPitch.exe
; FL Studio (or any host) keeping the plugin loaded: offer to close it.
CloseApplications=yes
RestartApplications=no
SetupLogging=yes

[Languages]
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "Instalación completa"
Name: "custom"; Description: "Instalación personalizada"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 (FL Studio y la mayoría de programas)"; Types: full custom
Name: "clap"; Description: "CLAP"; Types: full
Name: "standalone"; Description: "Aplicación independiente"; Types: full

[Files]
Source: "{#ArtefactsDir}\VST3\NovaPitch.vst3\*"; DestDir: "{commoncf64}\VST3\NovaPitch.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#ArtefactsDir}\CLAP\NovaPitch.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion
Source: "{#ArtefactsDir}\Standalone\NovaPitch.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "{#DocsDir}\Guia_de_usuario.html"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\THIRD_PARTY_LICENSES.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\NovaPitch"; Filename: "{app}\NovaPitch.exe"; Components: standalone
Name: "{autoprograms}\NovaPitch - Guía de usuario"; Filename: "{app}\Guia_de_usuario.html"

[Run]
Filename: "{app}\Guia_de_usuario.html"; Description: "Abrir la guía de usuario"; Flags: postinstall shellexec skipifsilent unchecked

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\NovaPitch.vst3"
