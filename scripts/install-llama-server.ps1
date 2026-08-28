[CmdletBinding()]
param(
	[string] $InstallRoot = (Join-Path $env:LOCALAPPDATA "ofxIC\servers"),
	[switch] $Force,
	[switch] $Plan
)

$ErrorActionPreference = "Stop"
$release = "b10516"
$cuda = "13.3"
$baseUrl = "https://github.com/ggml-org/llama.cpp/releases/download/$release"
$archives = @(
	@{ Name = "llama-$release-bin-win-cuda-$cuda-x64.zip"; Sha256 = "462fc1b2eddb78b594a93867778a1a0cb15a39f0c0b16e9337f0b56ca7aa25f9" },
	@{ Name = "cudart-llama-bin-win-cuda-$cuda-x64.zip"; Sha256 = "1462a050eb4c684921ba51dcc4cc488a036674c3e73e9945ee705b854808d03e" }
)
$installDirectory = Join-Path $InstallRoot "llama.cpp-$release-cuda-$cuda"
$executable = Join-Path $installDirectory "llama-server.exe"

if ($Plan) {
	Write-Output "llama.cpp release: $release"
	Write-Output "Backend: CUDA $cuda"
	Write-Output "Install directory: $installDirectory"
	foreach ($archive in $archives) {
		Write-Output "Download: $baseUrl/$($archive.Name)"
		Write-Output "SHA-256: $($archive.Sha256)"
	}
	return
}
if ($env:PROCESSOR_ARCHITECTURE -ne "AMD64") {
	throw "The pinned llama-server package requires 64-bit Windows on x64."
}
if ((Test-Path -LiteralPath $executable) -and -not $Force) {
	Write-Output "llama-server is already installed: $executable"
	return
}

$temporaryDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ("ofxIC-llama-install-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $temporaryDirectory | Out-Null
try {
	foreach ($archive in $archives) {
		$archivePath = Join-Path $temporaryDirectory $archive.Name
		Invoke-WebRequest -Uri "$baseUrl/$($archive.Name)" -OutFile $archivePath
		$actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
		if ($actualHash -ne $archive.Sha256) {
			throw "SHA-256 mismatch for $($archive.Name): expected $($archive.Sha256), received $actualHash"
		}
		Expand-Archive -LiteralPath $archivePath -DestinationPath $temporaryDirectory -Force
	}
	$server = Get-ChildItem -LiteralPath $temporaryDirectory -Recurse -File -Filter "llama-server.exe" | Select-Object -First 1
	if (-not $server) { throw "The verified archives did not contain llama-server.exe." }
	$payloadDirectory = $server.Directory.FullName
	if (Test-Path -LiteralPath $installDirectory) {
		if (-not $Force) { throw "Install directory already exists: $installDirectory" }
		Remove-Item -LiteralPath $installDirectory -Recurse -Force
	}
	New-Item -ItemType Directory -Path $InstallRoot -Force | Out-Null
	Move-Item -LiteralPath $payloadDirectory -Destination $installDirectory
	[ordered]@{
		product = "llama.cpp llama-server"; release = $release; backend = "cuda"; cuda = $cuda
		architecture = "x64"; source = "https://github.com/ggml-org/llama.cpp/releases/tag/$release"
		installedAtUtc = [DateTime]::UtcNow.ToString("o")
	} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $installDirectory "ofxIC-install.json") -Encoding utf8
	if (-not (Test-Path -LiteralPath $executable)) { throw "Installation did not produce $executable" }
	Write-Output "Installed llama-server: $executable"
} finally {
	if (Test-Path -LiteralPath $temporaryDirectory) { Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force }
}
