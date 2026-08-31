[CmdletBinding()]
param(
	[string] $InstallRoot = (Join-Path $env:LOCALAPPDATA "ofxIC\servers"),
	[string] $ModelRoot = (Join-Path $env:LOCALAPPDATA "ofxIC\models\sam"),
	[string] $ExistingModel,
	[switch] $DownloadModel,
	[switch] $Force,
	[switch] $Plan
)

$ErrorActionPreference = "Stop"
$runtime = "sam-python-1-cuda-13"
$installDirectory = Join-Path $InstallRoot $runtime
$venv = Join-Path $installDirectory ".venv"
$python = Join-Path $venv "Scripts\python.exe"
$runner = Join-Path $installDirectory "sam-python-runner.py"
$sourceRunner = Join-Path $PSScriptRoot "sam-python-runner.py"
$modelName = "sam_vit_b_01ec64.pth"
$defaultModel = Join-Path $ModelRoot $modelName
$modelUrl = "https://dl.fbaipublicfiles.com/segment_anything/$modelName"
$samCommit = "6fdee8f2727f4506cfbbe553e23b895e27956588"
$torchIndex = "https://download.pytorch.org/whl/cu130"
$torchVersion = "2.13.0+cu130"
$torchvisionVersion = "0.28.0+cu130"
$numpyVersion = "2.5.2"
$pillowVersion = "12.3.0"

if ($Plan) {
	Write-Output "SAM runtime: Meta Segment Anything 1"
	Write-Output "Backend: PyTorch CUDA 13.0"
	Write-Output "Install directory: $installDirectory"
	Write-Output "Runner: $runner"
	Write-Output "Meta SAM source commit: $samCommit"
	Write-Output "PyTorch: $torchVersion; torchvision: $torchvisionVersion"
	Write-Output "NumPy: $numpyVersion; Pillow: $pillowVersion"
	Write-Output "Existing checkpoint: $ExistingModel"
	Write-Output "Optional default checkpoint: $defaultModel"
	return
}
if ($env:PROCESSOR_ARCHITECTURE -ne "AMD64") {
	throw "The pinned SAM CUDA environment requires 64-bit Windows on x64."
}
$uv = (Get-Command uv.exe -ErrorAction SilentlyContinue).Source
if (-not $uv) { throw "uv.exe was not found on PATH. Install uv from https://docs.astral.sh/uv/." }
if (-not $env:UV_LOCK_TIMEOUT) { $env:UV_LOCK_TIMEOUT = "900" }
if (-not (Test-Path -LiteralPath $sourceRunner -PathType Leaf)) {
	throw "SAM runner source was not found: $sourceRunner"
}
if ((Test-Path -LiteralPath $python -PathType Leaf) -and
	(Test-Path -LiteralPath $runner -PathType Leaf) -and -not $Force) {
	Write-Output "SAM server environment is already installed: $python"
	return
}
if (Test-Path -LiteralPath $installDirectory) {
	if (-not $Force) { throw "Install directory already exists but is incomplete: $installDirectory" }
	Remove-Item -LiteralPath $installDirectory -Recurse -Force
}
New-Item -ItemType Directory -Path $installDirectory -Force | Out-Null
try {
	& $uv python install 3.12
	if ($LASTEXITCODE -ne 0) { throw "uv could not install Python 3.12." }
	& $uv venv $venv --python 3.12
	if ($LASTEXITCODE -ne 0) { throw "uv could not create the SAM environment." }
	& $uv pip install --python $python `
		("torch==" + $torchVersion) ("torchvision==" + $torchvisionVersion) `
		--index-url $torchIndex
	if ($LASTEXITCODE -ne 0) { throw "PyTorch CUDA installation failed." }
	& $uv pip install --python $python `
		("numpy==" + $numpyVersion) ("pillow==" + $pillowVersion) `
		("git+https://github.com/facebookresearch/segment-anything.git@" + $samCommit)
	if ($LASTEXITCODE -ne 0) { throw "Meta Segment Anything installation failed." }
	Copy-Item -LiteralPath $sourceRunner -Destination $runner -Force

	if ($ExistingModel) {
		$modelPath = [System.IO.Path]::GetFullPath($ExistingModel)
		if (-not (Test-Path -LiteralPath $modelPath -PathType Leaf)) {
			throw "Existing SAM checkpoint was not found: $modelPath"
		}
	} elseif ($DownloadModel) {
		New-Item -ItemType Directory -Path $ModelRoot -Force | Out-Null
		Invoke-WebRequest -Uri $modelUrl -OutFile $defaultModel
		$modelPath = $defaultModel
	} else {
		$modelPath = ""
	}

	$versions = & $python -c "import torch; print(torch.__version__); print(torch.version.cuda or '')"
	if ($LASTEXITCODE -ne 0) { throw "The installed SAM Python environment failed validation." }
	[ordered]@{
		product = "Meta Segment Anything 1 runner"
		backend = "cuda"
		cuda = if ($versions.Count -gt 1) { $versions[1] } else { "13.0" }
		python = "3.12"
		torch = if ($versions.Count -gt 0) { $versions[0] } else { "unknown" }
		torchvision = $torchvisionVersion
		numpy = $numpyVersion
		pillow = $pillowVersion
		source = "https://github.com/facebookresearch/segment-anything"
		commit = $samCommit
		runner = $runner
		model = $modelPath
		installedAtUtc = [DateTime]::UtcNow.ToString("o")
	} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $installDirectory "ofxIC-install.json") -Encoding utf8
	Write-Output "Installed SAM server environment: $python"
	Write-Output "Installed SAM runner: $runner"
	if ($modelPath) { Write-Output "SAM checkpoint: $modelPath" }
	else { Write-Output "Select a Meta SAM .pth checkpoint in the GUI." }
} catch {
	Write-Warning "SAM installation failed; removing incomplete environment."
	if (Test-Path -LiteralPath $installDirectory) {
		Remove-Item -LiteralPath $installDirectory -Recurse -Force
	}
	throw
}
