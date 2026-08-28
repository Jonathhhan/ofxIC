[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)] [string] $DiffusionModel,
	[string] $Vae,
	[string] $TextEncoder,
	[string] $ClipL,
	[string] $ClipG,
	[string] $Server,
	[string] $InstallRoot = (Join-Path $env:LOCALAPPDATA "ofxIC\servers"),
	[int] $Port = 8081,
	[switch] $FlashAttention,
	[switch] $OffloadToCpu,
	[switch] $CompleteCheckpoint,
	[switch] $Foreground,
	[switch] $Plan
)

$ErrorActionPreference = "Stop"
if (-not $Server) {
	$Server = Get-ChildItem -LiteralPath $InstallRoot -Recurse -File -Filter "sd-server.exe" -ErrorAction SilentlyContinue |
		Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $Server) { throw "sd-server is not installed. Run scripts\install-stable-diffusion-server.ps1 first." }
$serverPath = (Resolve-Path -LiteralPath $Server).Path
$modelPath = if ($Plan) { $DiffusionModel } else { (Resolve-Path -LiteralPath $DiffusionModel).Path }
$modelArgument = if ($Plan) { $modelPath } else { '"' + $modelPath + '"' }
$arguments = @($(if ($CompleteCheckpoint) { "--model" } else { "--diffusion-model" }), $modelArgument, "--backend", "cuda0", "--listen-ip", "127.0.0.1", "--listen-port", $Port)
if ($Vae) { $arguments += @("--vae", $(if ($Plan) { $Vae } else { (Resolve-Path -LiteralPath $Vae).Path })) }
if ($ClipL) { $arguments += @("--clip_l", $(if ($Plan) { $ClipL } else { (Resolve-Path -LiteralPath $ClipL).Path })) }
if ($ClipG) { $arguments += @("--clip_g", $(if ($Plan) { $ClipG } else { (Resolve-Path -LiteralPath $ClipG).Path })) }
if ($TextEncoder) { $arguments += @("--t5xxl", $(if ($Plan) { $TextEncoder } else { (Resolve-Path -LiteralPath $TextEncoder).Path })) }
if ($FlashAttention) { $arguments += "--diffusion-fa" }
if ($OffloadToCpu) { $arguments += "--offload-to-cpu" }
Write-Output "Executable: $serverPath"
Write-Output "Arguments: $($arguments -join ' ')"
if ($Plan) { return }

$logDirectory = Join-Path $env:LOCALAPPDATA "ofxIC\logs"
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$stdout = Join-Path $logDirectory "sd-server-$Port.stdout.log"
$stderr = Join-Path $logDirectory "sd-server-$Port.stderr.log"
if ($Foreground) { & $serverPath @arguments; return }
$process = Start-Process -FilePath $serverPath -ArgumentList $arguments `
	-WorkingDirectory (Split-Path -Parent $serverPath) -WindowStyle Hidden `
	-RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
Write-Output "Started sd-server (PID $($process.Id)) at http://127.0.0.1:$Port"
Write-Output "Logs: $stdout and $stderr"
