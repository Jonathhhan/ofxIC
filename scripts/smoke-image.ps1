param(
	[string] $Executable = (Join-Path $PSScriptRoot "..\ofxICExample\bin\ofxICExample.exe"),
	[int] $Port = 18087,
	[int] $DelayMilliseconds = 1500
)

$ErrorActionPreference = "Stop"
$repository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$executablePath = [System.IO.Path]::GetFullPath($Executable)
$fixtureServer = Join-Path $repository "tests\image_endpoint_fixture.py"
$temporary = Join-Path ([System.IO.Path]::GetTempPath()) ("ofxIC-image-" + [guid]::NewGuid())
$resultPath = Join-Path $temporary "result.txt"
$heartbeatPath = Join-Path $temporary "heartbeat.txt"
$requestMarker = Join-Path $temporary "request-started.txt"
$generatedPath = $null

if (-not (Test-Path -LiteralPath $executablePath)) {
	throw "Build the Release example first: $executablePath"
}

$names = @(
	"OFXIC_ENDPOINT_URL",
	"OFXIC_MEDIA_BACKEND",
	"OFXIC_MEDIA_ENDPOINT_URL",
	"OFXIC_MEDIA_AUTORUN",
	"OFXIC_MEDIA_PROMPT",
	"OFXIC_MEDIA_RESULT_PATH",
	"OFXIC_GUI_HEARTBEAT_PATH",
	"OFXIC_SETTINGS_PATH")
$savedEnvironment = @{}
foreach ($name in $names) {
	$savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name)
}

$server = $null
$example = $null
try {
	New-Item -ItemType Directory -Path $temporary | Out-Null
	$quotedFixture = '"' + $fixtureServer + '"'
	$server = Start-Process -FilePath "python.exe" -ArgumentList @(
		$quotedFixture, "--port", $Port, "--delay-ms", $DelayMilliseconds,
		"--request-marker", ('"' + $requestMarker + '"')) -WindowStyle Hidden -PassThru
	$readyDeadline = [DateTime]::UtcNow.AddSeconds(5)
	$serverReady = $false
	do {
		if ($server.HasExited) { throw "Image fixture server exited during startup" }
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
	if (-not $serverReady) { throw "Image fixture server did not become ready" }

	$env:OFXIC_ENDPOINT_URL = "http://127.0.0.1:1"
	$env:OFXIC_MEDIA_BACKEND = "openai"
	$env:OFXIC_MEDIA_ENDPOINT_URL = "http://127.0.0.1:$Port/v1"
	$env:OFXIC_MEDIA_AUTORUN = "image"
	$env:OFXIC_MEDIA_PROMPT = "deterministic image fixture"
	$env:OFXIC_MEDIA_RESULT_PATH = $resultPath
	$env:OFXIC_GUI_HEARTBEAT_PATH = $heartbeatPath
	$env:OFXIC_SETTINGS_PATH = (Join-Path $temporary "settings")
	$example = Start-Process -FilePath $executablePath `
		-WorkingDirectory (Split-Path $executablePath) -WindowStyle Hidden -PassThru

	$heartbeatDeadline = [DateTime]::UtcNow.AddSeconds(5)
	do {
		Start-Sleep -Milliseconds 50
		if ($example.HasExited) { throw "Image GUI exited before the fixture request" }
	} while ((-not (Test-Path -LiteralPath $requestMarker) -or
		-not (Test-Path -LiteralPath $heartbeatPath)) -and
		[DateTime]::UtcNow -lt $heartbeatDeadline)
	if (-not (Test-Path -LiteralPath $requestMarker) -or
		-not (Test-Path -LiteralPath $heartbeatPath)) {
		throw "Image GUI did not enter the delayed media request with a heartbeat"
	}
	$firstHeartbeat = [int64](Get-Content -Raw -LiteralPath $heartbeatPath)
	Start-Sleep -Milliseconds 500
	$secondHeartbeat = [int64](Get-Content -Raw -LiteralPath $heartbeatPath)
	if ($secondHeartbeat -le $firstHeartbeat) {
		throw "GUI update heartbeat stopped during media generation: $firstHeartbeat -> $secondHeartbeat"
	}

	$deadline = [DateTime]::UtcNow.AddSeconds(20)
	do {
		Start-Sleep -Milliseconds 100
		if ($example.HasExited) { throw "Image GUI exited before producing evidence" }
		$hasResult = (Test-Path -LiteralPath $resultPath) -and
			((Get-Item -LiteralPath $resultPath).Length -gt 0)
	} while (-not $hasResult -and [DateTime]::UtcNow -lt $deadline)
	if (-not $hasResult) { throw "Image GUI did not produce evidence within 20 seconds" }

	$result = Get-Content -Raw -LiteralPath $resultPath
	$pathMatch = [regex]::Match($result, '(?m)^Saved: (.+\.png)\r?$')
	if ($result -notmatch "OpenAI image generation completed" -or -not $pathMatch.Success) {
		throw "Unexpected image GUI evidence: $result"
	}
	$generatedPath = $pathMatch.Groups[1].Value
	if (-not (Test-Path -LiteralPath $generatedPath)) {
		throw "Image GUI reported a missing output file: $generatedPath"
	}
	$bytes = [System.IO.File]::ReadAllBytes($generatedPath)
	if ($bytes.Length -lt 24 -or $bytes[0] -ne 0x89 -or
		$bytes[1] -ne 0x50 -or $bytes[2] -ne 0x4e -or $bytes[3] -ne 0x47) {
		throw "Saved image is not a PNG"
	}
	$width = [System.Net.IPAddress]::NetworkToHostOrder([BitConverter]::ToInt32($bytes, 16))
	$height = [System.Net.IPAddress]::NetworkToHostOrder([BitConverter]::ToInt32($bytes, 20))
	if ($width -ne 8 -or $height -ne 8) {
		throw "Saved image dimensions are ${width}x${height}, expected 8x8"
	}
	Write-Output "Image GUI smoke passed: heartbeat advanced $firstHeartbeat -> $secondHeartbeat; decoded and saved ${width}x${height} PNG"
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
	foreach ($name in $savedEnvironment.Keys) {
		[Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name])
	}
	if (Test-Path -LiteralPath $temporary) {
		Remove-Item -LiteralPath $temporary -Recurse -Force
	}
}
