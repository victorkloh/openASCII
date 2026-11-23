; Instalador do AsciiPaper (Inno Setup 6: https://jrsoftware.org/isinfo.php).
;
; Normalmente é o scripts/package.ps1 que o compila, depois de preparar a pasta da release:
;   ISCC /DAppVersion=0.5.0 /DSourceDir=<dist\AsciiPaper-0.5.0-win64> /O<dist> installer\AsciiPaper.iss
;
; Decisões:
; - Instalação POR UTILIZADOR (%LOCALAPPDATA%\Programs\AsciiPaper), sem pedir administrador:
;   a app só mexe no ambiente de trabalho do próprio utilizador, e o arranque com o Windows é a
;   mesma entrada "Run" do registo (HKCU) que o interruptor das Definições usa.
; - Os wallpapers predefinidos são um componente opcional. Sem eles a app instala-se vazia: o
;   fundo do Windows fica à vista e o launcher convida a criar o primeiro wallpaper.
; - Antes de instalar/atualizar/desinstalar, a app é fechada com WM_CLOSE (como o "Sair"): repõe
;   o fundo original do Windows e retoma o Wallpaper Engine/Lively, se os tinha pausado.
; - As definições e os wallpapers do utilizador (%APPDATA%\AsciiPaper) ficam, a não ser que ele
;   diga que sim à pergunta no fim da desinstalação.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist\AsciiPaper-" + AppVersion + "-win64"
#endif

#define AppName "AsciiPaper"
#define AppExe "asciipaper.exe"

[Setup]
AppId={{51186E77-7AF1-4AAC-A132-6A267D4181B4}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppName}
VersionInfoVersion={#AppVersion}
VersionInfoDescription={#AppName} — wallpapers ASCII animados
DefaultDirName={localappdata}\Programs\{#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 10 1803+ (timer de alta resolução e DPI PerMonitorV2).
MinVersion=10.0.17134
LicenseFile=..\LICENSE
SetupIconFile=..\res\app.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
OutputBaseFilename={#AppName}-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; Fechamos a app nós próprios (ver [Code]): o Restart Manager não repõe o fundo do Windows.
CloseApplications=no
RestartApplications=no
ShowLanguageDialog=auto
UsePreviousTasks=yes
UsePreviousSetupType=yes

[Languages]
Name: "pt"; MessagesFile: "compiler:Languages\Portuguese.isl"
Name: "ptbr"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
; Inglês por defeito; pt e ptbr por cima.
TypeFull=Full installation (app + built-in wallpapers)
TypeApp=App only (no wallpapers: create your own)
TypeCustom=Custom installation
CompApp=AsciiPaper (the app)
CompWallpapers=Built-in wallpapers (28 effects and their variants)
TaskAutostart=Start AsciiPaper with Windows
TaskGroupStart=Startup:
RunNow=Open AsciiPaper now
AppStillRunning=AsciiPaper is still running. Close it from the tray icon (Quit) and try again.
DeleteUserData=Also delete your settings and the wallpapers you created?%n%n%1%n%nThis cannot be undone.
pt.TypeFull=Instalação completa (app + wallpapers predefinidos)
pt.TypeApp=Só a app (sem wallpapers: crias os teus)
pt.TypeCustom=Instalação personalizada
pt.CompApp=AsciiPaper (a aplicação)
pt.CompWallpapers=Wallpapers predefinidos (28 efeitos e as suas variantes)
pt.TaskAutostart=Iniciar o AsciiPaper com o Windows
pt.TaskGroupStart=Arranque:
pt.RunNow=Abrir o AsciiPaper agora
pt.AppStillRunning=O AsciiPaper ainda está a correr. Fecha-o no ícone da bandeja (Sair) e tenta de novo.
pt.DeleteUserData=Apagar também as tuas definições e os wallpapers que criaste?%n%n%1%n%nNão é possível desfazer.
ptbr.TypeFull=Instalação completa (app + wallpapers predefinidos)
ptbr.TypeApp=Somente o app (sem wallpapers: você cria os seus)
ptbr.TypeCustom=Instalação personalizada
ptbr.CompApp=AsciiPaper (o aplicativo)
ptbr.CompWallpapers=Wallpapers predefinidos (28 efeitos e suas variantes)
ptbr.TaskAutostart=Iniciar o AsciiPaper com o Windows
ptbr.TaskGroupStart=Inicialização:
ptbr.RunNow=Abrir o AsciiPaper agora
ptbr.AppStillRunning=O AsciiPaper ainda está em execução. Feche-o pelo ícone da bandeja (Sair) e tente novamente.
ptbr.DeleteUserData=Apagar também suas configurações e os wallpapers que você criou?%n%n%1%n%nNão é possível desfazer.

[Types]
Name: "full"; Description: "{cm:TypeFull}"
Name: "app"; Description: "{cm:TypeApp}"
Name: "custom"; Description: "{cm:TypeCustom}"; Flags: iscustom

[Components]
Name: "app"; Description: "{cm:CompApp}"; Types: full app custom; Flags: fixed
Name: "wallpapers"; Description: "{cm:CompWallpapers}"; Types: full

[Tasks]
Name: "autostart"; Description: "{cm:TaskAutostart}"; GroupDescription: "{cm:TaskGroupStart}"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; Numa atualização, os wallpapers predefinidos antigos saem antes de copiar os novos (ou de
; ficar sem eles, se o componente foi desmarcado). Os do utilizador vivem em %APPDATA%.
Type: filesandordirs; Name: "{app}\wallpapers"

[Files]
Source: "{#SourceDir}\{#AppExe}"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "{#SourceDir}\README.md"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "{#SourceDir}\CHANGELOG.md"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "{#SourceDir}\CREATING_WALLPAPERS.md"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "{#SourceDir}\LICENSE"; DestDir: "{app}"; Components: app; Flags: ignoreversion
Source: "{#SourceDir}\wallpapers\*"; DestDir: "{app}\wallpapers"; Components: wallpapers; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
; O atalho abre o launcher (e, se a app já estiver a correr, pede-lhe que o mostre).
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"; Parameters: "--launcher"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Parameters: "--launcher"; Tasks: desktopicon

[Registry]
; A mesma entrada que o interruptor "Iniciar com o Windows" das Definições lê e escreve.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "{#AppName}"; ValueData: """{app}\{#AppExe}"""; Tasks: autostart; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#AppExe}"; Parameters: "--launcher"; Description: "{cm:RunNow}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; A cache de shaders compilados é só cache: sai sempre.
Type: filesandordirs; Name: "{localappdata}\AsciiPaper\shadercache"
Type: dirifempty; Name: "{localappdata}\AsciiPaper"
Type: filesandordirs; Name: "{app}\wallpapers"
Type: dirifempty; Name: "{app}"

[Code]
const
  WM_CLOSE = $0010;
  RunKey = 'Software\Microsoft\Windows\CurrentVersion\Run';

// Fecha a app como o "Sair" do menu: WM_CLOSE para a janela controladora. Ela repõe o fundo
// do Windows e retoma o Wallpaper Engine/Lively antes de sair. Espera até 6 s.
function CloseRunningApp(): Boolean;
var
  Wnd: HWND;
  I: Integer;
begin
  for I := 1 to 60 do
  begin
    Wnd := FindWindowByClassName('AsciiPaperController');
    if Wnd = 0 then
    begin
      Result := True;
      Exit;
    end;
    if (I mod 10) = 1 then
      PostMessage(Wnd, WM_CLOSE, 0, 0);
    Sleep(100);
  end;
  Result := FindWindowByClassName('AsciiPaperController') = 0;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if not CloseRunningApp() then
    Result := CustomMessage('AppStillRunning');
end;

// Numa atualização em que o utilizador desmarcou "Iniciar com o Windows", desligar mesmo
// (o [Registry] só escreve a entrada; não a apaga quando a tarefa fica por marcar).
procedure CurStepChanged(CurStep: TSetupStep);
var
  Value: String;
begin
  if (CurStep = ssPostInstall) and not WizardIsTaskSelected('autostart') then
    if RegQueryStringValue(HKCU, RunKey, '{#AppName}', Value) then
      if Pos(Lowercase(ExpandConstant('{app}')), Lowercase(Value)) > 0 then
        RegDeleteValue(HKCU, RunKey, '{#AppName}');
end;

function InitializeUninstall(): Boolean;
begin
  Result := CloseRunningApp();
  if not Result then
    MsgBox(CustomMessage('AppStillRunning'), mbError, MB_OK);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Value, UserData: String;
begin
  if CurUninstallStep <> usPostUninstall then
    Exit;
  // O arranque com o Windows pode ter sido ligado nas Definições da app (e não pelo instalador):
  // apagar a entrada se apontar para esta instalação.
  if RegQueryStringValue(HKCU, RunKey, '{#AppName}', Value) then
    if Pos(Lowercase(ExpandConstant('{app}')), Lowercase(Value)) > 0 then
      RegDeleteValue(HKCU, RunKey, '{#AppName}');
  // Definições e wallpapers do utilizador: só com um "sim" explícito (o "não" é o botão por defeito).
  UserData := ExpandConstant('{userappdata}\AsciiPaper');
  if DirExists(UserData) and not UninstallSilent() then
    if MsgBox(FmtMessage(CustomMessage('DeleteUserData'), [UserData]), mbConfirmation,
              MB_YESNO or MB_DEFBUTTON2) = IDYES then
      DelTree(UserData, True, True, True);
end;
