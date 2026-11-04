# SubdAmp

Plugin guitare (VST3 + application standalone) orienté pitch : octaver, whammy,
harmonizer, frequency shifter, shimmer, textures. Usage personnel.

**État : étape 2.** Chaîne `entrée → GATE → [avant l'ampli] → AMPLI → CAB → [après l'ampli] → sortie`.

| Module | Rôle | Latence |
|---|---|---|
| GATE | Noise gate à hystérésis, ouverture instantanée, relâchement réglable | LIVE (0) |
| SUB | Octaver analogique façon OC-2 : -1 et -2 octaves, +1 octave (fuzz), tonalité | LIVE (0) |
| AMPLI | Capture Neural Amp Modeler (`.nam`) + gain, égaliseur 3 bandes, niveau | LIVE (0 à 48 kHz, < 1 ms sinon) |
| CAB | Réponse impulsionnelle (`.wav`, `.aif`), ou cab interne par filtres | LIVE (0) |
| FREQ SHIFT | Décaleur de fréquence (type Bode), mix, réinjection | LIVE (≈ 0) |

SUB et FREQ SHIFT se placent avant ou après l'ampli (bouton sur leur carte), sans clic.

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

Prérequis : CMake ≥ 3.22, Git, et Visual Studio 2022 (Windows) ou Xcode (macOS).
JUCE, NeuralAmpModelerCore, Eigen et Catch2 sont téléchargés automatiquement.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Binaires produits : `build/SubdAmp_artefacts/Release/VST3/` et `.../Standalone/`.

Sur macOS, ajouter `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` pour un binaire universel.

## Structure

```
dsp/          C++ pur, sans JUCE, testable unitairement : modules (gate, SUB, FREQ SHIFT,
              ampli NAM, égaliseur), rééchantillonnage, échange temps réel des captures
plugin/       AudioProcessor, paramètres, chaîne, cab (convolution JUCE), presets
gui/          interface du plugin (cartes de modules, sélecteurs de fichiers)
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
- **Le son sec n'attend jamais** : les modules lents (badge TEXTURE) tournent en
  parallèle et leur latence n'est pas reportée au DAW.
- **Paramètres** : les identifiants (`plugin/Parameters.h`) ne sont jamais renommés.
- **Presets** : l'état porte un numéro de version (`plugin/StateVersioning.h`). Un
  paramètre ajouté reprend sa valeur par défaut au chargement d'un ancien preset ;
  un changement incompatible ajoute une étape de migration.
- **Signal** : traitement mono (canal gauche si la piste est stéréo), sortie recopiée en stéréo.

## Licences des dépendances

| Dépendance | Licence | Conséquence |
|---|---|---|
| JUCE 9 | AGPLv3 ou licence commerciale JUCE | Aucune contrainte en usage perso. Pour distribuer un jour : code source publié sous AGPLv3, ou licence commerciale. |
| VST3 SDK 3.8 (fourni avec JUCE) | MIT | Aucune contrainte. |
| ASIO SDK (fourni avec JUCE, Windows) | Licence Steinberg ou GPLv3 | Aucune contrainte en usage perso. |
| NeuralAmpModelerCore | MIT | Aucune contrainte. |
| Eigen (utilisé par NAM) | MPL 2.0 | Aucune contrainte tant que ses fichiers ne sont pas modifiés. |
| Catch2 (tests seulement) | BSL-1.0 | Aucune contrainte. |
| pluginval (CI seulement, non lié) | GPLv3 | Outil de test, n'entre pas dans le plugin. |
