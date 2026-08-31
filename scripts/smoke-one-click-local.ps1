param(
	[string] $Executable = (Join-Path $PSScriptRoot "..\ofxICExample\bin\ofxICExample.exe"),
	[string] $Python = "python.exe",
	[ValidateRange(10, 300)]
	[int] $StartupEvidenceTimeoutSeconds = 30,
	[switch] $KeepEvidence
)

$ErrorActionPreference = "Stop"
$repository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$executablePath = [System.IO.Path]::GetFullPath($Executable)
$pythonCommand = Get-Command $Python -ErrorAction Stop
$pythonPath = $pythonCommand.Source
$aceFixture = Join-Path $repository "tests\acestep_endpoint_fixture.py"
$samFixture = Join-Path $repository "scripts\sam-bridge-server.py"
$noListenerFixture = Join-Path $repository "tests\runtime_no_listener_fixture.py"
$fixtureMarkers = @($aceFixture, $samFixture, $noListenerFixture)
$temporary = Join-Path ([System.IO.Path]::GetTempPath()) (
	"ofxIC-one-click-" + [guid]::NewGuid())
$environmentNames = @(
	"OFXIC_ENDPOINT_URL", "OFXIC_SETTINGS_PATH", "OFXIC_HISTORY_PATH",
	"OFXIC_MUSIC_BACKEND", "OFXIC_MUSIC_ENDPOINT_URL", "OFXIC_MUSIC_AUTORUN",
	"OFXIC_MUSIC_PROMPT", "OFXIC_MUSIC_DURATION", "OFXIC_MUSIC_OUTPUT_FORMAT",
	"OFXIC_MUSIC_RESULT_PATH", "OFXIC_ACESTEP_SERVER", "OFXIC_ACESTEP_SERVER_ARGS",
	"OFXIC_RUNTIME_START_TIMEOUT_SECONDS", "OFXIC_DIAGNOSTICS_PATH",
	"OFXIC_SEGMENTATION_ENDPOINT_URL", "OFXIC_SEGMENTATION_AUTORUN",
	"OFXIC_SEGMENTATION_IMAGE", "OFXIC_SEGMENTATION_POINT_X",
	"OFXIC_SEGMENTATION_POINT_Y", "OFXIC_GUI_RESULT_PATH",
	"OFXIC_SAM_BRIDGE_EXECUTABLE", "OFXIC_SAM_BRIDGE_ARGS")
$previousEnvironment = @{}
foreach ($name in $environmentNames) {
	$previousEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
}
$generatedPaths = [System.Collections.Generic.List[string]]::new()
$activeExample = $null
$smokePassed = $false
$fixtureProcessIdsBefore = @(
	Get-CimInstance Win32_Process -Filter "Name = 'python.exe'" -ErrorAction SilentlyContinue |
		Where-Object {
			$commandLine = $_.CommandLine
			$commandLine -and ($fixtureMarkers | Where-Object { $commandLine.Contains($_) })
		} | ForEach-Object { $_.ProcessId })

function Set-ProcessEnvironment([string] $Name, [string] $Value) {
	[Environment]::SetEnvironmentVariable($Name, $Value, "Process")
}

