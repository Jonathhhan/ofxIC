[CmdletBinding()]
param(
	[string] $InstallRoot = (Join-Path $env:LOCALAPPDATA "ofxIC\servers"),
	[string] $ModelRoot = (Join-Path $env:LOCALAPPDATA "ofxIC\models\whisper.cpp"),
	[switch] $SkipModel,
	[switch] $Force,
	[switch] $Plan
)

$ErrorActionPreference = "Stop"
$release = "b4938"
$cuda = "12.4"
$archive = "whisper-cublas-12.4.0-bin-x64.zip"
$sha256 = "c1b17166e1e31a91cc8e9c1f910d3785e3ce757bb2958bf9dce13fdb4880005f"
$url = "https://github.com/ggml-org/whisper.cpp/releases/download/$release/$archive"
$installDirectory = Join-Path $InstallRoot "whisper.cpp-$release-cuda-$cuda"
$executable = Join-Path $installDirectory "whisper-server.exe"
$modelName = "ggml-base-q5_1.bin"
$model = Join-Path $ModelRoot $modelName
$modelUrl = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/$modelName"
$modelSha1 = "a3733eda680ef76256db5fc5dd9de8629e62c5e7"

function Receive-File([string] $Source, [string] $Destination) {
	$partial = $Destination + ".download"
	Remove-Item -LiteralPath $partial -Force -ErrorAction SilentlyContinue
	try {
		$curl = Get-Command "curl.exe" -ErrorAction SilentlyContinue
		if ($curl) {
			& $curl.Source --fail --location --retry 3 --retry-delay 2 `
				--connect-timeout 20 --output $partial $Source
			if ($LASTEXITCODE -ne 0) {
				throw "curl.exe failed with exit code $LASTEXITCODE while downloading $Source"
			}
		} else {
			Invoke-WebRequest -Uri $Source -OutFile $partial
		}
		if (-not (Test-Path -LiteralPath $partial -PathType Leaf) -or
			(Get-Item -LiteralPath $partial).Length -eq 0) {
			throw "Download produced an empty file: $Source"
		}
		Move-Item -LiteralPath $partial -Destination $Destination -Force
	} finally {
		Remove-Item -LiteralPath $partial -Force -ErrorAction SilentlyContinue
	}
}

if ($Plan) {
	Write-Output "whisper.cpp release: $release"
	Write-Output "Backend: CUDA $cuda (runs with newer NVIDIA drivers, including CUDA 13-capable drivers)"
	Write-Output "Install directory: $installDirectory"
	Write-Output "Download: $url"
	Write-Output "SHA-256: $sha256"
	Write-Output "Default model: $model"
	Write-Output "Default model SHA-1: $modelSha1"
	return
}
if ($env:PROCESSOR_ARCHITECTURE -ne "AMD64") {
	throw "The pinned whisper.cpp CUDA package requires 64-bit Windows on x64."
}
if ((Test-Path -LiteralPath $executable -PathType Leaf) -and -not $Force) {
	if (-not $SkipModel -and -not (Test-Path -LiteralPath $model -PathType Leaf)) {
		New-Item -ItemType Directory -Path $ModelRoot -Force | Out-Null
		Receive-File $modelUrl $model
		$actualModelHash = (Get-FileHash -LiteralPath $model -Algorithm SHA1).Hash.ToLowerInvariant()
		if ($actualModelHash -ne $modelSha1) {
			Remove-Item -LiteralPath $model -Force
			throw "SHA-1 mismatch for ${modelName}: expected $modelSha1, received $actualModelHash"
		}
	}
	Write-Output "whisper-server is already installed: $executable"
	if (-not $SkipModel) { Write-Output "Whisper model is already installed: $model" }
	return
}

$temporaryDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ("ofxIC-whisper-install-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $temporaryDirectory | Out-Null
try {
	$archivePath = Join-Path $temporaryDirectory $archive
	Receive-File $url $archivePath
	$actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
	if ($actualHash -ne $sha256) {
		throw "SHA-256 mismatch for ${archive}: expected $sha256, received $actualHash"
	}
	$expanded = Join-Path $temporaryDirectory "expanded"
	Expand-Archive -LiteralPath $archivePath -DestinationPath $expanded -Force
	$server = Get-ChildItem -LiteralPath $expanded -Recurse -File -Filter "whisper-server.exe" | Select-Object -First 1
	if (-not $server) { throw "The verified archive did not contain whisper-server.exe." }
	if (Test-Path -LiteralPath $installDirectory) {
		if (-not $Force) { throw "Install directory already exists: $installDirectory" }
		Remove-Item -LiteralPath $installDirectory -Recurse -Force
	}
	New-Item -ItemType Directory -Path $InstallRoot -Force | Out-Null
	New-Item -ItemType Directory -Path $installDirectory | Out-Null
	$payloadItems = Get-ChildItem -LiteralPath $server.Directory.FullName -Force
	Copy-Item -LiteralPath $payloadItems.FullName -Destination $installDirectory -Recurse -Force
	if (-not $SkipModel) {
		New-Item -ItemType Directory -Path $ModelRoot -Force | Out-Null
		if ($Force -or -not (Test-Path -LiteralPath $model -PathType Leaf)) {
			Receive-File $modelUrl $model
		}
		$actualModelHash = (Get-FileHash -LiteralPath $model -Algorithm SHA1).Hash.ToLowerInvariant()
		if ($actualModelHash -ne $modelSha1) {
			Remove-Item -LiteralPath $model -Force
			throw "SHA-1 mismatch for ${modelName}: expected $modelSha1, received $actualModelHash"
		}
	}
	[ordered]@{
		product = "whisper.cpp whisper-server"; release = $release; backend = "cuda"; cuda = $cuda
		architecture = "x64"; source = "https://github.com/ggml-org/whisper.cpp/releases/tag/$release"
		model = if ($SkipModel) { "" } else { $model }
		installedAtUtc = [DateTime]::UtcNow.ToString("o")
	} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $installDirectory "ofxIC-install.json") -Encoding utf8
	if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw "Installation did not produce $executable" }
	Write-Output "Installed whisper-server: $executable"
	if (-not $SkipModel) { Write-Output "Installed Whisper model: $model" }
} finally {
	if (Test-Path -LiteralPath $temporaryDirectory) { Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force }
}
