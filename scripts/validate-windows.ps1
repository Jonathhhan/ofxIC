param(
	[ValidateSet("Debug", "Release")]
	[string] $Configuration = "Release",
	[switch] $IncrementalExample,
	[switch] $SkipMinimalExamples,
	[switch] $SkipGuiSmoke,
	[switch] $SkipRuntimePlan
)

$ErrorActionPreference = "Stop"
$repository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$testBuild = Join-Path ([System.IO.Path]::GetTempPath()) "ofxIC-professional-validation"

Write-Output "[1/6] Deterministic C++ and Python tests"
cmake -S (Join-Path $repository "tests") -B $testBuild
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed with exit code $LASTEXITCODE" }
cmake --build $testBuild --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "Test build failed with exit code $LASTEXITCODE" }
ctest --test-dir $testBuild -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Deterministic tests failed with exit code $LASTEXITCODE" }

if ($SkipMinimalExamples) {
	Write-Output "[2/6] Minimal examples skipped by explicit request"
} else {
	Write-Output "[2/6] Minimal chat example rebuild"
	$chatArguments = @{ Configuration = $Configuration; Example = "example-chat" }
	if (-not $IncrementalExample) { $chatArguments.Rebuild = $true }
	& (Join-Path $PSScriptRoot "build-example.ps1") @chatArguments

	Write-Output "[3/6] Grounded documents example rebuild"
	$documentsArguments = @{ Configuration = $Configuration; Example = "example-documents" }
	if (-not $IncrementalExample) { $documentsArguments.Rebuild = $true }
	& (Join-Path $PSScriptRoot "build-example.ps1") @documentsArguments
}

Write-Output "[4/6] Complete workbench rebuild"
$buildArguments = @{ Configuration = $Configuration }
if (-not $IncrementalExample) { $buildArguments.Rebuild = $true }
& (Join-Path $PSScriptRoot "build-example.ps1") @buildArguments

if ($SkipGuiSmoke) {
	Write-Output "[5/6] GUI smoke skipped by explicit request"
} else {
	Write-Output "[5/6] Model-free one-click GUI vertical smoke"
	& (Join-Path $PSScriptRoot "smoke-one-click-local.ps1")
}

if ($SkipRuntimePlan) {
	Write-Output "[6/6] Local runtime discovery plan skipped by explicit request"
} else {
	Write-Output "[6/6] Local runtime discovery plan (no server starts)"
	& (Join-Path $PSScriptRoot "smoke-local-runtime-matrix.ps1")
}

Write-Output "ofxIC Windows validation passed"