function Test-PortListening([int] $Port) {
	return [System.Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().
		GetActiveTcpListeners().Port -contains $Port
}

function Wait-PortState([int] $Port, [bool] $Listening, [int] $Seconds) {
	$deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
	do {
		if ((Test-PortListening $Port) -eq $Listening) { return $true }
		Start-Sleep -Milliseconds 100
	} while ([DateTime]::UtcNow -lt $deadline)
	return (Test-PortListening $Port) -eq $Listening
}

function Wait-File([string] $Path, [System.Diagnostics.Process] $Process, [int] $Seconds) {
	$deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
	do {
		if (Test-Path -LiteralPath $Path) { return }
		if ($Process.HasExited) {
			throw "ofxICExample exited early with code $($Process.ExitCode)"
		}
		Start-Sleep -Milliseconds 100
	} while ([DateTime]::UtcNow -lt $deadline)
	throw "Timed out waiting for GUI evidence: $Path"
}

function Wait-History([string] $Path, [string] $Task, [int] $Seconds) {
	$deadline = [DateTime]::UtcNow.AddSeconds($Seconds)
	do {
		if (Test-Path -LiteralPath $Path) {
			$content = Get-Content -Raw -LiteralPath $Path
			if ($content -match [regex]::Escape($Task)) { return $content }
		}
		Start-Sleep -Milliseconds 100
	} while ([DateTime]::UtcNow -lt $deadline)
	throw "History did not record $Task within $Seconds seconds"
}

function Stop-ExampleAndRequireOwnedRuntimeExit(
	[System.Diagnostics.Process] $Process, [int] $Port) {
	if (-not $Process.HasExited) {
		$Process.CloseMainWindow() | Out-Null
		if (-not $Process.WaitForExit(5000)) {
			throw "ofxICExample did not close gracefully; cannot prove owned-runtime shutdown"
		}
	}
	if (-not (Wait-PortState $Port $false 5)) {
		throw "GUI-owned runtime remained reachable on port $Port after example shutdown"
	}
}

function Start-Example([string] $Name) {
	$stdout = Join-Path $temporary "$Name.stdout.log"
	$stderr = Join-Path $temporary "$Name.stderr.log"
	return Start-Process -FilePath $executablePath `
		-WorkingDirectory (Split-Path -Parent $executablePath) -WindowStyle Hidden `
		-RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
}

function Stop-FixtureProcesses {
	Get-CimInstance Win32_Process -Filter "Name = 'python.exe'" -ErrorAction SilentlyContinue |
		Where-Object {
			$commandLine = $_.CommandLine
			$commandLine -and
				($fixtureMarkers | Where-Object { $commandLine.Contains($_) }) -and
				$fixtureProcessIdsBefore -notcontains $_.ProcessId
		} | ForEach-Object {
			Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
		}
}

if (-not (Test-Path -LiteralPath $executablePath)) {
	throw "Build the Release example first: $executablePath"
}
if (-not (Test-Path -LiteralPath $aceFixture) -or
	-not (Test-Path -LiteralPath $samFixture) -or
	-not (Test-Path -LiteralPath $noListenerFixture)) {
	throw "Required deterministic fixture scripts are missing"
}
if ((Test-PortListening 8085) -or (Test-PortListening 18085)) {
	throw "One-click smoke requires free ports 8085 and 18085"
}

try {
	New-Item -ItemType Directory -Path $temporary | Out-Null
	Set-ProcessEnvironment "OFXIC_ENDPOINT_URL" `
		"http://diagnostic-user:diagnostic-secret@127.0.0.1:1/v1?api_key=diagnostic-secret"

	$musicResult = Join-Path $temporary "music-result.txt"
	$musicHistory = Join-Path $temporary "music-history.txt"
	$musicDiagnostics = Join-Path $temporary "music-diagnostics.txt"
	Set-ProcessEnvironment "OFXIC_SETTINGS_PATH" (Join-Path $temporary "music.settings")
	Set-ProcessEnvironment "OFXIC_HISTORY_PATH" $musicHistory
	Set-ProcessEnvironment "OFXIC_MUSIC_BACKEND" "acestep"
	Set-ProcessEnvironment "OFXIC_MUSIC_ENDPOINT_URL" "http://127.0.0.1:8085"
	Set-ProcessEnvironment "OFXIC_MUSIC_AUTORUN" "acestep"
	Set-ProcessEnvironment "OFXIC_MUSIC_PROMPT" "deterministic timestamp music"
	Set-ProcessEnvironment "OFXIC_MUSIC_DURATION" "10"
	Set-ProcessEnvironment "OFXIC_MUSIC_OUTPUT_FORMAT" "wav"
	Set-ProcessEnvironment "OFXIC_MUSIC_RESULT_PATH" $musicResult
	Set-ProcessEnvironment "OFXIC_DIAGNOSTICS_PATH" $musicDiagnostics
	Set-ProcessEnvironment "OFXIC_ACESTEP_SERVER" $pythonPath
	Set-ProcessEnvironment "OFXIC_ACESTEP_SERVER_ARGS" (
		'"' + $aceFixture + '" --port 8085')
	$activeExample = Start-Example "music"
	Wait-File $musicDiagnostics $activeExample $StartupEvidenceTimeoutSeconds
	$diagnostics = Get-Content -Raw -LiteralPath $musicDiagnostics
	if ($diagnostics -notmatch '(?m)^ofxIC\.version=0\.2\.1-dev$' -or
		$diagnostics -notmatch '(?m)^privacy=' -or
		$diagnostics -notmatch '(?m)^chat\.endpoint=http://127\.0\.0\.1:1/v1$' -or
		$diagnostics -match 'diagnostic-secret|diagnostic-user|api_key=|deterministic timestamp music' -or
		$diagnostics -match [regex]::Escape($env:USERPROFILE)) {
		throw "Diagnostics are incomplete or disclose private input: $diagnostics"
	}
	Set-ProcessEnvironment "OFXIC_DIAGNOSTICS_PATH" $null
	Wait-File $musicResult $activeExample 30
	$musicEvidence = Get-Content -Raw -LiteralPath $musicResult
	$musicMatch = [regex]::Match(
		$musicEvidence, '(?m)^Saved(?: and playing|, but playback could not load): (.+\.wav)\r?$')
	if ($musicEvidence -notmatch "completed" -or -not $musicMatch.Success) {
		throw "Unexpected managed ACE-Step evidence: $musicEvidence"
	}
	$musicPath = $musicMatch.Groups[1].Value
	if (-not (Test-Path -LiteralPath $musicPath)) {
		throw "Managed ACE-Step reported a missing output: $musicPath"
	}
	$generatedPaths.Add($musicPath)
	$history = Wait-History $musicHistory "music-generation" 5
	if ($history -notmatch '"completed"' -or
		$history -match "deterministic timestamp music") {
		throw "Music history is incomplete or contains the private prompt"
	}
	Stop-ExampleAndRequireOwnedRuntimeExit $activeExample 8085
	$activeExample = $null
	Write-Output "Managed ACE-Step one-click task passed"

	$failureResult = Join-Path $temporary "startup-failure-result.txt"
	$failureHistory = Join-Path $temporary "startup-failure-history.txt"
	Set-ProcessEnvironment "OFXIC_SETTINGS_PATH" (Join-Path $temporary "startup-failure.settings")
	Set-ProcessEnvironment "OFXIC_HISTORY_PATH" $failureHistory
	Set-ProcessEnvironment "OFXIC_MUSIC_RESULT_PATH" $failureResult
	Set-ProcessEnvironment "OFXIC_ACESTEP_SERVER_ARGS" (
		'"' + $noListenerFixture + '" --seconds 30')
	Set-ProcessEnvironment "OFXIC_RUNTIME_START_TIMEOUT_SECONDS" "1"
	$activeExample = Start-Example "startup-failure"
	Wait-File $failureResult $activeExample $StartupEvidenceTimeoutSeconds
	$failureEvidence = Get-Content -Raw -LiteralPath $failureResult
	if ($failureEvidence -notmatch "timed out while waiting for the local runtime") {
		throw "Unexpected runtime-start timeout evidence: $failureEvidence"
	}
	$history = Wait-History $failureHistory "music-generation" 5
	if ($history -notmatch '"failed"') {
		throw "Runtime-start timeout was not classified as failed in history"
	}
	$activeExample.CloseMainWindow() | Out-Null
	if (-not $activeExample.WaitForExit(5000)) {
		throw "ofxICExample did not close after runtime-start timeout"
	}
	$activeExample = $null
	Write-Output "Managed runtime-start timeout path passed"

	Set-ProcessEnvironment "OFXIC_MUSIC_AUTORUN" $null
	Set-ProcessEnvironment "OFXIC_MUSIC_RESULT_PATH" $null
	Set-ProcessEnvironment "OFXIC_RUNTIME_START_TIMEOUT_SECONDS" $null
	$imagePath = Join-Path $temporary "segmentation-fixture.ppm"
	[System.IO.File]::WriteAllBytes($imagePath,
		[System.Text.Encoding]::ASCII.GetBytes("P6`n2 2`n255`nabcdefghijkl"))
	$samResult = Join-Path $temporary "sam-result.txt"
	$samHistory = Join-Path $temporary "sam-history.txt"
	Set-ProcessEnvironment "OFXIC_SETTINGS_PATH" (Join-Path $temporary "sam.settings")
	Set-ProcessEnvironment "OFXIC_HISTORY_PATH" $samHistory
	Set-ProcessEnvironment "OFXIC_SEGMENTATION_ENDPOINT_URL" "http://127.0.0.1:18085"
	Set-ProcessEnvironment "OFXIC_SEGMENTATION_AUTORUN" "1"
	Set-ProcessEnvironment "OFXIC_SEGMENTATION_IMAGE" $imagePath
	Set-ProcessEnvironment "OFXIC_SEGMENTATION_POINT_X" "0.5"
	Set-ProcessEnvironment "OFXIC_SEGMENTATION_POINT_Y" "0.5"
	Set-ProcessEnvironment "OFXIC_GUI_RESULT_PATH" $samResult
	Set-ProcessEnvironment "OFXIC_SAM_BRIDGE_EXECUTABLE" $pythonPath
	# Fixture mode still exercises the same explicit runner/model precedence used
	# by a real installation; the bridge does not execute these paths here.
	Set-ProcessEnvironment "OFXIC_SAM_RUNNER" $samFixture
	Set-ProcessEnvironment "OFXIC_SAM_MODEL" $imagePath
	Set-ProcessEnvironment "OFXIC_SAM_BRIDGE_ARGS" "--fixture-mask"
	$activeExample = Start-Example "sam"
	Wait-File $samResult $activeExample 30
	$samEvidence = Get-Content -Raw -LiteralPath $samResult
	if ($samEvidence -notmatch "Segmentation completed" -or
		$samEvidence -notmatch "PGM mask") {
		throw "Unexpected managed SAM evidence: $samEvidence"
	}
	$history = Wait-History $samHistory "segmentation" 5
	if ($history -notmatch '"completed"' -or $history -match "abcdefghijkl") {
		throw "SAM history is incomplete or contains image payload content"
	}
	Stop-ExampleAndRequireOwnedRuntimeExit $activeExample 18085
	$activeExample = $null
	Write-Output "Managed SAM one-click task passed"
	Write-Output "One-click local GUI smoke passed"
	$smokePassed = $true
} finally {
	if ($activeExample -and -not $activeExample.HasExited) {
		Stop-Process -Id $activeExample.Id -Force -ErrorAction SilentlyContinue
	}
	Stop-FixtureProcesses
	foreach ($path in $generatedPaths) {
		if (Test-Path -LiteralPath $path) {
			Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue
		}
	}
	foreach ($name in $environmentNames) {
		Set-ProcessEnvironment $name $previousEnvironment[$name]
	}
	if ($smokePassed -and -not $KeepEvidence -and (Test-Path -LiteralPath $temporary)) {
		Remove-Item -LiteralPath $temporary -Recurse -Force
	} elseif (Test-Path -LiteralPath $temporary) {
		Write-Warning "One-click smoke evidence retained at: $temporary"
	}
}
