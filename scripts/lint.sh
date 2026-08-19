#!/bin/sh
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)

clang-tidy --quiet --warnings-as-errors='*' \
  -checks='-*,bugprone-incorrect-roundings,bugprone-narrowing-conversions,bugprone-signed-char-misuse,bugprone-macro-parentheses,bugprone-branch-clone,bugprone-suspicious-memset-usage,bugprone-sizeof-expression,bugprone-string-constructor,bugprone-undefined-memory-manipulation,bugprone-use-after-move,clang-analyzer-core.*,clang-analyzer-cplusplus.*,clang-analyzer-unix.*,performance-*' \
  "$ROOT"/bridge/src/*.cpp "$ROOT"/bridge/src/*/*.cpp \
  -- -std=c++17 \
  -DRADIOLIB_GODMODE=1 \
  -I"$ROOT/bridge/include" \
  -isystem "$ROOT/test/support" \
  -isystem "$ROOT/external/radiolib/src" \
  -isystem "$ROOT/external/meshcore/src" \
  -isystem "$ROOT/external/meshcore/lib/ed25519" \
  -isystem "$ROOT/external/crypto" \
  -isystem "$ROOT/external/nanopb" \
  -isystem "$ROOT/external/meshtastic/src/mesh/generated" \
  -isystem "$ROOT/external/meshtastic/src"
