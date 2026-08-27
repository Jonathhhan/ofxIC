param(
	[string]$Example = "$PSScriptRoot\..\ofxICExample\bin\ofxICExample.exe"
)

$ErrorActionPreference = "Stop"
$port = Get-Random -Minimum 18100 -Maximum 18900
$resultPath = Join-Path $env:TEMP "ofxic-provider-error-result-$PID.txt"
$stdoutPath = Join-Path $env:TEMP "ofxic-provider-error-stdout-$PID.log"
$stderrPath = Join-Path $env:TEMP "ofxic-provider-error-stderr-$PID.log"
$previous = @{}
foreach ($name in @("OFXIC_ENDPOINT_URL", "OFXIC_MODEL", "OFXIC_CHAT_STREAM",
	"OFXIC_CHAT_AUTORUN", "OFXIC_GUI_RESULT_PATH")) {
	$previous[$name] = [Environment]::GetEnvironmentVariable($name)
}

$server = $null
$app = $null
try {
	Remove-Item -LiteralPath $resultPath, $stdoutPath, $stderrPath `
		-Force -ErrorAction SilentlyContinue
	$serverScript = "$PSScriptRoot\provider-error-fixture.py"
	$server = Start-Process python -ArgumentList "`"$serverScript`" --port $port" `
		-PassThru -WindowStyle Hidden
	Start-Sleep -Milliseconds 300
	if ($server.HasExited) { throw "provider-error fixture server failed to start" }
	$env:OFXIC_ENDPOINT_URL = "http://127.0.0.1:$port"
	$env:OFXIC_MODEL = "credit-fixture-model"
	$env:OFXIC_CHAT_STREAM = "0"
	$env:OFXIC_CHAT_AUTORUN = "trigger provider error"
	$env:OFXIC_GUI_RESULT_PATH = $resultPath
	$app = Start-Process $Example -PassThru `
		-RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath

	$deadline = (Get-Date).AddSeconds(10)
	while ((Get-Date) -lt $deadline -and -not (Test-Path -LiteralPath $resultPath)) {
		if ($app.HasExited) { throw "example exited before reporting provider error" }
		Start-Sleep -Milliseconds 50
	}
	if (-not (Test-Path -LiteralPath $resultPath)) { throw "provider-error GUI smoke timed out" }
	$expectedError = "chat endpoint returned HTTP 402: You have depleted your monthly included credits."
	$result = Get-Content -Raw -LiteralPath $resultPath
	if (-not $result.Contains("Request failed: $expectedError")) {
		throw "GUI result did not preserve provider error detail: $result"
	}
	$console = (Get-Content -Raw -LiteralPath $stdoutPath) +
		(Get-Content -Raw -LiteralPath $stderrPath)
	foreach ($expected in @(
		"user: trigger provider error",
		"route: endpoint=http://127.0.0.1:$port model=credit-fixture-model",
		"request failed: $expectedError")) {
		if (-not $console.Contains($expected)) {
			throw "console log did not contain: $expected"
		}
	}
	Write-Host "Provider-error GUI smoke passed (HTTP 402 detail and route observed)"
} finally {
	if ($app -and -not $app.HasExited) { Stop-Process -Id $app.Id -Force }
	if ($server -and -not $server.HasExited) { Stop-Process -Id $server.Id -Force }
	foreach ($name in $previous.Keys) {
		[Environment]::SetEnvironmentVariable($name, $previous[$name])
	}
	Remove-Item -LiteralPath $resultPath, $stdoutPath, $stderrPath `
		-Force -ErrorAction SilentlyContinue
}
