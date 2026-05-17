#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${BUILD_DIR:-${script_dir}/build-cmake}"

rm -rf "$build_dir"

cmake -S "$script_dir" -B "$build_dir" "$@"
cmake --build "$build_dir" --target flexi-ninja
sudo cmake --install "$build_dir" --component flexi-ninja
cmake --build "$build_dir" --target flexi-ninja-eth
sudo cmake --install "$build_dir" --component flexi-ninja-eth
