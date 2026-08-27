param(
	[Parameter(Mandatory = $true)]
	[string] $Server,
	[Parameter(Mandatory = $true)]
	[string] $Model,
	[Parameter(Mandatory = $true)]
	[string] $Document,
	[string] $Executable = (Join-Path $PSScriptRoot "..\ofxICExample\bin\ofxICExample.exe"),
	[string] $Prompt = "According to the loaded document, why does ofxIC keep inference behind a process boundary? Cite the source identifier.",
	[string] $ModelAlias = "ofxic-local-smoke",
	[string] $EvidenceDirectory = "",
	[int] $Port = 18090,
	[int] $TimeoutSeconds = 600,
	[switch] $ValidateOnly
)

$ErrorActionPreference = "Stop"

if ($env:OFXIC_RUN_LIVE_LLAMA_SERVER -ne "1") {
	throw "Live llama-server inference is disabled. Set OFXIC_RUN_LIVE_LLAMA_SERVER=1 to opt in."
}
if ($Port -lt 1024 -or $Port -gt 65535) { throw "Port must be between 1024 and 65535." }
if ($TimeoutSeconds -lt 10) { throw "TimeoutSeconds must be at least 10." }
if ([string]::IsNullOrWhiteSpace($Prompt)) { throw "Prompt cannot be empty." }
if ([string]::IsNullOrWhiteSpace($ModelAlias)) { throw "ModelAlias cannot be empty." }

$serverPath = [System.IO.Path]::GetFullPath($Server)
$modelPath = [System.IO.Path]::GetFullPath($Model)
$documentPath = [System.IO.Path]::GetFullPath($Document)
$executablePath = [System.IO.Path]::GetFullPath($Executable)
foreach ($required in @($serverPath, $modelPath, $documentPath, $executablePath)) {
	if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
		throw "Required local-live-smoke input is missing: $required"
	}
}
if ([System.IO.Path]::GetExtension($serverPath) -ne ".exe") {
	throw "Server must be a Windows llama-server executable."
}
if ([System.IO.Path]::GetExtension($modelPath) -ne ".gguf") {
	throw "Model must be a user-supplied .gguf file."
}
$documentExtension = [System.IO.Path]::GetExtension($documentPath).ToLowerInvariant()
if ($documentExtension -notin @(".md", ".txt")) {
	throw "Document must be an explicitly selected .md or .txt file."
}
if ($ValidateOnly) {
	Write-Output "Local llama-server live-smoke inputs validated"
	return
}

$portProbe = [System.Net.Sockets.TcpListener]::new(
	[System.Net.IPAddress]::Loopback, $Port)
try {
	$portProbe.Start()
} catch {
	throw "Port $Port is already in use; no server was started."
} finally {
	$portProbe.Stop()
}

$runName = "ofxIC-llama-live-" + (Get-Date -Format "yyyyMMdd-HHmmss-fff") +
	"-" + [guid]::NewGuid().ToString("N").Substring(0, 8)
