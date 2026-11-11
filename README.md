# SubdAmp

Plugin guitare (VST3 + application standalone) orienté pitch : octaver, whammy,
harmonizer, frequency shifter, shimmer, textures. Usage personnel.

**État : étape 4 + chaîne en série.** Les 11 modules sont en série, dans l'ordre de ton choix
(glisser une pédale pour la déplacer). Chacun a un potard **MIX** : `sortie = sec × (1 − MIX) + traité × MIX`.
Le signal est stéréo de bout en bout.

| Module | Rôle | Latence |
|---|---|---|
| GATE | Noise gate à hystérésis, ouverture instantanée, relâchement réglable ; voyant rouge quand il coupe ; pas de MIX | LIVE (0) |
| SUB | Octaver analogique façon OC-2 : -1 et -2 octaves, +1 octave (fuzz), tonalité | LIVE (0) |
| WHAMMY | Transposition polyphonique ±2 octaves ; mode fixe ou glissement relancé à chaque attaque | JOUABLE (15 ms) |
| HARMONIZER | 2 voix diatoniques (tonalité + gamme), formants réglables ; la part sèche du MIX reste sans latence | JOUABLE (≈ 1,5 à 2,5 périodes : 5 à 25 ms) |
| AMPLI | Capture Neural Amp Modeler (`.nam`) + gain, égaliseur 3 bandes, niveau | LIVE (0 à 48 kHz, < 1 ms sinon) |
| CAB | Réponse impulsionnelle (`.wav`, `.aif`), ou cab interne par filtres | LIVE (0) |
| FREQ SHIFT | Décaleur de fréquence (type Bode), mix, réinjection | LIVE (≈ 0) |
| SHIMMER | Réverbe (réseau de 8 lignes) dont la queue est transposée en boucle : +octave, +octave et quinte… | TEXTURE (≈ 30 ms à MIX 100 %) |
| POLY | Transposition polyphonique haute qualité (Signalsmith Stretch), 2 voix en stéréo, formants | TEXTURE (≈ 150 ms) |
| FREEZE | Bouton FIGER : le son du moment est tenu en nappe infinie (gel spectral) ; le MIX dose la nappe, le jeu passe toujours | TEXTURE |
| GRANULAR | Grains des 8 dernières secondes : nuage dispersé, ou stutter calé sur le tempo (grille du DAW en lecture) | TEXTURE |

**Chaîne en série** : l'ordre se change en glissant une pédale avant ou après une autre ; il est
enregistré avec la session ou le preset. Le changement passe par un fondu de 20 ms au plus (pas de
« pop » comme en rebranchant de vraies pédales). Ordre d'un nouveau réglage :
GATE → SUB → WHAMMY → AMPLI → CAB → HARMO → FREQ → POLY → GRAIN → FREEZE → SHIMMER.

**MIX** : à 100 %, seul le son traité sort ; à 0 %, le module est transparent. Tous les modules
en ont un, sauf le GATE (un gate à moitié mélangé n'a pas de sens). Quelques cas :
- SUB et HARMONIZER : le traité ne contient que les voix (les anciens réglages DIRECT sont
  remplacés par le MIX) ;
- FREEZE : la nappe s'ajoute au jeu, qui n'est jamais atténué (MIX = niveau de la nappe) ;
- AMPLI : si la capture est rééchantillonnée, la part sèche est retardée d'autant pour qu'un
  mélange partiel ne filtre pas en peigne.

**Textures** (SHIMMER, POLY, FREEZE, GRANULAR) : elles sortent en stéréo. Un module mono placé
après elles (ampli, cab…) traite la somme mono, et la part sèche de son MIX garde la stéréo
(comme sur un Helix). Une texture à MIX partiel ne retarde pas le son direct ; à MIX 100 %, il n'y
a plus de son direct et tout ce qui suit attend sa latence (POLY ≈ 150 ms) : le badge passe alors
en TEXTURE. Arrêtées, les textures laissent finir leur queue (mode « trails ») puis ne coûtent
plus rien. Le tempo utilisé est celui du DAW ; en standalone, celui du potard TEMPO du GRANULAR.

**Interface** : pédalier en haut, dans l'ordre réel de la chaîne (clic sur une pédale : ses
réglages ; clic sur sa LED : marche / arrêt ; glisser : changer de place). Le MIX s'affiche sur
la pédale quand il n'est pas à 100 %. La couleur du haut de chaque pédale et les badges en haut
à droite indiquent la latence : vert LIVE, orange JOUABLE, violet TEXTURE (badge violet séparé
quand des textures sont mélangées au son direct).

