param(
	[string] $Executable = (Join-Path $PSScriptRoot "..\ofxICExample\bin\ofxICExample.exe"),
	[int] $Port = 18081
)

$ErrorActionPreference = "Stop"
$repository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$executablePath = [System.IO.Path]::GetFullPath($Executable)
$fixture = Join-Path $repository "tests\slow_endpoint_fixture.py"

if (-not (Test-Path -LiteralPath $executablePath)) {
	throw "Build the Release example first: $executablePath"
}

$previousUrl = $env:OFXIC_ENDPOINT_URL
$previousAutorun = $env:OFXIC_INSPECT_AUTORUN
$server = $null
$example = $null
try {
	$server = Start-Process -FilePath "python.exe" -ArgumentList @(
		$fixture, "--port", $Port, "--delay", 30) -WindowStyle Hidden -PassThru
	Start-Sleep -Seconds 1
	$env:OFXIC_ENDPOINT_URL = "http://127.0.0.1:$Port"
	$env:OFXIC_INSPECT_AUTORUN = "1"
	$example = Start-Process -FilePath $executablePath `
		-WorkingDirectory (Split-Path $executablePath) -PassThru

	$deadline = [DateTime]::UtcNow.AddSeconds(8)
	do {
		Start-Sleep -Milliseconds 200
		$window = Get-Process -Id $example.Id -ErrorAction SilentlyContinue
	} while ($window -and $window.MainWindowHandle -eq 0 -and
		[DateTime]::UtcNow -lt $deadline)
	if (-not $window -or $window.MainWindowHandle -eq 0) {
		throw "GUI window did not appear"
	}

	Start-Sleep -Seconds 2
	Add-Type 'using System; using System.Runtime.InteropServices; public static class NativeClose { [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam); }'
	$timer = [Diagnostics.Stopwatch]::StartNew()
	[NativeClose]::PostMessage(
		$window.MainWindowHandle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
	do {
		Start-Sleep -Milliseconds 100
		$remaining = Get-Process -Id $example.Id -ErrorAction SilentlyContinue
	} while ($remaining -and $timer.Elapsed.TotalSeconds -lt 8)
	$timer.Stop()
	if ($remaining) { throw "GUI did not close within 8 seconds" }
	Write-Output ("Cancellation smoke passed in {0:N3} seconds" -f $timer.Elapsed.TotalSeconds)
} finally {
	if ($example -and -not $example.HasExited) {
		Stop-Process -Id $example.Id -Force -ErrorAction SilentlyContinue
	}
	if ($server -and -not $server.HasExited) {
		Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
	}
	$env:OFXIC_ENDPOINT_URL = $previousUrl
	$env:OFXIC_INSPECT_AUTORUN = $previousAutorun
}