$keepEvidence = -not [string]::IsNullOrWhiteSpace($EvidenceDirectory)
$evidenceRoot = if ($keepEvidence) {
	[System.IO.Path]::GetFullPath($EvidenceDirectory)
} else {
	[System.IO.Path]::GetTempPath()
}
$runDirectory = Join-Path $evidenceRoot $runName
$resultPath = Join-Path $runDirectory "result.txt"
$serverOut = Join-Path $runDirectory "llama-server.stdout.txt"
$serverErr = Join-Path $runDirectory "llama-server.stderr.txt"
$exampleOut = Join-Path $runDirectory "ofxICExample.stdout.txt"
$exampleErr = Join-Path $runDirectory "ofxICExample.stderr.txt"
$settingsPath = Join-Path $runDirectory "settings"
$sourceIdentifier = [System.IO.Path]::GetFileName($documentPath)
$previous = @{}
foreach ($name in @(
	"OFXIC_ENDPOINT_URL", "OFXIC_MODEL", "OFXIC_DOCUMENT_PATH",
	"OFXIC_CHAT_AUTORUN", "OFXIC_CHAT_STREAM", "OFXIC_GUI_RESULT_PATH",
	"OFXIC_SETTINGS_PATH", "OFXIC_INSPECT_AUTORUN",
	"OFXIC_INSPECT_CANCEL_AFTER_MS")) {
	$previous[$name] = [Environment]::GetEnvironmentVariable($name)
}
$serverProcess = $null
$exampleProcess = $null
try {
	New-Item -ItemType Directory -Force -Path $runDirectory | Out-Null
	$serverArguments = @(
		"--model", ('"' + $modelPath + '"'),
		"--alias", $ModelAlias,
		"--host", "127.0.0.1",
		"--port", $Port,
		"--ctx-size", "4096",
		"--no-ui")
	$serverProcess = Start-Process -FilePath $serverPath -ArgumentList $serverArguments `
		-WorkingDirectory (Split-Path $serverPath) -WindowStyle Hidden -PassThru `
		-RedirectStandardOutput $serverOut -RedirectStandardError $serverErr

	$readyDeadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
	$serverReady = $false
	do {
		if ($serverProcess.HasExited) {
			$startupError = if (Test-Path -LiteralPath $serverErr) {
				Get-Content -Raw -LiteralPath $serverErr
			} else { "no stderr captured" }
			throw "llama-server exited during startup: $startupError"
		}
		try {
			$health = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/health" `
				-Method Get -TimeoutSec 2
			$serverReady = $health.status -eq "ok"
		} catch {
			Start-Sleep -Milliseconds 250
		}
	} while (-not $serverReady -and [DateTime]::UtcNow -lt $readyDeadline)
	if (-not $serverReady) { throw "llama-server did not become ready within $TimeoutSeconds seconds" }

	$env:OFXIC_ENDPOINT_URL = "http://127.0.0.1:$Port"
	$env:OFXIC_MODEL = $ModelAlias
	$env:OFXIC_DOCUMENT_PATH = $documentPath
	$env:OFXIC_CHAT_AUTORUN = $Prompt
	$env:OFXIC_CHAT_STREAM = "0"
	$env:OFXIC_GUI_RESULT_PATH = $resultPath
	$env:OFXIC_SETTINGS_PATH = $settingsPath
	$env:OFXIC_INSPECT_AUTORUN = "1"
	Remove-Item Env:OFXIC_INSPECT_CANCEL_AFTER_MS -ErrorAction SilentlyContinue
	$exampleProcess = Start-Process -FilePath $executablePath `
		-WorkingDirectory (Split-Path $executablePath) -WindowStyle Hidden -PassThru `
		-RedirectStandardOutput $exampleOut -RedirectStandardError $exampleErr

	$resultDeadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
	$result = ""
	do {
		if ($exampleProcess.HasExited) { throw "ofxICExample exited before producing local-live evidence" }
		if (Test-Path -LiteralPath $resultPath) {
			$result = Get-Content -Raw -LiteralPath $resultPath
			if ($result.StartsWith("Inference completed") -or
				$result.StartsWith("Request failed:")) {
				break
			}
		}
		Start-Sleep -Milliseconds 100
	} while ([DateTime]::UtcNow -lt $resultDeadline)
	if ([string]::IsNullOrWhiteSpace($result) -or
		(-not $result.StartsWith("Inference completed") -and
		 -not $result.StartsWith("Request failed:"))) {
		throw "ofxICExample did not produce local-live evidence within $TimeoutSeconds seconds"
	}

	if (-not $result.Contains("Inference completed with 2 model request(s)")) {
		throw "Local GUI did not complete the grounded two-request tool path: $result"
	}
	if (-not $result.Contains($sourceIdentifier)) {
		throw "Grounded answer omitted source identifier '$sourceIdentifier': $result"
	}
	$console = (Get-Content -Raw -LiteralPath $exampleOut) +
		(Get-Content -Raw -LiteralPath $exampleErr)
	foreach ($expected in @(
		"ofxIC inspect: route: endpoint=http://127.0.0.1:$Port model=$ModelAlias",
		"ofxIC inspect: Endpoint reachable; configured model: $ModelAlias",
		"ofxIC chat: route: endpoint=http://127.0.0.1:$Port model=$ModelAlias")) {
		if (-not $console.Contains($expected)) {
			throw "Local GUI console did not contain: $expected"
		}
	}

	Write-Output "Local llama-server GUI smoke passed"
	Write-Output "Server: $serverPath"
	Write-Output "Model file: $modelPath"
	Write-Output "Selected model: $ModelAlias"
	Write-Output "Source: $sourceIdentifier"
	if ($keepEvidence) { Write-Output "Evidence: $runDirectory" }
} finally {
	if ($exampleProcess -and -not $exampleProcess.HasExited) {
		$exampleProcess.CloseMainWindow() | Out-Null
		if (-not $exampleProcess.WaitForExit(3000)) {
			Stop-Process -Id $exampleProcess.Id -Force -ErrorAction SilentlyContinue
			$exampleProcess.WaitForExit(3000) | Out-Null
		}
	}
	if ($serverProcess -and -not $serverProcess.HasExited) {
		Stop-Process -Id $serverProcess.Id -Force -ErrorAction SilentlyContinue
		$serverProcess.WaitForExit(5000) | Out-Null
	}
	foreach ($name in $previous.Keys) {
		[Environment]::SetEnvironmentVariable($name, $previous[$name])
	}
	if (-not $keepEvidence -and (Test-Path -LiteralPath $runDirectory)) {
		Remove-Item -LiteralPath $runDirectory -Recurse -Force
	}
}
