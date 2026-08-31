param(
	[string]$EndpointUrl = "http://127.0.0.1:8085",
	[string]$BuildDir = "tests/build",
	[string]$OutputPath = "",
	[ValidateSet("official-1.5", "native-cpp")]
	[string]$Protocol = "native-cpp"
)

$ErrorActionPreference = "Stop"

if ($env:OFXIC_RUN_LIVE_ACESTEP -ne "1") {
	throw "Live ACE-Step generation is disabled. Set OFXIC_RUN_LIVE_ACESTEP=1 to opt in."
}

$repositoryRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$resolvedBuildDir = Join-Path $repositoryRoot $BuildDir
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
	$timestamp = Get-Date -Format "yyyyMMdd-HHmmss-fff"
	$OutputPath = "tests/build/live/ofxIC-acestep-live-$timestamp.wav"
}
$resolvedOutputPath = Join-Path $repositoryRoot $OutputPath
$outputDirectory = Split-Path -Parent $resolvedOutputPath
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

& cmake -S (Join-Path $repositoryRoot "tests") -B $resolvedBuildDir
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed with exit code $LASTEXITCODE" }

& cmake --build $resolvedBuildDir --target ofxIC_acestep_live_smoke --config Debug
if ($LASTEXITCODE -ne 0) { throw "ACE-Step live smoke build failed with exit code $LASTEXITCODE" }

$executable = Join-Path $resolvedBuildDir "Debug/ofxIC_acestep_live_smoke.exe"
if (!(Test-Path -LiteralPath $executable -PathType Leaf)) {
	$executable = Join-Path $resolvedBuildDir "ofxIC_acestep_live_smoke"
}
& $executable $EndpointUrl $resolvedOutputPath $Protocol
if ($LASTEXITCODE -ne 0) { throw "ACE-Step live smoke failed with exit code $LASTEXITCODE" }
