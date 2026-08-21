$ErrorActionPreference = "Stop"

$buildDirectory = Join-Path ([System.IO.Path]::GetTempPath()) "ofxic-tests"
cmake -S tests -B $buildDirectory
cmake --build $buildDirectory --config Release
ctest --test-dir $buildDirectory -C Release --output-on-failure
