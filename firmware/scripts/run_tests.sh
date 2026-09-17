#!/usr/bin/env bash
#
# run_tests.sh -- compile and run the orbit tests with plain g++/clang++.
#
#   ./scripts/run_tests.sh              run everything
#   ./scripts/run_tests.sh geo          run only test_geo.cpp
#
# Same tests `pio test -e native` runs, minus PlatformIO and minus any network
# access. Sub-second, so run it after every edit.
#
# Each test_*.cpp has its own main(), so they are built as separate binaries.
#
set -uo pipefail
cd "$(dirname "$0")/.."

OUT=".pio/native_test"
mkdir -p "$OUT"
FILTER="${1:-}"

fail=0
for src in test/test_orbit/test_*.cpp; do
    name="$(basename "$src" .cpp)"
    if [[ -n "$FILTER" && "$name" != *"$FILTER"* ]]; then continue; fi

    if ! c++ -std=c++17 -Wall -Wextra -g \
        -I lib/orbit/include -I test/unity_vendor -DUNITY_INCLUDE_DOUBLE \
        test/unity_vendor/unity.c lib/orbit/src/*.cpp "$src" \
        -o "$OUT/$name"; then
        echo "BUILD FAILED: $name"
        fail=1
        continue
    fi

    echo "── $name ──────────────────────────────"
    "$OUT/$name" || fail=1
    echo
done

if [[ $fail -ne 0 ]]; then
    echo "SOME TESTS FAILED"
    exit 1
fi
echo "ALL SUITES PASSED"
