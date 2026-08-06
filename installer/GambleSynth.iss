; Inno Setup script for GambleSynth.
;
; Built by CI on a Windows runner (see .github/workflows/build-windows.yml),
; which installs Inno Setup and runs this against the freshly built artefacts.
;
; The VST3 goes to the standard shared folder every host scans; the standalone
; goes under Program Files with a Start Menu entry. Both are optional, because
; someone who only wants the standalone should not have to install a plugin.

#define AppName        "GambleSynth"
#define AppPublisher   "HWCDealer"
#define AppURL         "https://github.com/alwynhawk/GambleSynth"
#define AppExeName     "GambleSynth.exe"

; Passed in by CI as /DAppVersion=x.y.z; falls back for local runs.
#ifndef AppVersion
  #define AppVersion "0.9.0"
#endif

[Setup]
AppId={{7A3F1C42-8E5B-4D69-9A21-6C0E4B7D2F18}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
DefaultDirName={autopf}\{#AppPublisher}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
OutputBaseFilename=GambleSynth-{#AppVersion}-Windows
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; The VST3 folder is under Program Files, so this needs elevation.
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
UninstallDisplayName={#AppName} {#AppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full";   Description: "Everything"
Name: "custom"; Description: "Choose what to install"; Flags: iscustom

[Components]
Name: "vst3";       Description: "VST3 plugin (for your DAW)"; Types: full custom; Flags: checkablealone
Name: "standalone"; Description: "Standalone app (no DAW needed)"; Types: full custom

[Files]
; A VST3 on Windows is a folder, not a single file.
Source: "..\dist\GambleSynth\GambleSynth.vst3\*"; \
    DestDir: "{commoncf64}\VST3\GambleSynth.vst3"; \
    Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs

Source: "..\dist\GambleSynth\{#AppExeName}"; \
    DestDir: "{app}"; Components: standalone; Flags: ignoreversion

Source: "..\dist\GambleSynth\README.md"; \
    DestDir: "{app}"; DestName: "README.txt"; Flags: ignoreversion isreadme

[Icons]
Name: "{group}\{#AppName}";                  Filename: "{app}\{#AppExeName}"; Components: standalone
Name: "{autodesktop}\{#AppName}";            Filename: "{app}\{#AppExeName}"; Components: standalone; Tasks: desktopicon
Name: "{group}\Uninstall {#AppName}";        Filename: "{uninstallexe}"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Components: standalone

[Run]
Filename: "{app}\{#AppExeName}"; Description: "Run {#AppName} now"; \
    Flags: nowait postinstall skipifsilent; Components: standalone

[UninstallDelete]
; The plugin folder is ours entirely, so remove what is left of it.
Type: filesandordirs; Name: "{commoncf64}\VST3\GambleSynth.vst3"