### Comment marchent les modules de pitch

- **WHAMMY** : ligne à retard lue à vitesse variable, avec raccords alignés par corrélation
  (type WSOLA) : peu de battement, même sur les accords. En mode attaque, un détecteur de coup
  de médiator relance le glissement ; près de l'unisson, le module s'efface au profit du son direct.
- **HARMONIZER** : détection de hauteur YIN (décimée puis affinée, précision ≈ 1 centième de
  demi-ton, de 60 à 1400 Hz) et synthèse TD-PSOLA, qui garde le timbre de la note. Les voix
  suivent les bends et le vibrato ; le niveau de chaque voix est ramené à celui de la note jouée.
  Monophonique : jouer des notes isolées. Placé après l'ampli par défaut (harmonies plus nettes).
- **SHIMMER** : à chaque tour, la queue de la réverbe passe par un transpositeur (vocodeur de
  phase Signalsmith, sans repliement) puis par un passe-haut à 150 Hz. Ce filtre est essentiel :
  la transposition déplace toutes les fréquences sauf le grave extrême, qui sinon tourne sans fin
  dans la boucle et sature (c'était le défaut de la première version). Au maximum, chaque
  génération garde 80 % de l'énergie de la précédente : nappe longue, jamais d'emballement.
  Les aigus au-dessus de TONALITÉ s'éteignent deux fois plus vite que les graves.

## Captures d'amplis et IR

- **Captures NAM** : des milliers de captures gratuites d'amplis et de pédales sur
  [Tone3000](https://www.tone3000.com) (ex-ToneHunt). Bouton **Charger…** de la carte AMPLI.
  Le volume est normalisé d'une capture à l'autre (comme le plugin NAM officiel).
- **IR de cab** : n'importe quelle réponse impulsionnelle mono ou stéréo (seul le canal gauche
  est utilisé). Sans IR, le cab interne simule un 4×12.
- Les chemins sont enregistrés avec la session ou le preset. Si un fichier a été déplacé, la
  carte l'indique (« Fichier introuvable ») : il suffit de le recharger.
- Les captures sont entraînées à 48 kHz : c'est la fréquence conseillée. À une autre
  fréquence (88,2 kHz...), SubdAmp rééchantillonne en interne (moins d'1 ms de latence,
  déclarée au DAW).

### Charge CPU

Une capture WaveNet « standard » est le module le plus lourd. Mesure sur un cœur de
processeur serveur à 2,1 GHz, blocs de 64 échantillons : environ 16 à 23 % d'un cœur
(A2 : ~6 %, LSTM : < 1 %). Un processeur de bureau récent fait nettement mieux.
Les modules de pitch et les textures restent légers : moins de 2 % d'un cœur chacun à
48 kHz, dans le pire cas (±2 octaves, deux voix, nuage dense, boucle de shimmer au maximum).
Pour mesurer sur ta machine : `build/tests/SubdAmpBenchmark` (accepte tes propres `.nam`).

Sur x86, le moteur NAM est compilé avec AVX2/FMA (≈ 30 % de CPU en moins). Pour un
processeur antérieur à 2013 : `-DSUBDAMP_NAM_AVX2=OFF`.

## Récupérer les binaires (sans rien compiler)

Chaque push sur `main` déclenche une compilation sur GitHub (onglet **Actions**) pour
Windows, macOS (universel Intel + Apple Silicon) et Linux. Dans le dernier run, la
section **Artifacts** contient un zip par plateforme avec le VST3 et l'application.

### Windows

1. Copier `SubdAmp.vst3` dans `C:\Program Files\Common Files\VST3\`.
2. Lancer `SubdAmp.exe` pour le mode standalone (aucune installation).
3. Au premier lancement, les réglages audio s'ouvrent : choisir le pilote **ASIO**
   (sélectionné d'office s'il existe), l'interface Focusrite et l'**entrée guitare**.
   Sur une Scarlett Solo, la guitare est sur l'entrée 2 (entrée instrument).
4. Le petit vumètre à côté de « Entrée guitare » doit bouger quand tu joues.

Viser un buffer de 64 ou 128 échantillons à 48 kHz.

### macOS

1. Dézipper, puis retirer la quarantaine (binaires non signés par un compte développeur Apple) :
   `xattr -dr com.apple.quarantine SubdAmp.app SubdAmp.vst3`
2. Copier `SubdAmp.vst3` dans `~/Library/Audio/Plug-Ins/VST3/` et `SubdAmp.app` dans `Applications`.
3. Au premier lancement, macOS demande l'accès au micro : c'est l'entrée de la carte son, il faut accepter.

## Compiler soi-même

Prérequis : CMake ≥ 3.25, Git, et Visual Studio 2022 (Windows) ou Xcode (macOS).
JUCE, NeuralAmpModelerCore, Eigen, Signalsmith Stretch et Catch2 sont téléchargés automatiquement.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Binaires produits : `build/SubdAmp_artefacts/Release/VST3/` et `.../Standalone/`.

Sur macOS, ajouter `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` pour un binaire universel.

## Structure

```
dsp/          C++ pur, sans JUCE, testable unitairement : modules (gate, SUB, WHAMMY,
              HARMONIZER, FREQ SHIFT, ampli NAM, égaliseur), textures (SHIMMER, POLY,
              FREEZE, GRANULAR), emplacement de chaîne (marche / arrêt, MIX),
              détection de hauteur, FFT, rééchantillonnage, échange temps réel des captures
plugin/       AudioProcessor, paramètres, ordre de la chaîne, cab (convolution JUCE), presets
gui/          interface du plugin (pédalier, panneaux de modules, sélecteurs de fichiers)
standalone/   application standalone : carte son, canal guitare, réglages
third_party/  compilation de NeuralAmpModelerCore
tests/        dsp/ (unitaires), realtime/ (aucune allocation dans l'audio),
              integration/ (processeur complet, presets), benchmark/ (charge CPU)
```

`SubdAmpRealtimeTests` et `SubdAmpProcessorTests` remplacent `malloc`/`new` pour compter
les allocations : toute allocation dans le traitement audio fait échouer les tests.

## Règles du projet

- **Thread audio** : aucune allocation, aucun verrou, aucun log, aucune I/O dans
  `processBlock` ni dans le callback audio du standalone. Tous les tampons sont
  préalloués dans `prepareToPlay` / `audioDeviceAboutToStart`.
- **Le son sec n'attend pas les textures** : leur latence n'est pas compensée dans leur MIX
  ni reportée au DAW (seule celle d'une capture rééchantillonnée l'est).
- **Paramètres** : les identifiants (`plugin/Parameters.h`) ne sont jamais renommés.
- **Presets** : l'état porte un numéro de version (`plugin/StateVersioning.h`). Un
  paramètre ajouté reprend sa valeur par défaut au chargement d'un ancien preset ;
  un changement incompatible ajoute une étape de migration.
- **Signal** : entrée mono (canal gauche si la piste est stéréo), chaîne stéréo ; chaque
  module mono traite la somme (L + R) / 2.
- **Ordre de la chaîne** : propriété `chainOrder` de l'état (noms en toutes lettres, lecture
  tolérante), transmise au thread audio par un atomic de 64 bits.

## Licences des dépendances

| Dépendance | Licence | Conséquence |
|---|---|---|
| JUCE 9 | AGPLv3 ou licence commerciale JUCE | Aucune contrainte en usage perso. Pour distribuer un jour : code source publié sous AGPLv3, ou licence commerciale. |
| VST3 SDK 3.8 (fourni avec JUCE) | MIT | Aucune contrainte. |
| ASIO SDK (fourni avec JUCE, Windows) | Licence Steinberg ou GPLv3 | Aucune contrainte en usage perso. |
| NeuralAmpModelerCore | MIT | Aucune contrainte. |
| Eigen (utilisé par NAM) | MPL 2.0 | Aucune contrainte tant que ses fichiers ne sont pas modifiés. |
| Signalsmith Stretch + Signalsmith Linear | MIT | Aucune contrainte. |
| Catch2 (tests seulement) | BSL-1.0 | Aucune contrainte. |
| pluginval (CI seulement, non lié) | GPLv3 | Outil de test, n'entre pas dans le plugin. |
