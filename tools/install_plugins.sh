#!/usr/bin/env bash
# Copies the built NovaPitch plugins to the per-user plugin folders so FL Studio
# (macOS) or any Linux host can find them.
#
#   ./tools/install_plugins.sh [build-dir] [config]
#
# macOS : ~/Library/Audio/Plug-Ins/{VST3,Components,CLAP}
# Linux : ~/.vst3 and ~/.clap
set -euo pipefail

BUILD_DIR="${1:-build}"
CONFIG="${2:-Release}"
ART="$BUILD_DIR/NovaPitch_artefacts/$CONFIG"

if [[ ! -d "$ART" ]]; then
    echo "No artefacts found in $ART. Build first: cmake --build $BUILD_DIR --config $CONFIG" >&2
    exit 1
fi

copy_bundle() {
    local src="$1" dst_dir="$2"
    if [[ -e "$src" ]]; then
        mkdir -p "$dst_dir"
        rm -rf "$dst_dir/$(basename "$src")"
        cp -R "$src" "$dst_dir/"
        echo "  -> $dst_dir/$(basename "$src")"
    fi
}

case "$(uname -s)" in
    Darwin)
        PLUG="$HOME/Library/Audio/Plug-Ins"
        echo "Installing NovaPitch ($CONFIG) for macOS:"
        copy_bundle "$ART/VST3/NovaPitch.vst3"     "$PLUG/VST3"
        copy_bundle "$ART/AU/NovaPitch.component"  "$PLUG/Components"
        copy_bundle "$ART/CLAP/NovaPitch.clap"     "$PLUG/CLAP"
        # Bundles must be signed (ad-hoc is enough locally) to load on Apple Silicon.
        for b in "$PLUG/VST3/NovaPitch.vst3" "$PLUG/Components/NovaPitch.component" "$PLUG/CLAP/NovaPitch.clap"; do
            [[ -e "$b" ]] && codesign --force --deep --sign - "$b" >/dev/null 2>&1 || true
            [[ -e "$b" ]] && xattr -dr com.apple.quarantine "$b" 2>/dev/null || true
        done
        # Make the system notice the new/updated Audio Unit.
        killall -9 AudioComponentRegistrar 2>/dev/null || true
        echo "Done. In FL Studio: Options > Manage plugins > Find more plugins."
        ;;
    Linux)
        echo "Installing NovaPitch ($CONFIG) for Linux:"
        copy_bundle "$ART/VST3/NovaPitch.vst3" "$HOME/.vst3"
        copy_bundle "$ART/CLAP/NovaPitch.clap" "$HOME/.clap"
        ;;
    *)
        echo "On Windows use tools/install_plugins.ps1" >&2
        exit 1
        ;;
esac
