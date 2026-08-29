param(
	[ValidateSet("Debug", "Release")]
	[string] $Configuration = "Release",
	[switch] $IncrementalExample,
	[switch] $SkipGuiSmoke
)

$ErrorActionPreference = "Stop"
$repository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$testBuild = Join-Path ([System.IO.Path]::GetTempPath()) "ofxIC-professional-validation"

Write-Output "[1/3] Deterministic C++ and Python tests"
cmake -S (Join-Path $repository "tests") -B $testBuild
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed with exit code $LASTEXITCODE" }
cmake --build $testBuild --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "Test build failed with exit code $LASTEXITCODE" }
ctest --test-dir $testBuild -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Deterministic tests failed with exit code $LASTEXITCODE" }

Write-Output "[2/3] Generated project verification and Example rebuild"
$buildArguments = @{ Configuration = $Configuration }
if ($IncrementalExample) { $buildArguments.Incremental = $true }
& (Join-Path $PSScriptRoot "build-example.ps1") @buildArguments

if ($SkipGuiSmoke) {
	Write-Output "[3/3] GUI smoke skipped by explicit request"
} else {
	Write-Output "[3/3] Model-free one-click GUI vertical smoke"
	& (Join-Path $PSScriptRoot "smoke-one-click-local.ps1")
}

Write-Output "ofxIC Windows validation passed"
