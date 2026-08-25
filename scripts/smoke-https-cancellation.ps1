param(
	[string] $Executable = (Join-Path $PSScriptRoot "..\ofxICExample\bin\ofxICExample.exe"),
	[string] $DelayedBaseUrl = "https://deelay.me/30000/https://router.huggingface.co"
)

$ErrorActionPreference = "Stop"
$executablePath = [System.IO.Path]::GetFullPath($Executable)
if (-not (Test-Path -LiteralPath $executablePath)) {
	throw "Build the Release example first: $executablePath"
}

$previousUrl = $env:OFXIC_ENDPOINT_URL
$previousAutorun = $env:OFXIC_INSPECT_AUTORUN
$previousKey = $env:OFXIC_API_KEY
$example = $null
try {
	$env:OFXIC_ENDPOINT_URL = $DelayedBaseUrl
	$env:OFXIC_INSPECT_AUTORUN = "1"
	$env:OFXIC_API_KEY = "diagnostic-not-a-real-secret"
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
	Add-Type 'using System; using System.Runtime.InteropServices; public static class HttpsCancelClose { [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam); }'
	$timer = [Diagnostics.Stopwatch]::StartNew()
	[HttpsCancelClose]::PostMessage(
		$window.MainWindowHandle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
	do {
		Start-Sleep -Milliseconds 50
		$remaining = Get-Process -Id $example.Id -ErrorAction SilentlyContinue
	} while ($remaining -and $timer.Elapsed.TotalSeconds -lt 8)
	$timer.Stop()
	if ($remaining) { throw "HTTPS GUI did not close within 8 seconds" }
	Write-Output ("WinHTTP blocking cancellation passed in {0:N3} seconds" -f $timer.Elapsed.TotalSeconds)
} finally {
	if ($example -and -not $example.HasExited) {
		Stop-Process -Id $example.Id -Force -ErrorAction SilentlyContinue
	}
	$env:OFXIC_ENDPOINT_URL = $previousUrl
	$env:OFXIC_INSPECT_AUTORUN = $previousAutorun
	$env:OFXIC_API_KEY = $previousKey
}
