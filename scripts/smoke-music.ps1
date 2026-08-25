param(
	[string] $Executable = (Join-Path $PSScriptRoot "..\ofxICExample\bin\ofxICExample.exe"),
	[int] $Port = 18086,
	[switch] $Live,
	[string] $Endpoint = "http://127.0.0.1:8085"
)

$ErrorActionPreference = "Stop"
$repository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$executablePath = [System.IO.Path]::GetFullPath($Executable)
$fixtureServer = Join-Path $repository "tests\acestep_endpoint_fixture.py"
$temporary = Join-Path ([System.IO.Path]::GetTempPath()) ("ofxIC-music-" + [guid]::NewGuid())
$resultPath = Join-Path $temporary "result.txt"
$generatedPath = $null

if (-not (Test-Path -LiteralPath $executablePath)) {
	throw "Build the Release example first: $executablePath"
}
if ($Live -and $env:OFXIC_RUN_LIVE_ACESTEP -ne "1") {
	throw "Set OFXIC_RUN_LIVE_ACESTEP=1 to allow the real model-backed GUI smoke"
}

$previous = @{
	Endpoint = $env:OFXIC_ENDPOINT_URL
	Backend = $env:OFXIC_MUSIC_BACKEND
	MusicEndpoint = $env:OFXIC_MUSIC_ENDPOINT_URL
	Autorun = $env:OFXIC_MUSIC_AUTORUN
	Prompt = $env:OFXIC_MUSIC_PROMPT
	Duration = $env:OFXIC_MUSIC_DURATION
	Format = $env:OFXIC_MUSIC_OUTPUT_FORMAT
	Result = $env:OFXIC_MUSIC_RESULT_PATH
	Settings = $env:OFXIC_SETTINGS_PATH
}
$server = $null
$example = $null
try {
	New-Item -ItemType Directory -Path $temporary | Out-Null
	if (-not $Live) {
		$quotedFixture = '"' + $fixtureServer + '"'
		$server = Start-Process -FilePath "python.exe" -ArgumentList @(
			$quotedFixture, "--port", $Port) -WindowStyle Hidden -PassThru
		$readyDeadline = [DateTime]::UtcNow.AddSeconds(5)
		$serverReady = $false
		do {
			if ($server.HasExited) { throw "ACE-Step fixture server exited during startup" }
			$probe = [System.Net.Sockets.TcpClient]::new()
			try {
				$probe.Connect("127.0.0.1", $Port)
				$serverReady = $true
			} catch {
				Start-Sleep -Milliseconds 100
			} finally {
				$probe.Dispose()
			}
		} while (-not $serverReady -and [DateTime]::UtcNow -lt $readyDeadline)
		if (-not $serverReady) { throw "ACE-Step fixture server did not become ready" }
		$Endpoint = "http://127.0.0.1:$Port"
	}

	$env:OFXIC_ENDPOINT_URL = "http://127.0.0.1:1"
	$env:OFXIC_MUSIC_BACKEND = "acestep"
	$env:OFXIC_MUSIC_ENDPOINT_URL = $Endpoint
	$env:OFXIC_MUSIC_AUTORUN = "acestep"
	$env:OFXIC_MUSIC_PROMPT = if ($Live) {
		"short warm ambient electronic pulse, sparse and clean"
	} else {
		"deterministic timestamp music"
	}
	$env:OFXIC_MUSIC_DURATION = if ($Live) { "4" } else { "1" }
	$env:OFXIC_MUSIC_OUTPUT_FORMAT = "wav"
	$env:OFXIC_MUSIC_RESULT_PATH = $resultPath
	$env:OFXIC_SETTINGS_PATH = (Join-Path $temporary "settings")
	$example = Start-Process -FilePath $executablePath `
		-WorkingDirectory (Split-Path $executablePath) -WindowStyle Hidden -PassThru

	$timeoutSeconds = if ($Live) { 1200 } else { 20 }
	$deadline = [DateTime]::UtcNow.AddSeconds($timeoutSeconds)
	do {
		Start-Sleep -Milliseconds 100
	} while (-not (Test-Path -LiteralPath $resultPath) -and
		-not $example.HasExited -and [DateTime]::UtcNow -lt $deadline)
	if (-not (Test-Path -LiteralPath $resultPath)) {
		throw "Music GUI did not produce evidence within $timeoutSeconds seconds"
	}
	$result = Get-Content -Raw -LiteralPath $resultPath
	$pathMatch = [regex]::Match(
		$result,
		'(?m)^Saved(?: and playing|, but playback could not load): (.+\.wav)\r?$')
	if ($result -notmatch "completed" -or -not $pathMatch.Success) {
		throw "Unexpected music GUI evidence: $result"
	}
	$generatedPath = $pathMatch.Groups[1].Value
	if (-not (Test-Path -LiteralPath $generatedPath)) {
		throw "Music GUI reported a missing output file: $generatedPath"
	}
	$generatedName = [System.IO.Path]::GetFileName($generatedPath)
	if ($generatedName -notmatch '^ofxIC-music-\d{8}-\d{6}-\d{3}\.wav$') {
		throw "Music output filename has no date/time stamp: $generatedName"
	}
	$mode = if ($Live) { "real local ACE-Step" } else { "deterministic fixture" }
	Write-Output "Music GUI smoke passed ($mode): $generatedName"
} finally {
	if ($example -and -not $example.HasExited) {
		$example.CloseMainWindow() | Out-Null
		if (-not $example.WaitForExit(3000)) {
			Stop-Process -Id $example.Id -Force -ErrorAction SilentlyContinue
		}
	}
	if ($server -and -not $server.HasExited) {
		Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
	}
	if ($generatedPath -and (Test-Path -LiteralPath $generatedPath)) {
		Remove-Item -LiteralPath $generatedPath -Force
	}
	$env:OFXIC_ENDPOINT_URL = $previous.Endpoint
	$env:OFXIC_MUSIC_BACKEND = $previous.Backend
	$env:OFXIC_MUSIC_ENDPOINT_URL = $previous.MusicEndpoint
	$env:OFXIC_MUSIC_AUTORUN = $previous.Autorun
	$env:OFXIC_MUSIC_PROMPT = $previous.Prompt
	$env:OFXIC_MUSIC_DURATION = $previous.Duration
	$env:OFXIC_MUSIC_OUTPUT_FORMAT = $previous.Format
	$env:OFXIC_MUSIC_RESULT_PATH = $previous.Result
	$env:OFXIC_SETTINGS_PATH = $previous.Settings
	if (Test-Path -LiteralPath $temporary) {
		Remove-Item -LiteralPath $temporary -Recurse -Force
	}
}
