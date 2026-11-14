#!/bin/bash
# Fabrique l'image disque SubdAmp.dmg : « Installer SubdAmp.pkg » et « Désinstaller SubdAmp.pkg ».
#
# Usage : bash installer/macos/build_pkg.sh <dossier des artefacts JUCE> [dossier de sortie]
#   par exemple : bash installer/macos/build_pkg.sh build/SubdAmp_artefacts/Release dist-installer
#
# L'installateur retire d'abord l'ancienne version (y compris une copie manuelle dans le
# dossier personnel), puis pose :
#   - le plugin VST3   dans /Library/Audio/Plug-Ins/VST3
#   - l'application    dans /Applications
# Les licences des polices (SIL OFL) voyagent dans chaque bundle (Contents/Resources/licences).
set -euo pipefail
cd "$(dirname "$0")/../.."

ARTEFACTS="${1:?dossier des artefacts JUCE (…/SubdAmp_artefacts/Release)}"
OUTDIR="${2:-dist-installer}"
mkdir -p "$OUTDIR"
VERSION="$(sed -n 's/^project(SubdAmp VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

stage() {  # nom, chemin d'installation, bundle source
  mkdir -p "$WORK/$1/$2"
  cp -R "$3" "$WORK/$1/$2/"
  local bundle="$WORK/$1/$2/$(basename "$3")"
  mkdir -p "$bundle/Contents/Resources/licences"
  cp assets/fonts/OFL-*.txt "$bundle/Contents/Resources/licences/"
  # Signature locale (ad hoc), refaite après l'ajout des licences : sans elle, un Mac
  # Apple Silicon refuse de charger le code.
  codesign --force --deep --sign - "$bundle"
}

stage vst3 "Library/Audio/Plug-Ins/VST3" "$ARTEFACTS/VST3/SubdAmp.vst3"
stage app  "Applications"                "$ARTEFACTS/Standalone/SubdAmp.app"

scripts() {  # nom -> option --scripts (ou rien)
  if [ -d "installer/macos/scripts/$1" ]; then
    mkdir -p "$WORK/scripts-$1"
    cp installer/macos/scripts/"$1"/* installer/macos/scripts/common/* "$WORK/scripts-$1/"
    chmod +x "$WORK/scripts-$1"/pre* "$WORK/scripts-$1"/post* 2>/dev/null || true
    echo --scripts "$WORK/scripts-$1"
  fi
}

mkdir -p "$WORK/pkgs"
component() {  # nom, identifiant
  # Bundles non « déplaçables » : sinon, s'il existe déjà un SubdAmp ailleurs sur le
  # disque, l'installateur l'écraserait à sa place.
  pkgbuild --analyze --root "$WORK/$1" "$WORK/$1.plist" >/dev/null
  python3 - "$WORK/$1.plist" <<'PY'
import plistlib, sys
path = sys.argv[1]
with open(path, 'rb') as f:
    bundles = plistlib.load(f)
for b in bundles:
    b['BundleIsRelocatable'] = False
with open(path, 'wb') as f:
    plistlib.dump(bundles, f)
PY
  pkgbuild --root "$WORK/$1" --component-plist "$WORK/$1.plist" $(scripts "$1") \
           --identifier "$2" --version "$VERSION" --install-location / "$WORK/pkgs/$1.pkg"
}
component vst3 com.labtechno.subdamp.vst3
component app  com.labtechno.subdamp.app

sed "s/@VERSION@/$VERSION/g" installer/macos/distribution.xml > "$WORK/distribution.xml"
productbuild --distribution "$WORK/distribution.xml" --resources installer/macos/resources \
             --package-path "$WORK/pkgs" "$OUTDIR/Installer SubdAmp.pkg"

# Désinstallateur : un paquet sans fichiers, qui exécute le script de retrait.
mkdir -p "$WORK/uninstall-pkgs"
pkgbuild --nopayload $(scripts uninstall) --identifier com.labtechno.subdamp.uninstall \
         --version "$VERSION" "$WORK/uninstall-pkgs/uninstall.pkg"
sed "s/@VERSION@/$VERSION/g" installer/macos/distribution-uninstall.xml > "$WORK/distribution-uninstall.xml"
productbuild --distribution "$WORK/distribution-uninstall.xml" --resources installer/macos/resources-uninstall \
             --package-path "$WORK/uninstall-pkgs" "$OUTDIR/Désinstaller SubdAmp.pkg"

mkdir -p "$WORK/dmg"
cp "$OUTDIR/Installer SubdAmp.pkg" "$OUTDIR/Désinstaller SubdAmp.pkg" "$WORK/dmg/"
rm -f "$OUTDIR/SubdAmp.dmg"
hdiutil create -volname "SubdAmp $VERSION" -srcfolder "$WORK/dmg" -fs HFS+ -format UDZO -ov "$OUTDIR/SubdAmp.dmg" >/dev/null
echo "Image disque : $OUTDIR/SubdAmp.dmg (version $VERSION)"
