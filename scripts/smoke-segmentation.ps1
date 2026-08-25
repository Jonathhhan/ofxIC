param(
	[string] $Executable = (Join-Path $PSScriptRoot "..\ofxICExample\bin\ofxICExample.exe"),
	[int] $Port = 18085
)

$ErrorActionPreference = "Stop"
$repository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$executablePath = [System.IO.Path]::GetFullPath($Executable)
$bridge = Join-Path $repository "scripts\sam-bridge-server.py"
$temporary = Join-Path ([System.IO.Path]::GetTempPath()) ("ofxIC-sam-" + [guid]::NewGuid())
$imagePath = Join-Path $temporary "fixture.ppm"
$resultPath = Join-Path $temporary "result.txt"
$previous = @{
	Endpoint = $env:OFXIC_ENDPOINT_URL; Autorun = $env:OFXIC_SEGMENTATION_AUTORUN
	Image = $env:OFXIC_SEGMENTATION_IMAGE; X = $env:OFXIC_SEGMENTATION_POINT_X
	Y = $env:OFXIC_SEGMENTATION_POINT_Y; Result = $env:OFXIC_GUI_RESULT_PATH
	Settings = $env:OFXIC_SETTINGS_PATH
}
$server = $null
$example = $null
try {
	New-Item -ItemType Directory -Path $temporary | Out-Null
	[System.IO.File]::WriteAllBytes($imagePath,
		[System.Text.Encoding]::ASCII.GetBytes("P6`n2 2`n255`nabcdefghijkl"))
	$server = Start-Process python.exe -ArgumentList @(
		('"' + $bridge + '"'), '--port', $Port, '--fixture-mask') -WindowStyle Hidden -PassThru
	$deadline = [DateTime]::UtcNow.AddSeconds(5)
	do {
		if ($server.HasExited) { throw "SAM bridge exited during startup" }
		$probe = [Net.Sockets.TcpClient]::new()
		try { $probe.Connect('127.0.0.1', $Port); $ready = $true }
		catch { Start-Sleep -Milliseconds 100 }
		finally { $probe.Dispose() }
	} while (-not $ready -and [DateTime]::UtcNow -lt $deadline)
	if (-not $ready) { throw "SAM bridge did not become ready" }
	$env:OFXIC_ENDPOINT_URL = "http://127.0.0.1:$Port"
	$env:OFXIC_SEGMENTATION_AUTORUN = '1'
	$env:OFXIC_SEGMENTATION_IMAGE = $imagePath
	$env:OFXIC_SEGMENTATION_POINT_X = '0.5'
	$env:OFXIC_SEGMENTATION_POINT_Y = '0.5'
	$env:OFXIC_GUI_RESULT_PATH = $resultPath
	$env:OFXIC_SETTINGS_PATH = (Join-Path $temporary 'settings')
	$example = Start-Process $executablePath -WorkingDirectory (Split-Path $executablePath) `
		-WindowStyle Hidden -PassThru
	$deadline = [DateTime]::UtcNow.AddSeconds(15)
	do { Start-Sleep -Milliseconds 100 } while (-not (Test-Path $resultPath) -and
		-not $example.HasExited -and [DateTime]::UtcNow -lt $deadline)
	if (-not (Test-Path $resultPath)) { throw "GUI produced no segmentation evidence" }
	$result = Get-Content -Raw $resultPath
	if ($result -notmatch 'Segmentation completed' -or $result -notmatch 'PGM mask') {
		throw "Unexpected segmentation evidence: $result"
	}
	Write-Output 'Segmentation GUI smoke passed (SAM bridge v1 fixture)'
} finally {
	if ($example -and -not $example.HasExited) { Stop-Process $example.Id -Force -ErrorAction SilentlyContinue }
	if ($server -and -not $server.HasExited) { Stop-Process $server.Id -Force -ErrorAction SilentlyContinue }
	$env:OFXIC_ENDPOINT_URL=$previous.Endpoint; $env:OFXIC_SEGMENTATION_AUTORUN=$previous.Autorun
	$env:OFXIC_SEGMENTATION_IMAGE=$previous.Image; $env:OFXIC_SEGMENTATION_POINT_X=$previous.X
	$env:OFXIC_SEGMENTATION_POINT_Y=$previous.Y; $env:OFXIC_GUI_RESULT_PATH=$previous.Result
	$env:OFXIC_SETTINGS_PATH=$previous.Settings
	if (Test-Path $temporary) { Remove-Item -LiteralPath $temporary -Recurse -Force }
}
