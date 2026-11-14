; Installateur Windows de SubdAmp (Inno Setup 6, licence libre : aucune obligation sur
; l'installateur produit). Le désinstallateur est créé avec : menu Démarrer
; « Désinstaller SubdAmp », et Paramètres > Applications.
;
; Compilation (voir .github/workflows/build.yml) :
;   ISCC.exe /DAppVersion=0.1.0 /DArtefacts=<…\SubdAmp_artefacts\Release> /DRepo=<racine du dépôt> /O<sortie> installer\windows\SubdAmp.iss
;
; Pose :
;   - le plugin VST3    dans C:\Program Files\Common Files\VST3   (le dossier standard VST3)
;   - l'application     dans C:\Program Files\SubdAmp, avec raccourci dans le menu Démarrer
; Une version déjà installée est remplacée (même AppId) ; l'ancien plugin est d'abord
; retiré en entier, y compris une copie posée à la main.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
; Identifiant fixe : ne jamais le changer, c'est lui qui relie les mises à jour.
AppId={{4EDFF4C8-1EDA-4721-866F-8002A324713E}
AppName=SubdAmp
AppVersion={#AppVersion}
AppVerName=SubdAmp {#AppVersion}
AppPublisher=Labtechno
AppPublisherURL=https://github.com/Labtechno/SubdAMP
DefaultDirName={autopf}\SubdAmp
DisableProgramGroupPage=yes
DisableDirPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputBaseFilename=Installer SubdAmp
UninstallDisplayName=SubdAmp
UninstallDisplayIcon={app}\SubdAmp.exe
CloseApplications=yes
RestartApplications=no
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"

[Messages]
fr.WelcomeLabel2=Plugin guitare orienté hauteur : octaver, whammy, harmonizer, shimmer, textures, ampli NAM et cab.%n%nCet installateur pose le plugin VST3 (là où les logiciels audio le cherchent) et l'application SubdAmp (pour jouer directement avec ta carte son).%n%nUne version déjà installée est remplacée ; tes réglages, captures NAM et IR ne sont pas touchés. Ferme ton logiciel audio avant de continuer.

[Types]
Name: "full"; Description: "Plugin VST3 et application"
Name: "custom"; Description: "Au choix"; Flags: iscustom

[Components]
Name: "vst3"; Description: "Plugin VST3 (C:\Program Files\Common Files\VST3)"; Types: full custom
Name: "app"; Description: "Application SubdAmp (sans logiciel audio)"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "Raccourci sur le Bureau"; Components: app; Flags: unchecked

[InstallDelete]
; Ancien plugin retiré en entier : aucun vieux fichier ne reste dans le bundle.
Type: filesandordirs; Name: "{commoncf64}\VST3\SubdAmp.vst3"

[Files]
Source: "{#Artefacts}\VST3\SubdAmp.vst3\*"; DestDir: "{commoncf64}\VST3\SubdAmp.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Artefacts}\Standalone\SubdAmp.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion
; Polices de l'interface (SIL OFL 1.1) : leur licence voyage avec le binaire.
Source: "{#Repo}\assets\fonts\OFL-*.txt"; DestDir: "{app}\licences"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\SubdAmp"; Filename: "{app}\SubdAmp.exe"; Components: app
Name: "{autoprograms}\Désinstaller SubdAmp"; Filename: "{uninstallexe}"
Name: "{autodesktop}\SubdAmp"; Filename: "{app}\SubdAmp.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\SubdAmp.exe"; Description: "Lancer SubdAmp"; Components: app; Flags: postinstall nowait skipifsilent

[UninstallDelete]
; Le plugin, et les réglages de l'application (carte son, dernier son joué).
; Les captures NAM, IR et projets ne sont pas touchés.
Type: filesandordirs; Name: "{commoncf64}\VST3\SubdAmp.vst3"
Type: filesandordirs; Name: "{userappdata}\SubdAmp"
Type: dirifempty; Name: "{app}"
