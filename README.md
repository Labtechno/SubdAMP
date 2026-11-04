# SubdAmp

Plugin guitare (VST3 + application standalone) orienté pitch : octaver, whammy,
harmonizer, frequency shifter, shimmer, textures. Usage personnel.

**État : étape 1 — squelette.** Le son traverse le plugin avec gain d'entrée, volume
de sortie, bypass sans clic et vumètres. La chaîne de modules arrive aux étapes suivantes.

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
JUCE et Catch2 sont téléchargés automatiquement.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Binaires produits : `build/SubdAmp_artefacts/Release/VST3/` et `.../Standalone/`.

Sur macOS, ajouter `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` pour un binaire universel.

## Structure

```
dsp/          C++ pur, sans JUCE, testable unitairement (lissage, gain, bypass, mesure)
plugin/       AudioProcessor, paramètres, état sauvegardé et sa migration
gui/          interface du plugin
standalone/   application standalone : carte son, canal guitare, réglages
tests/        tests unitaires (Catch2)
```

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
| Catch2 (tests seulement) | BSL-1.0 | Aucune contrainte. |
| pluginval (CI seulement, non lié) | GPLv3 | Outil de test, n'entre pas dans le plugin. |
