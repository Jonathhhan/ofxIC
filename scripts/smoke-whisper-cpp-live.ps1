param(
	[Parameter(Mandatory = $true)]
	[string] $Server,
	[Parameter(Mandatory = $true)]
	[string] $Model,
	[Parameter(Mandatory = $true)]
	[string] $Audio,
	[string] $Executable = (Join-Path $PSScriptRoot "..\ofxICExample\bin\ofxICExample.exe"),
	[int] $Port = 18084,
	[int] $TimeoutSeconds = 90
)

$ErrorActionPreference = "Stop"
$serverPath = [System.IO.Path]::GetFullPath($Server)
$modelPath = [System.IO.Path]::GetFullPath($Model)
$audioPath = [System.IO.Path]::GetFullPath($Audio)
$executablePath = [System.IO.Path]::GetFullPath($Executable)
foreach ($required in @($serverPath, $modelPath, $audioPath, $executablePath)) {
	if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
		throw "Required live-smoke input is missing: $required"
	}
}

$temporary = Join-Path ([System.IO.Path]::GetTempPath()) ("ofxIC-whisper-live-" + [guid]::NewGuid())
$resultPath = Join-Path $temporary "result.txt"
$serverOut = Join-Path $temporary "server.stdout.txt"
$serverErr = Join-Path $temporary "server.stderr.txt"
$previous = @{
	Endpoint = $env:OFXIC_ENDPOINT_URL
	TranscriptionEndpoint = $env:OFXIC_TRANSCRIPTION_ENDPOINT_URL
	Audio = $env:OFXIC_AUDIO_PATH
	Autorun = $env:OFXIC_TRANSCRIPTION_AUTORUN
	Result = $env:OFXIC_GUI_RESULT_PATH
	Settings = $env:OFXIC_SETTINGS_PATH
}
$serverProcess = $null
$example = $null
try {
	New-Item -ItemType Directory -Path $temporary | Out-Null
	$arguments = @('-m', ('"' + $modelPath + '"'), '--host', '127.0.0.1',
		'--port', $Port, '--no-gpu', '--no-timestamps')
	$serverProcess = Start-Process -FilePath $serverPath -ArgumentList $arguments `
		-WorkingDirectory (Split-Path $serverPath) -WindowStyle Hidden -PassThru `
		-RedirectStandardOutput $serverOut -RedirectStandardError $serverErr
	$readyDeadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
	$serverReady = $false
	do {
		if ($serverProcess.HasExited) {
			throw "whisper-server exited during startup: $(Get-Content -Raw $serverErr)"
		}
		$probe = [System.Net.Sockets.TcpClient]::new()
		try {
			$probe.Connect('127.0.0.1', $Port)
			$serverReady = $true
		} catch {
			Start-Sleep -Milliseconds 200
		} finally {
			$probe.Dispose()
		}
	} while (-not $serverReady -and [DateTime]::UtcNow -lt $readyDeadline)
	if (-not $serverReady) { throw "whisper-server did not become ready" }

	$env:OFXIC_ENDPOINT_URL = "http://127.0.0.1:1"
	$env:OFXIC_TRANSCRIPTION_ENDPOINT_URL = "http://127.0.0.1:$Port"
	$env:OFXIC_AUDIO_PATH = $audioPath
	$env:OFXIC_TRANSCRIPTION_AUTORUN = "whisper-cpp"
	$env:OFXIC_GUI_RESULT_PATH = $resultPath
	$env:OFXIC_SETTINGS_PATH = (Join-Path $temporary "settings")
	$example = Start-Process -FilePath $executablePath `
		-WorkingDirectory (Split-Path $executablePath) -WindowStyle Hidden -PassThru
	$resultDeadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
	do {
		Start-Sleep -Milliseconds 100
	} while (-not (Test-Path -LiteralPath $resultPath) -and
		-not $example.HasExited -and [DateTime]::UtcNow -lt $resultDeadline)
	if (-not (Test-Path -LiteralPath $resultPath)) {
		throw "GUI did not produce live transcription evidence"
	}
	$lines = Get-Content -LiteralPath $resultPath
	$status = $lines | Select-Object -First 1
	$transcript = ($lines | Select-Object -Skip 1) -join "`n"
	if ($status -ne "Transcription completed" -or $transcript.Trim().Length -lt 20) {
		throw "Unexpected live transcription evidence: $status`n$transcript"
	}
	Write-Output "Live whisper.cpp GUI smoke passed"
	Write-Output $transcript.Trim()
} finally {
	if ($example -and -not $example.HasExited) {
		$example.CloseMainWindow() | Out-Null
		if (-not $example.WaitForExit(3000)) {
			Stop-Process -Id $example.Id -Force -ErrorAction SilentlyContinue
		}
	}
	if ($serverProcess -and -not $serverProcess.HasExited) {
		Stop-Process -Id $serverProcess.Id -Force -ErrorAction SilentlyContinue
	}
	$env:OFXIC_ENDPOINT_URL = $previous.Endpoint
	$env:OFXIC_TRANSCRIPTION_ENDPOINT_URL = $previous.TranscriptionEndpoint
	$env:OFXIC_AUDIO_PATH = $previous.Audio
	$env:OFXIC_TRANSCRIPTION_AUTORUN = $previous.Autorun
	$env:OFXIC_GUI_RESULT_PATH = $previous.Result
	$env:OFXIC_SETTINGS_PATH = $previous.Settings
	if (Test-Path -LiteralPath $temporary) {
		Remove-Item -LiteralPath $temporary -Recurse -Force
	}
}
