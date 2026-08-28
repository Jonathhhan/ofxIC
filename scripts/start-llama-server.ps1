[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)] [string] $Model,
	[string] $Server,
	[string] $InstallRoot = (Join-Path $env:LOCALAPPDATA "ofxIC\servers"),
	[int] $Port = 8080,
	[int] $ContextSize = 4096,
	[int] $GpuLayers = 999,
	[switch] $FlashAttention,
	[switch] $Foreground,
	[switch] $Plan
)

$ErrorActionPreference = "Stop"
if (-not $Server) {
	$Server = Get-ChildItem -LiteralPath $InstallRoot -Recurse -File -Filter "llama-server.exe" -ErrorAction SilentlyContinue |
		Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $Server) { throw "llama-server is not installed. Run scripts\install-llama-server.ps1 first." }
$serverPath = (Resolve-Path -LiteralPath $Server).Path
$modelPath = if ($Plan) { $Model } else { (Resolve-Path -LiteralPath $Model).Path }
$arguments = @("--model", ('"' + $modelPath + '"'), "--host", "127.0.0.1", "--port", $Port,
	"--ctx-size", $ContextSize, "--n-gpu-layers", $GpuLayers)
if ($FlashAttention) { $arguments += @("--flash-attn", "on") }
Write-Output "Executable: $serverPath"
Write-Output "Arguments: $($arguments -join ' ')"
if ($Plan) { return }

$logDirectory = Join-Path $env:LOCALAPPDATA "ofxIC\logs"
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$stdout = Join-Path $logDirectory "llama-server-$Port.stdout.log"
$stderr = Join-Path $logDirectory "llama-server-$Port.stderr.log"
if ($Foreground) { & $serverPath @arguments; return }
$process = Start-Process -FilePath $serverPath -ArgumentList $arguments `
	-WorkingDirectory (Split-Path -Parent $serverPath) -WindowStyle Hidden `
	-RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
Write-Output "Started llama-server (PID $($process.Id)) at http://127.0.0.1:$Port"
Write-Output "Logs: $stdout and $stderr"
