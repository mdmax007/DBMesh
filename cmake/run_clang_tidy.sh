#!/usr/bin/env bash
# run_clang_tidy.sh <clang-tidy> <compile_commands_dir> <source_root>
set -euo pipefail

CLANG_TIDY="$1"
COMPILE_COMMANDS_DIR="$2"
SOURCE_ROOT="$3"

find "$SOURCE_ROOT" \
  \( -path "$SOURCE_ROOT/build" -o -path "$SOURCE_ROOT/build-*" \) -prune \
  -o -name "*.cpp" -print \
  | xargs -P "$(nproc)" "$CLANG_TIDY" \
    -p "$COMPILE_COMMANDS_DIR" \
    --quiet \
    2>&1 | grep -v "^$"
