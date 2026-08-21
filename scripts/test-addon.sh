#!/usr/bin/env sh
set -eu

build_dir="${TMPDIR:-/tmp}/ofxic-tests"
cmake -S tests -B "$build_dir"
cmake --build "$build_dir"
ctest --test-dir "$build_dir" --output-on-failure
