param(
	[string] $Executable = (Join-Path $PSScriptRoot "..\ofxICExample\bin\ofxICExample.exe"),
	[int] $Port = 18081
)

$ErrorActionPreference = "Stop"
$repository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$executablePath = [System.IO.Path]::GetFullPath($Executable)
$fixture = Join-Path $repository "tests\slow_endpoint_fixture.py"
$resultDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ("ofxIC-lifecycle-" + [Guid]::NewGuid())

if (-not (Test-Path -LiteralPath $executablePath)) {
	throw "Build the Release example first: $executablePath"
}

$savedEnvironment = @{}
foreach ($name in @(
	"OFXIC_ENDPOINT_URL",
	"OFXIC_INSPECT_AUTORUN",
	"OFXIC_INSPECT_TIMEOUT_SECONDS",
	"OFXIC_INSPECT_CANCEL_AFTER_MS",
	"OFXIC_GUI_RESULT_PATH")) {
	$savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name)
}

function Invoke-LifecycleCase {
	param(
		[string] $Name,
		[int] $TimeoutSeconds,
		[int] $CancelAfterMillis,
		[string] $ExpectedStatus
	)

	$resultPath = Join-Path $resultDirectory ($Name + ".txt")
	$env:OFXIC_INSPECT_TIMEOUT_SECONDS = $TimeoutSeconds.ToString()
	$env:OFXIC_INSPECT_CANCEL_AFTER_MS = $CancelAfterMillis.ToString()
	$env:OFXIC_GUI_RESULT_PATH = $resultPath
	$example = Start-Process -FilePath $executablePath `
		-WorkingDirectory (Split-Path $executablePath) -PassThru
	try {
		$deadline = [DateTime]::UtcNow.AddSeconds(12)
		do {
			Start-Sleep -Milliseconds 100
			if ($example.HasExited) {
				throw "$Name GUI exited before producing lifecycle evidence"
			}
			$hasResult = (Test-Path -LiteralPath $resultPath) -and
				((Get-Item -LiteralPath $resultPath).Length -gt 0)
		} while (-not $hasResult -and [DateTime]::UtcNow -lt $deadline)
		if (-not $hasResult) {
			throw "$Name GUI did not produce evidence within 12 seconds"
		}
		$lines = Get-Content -LiteralPath $resultPath
		if ($lines.Count -lt 1 -or $lines[0] -ne $ExpectedStatus) {
			throw "$Name expected '$ExpectedStatus', received '$($lines -join ' | ')'"
		}
		Write-Output "$Name passed: $ExpectedStatus"
	} finally {
		if (-not $example.HasExited) {
			$example.CloseMainWindow() | Out-Null
			if (-not $example.WaitForExit(5000)) {
				Stop-Process -Id $example.Id -Force -ErrorAction SilentlyContinue
			}
		}
	}
}

$server = $null
try {
	New-Item -ItemType Directory -Path $resultDirectory | Out-Null
	$server = Start-Process -FilePath "python.exe" -ArgumentList @(
		$fixture, "--port", $Port, "--delay", 10) -WindowStyle Hidden -PassThru
	Start-Sleep -Seconds 1
	$env:OFXIC_ENDPOINT_URL = "http://127.0.0.1:$Port"
	$env:OFXIC_INSPECT_AUTORUN = "1"

	Invoke-LifecycleCase -Name "timeout" -TimeoutSeconds 1 `
		-CancelAfterMillis 0 -ExpectedStatus "Inspection timed out"
	Invoke-LifecycleCase -Name "cancel" -TimeoutSeconds 10 `
		-CancelAfterMillis 500 -ExpectedStatus "Inspection cancelled"

	Write-Output "Request lifecycle GUI smoke passed"
} finally {
	if ($server -and -not $server.HasExited) {
		Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
	}
	foreach ($name in $savedEnvironment.Keys) {
		[Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name])
	}
	if (Test-Path -LiteralPath $resultDirectory) {
		Remove-Item -LiteralPath $resultDirectory -Recurse -Force
	}
}
