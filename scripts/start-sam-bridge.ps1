param(
	[Parameter(Mandatory = $true)] [string] $Runner,
	[Parameter(Mandatory = $true)] [string] $Model,
	[ValidateSet("cpu", "cuda")] [string] $Backend = "cuda",
	[int] $Port = 18085,
	[int] $RunnerTimeout = 300
)

$ErrorActionPreference = "Stop"
$repository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$bridge = Join-Path $repository "scripts\sam-bridge-server.py"
$runnerPath = [System.IO.Path]::GetFullPath($Runner)
$modelPath = [System.IO.Path]::GetFullPath($Model)

foreach ($path in @($bridge, $runnerPath, $modelPath)) {
	if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
		throw "Required file not found: $path"
	}
}
if ($Port -lt 1 -or $Port -gt 65535) { throw "Port must be between 1 and 65535" }
if ($RunnerTimeout -lt 1 -or $RunnerTimeout -gt 3600) {
	throw "RunnerTimeout must be between 1 and 3600 seconds"
}

Write-Output "Starting ofxIC SAM bridge at http://127.0.0.1:$Port"
Write-Output "Backend: $Backend"
Write-Output "Keep this terminal open while using SAM in ofxICExample."
& python.exe $bridge --port $Port --adapter $runnerPath --model $modelPath `
	--backend $Backend --runner-timeout $RunnerTimeout
exit $LASTEXITCODE
