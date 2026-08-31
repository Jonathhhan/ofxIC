param(
	[string] $Executable = (Join-Path $PSScriptRoot "..\ofxICExample\bin\ofxICExample.exe"),
	[ValidateSet("llama", "stable-diffusion", "ace-step", "whisper", "sam")]
	[string[]] $Runtime = @("llama", "stable-diffusion", "ace-step", "whisper", "sam"),
	[switch] $Live,
	[ValidateRange(1, 3600)]
	[int] $TimeoutSeconds = 900,
	[switch] $KeepLogs
)

$ErrorActionPreference = "Stop"
$executablePath = [System.IO.Path]::GetFullPath($Executable)
if (-not (Test-Path -LiteralPath $executablePath -PathType Leaf)) {
	throw "Build the Release workbench first: $executablePath"
}
if ($Live -and $env:OFXIC_RUN_LIVE_RUNTIME_MATRIX -ne "1") {
	throw "Set OFXIC_RUN_LIVE_RUNTIME_MATRIX=1 to allow real local runtime starts."
}

$ports = @{
	"llama" = 8080
	"stable-diffusion" = 8081
	"whisper" = 8082
	"ace-step" = 8085
	"sam" = 18085
}
$temporary = Join-Path ([System.IO.Path]::GetTempPath()) ("ofxIC-runtime-matrix-" + [guid]::NewGuid())
$environmentNames = @(
	"OFXIC_RUNTIME_AUTOSTART",
	"OFXIC_RUNTIME_RESULT_PATH",
	"OFXIC_RUNTIME_TIMEOUT_SECONDS",
	"OFXIC_RUNTIME_PLAN",
	"OFXIC_SETTINGS_PATH",
	"OFXIC_HISTORY_PATH"
)
$previousEnvironment = @{}
foreach ($name in $environmentNames) {
	$previousEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
}
$results = @()
$succeeded = $false

function Test-LocalPortListening([int] $Port) {
	$client = [System.Net.Sockets.TcpClient]::new()
	try {
		$task = $client.ConnectAsync("127.0.0.1", $Port)
		return $task.Wait(250) -and $client.Connected
	} catch {
		return $false
	} finally {
		$client.Dispose()
	}
}

function Read-KeyValueResult([string] $Path) {
	$values = [ordered]@{}
	foreach ($line in Get-Content -LiteralPath $Path) {
		$separator = $line.IndexOf('=')
		if ($separator -gt 0) {
			$values[$line.Substring(0, $separator)] = $line.Substring($separator + 1)
		}
	}
	return [pscustomobject]$values
}

try {
	New-Item -ItemType Directory -Path $temporary | Out-Null
	foreach ($name in $Runtime) {
		if ($Live -and (Test-LocalPortListening $ports[$name])) {
			throw "$name cannot be tested as a GUI-owned runtime because port $($ports[$name]) is already listening."
		}

		$resultPath = Join-Path $temporary "$name.result.txt"
		$stdoutPath = Join-Path $temporary "$name.stdout.log"
		$stderrPath = Join-Path $temporary "$name.stderr.log"
		$settingsPath = Join-Path $temporary "$name.settings"
		$historyPath = Join-Path $temporary "$name.history"
		$env:OFXIC_RUNTIME_AUTOSTART = $name
		$env:OFXIC_RUNTIME_RESULT_PATH = $resultPath
		$env:OFXIC_RUNTIME_TIMEOUT_SECONDS = $TimeoutSeconds.ToString()
		$env:OFXIC_RUNTIME_PLAN = if ($Live) { "0" } else { "1" }
		$env:OFXIC_SETTINGS_PATH = $settingsPath
		$env:OFXIC_HISTORY_PATH = $historyPath

		$mode = if ($Live) { "GUI start" } else { "plan" }
		Write-Output "[$name] $mode"
		$example = Start-Process -FilePath $executablePath `
			-WorkingDirectory (Split-Path $executablePath) -WindowStyle Hidden `
			-RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath -PassThru
		try {
			$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds + 30)
			do {
				Start-Sleep -Milliseconds 100
			} while (-not (Test-Path -LiteralPath $resultPath) -and
				-not $example.HasExited -and [DateTime]::UtcNow -lt $deadline)
			if (-not (Test-Path -LiteralPath $resultPath)) {
				$tail = @()
				if (Test-Path -LiteralPath $stdoutPath) { $tail += Get-Content -LiteralPath $stdoutPath -Tail 100 }
				if (Test-Path -LiteralPath $stderrPath) { $tail += Get-Content -LiteralPath $stderrPath -Tail 100 }
				throw "$name did not write runtime evidence within $($TimeoutSeconds + 30) seconds.`n$($tail -join "`n")"
			}
			$record = Read-KeyValueResult $resultPath
			$results += $record
			if ($Live) {
				if ($record.state -ne "ready" -or $record.process_state -ne "ready" -or
					$record.ownership -ne "owned" -or $record.configuration -ne "ready") {
					$tail = @()
					if (Test-Path -LiteralPath $stdoutPath) { $tail += Get-Content -LiteralPath $stdoutPath -Tail 140 }
					if (Test-Path -LiteralPath $stderrPath) { $tail += Get-Content -LiteralPath $stderrPath -Tail 140 }
					throw "$name runtime failed: $($record.status)`n$($tail -join "`n")"
				}
				Write-Output "[$name] ready as GUI-owned PID $($record.pid)"
			}
		} finally {
			if ($example) {
				if (-not $example.HasExited -and -not $example.WaitForExit(15000)) {
					Stop-Process -Id $example.Id -Force -ErrorAction SilentlyContinue
					if (-not $example.WaitForExit(5000)) {
						throw "$name workbench process could not be stopped."
					}
				}
				# WaitForExit() without a timeout also drains asynchronous redirected streams.
				$example.WaitForExit()
				$example.Dispose()
			}
		}
		if ($Live -and (Test-LocalPortListening $ports[$name])) {
			throw "$name left port $($ports[$name]) listening after the workbench exited."
		}
	}

	$results | Select-Object runtime, state, configuration, ownership, executable, model, status |
		Format-Table -AutoSize -Wrap
	$succeeded = $true
	if ($Live) {
		Write-Output "Local runtime matrix passed: all selected servers reached readiness through the workbench and stopped cleanly."
	} else {
		Write-Output "Local runtime matrix plan complete. No server process was started."
	}
} finally {
	foreach ($name in $environmentNames) {
		[Environment]::SetEnvironmentVariable($name, $previousEnvironment[$name], "Process")
	}
	if ($succeeded -and -not $KeepLogs -and (Test-Path -LiteralPath $temporary)) {
		Remove-Item -LiteralPath $temporary -Recurse -Force
	} elseif (Test-Path -LiteralPath $temporary) {
		Write-Output "Runtime matrix evidence retained at: $temporary"
	}
}
