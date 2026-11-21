; Apex by DSK - Inno Setup installer. Build: iscc /DAppVersion=1.0.0 /DArtDir=<artefacts\Release> installer\apex.iss
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef ArtDir
  #define ArtDir "..\build\Apex_artefacts\Release"
#endif

[Setup]
AppId={{4122ED6B-9559-4FCA-AC84-E3DA240B98C1}
AppName=Apex by DSK
AppVersion={#AppVersion}
AppPublisher=DSK Music
AppPublisherURL=https://dskmusic.com
DefaultDirName={autopf}\DSK Music\Apex
DefaultGroupName=DSK Music
OutputBaseFilename=Apex-by-DSK-win-x64-setup
OutputDir=..\dist
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2
SolidCompression=yes
LicenseFile=..\LICENSE
DisableProgramGroupPage=yes
SetupIconFile=..\branding\apex.ico
UninstallDisplayIcon={app}\Apex by DSK.exe

[Components]
Name: "vst3"; Description: "VST3 plugin"; Types: full compact custom; Flags: fixed
Name: "standalone"; Description: "Standalone application"; Types: full

[Files]
Source: "{#ArtDir}\VST3\Apex by DSK.vst3\*"; DestDir: "{commoncf64}\VST3\Apex by DSK.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#ArtDir}\Standalone\Apex by DSK.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion

[Icons]
Name: "{group}\Apex by DSK"; Filename: "{app}\Apex by DSK.exe"; Components: standalone
