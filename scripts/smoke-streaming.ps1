param(
	[string]$Example = "$PSScriptRoot\..\ofxICExample\bin\ofxICExample.exe"
)

$ErrorActionPreference = "Stop"
$port = Get-Random -Minimum 18100 -Maximum 18900
$resultPath = Join-Path $env:TEMP "ofxic-stream-final-$PID.txt"
$partialPath = Join-Path $env:TEMP "ofxic-stream-partial-$PID.txt"
$previous = @{}
foreach ($name in @("OFXIC_ENDPOINT_URL", "OFXIC_MODEL", "OFXIC_CHAT_STREAM",
	"OFXIC_CHAT_AUTORUN", "OFXIC_GUI_RESULT_PATH", "OFXIC_STREAM_RESULT_PATH")) {
	$previous[$name] = [Environment]::GetEnvironmentVariable($name)
}

$server = $null
$app = $null
try {
	Remove-Item -LiteralPath $resultPath, $partialPath -Force -ErrorAction SilentlyContinue
	$serverScript = "$PSScriptRoot\stream-fixture-server.py"
	$server = Start-Process python -ArgumentList "`"$serverScript`" --port $port" `
		-PassThru -WindowStyle Hidden
	Start-Sleep -Milliseconds 300
	if ($server.HasExited) { throw "streaming fixture server failed to start" }
	$env:OFXIC_ENDPOINT_URL = "http://127.0.0.1:$port"
	$env:OFXIC_MODEL = "fixture-model"
	$env:OFXIC_CHAT_STREAM = "1"
	$env:OFXIC_CHAT_AUTORUN = "stream this"
	$env:OFXIC_GUI_RESULT_PATH = $resultPath
	$env:OFXIC_STREAM_RESULT_PATH = $partialPath
	$app = Start-Process $Example -PassThru

	$deadline = (Get-Date).AddSeconds(10)
	$observedPartial = $false
	$observedValues = [System.Collections.Generic.HashSet[string]]::new()
	while ((Get-Date) -lt $deadline -and -not (Test-Path -LiteralPath $resultPath)) {
		if (Test-Path -LiteralPath $partialPath) {
			$partial = Get-Content -Raw -LiteralPath $partialPath
			[void]$observedValues.Add($partial)
			if ($partial -eq "First ") { $observedPartial = $true }
		}
		Start-Sleep -Milliseconds 50
	}
	if (Test-Path -LiteralPath $partialPath) {
		$partial = Get-Content -Raw -LiteralPath $partialPath
		[void]$observedValues.Add($partial)
		if ($partial -eq "First ") { $observedPartial = $true }
	}
	if (-not $observedPartial) {
		$seen = ($observedValues | ForEach-Object { "[$_]" }) -join ", "
		$completed = if (Test-Path -LiteralPath $resultPath) {
			Get-Content -Raw -LiteralPath $resultPath
		} else { "<none>" }
		throw "GUI state never exposed the first SSE chunk before completion; observed: $seen; final: $completed"
	}
	if (-not (Test-Path -LiteralPath $resultPath)) { throw "streaming GUI smoke timed out" }
	$final = Get-Content -Raw -LiteralPath $resultPath
	if ($final -notmatch "Streaming inference completed" -or $final -notmatch "First second") {
		throw "unexpected streaming GUI result: $final"
	}
	Write-Host "Streaming GUI smoke passed (partial and final SSE state observed)"
} finally {
	if ($app -and -not $app.HasExited) { Stop-Process -Id $app.Id -Force }
	if ($server -and -not $server.HasExited) { Stop-Process -Id $server.Id -Force }
	foreach ($name in $previous.Keys) {
		[Environment]::SetEnvironmentVariable($name, $previous[$name])
	}
	Remove-Item -LiteralPath $resultPath, $partialPath -Force -ErrorAction SilentlyContinue
}
