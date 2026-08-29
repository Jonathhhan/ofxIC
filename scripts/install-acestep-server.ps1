[CmdletBinding()]
param(
	[string] $InstallRoot = (Join-Path $env:LOCALAPPDATA "ofxIC\servers"),
	[switch] $Force,
	[switch] $Plan
)

$ErrorActionPreference = "Stop"
$release = "v0.1.8"
$expectedCommitPrefix = "dce6214"
$installDirectory = Join-Path $InstallRoot "ACE-Step-1.5-$release-cuda"
$python = Join-Path $installDirectory ".venv\Scripts\python.exe"

if ($Plan) {
	Write-Output "ACE-Step release: $release ($expectedCommitPrefix)"
	Write-Output "Backend: CUDA through the official Python environment"
	Write-Output "Python: uv-managed 3.12"
	Write-Output "Install directory: $installDirectory"
	Write-Output "Source: https://github.com/ACE-Step/ACE-Step-1.5.git"
	Write-Output "Models are downloaded by ACE-Step on first start and remain outside Git."
	return
}
if ($env:PROCESSOR_ARCHITECTURE -ne "AMD64") {
	throw "The ACE-Step setup requires 64-bit Windows on x64."
}
$git = (Get-Command git.exe -ErrorAction SilentlyContinue).Source
$uv = (Get-Command uv.exe -ErrorAction SilentlyContinue).Source
if (-not $git) { throw "git.exe was not found on PATH." }
if (-not $uv) { throw "uv.exe was not found on PATH. Install uv from https://docs.astral.sh/uv/." }
if ((Test-Path -LiteralPath $python -PathType Leaf) -and -not $Force) {
	Write-Output "ACE-Step server environment is already installed: $python"
	return
}
if (Test-Path -LiteralPath $installDirectory) {
	if (-not $Force) { throw "Install directory already exists but is incomplete: $installDirectory" }
	Remove-Item -LiteralPath $installDirectory -Recurse -Force
}
New-Item -ItemType Directory -Path $InstallRoot -Force | Out-Null
& $git clone --branch $release --depth 1 https://github.com/ACE-Step/ACE-Step-1.5.git $installDirectory
if ($LASTEXITCODE -ne 0) { throw "git clone failed with exit code $LASTEXITCODE" }
try {
	$commit = (& $git -C $installDirectory rev-parse HEAD).Trim()
	if (-not $commit.StartsWith($expectedCommitPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
		throw "ACE-Step tag $release resolved to unexpected commit $commit"
	}
	& $uv python install 3.12
	if ($LASTEXITCODE -ne 0) { throw "uv could not install Python 3.12." }
	& $uv sync --directory $installDirectory --python 3.12 --frozen
	if ($LASTEXITCODE -ne 0) { throw "uv sync failed with exit code $LASTEXITCODE" }
	if (-not (Test-Path -LiteralPath $python -PathType Leaf)) {
		throw "ACE-Step setup did not produce $python"
	}
	[ordered]@{
		product = "ACE-Step 1.5 API server"; release = $release; commit = $commit
		backend = "cuda"; python = "3.12"; source = "https://github.com/ACE-Step/ACE-Step-1.5"
		installedAtUtc = [DateTime]::UtcNow.ToString("o")
	} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $installDirectory "ofxIC-install.json") -Encoding utf8
	Write-Output "Installed ACE-Step server environment: $python"
} catch {
	Write-Warning "ACE-Step installation failed; removing incomplete environment."
	if (Test-Path -LiteralPath $installDirectory) { Remove-Item -LiteralPath $installDirectory -Recurse -Force }
	throw
}
