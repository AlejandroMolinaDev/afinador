#!/usr/bin/env bash
# NovaPitch — macOS installer (.pkg) with VST3, AU, CLAP, the standalone app and
# the user guide, for Apple Silicon and Intel (Universal binaries).
#
#   installer/macos/build_pkg.sh <artefacts-dir> <version> <guide.html> <out-dir>
#
# Signing (optional; without it bundles are ad-hoc signed and the .pkg is unsigned):
#   DEVELOPER_ID_APP        "Developer ID Application: Name (TEAMID)"  -> bundles, hardened runtime
#   DEVELOPER_ID_INSTALLER  "Developer ID Installer: Name (TEAMID)"    -> productsign
#   NOTARY_PROFILE          keychain profile created with `xcrun notarytool store-credentials`
#                           -> notarize + staple (needs both identities above)
#
# FROZEN: the package identifiers (com.novapitch.novapitch.pkg.*) identify the
# components for upgrades. Never change them.
set -euo pipefail

ART="$1"; VERSION="$2"; GUIDE="$3"; OUT="$4"
HERE="$(cd "$(dirname "$0")" && pwd)"
IDENT="com.novapitch.novapitch.pkg"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/pkgs" "$OUT"

sign_bundle() {
    if [[ -n "${DEVELOPER_ID_APP:-}" ]]; then
        local entitlements=()
        [[ "$1" == *.app ]] && entitlements=(--entitlements "$HERE/NovaPitch.entitlements")
        codesign --force --deep --options runtime --timestamp ${entitlements[@]+"${entitlements[@]}"} --sign "$DEVELOPER_ID_APP" "$1"
    else
        codesign --force --deep --sign - "$1"
    fi
    codesign --verify --strict "$1"
}

# id | bundle (relative to ART) | install location | scripts dir (optional)
components=(
    "vst3|VST3/NovaPitch.vst3|/Library/Audio/Plug-Ins/VST3|"
    "au|AU/NovaPitch.component|/Library/Audio/Plug-Ins/Components|$HERE/scripts/au"
    "clap|CLAP/NovaPitch.clap|/Library/Audio/Plug-Ins/CLAP|"
    "app|Standalone/NovaPitch.app|/Applications|"
)

for entry in "${components[@]}"; do
    IFS='|' read -r id bundle location scripts <<< "$entry"
    root="$WORK/root-$id"
    mkdir -p "$root"
    cp -R "$ART/$bundle" "$root/"
    sign_bundle "$root/$(basename "$bundle")"

    # Plugin bundles share one CFBundleIdentifier across formats: never let the
    # installer "relocate" a bundle to where another format already lives.
    pkgbuild --analyze --root "$root" "$WORK/$id.plist"
    i=0
    while /usr/libexec/PlistBuddy -c "Print :$i" "$WORK/$id.plist" > /dev/null 2>&1; do
        # pkgbuild omits the key for plugin bundles: (re)add it instead of Set.
        /usr/libexec/PlistBuddy -c "Delete :$i:BundleIsRelocatable" "$WORK/$id.plist" > /dev/null 2>&1 || true
        /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$WORK/$id.plist"
        i=$((i + 1))
    done

    args=(--root "$root" --component-plist "$WORK/$id.plist" --identifier "$IDENT.$id"
          --version "$VERSION" --install-location "$location")
    [[ -n "$scripts" ]] && args+=(--scripts "$scripts")
    pkgbuild "${args[@]}" "$WORK/pkgs/$id.pkg"
done

# User guide + third-party licenses
mkdir -p "$WORK/root-docs"
cp "$GUIDE" "$WORK/root-docs/Guia_de_usuario.html"
cp "$HERE/../../THIRD_PARTY_LICENSES.md" "$WORK/root-docs/"
pkgbuild --root "$WORK/root-docs" --identifier "$IDENT.docs" --version "$VERSION" \
         --install-location "/Library/Application Support/NovaPitch" "$WORK/pkgs/docs.pkg"

# Distribution (choices, Spanish texts, Universal, macOS 10.13+)
mkdir -p "$WORK/resources"
cp "$HERE/resources/"*.html "$WORK/resources/"
cp "$HERE/../LICENCIAS.txt" "$WORK/resources/LICENCIAS.txt"
sed "s/@VERSION@/$VERSION/g" "$HERE/distribution.xml" > "$WORK/distribution.xml"

PKG="$OUT/NovaPitch-$VERSION-macOS.pkg"
productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/resources" \
             --package-path "$WORK/pkgs" "$WORK/unsigned.pkg"

if [[ -n "${DEVELOPER_ID_INSTALLER:-}" ]]; then
    productsign --sign "$DEVELOPER_ID_INSTALLER" --timestamp "$WORK/unsigned.pkg" "$PKG"
    if [[ -n "${NOTARY_PROFILE:-}" && -n "${DEVELOPER_ID_APP:-}" ]]; then
        xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY_PROFILE" --wait
        xcrun stapler staple "$PKG"
    fi
else
    cp "$WORK/unsigned.pkg" "$PKG"
    echo "NOTE: unsigned .pkg (set DEVELOPER_ID_* and NOTARY_PROFILE for a distributable build)"
fi
echo "Built $PKG"
