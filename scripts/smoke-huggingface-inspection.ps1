param(
	[string] $Executable = (Join-Path $PSScriptRoot "..\ofxICExample\bin\ofxICExample.exe")
)

$ErrorActionPreference = "Stop"
$executablePath = [System.IO.Path]::GetFullPath($Executable)
if (-not (Test-Path -LiteralPath $executablePath)) {
	throw "Build the Release example first: $executablePath"
}

$resultPath = Join-Path $env:TEMP ("ofxic-hf-inspection-{0}.txt" -f [Guid]::NewGuid())
$previousUrl = $env:OFXIC_ENDPOINT_URL
$previousAutorun = $env:OFXIC_INSPECT_AUTORUN
$previousResult = $env:OFXIC_GUI_RESULT_PATH
$previousKey = $env:OFXIC_API_KEY
$example = $null
try {
	$env:OFXIC_ENDPOINT_URL = "https://router.huggingface.co/v1"
	$env:OFXIC_INSPECT_AUTORUN = "1"
	$env:OFXIC_GUI_RESULT_PATH = $resultPath
	$env:OFXIC_API_KEY = "diagnostic-not-a-real-secret"
	$example = Start-Process -FilePath $executablePath `
		-WorkingDirectory (Split-Path $executablePath) -WindowStyle Hidden -PassThru

	$deadline = [DateTime]::UtcNow.AddSeconds(20)
	while (-not (Test-Path -LiteralPath $resultPath) -and
		-not $example.HasExited -and [DateTime]::UtcNow -lt $deadline) {
		Start-Sleep -Milliseconds 200
	}
	if (-not (Test-Path -LiteralPath $resultPath)) {
		throw "HF inspection did not produce a GUI automation result"
	}
	$status = (Get-Content -LiteralPath $resultPath -TotalCount 1).Trim()
	if ($status -notlike "Endpoint reachable; model: * (authentication not tested)") {
		throw "Unexpected HF inspection result: $status"
	}
	Write-Output "Hugging Face inspection smoke passed: $status"
} finally {
	if ($example -and -not $example.HasExited) {
		Stop-Process -Id $example.Id -Force -ErrorAction SilentlyContinue
	}
	$env:OFXIC_ENDPOINT_URL = $previousUrl
	$env:OFXIC_INSPECT_AUTORUN = $previousAutorun
	$env:OFXIC_GUI_RESULT_PATH = $previousResult
	$env:OFXIC_API_KEY = $previousKey
	Remove-Item -LiteralPath $resultPath -Force -ErrorAction SilentlyContinue
}
