[CmdletBinding()]
param(
	[string] $InstallRoot = (Join-Path $env:LOCALAPPDATA "ofxIC\servers"),
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

if ($Plan) {
	Write-Output "whisper.cpp release: $release"
	Write-Output "Backend: CUDA $cuda (runs with newer NVIDIA drivers, including CUDA 13-capable drivers)"
	Write-Output "Install directory: $installDirectory"
	Write-Output "Download: $url"
	Write-Output "SHA-256: $sha256"
	Write-Output "Model files are intentionally not downloaded; select a whisper.cpp ggml model in the GUI."
	return
}
if ($env:PROCESSOR_ARCHITECTURE -ne "AMD64") {
	throw "The pinned whisper.cpp CUDA package requires 64-bit Windows on x64."
}
if ((Test-Path -LiteralPath $executable -PathType Leaf) -and -not $Force) {
	Write-Output "whisper-server is already installed: $executable"
	return
}

$temporaryDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ("ofxIC-whisper-install-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $temporaryDirectory | Out-Null
try {
	$archivePath = Join-Path $temporaryDirectory $archive
	Invoke-WebRequest -Uri $url -OutFile $archivePath
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
	[ordered]@{
		product = "whisper.cpp whisper-server"; release = $release; backend = "cuda"; cuda = $cuda
		architecture = "x64"; source = "https://github.com/ggml-org/whisper.cpp/releases/tag/$release"
		installedAtUtc = [DateTime]::UtcNow.ToString("o")
	} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $installDirectory "ofxIC-install.json") -Encoding utf8
	if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw "Installation did not produce $executable" }
	Write-Output "Installed whisper-server: $executable"
} finally {
	if (Test-Path -LiteralPath $temporaryDirectory) { Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force }
}
