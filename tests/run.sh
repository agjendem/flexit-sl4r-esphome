#!/usr/bin/env bash
# Builds and runs the protocol tests. Needs a C++17 compiler and nothing else -
# no ESPHome, no toolchain for the ESP32, no hardware.
set -euo pipefail

cd "$(dirname "$0")"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "Building..."
c++ -std=c++17 -Wall -Wextra -Werror -O1 -o "$tmp/test_protocol" test_protocol.cpp

# The captures are stored gzipped; the test reads plain hex text.
for f in ../research/captures/*.hex.gz; do
  [ -e "$f" ] || continue
  gunzip -c "$f" > "$tmp/$(basename "${f%.gz}")"
done

"$tmp/test_protocol" \
  "$tmp/2026-08-13-panelsekvens.hex" \
  "$tmp/2026-08-14-tilluftkorrelasjon.hex" \
  "$tmp/2026-08-21-viftetrinn-ubalanse.hex"
