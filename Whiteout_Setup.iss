; ============================================================================
; WhiteoutDex Toolkit - Inno Setup Installer Script
; Warcraft III MDX/MDL Modeling Toolkit for 3ds Max 2022-2027
; Author: Fernando sahmkow & Benjamin Schiefer
; ============================================================================

#define MyAppName      "WhiteoutDex Toolkit"
#define MyAppVersion   "1.0.0"
#define MyAppPublisher "Whiteout Contributors"
#define MyAppURL       "https://www.whiteout.de"

; --- GitHub Update Config ---
#define GitHubUser     "FernandoS27"
#define GitHubRepo     "WhiteoutDex"

[Setup]
AppId={{C508AF39-A032-496D-AC08-EE5B20E69FDB}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL=https://github.com/{#GitHubUser}/{#GitHubRepo}/releases
VersionInfoVersion={#MyAppVersion}

; Important: %APPDATA%\Autodesk\ApplicationPlugins\WhiteoutDex
; {userappdata} = %APPDATA% (Roaming)
DefaultDirName={userappdata}\Autodesk\ApplicationPlugins\WhiteoutDex
DirExistsWarning=no
DisableProgramGroupPage=yes
DisableDirPage=yes

; Output
OutputDir=Output
OutputBaseFilename=WhiteoutDex_Setup_v{#MyAppVersion}
; SetupIconFile=Assets\whiteoutdex_icon.ico       ← Entkommentiere wenn du ein .ico hast
; UninstallDisplayIcon={app}\whiteoutdex_icons\ndx_sb_about.png

; Kompression
Compression=lzma2/ultra64
SolidCompression=yes
LZMANumBlockThreads=4

; Sonstiges
PrivilegesRequired=lowest
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile=Source\LICENSE.md
InfoBeforeFile=Source\RELEASENOTES.rtf
WizardStyle=modern
WizardSizePercent=120
ShowLanguageDialog=no
LanguageDetectionMethod=uilanguage

; Erlaube Update über bestehende Installation
UsePreviousAppDir=yes
CloseApplications=no

[Languages]
Name: "english";   MessagesFile: "compiler:Default.isl"
Name: "german";    MessagesFile: "compiler:Languages\German.isl"
Name: "chinese";   MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "japanese";  MessagesFile: "compiler:Languages\Japanese.isl"
Name: "russian";   MessagesFile: "compiler:Languages\Russian.isl"
Name: "korean";    MessagesFile: "compiler:Languages\Korean.isl"

; ============================================================================
; DATEIEN
; ============================================================================
[Files]

; === ROOT FILES ===
Source: "PackageContents.xml";  DestDir: "{app}"; Flags: ignoreversion
Source: "LICENSE.md";           DestDir: "{app}"; Flags: ignoreversion
Source: "THIRD_PARTY.md";      DestDir: "{app}"; Flags: ignoreversion
Source: "build\WhiteoutTexCLI.exe";  DestDir: "{app}"; Flags: ignoreversion

; Settings: NUR installieren wenn noch nicht vorhanden (User-Einstellungen erhalten!)
Source: "Source\WhiteoutDex_Settings.ini"; DestDir: "{app}"; Flags: onlyifdoesntexist

; === NEODEX ICONS ===
Source: "Source\whiteoutdex_icons\*"; DestDir: "{app}\whiteoutdex_icons"; Flags: ignoreversion recursesubdirs

; === NATIVE PLUGINS - Versionsspezifische blp.bmi ===
Source: "Source\native plugins\Max2022\blp.bmi";  DestDir: "{app}\native plugins\Max2022";  Flags: ignoreversion
Source: "Source\native plugins\Max2023\blp.bmi";  DestDir: "{app}\native plugins\Max2023";  Flags: ignoreversion
Source: "Source\native plugins\Max2024\blp.bmi";  DestDir: "{app}\native plugins\Max2024";  Flags: ignoreversion
Source: "Source\native plugins\Max2025\blp.bmi";  DestDir: "{app}\native plugins\Max2025";  Flags: ignoreversion
Source: "Source\native plugins\Max2026\blp.bmi";  DestDir: "{app}\native plugins\Max2026";  Flags: ignoreversion
Source: "Source\native plugins\Max2027\blp.bmi";  DestDir: "{app}\native plugins\Max2027";  Flags: ignoreversion

; === NATIVE PLUGINS - WhiteoutDexNative.dll (Max 2022-2025, ohne .NET 8 Host) ===
Source: "Source\native plugins\Max2022-2025\WhiteoutDexNative.dll";  DestDir: "{app}\native plugins\Max2022-2025"; Flags: ignoreversion

; === NATIVE PLUGINS - WhiteoutDexNative + .NET 8 Host (Max 2026+) ===
Source: "Source\native plugins\Max2026+\Ijwhost.dll";                     DestDir: "{app}\native plugins\Max2026+"; Flags: ignoreversion
Source: "Source\native plugins\Max2026+\WhiteoutDexNative.dll";                DestDir: "{app}\native plugins\Max2026+"; Flags: ignoreversion
Source: "Source\native plugins\Max2026+\WhiteoutDexNative.runtimeconfig.json"; DestDir: "{app}\native plugins\Max2026+"; Flags: ignoreversion

; === SCRIPTED PLUGINS — alles aus dem Ordner ===
Source: "Source\scripted plugins parts\*.ms"; DestDir: "{app}\scripted plugins parts"; Flags: ignoreversion

; === PRE-START-UP SCRIPTS — alles aus dem Ordner ===
Source: "Source\pre-start-up scripts parts\*.ms"; DestDir: "{app}\pre-start-up scripts parts"; Flags: ignoreversion

; === POST-START-UP SCRIPTS — alles aus dem Ordner ===
Source: "Source\post-start-up scripts parts\*.ms"; DestDir: "{app}\post-start-up scripts parts"; Flags: ignoreversion

; === MACROSCRIPTS — alles aus dem Ordner ===
Source: "Source\macroscripts parts\*.mcr"; DestDir: "{app}\macroscripts parts"; Flags: ignoreversion

; === NATIVE PLUGINS — kompletter Ordner mit Unterordnern ===
Source: "Source\native plugins\*"; DestDir: "{app}\native plugins"; Flags: ignoreversion recursesubdirs createallsubdirs

; === MAPS (TeamGlow Texturen etc.) ===
Source: "Source\maps\*"; DestDir: "{app}\maps"; Flags: ignoreversion recursesubdirs createallsubdirs

; === Version File ===
Source: "Source\version.txt"; DestDir: "{app}"; Flags: ignoreversion

; === Old Version Cleanup Script (temp, nur während Installation) ===
Source: "Source\uninstall_old_whiteoutdex.bat"; Flags: dontcopy

; ============================================================================
; REGISTRY - Version + Pfad für Update-Check
; ============================================================================
[Registry]
Root: HKCU; Subkey: "Software\WhiteoutDex"; ValueType: string; ValueName: "InstallPath"; ValueData: "{app}";              Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\WhiteoutDex"; ValueType: string; ValueName: "Version";     ValueData: "{#MyAppVersion}";    Flags: uninsdeletekey

; ============================================================================
; ORDNER DIE BEIM DEINSTALLIEREN GELÖSCHT WERDEN
; (maps wird NICHT gelöscht - User-Daten)
; ============================================================================
[UninstallDelete]
Type: filesandordirs; Name: "{app}\whiteoutdex_icons"
Type: filesandordirs; Name: "{app}\native plugins"
Type: filesandordirs; Name: "{app}\scripted plugins parts"
Type: filesandordirs; Name: "{app}\pre-start-up scripts parts"
Type: filesandordirs; Name: "{app}\post-start-up scripts parts"
Type: filesandordirs; Name: "{app}\macroscripts parts"
Type: files;          Name: "{app}\PackageContents.xml"
Type: files;          Name: "{app}\LICENSE.md"
Type: files;          Name: "{app}\THIRD_PARTY.md"
Type: files;          Name: "{app}\WhiteoutTexCLI.exe"
Type: files;          Name: "{app}\WhiteoutDex_Settings.ini"
Type: files;          Name: "{app}\version.txt"

; ============================================================================
; PASCAL SCRIPT
; ============================================================================
[Code]

// -------------------------------------------------------
// Prüft ob .NET 8.0 Desktop Runtime installiert ist
// -------------------------------------------------------
function IsDotNet8Installed(): Boolean;
var
  FindRec: TFindRec;
begin
  Result := FindFirst(ExpandConstant('{pf}\dotnet\shared\Microsoft.WindowsDesktop.App\8.*'), FindRec);
  if Result then
    FindClose(FindRec);
end;

// -------------------------------------------------------
// Prüft ob Autodesk AppData vorhanden ist
// -------------------------------------------------------
function IsAutodeskInstalled(): Boolean;
begin
  Result := DirExists(ExpandConstant('{userappdata}\Autodesk'));
end;

// -------------------------------------------------------
// Alte Version aus version.txt lesen
// -------------------------------------------------------
function GetOldVersion(InstallDir: String): String;
var
  VersionFile: String;
  Lines: TArrayOfString;
begin
  Result := '';
  VersionFile := InstallDir + '\version.txt';
  if FileExists(VersionFile) then
  begin
    if LoadStringsFromFile(VersionFile, Lines) and (GetArrayLength(Lines) > 0) then
      Result := Trim(Lines[0]);
  end;
end;

// -------------------------------------------------------
// Prüft ob eine vorherige INNO SETUP Installation existiert
// (Hat einen UninstallString in der Registry)
// -------------------------------------------------------
function GetPreviousInnoUninstallString(): String;
var
  UninstallKey: String;
begin
  Result := '';
  UninstallKey := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{#SetupSetting("AppId")}_is1';
  if not RegQueryStringValue(HKCU, UninstallKey, 'UninstallString', Result) then
    Result := '';
end;

// -------------------------------------------------------
// Vorherige Inno Setup Version silent deinstallieren
// -------------------------------------------------------
function UninstallPreviousInnoSetup(): Boolean;
var
  UninstallString: String;
  ResultCode: Integer;
begin
  Result := True;
  UninstallString := GetPreviousInnoUninstallString();
  
  if UninstallString <> '' then
  begin
    Log('Vorherige Inno Setup Installation gefunden. Deinstalliere...');
    
    // Anführungszeichen entfernen
    if (Length(UninstallString) > 1) and (UninstallString[1] = '"') then
      UninstallString := Copy(UninstallString, 2, Length(UninstallString) - 2);
    
    // Silent deinstallieren
    if Exec(UninstallString, '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    begin
      Log('Vorherige Version erfolgreich deinstalliert.');
      // Kurz warten damit Dateien freigegeben werden
      Sleep(1000);
    end else
    begin
      Log('Deinstallation fehlgeschlagen (Code: ' + IntToStr(ResultCode) + ')');
    end;
  end;
end;

// -------------------------------------------------------
// Alte BAT-Installation bereinigen
// (User hatte install.bat verwendet)
// -------------------------------------------------------
procedure CleanOldBatInstallation();
var
  AppDir: String;
begin
  AppDir := ExpandConstant('{app}');
  
  if DirExists(AppDir) then
  begin
    Log('Alte Installation gefunden - bereinige...');
    
    // Script-Ordner komplett löschen (werden neu angelegt)
    DelTree(AppDir + '\pre-start-up scripts parts', True, True, True);
    DelTree(AppDir + '\post-start-up scripts parts', True, True, True);
    DelTree(AppDir + '\scripted plugins parts', True, True, True);
    DelTree(AppDir + '\macroscripts parts', True, True, True);
    DelTree(AppDir + '\native plugins', True, True, True);
    DelTree(AppDir + '\whiteoutdex_icons', True, True, True);
    
    // Alte Root-Dateien löschen
    DeleteFile(AppDir + '\PackageContents.xml');
    DeleteFile(AppDir + '\LICENSE.md');
    DeleteFile(AppDir + '\THIRD_PARTY.md');
    DeleteFile(AppDir + '\WhiteoutTexCLI.exe');
    
    // Alte Bat-Installer/Uninstaller entfernen
    DeleteFile(AppDir + '\install.bat');
    DeleteFile(AppDir + '\uninstall.bat');
    
    // NICHT löschen: WhiteoutDex_Settings.ini, maps/
    Log('Bereinigung abgeschlossen. maps/ und Settings bleiben erhalten.');
  end;
end;

// -------------------------------------------------------
// Erkennt alte WhiteoutDex 2.x Installation in Max-Verzeichnissen
// (Dateien direkt in Program Files\Autodesk\3ds Max YYYY\)
// -------------------------------------------------------
function HasOldWhiteoutDexInMaxDirs(): Boolean;
var
  Year: Integer;
  MaxDir, MacroDir: String;
  FindRec: TFindRec;
begin
  Result := False;
  for Year := 2022 to 2027 do
  begin
    MaxDir := ExpandConstant('{pf}') + '\Autodesk\3ds Max ' + IntToStr(Year) + '\';
    if DirExists(MaxDir) then
    begin
      // Check for old WhiteoutDex folders and files
      if DirExists(MaxDir + 'Scripts\WhiteoutDexTools') or
         DirExists(MaxDir + 'Scripts\WhiteoutDexModules') or
         DirExists(MaxDir + 'Scripts\WhiteoutDexExtraTools') or
         DirExists(MaxDir + 'Scripts\Startup\WhiteoutDex') or
         FileExists(MaxDir + 'Scripts\WhiteoutDexInstaller.ms') or
         FileExists(MaxDir + 'plugins\BlizzPart1.ms') or
         FileExists(MaxDir + 'plugins\Wc3Material.ms') or
         FileExists(MaxDir + 'stdplugs\stdscripts\WhiteoutDexGlobals.ms') then
      begin
        Result := True;
        Exit;
      end;
      
      // Check scripts\Startup for any WhiteoutDex files
      if FindFirst(MaxDir + 'Scripts\Startup\*WhiteoutDex*', FindRec) then
      begin
        FindClose(FindRec);
        Result := True;
        Exit;
      end;
    end;
    
    // Check usermacros in %LOCALAPPDATA%
    MacroDir := ExpandConstant('{localappdata}') + '\Autodesk\3dsMax\' + IntToStr(Year) + ' - 64bit\ENU\usermacros\';
    if DirExists(MacroDir) then
    begin
      if FindFirst(MacroDir + '*WhiteoutDex*', FindRec) then
      begin
        FindClose(FindRec);
        Result := True;
        Exit;
      end;
    end;
  end;
end;

// -------------------------------------------------------
// Alte WhiteoutDex 2.x aus Max-Verzeichnissen entfernen
// Startet das Cleanup-Bat mit Admin-Rechten
// -------------------------------------------------------
procedure CleanOldWhiteoutDex2x();
var
  BatFile: String;
  ResultCode: Integer;
begin
  ExtractTemporaryFile('uninstall_old_whiteoutdex.bat');
  BatFile := ExpandConstant('{tmp}\uninstall_old_whiteoutdex.bat');
  
  // Bat-Script mit Admin-Rechten starten (UAC Prompt)
  ShellExec('runas', BatFile, '', '', SW_SHOW, ewWaitUntilTerminated, ResultCode);
end;

// -------------------------------------------------------
// Setup initialisieren - Alles prüfen und alte Versionen behandeln
// -------------------------------------------------------
function InitializeSetup(): Boolean;
var
  AppDir: String;
  OldVersion: String;
  HasOldInno: Boolean;
  HasOldBat: Boolean;
  Msg: String;
begin
  Result := True;
  AppDir := ExpandConstant('{userappdata}\Autodesk\ApplicationPlugins\WhiteoutDex');
  
  // Autodesk-Ordner prüfen
  if not IsAutodeskInstalled() then
  begin
    if MsgBox(
      'The Autodesk AppData folder was not found.' + #13#10 +
      'Is 3ds Max installed?' + #13#10#13#10 +
      'Install anyway?',
      mbConfirmation, MB_YESNO) = IDNO then
    begin
      Result := False;
      Exit;
    end;
  end;

  // -------------------------------------------------------
  // Vorherige Installation erkennen
  // -------------------------------------------------------
  HasOldInno := (GetPreviousInnoUninstallString() <> '');
  HasOldBat := (not HasOldInno) and DirExists(AppDir) and FileExists(AppDir + '\PackageContents.xml');
  
  if HasOldInno then
  begin
    // Vorherige Inno Setup Version gefunden
    OldVersion := GetOldVersion(AppDir);
    if OldVersion <> '' then
      Msg := 'WhiteoutDex Toolkit v' + OldVersion + ' is already installed.'
    else
      Msg := 'A previous version of WhiteoutDex Toolkit is installed.';
    
    Msg := Msg + #13#10#13#10 +
      'The old version will be uninstalled automatically before updating.' + #13#10 +
      'Your maps folder and settings will be preserved.' + #13#10#13#10 +
      'Continue?';
    
    if MsgBox(Msg, mbConfirmation, MB_YESNO) = IDNO then
    begin
      Result := False;
      Exit;
    end;
    
    // Alte Inno Setup Version silent deinstallieren
    UninstallPreviousInnoSetup();
    
  end else if HasOldBat then
  begin
    // Alte BAT-Installation gefunden (install.bat)
    OldVersion := GetOldVersion(AppDir);
    if OldVersion <> '' then
      Msg := 'WhiteoutDex Toolkit v' + OldVersion + ' was found (installed via batch file).'
    else
      Msg := 'A previous WhiteoutDex installation was found (installed via batch file).';
    
    Msg := Msg + #13#10#13#10 +
      'The old installation will be cleaned up and replaced.' + #13#10 +
      'Your maps folder and settings will be preserved.' + #13#10#13#10 +
      'Continue?';
    
    if MsgBox(Msg, mbConfirmation, MB_YESNO) = IDNO then
    begin
      Result := False;
      Exit;
    end;
  end;

  // .NET 8 Hinweis (nicht blockierend)
  if not IsDotNet8Installed() then
  begin
    MsgBox(
      'Note: .NET 8.0 Desktop Runtime was not detected.' + #13#10#13#10 +
      'WhiteoutDex Native features for Max 2026+ require it.' + #13#10 +
      'Download: https://dotnet.microsoft.com/download/dotnet/8.0' + #13#10#13#10 +
      'The installation will continue. You can install .NET 8.0 later.',
      mbInformation, MB_OK);
  end;

  // -------------------------------------------------------
  // Alte WhiteoutDex 2.x in Max-Verzeichnissen erkennen
  // -------------------------------------------------------
  if HasOldWhiteoutDexInMaxDirs() then
  begin
    if MsgBox(
      'An old WhiteoutDex installation (v2.x) was detected in your 3ds Max directories.' + #13#10#13#10 +
      'These old files need to be removed to prevent conflicts with the new version.' + #13#10 +
      'Administrator privileges are required (files are in Program Files).' + #13#10#13#10 +
      'Run cleanup now?',
      mbConfirmation, MB_YESNO) = IDYES then
    begin
      CleanOldWhiteoutDex2x();
    end;
  end;
end;

// -------------------------------------------------------
// PackageContents.xml automatisch aktualisieren
// - AppVersion auf aktuelle Version setzen
// - ProductCode mit neuer GUID ersetzen
// - UpgradeCode bleibt IMMER gleich (wichtig!)
// -------------------------------------------------------
procedure UpdatePackageContentsXml();
var
  XmlFile: String;
  Content: AnsiString;
  NewProductCode: String;
  PosStart, PosEnd: Integer;
  Before, After: String;
begin
  XmlFile := ExpandConstant('{app}\PackageContents.xml');
  if not FileExists(XmlFile) then Exit;
  
  // Datei lesen
  if not LoadStringFromFile(XmlFile, Content) then Exit;
  
  // --- AppVersion aktualisieren ---
  PosStart := Pos('AppVersion="', Content);
  if PosStart > 0 then
  begin
    PosStart := PosStart + Length('AppVersion="');
    PosEnd := PosStart;
    while (PosEnd <= Length(Content)) and (Content[PosEnd] <> '"') do
      PosEnd := PosEnd + 1;
    
    Before := Copy(Content, 1, PosStart - 1);
    After := Copy(Content, PosEnd, Length(Content) - PosEnd + 1);
    Content := Before + '{#MyAppVersion}' + After;
    
    Log('PackageContents.xml: AppVersion auf {#MyAppVersion} gesetzt.');
  end;
  
  // --- ProductCode mit neuer GUID ersetzen ---
  // Generiere eine deterministische GUID basierend auf Version + Datum
  NewProductCode := '{' + GetDateTimeString('yyyy', #0, #0) 
    + GetDateTimeString('mmdd', #0, #0) 
    + '-' + GetDateTimeString('hhmm', #0, #0)
    + '-' + '{#MyAppVersion}'
    + '-0000-NEODEXTOOL}';
  
  PosStart := Pos('ProductCode="', Content);
  if PosStart > 0 then
  begin
    PosStart := PosStart + Length('ProductCode="');
    PosEnd := PosStart;
    while (PosEnd <= Length(Content)) and (Content[PosEnd] <> '"') do
      PosEnd := PosEnd + 1;
    
    Before := Copy(Content, 1, PosStart - 1);
    After := Copy(Content, PosEnd, Length(Content) - PosEnd + 1);
    Content := Before + NewProductCode + After;
    
    Log('PackageContents.xml: ProductCode auf ' + NewProductCode + ' gesetzt.');
  end;
  
  // UpgradeCode wird NICHT geändert - muss immer gleich bleiben!
  
  // Datei zurückschreiben
  SaveStringToFile(XmlFile, Content, False);
  Log('PackageContents.xml erfolgreich aktualisiert.');
end;

// -------------------------------------------------------
// Vor/Nach der Installation
// -------------------------------------------------------
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
  begin
    // Alte BAT-Installation bereinigen (falls vorhanden)
    CleanOldBatInstallation();
  end;
  
  if CurStep = ssPostInstall then
  begin
    // Version in version.txt schreiben
    SaveStringToFile(
      ExpandConstant('{app}\version.txt'),
      '{#MyAppVersion}',
      False
    );
    
    // PackageContents.xml mit aktueller Version + neuem ProductCode patchen
    UpdatePackageContentsXml();
  end;
end;
