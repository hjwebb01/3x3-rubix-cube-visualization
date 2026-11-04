#!/bin/sh
set -eu
unset LD_LIBRARY_PATH
cd "$(dirname "$0")"
cmake -S . -B build -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build build --target rubik_gui -j
exec ./build/rubik_gui
