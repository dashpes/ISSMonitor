#!/usr/bin/env bash
#
# Fetch the third-party display and orbit libraries into firmware/lib/.
# They are gitignored: LovyanGFX ships ~126 MB of CJK font tables, which has
# no business in this repo.
#
# Run once after cloning:  ./scripts/vendor_libs.sh
#
set -euo pipefail
cd "$(dirname "$0")/.."
LIB=firmware/lib
mkdir -p "$LIB"

fetch() {   # fetch <name> <repo-url>
    local name="$1" url="$2" tmp
    if [ -d "$LIB/$name/src" ]; then
        echo "$name: already present, skipping"
        return
    fi
    echo "$name: cloning..."
    tmp="$(mktemp -d)"
    git clone --depth 1 --quiet "$url" "$tmp/repo"
    mkdir -p "$LIB/$name"
    cp -R "$tmp/repo/src" "$LIB/$name/"
    cp "$tmp/repo/library.json" "$tmp/repo/library.properties" "$LIB/$name/" 2>/dev/null || true
    rm -rf "$tmp"
    echo "$name: done"
}

fetch LovyanGFX https://github.com/lovyan03/LovyanGFX.git
fetch Sgp4      https://github.com/Hopperpop/Sgp4-Library.git

echo
echo "Libraries in place. Now:"
echo "  cp firmware/src/secrets_example.h firmware/src/secrets.h   # add your wifi"
echo "  cd firmware && pio run -t upload -t monitor"
