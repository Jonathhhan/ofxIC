param(
	[string] $Executable = (Join-Path $PSScriptRoot "..\ofxICExample\bin\ofxICExample.exe"),
	[ValidateSet("openai", "whisper-cpp")]
	[string] $Protocol = "openai",
	[int] $Port = 18082
)

$ErrorActionPreference = "Stop"
$repository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$executablePath = [System.IO.Path]::GetFullPath($Executable)
$fixtureServer = Join-Path $repository "tests\transcription_endpoint_fixture.py"
$temporary = Join-Path ([System.IO.Path]::GetTempPath()) ("ofxIC-transcription-" + [guid]::NewGuid())
$audioPath = Join-Path $temporary "fixture.wav"
$resultPath = Join-Path $temporary "result.txt"

if (-not (Test-Path -LiteralPath $executablePath)) {
	throw "Build the Release example first: $executablePath"
}

$previous = @{
	Endpoint = $env:OFXIC_ENDPOINT_URL
	TranscriptionEndpoint = $env:OFXIC_TRANSCRIPTION_ENDPOINT_URL
	Audio = $env:OFXIC_AUDIO_PATH
	Autorun = $env:OFXIC_TRANSCRIPTION_AUTORUN
	Model = $env:OFXIC_TRANSCRIPTION_MODEL
	Result = $env:OFXIC_GUI_RESULT_PATH
	Settings = $env:OFXIC_SETTINGS_PATH
}
$server = $null
$example = $null
try {
	New-Item -ItemType Directory -Path $temporary | Out-Null
	[System.IO.File]::WriteAllBytes($audioPath,
		[System.Text.Encoding]::ASCII.GetBytes("RIFF_OFXIC_AUDIO_FIXTURE"))
	$quotedFixture = '"' + $fixtureServer + '"'
	$server = Start-Process -FilePath "python.exe" -ArgumentList @(
		$quotedFixture, "--port", $Port) -WindowStyle Hidden -PassThru
	$readyDeadline = [DateTime]::UtcNow.AddSeconds(5)
	$serverReady = $false
	do {
		if ($server.HasExited) { throw "Transcription fixture server exited during startup" }
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
	if (-not $serverReady) { throw "Transcription fixture server did not become ready" }
	$env:OFXIC_ENDPOINT_URL = "http://127.0.0.1:1"
	$env:OFXIC_TRANSCRIPTION_ENDPOINT_URL = "http://127.0.0.1:$Port"
	$env:OFXIC_AUDIO_PATH = $audioPath
	$env:OFXIC_TRANSCRIPTION_AUTORUN = $Protocol
	$env:OFXIC_TRANSCRIPTION_MODEL = "whisper-1"
	$env:OFXIC_GUI_RESULT_PATH = $resultPath
	$env:OFXIC_SETTINGS_PATH = (Join-Path $temporary "settings")
	$example = Start-Process -FilePath $executablePath `
		-WorkingDirectory (Split-Path $executablePath) -WindowStyle Hidden -PassThru

	$deadline = [DateTime]::UtcNow.AddSeconds(15)
	do {
		Start-Sleep -Milliseconds 100
	} while (-not (Test-Path -LiteralPath $resultPath) -and
		-not $example.HasExited -and [DateTime]::UtcNow -lt $deadline)
	if (-not (Test-Path -LiteralPath $resultPath)) {
		throw "GUI transcription did not produce evidence within 15 seconds"
	}
	$result = Get-Content -Raw -LiteralPath $resultPath
	if ($result -notmatch "Transcription completed" -or
		$result -notmatch "deterministic GUI transcript") {
		throw "Unexpected GUI transcription evidence: $result"
	}
	Write-Output "Transcription GUI smoke passed ($Protocol)"
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
	$env:OFXIC_ENDPOINT_URL = $previous.Endpoint
	$env:OFXIC_TRANSCRIPTION_ENDPOINT_URL = $previous.TranscriptionEndpoint
	$env:OFXIC_AUDIO_PATH = $previous.Audio
	$env:OFXIC_TRANSCRIPTION_AUTORUN = $previous.Autorun
	$env:OFXIC_TRANSCRIPTION_MODEL = $previous.Model
	$env:OFXIC_GUI_RESULT_PATH = $previous.Result
	$env:OFXIC_SETTINGS_PATH = $previous.Settings
	if (Test-Path -LiteralPath $temporary) {
		Remove-Item -LiteralPath $temporary -Recurse -Force
	}
}
