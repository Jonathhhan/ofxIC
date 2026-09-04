[CmdletBinding()]
param(
	[string] $InstallRoot = (Join-Path $env:LOCALAPPDATA "ofxIC\servers"),
	[switch] $Force,
	[switch] $Plan
)

$ErrorActionPreference = "Stop"
$release = "master-829-0a565f2"
$revision = "0a565f2"
$cuda = "12"
$baseUrl = "https://github.com/leejet/stable-diffusion.cpp/releases/download/$release"
$archives = @(
	@{ Name = "sd-master-$revision-bin-win-cuda12-x64.zip"; Sha256 = "4c130c10ebfb41221f71dee7cff2ac7968cb86a4b8ea01a30837499790ff7868" },
	@{ Name = "cudart-sd-bin-win-cu12-x64.zip"; Sha256 = "fe20366827d357c00797eebb58244dddab7fd9a348d70090c3871004c320f38d" }
)
$installDirectory = Join-Path $InstallRoot "stable-diffusion.cpp-$release-cuda$cuda"
$executable = Join-Path $installDirectory "sd-server.exe"

if ($Plan) {
	Write-Output "stable-diffusion.cpp release: $release"
	Write-Output "Backend: CUDA $cuda (requires a compatible NVIDIA driver; driver compatibility is not checked by -Plan)"
	Write-Output "Install directory: $installDirectory"
	foreach ($archive in $archives) {
		Write-Output "Download: $baseUrl/$($archive.Name)"
		Write-Output "SHA-256: $($archive.Sha256)"
	}
	return
}
if ($env:PROCESSOR_ARCHITECTURE -ne "AMD64") { throw "The pinned sd-server package requires 64-bit Windows on x64." }
if ((Test-Path -LiteralPath $executable) -and -not $Force) {
	Write-Output "sd-server is already installed: $executable"
	return
}

$temporaryDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ("ofxIC-sdcpp-install-" + [guid]::NewGuid())
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
	$server = Get-ChildItem -LiteralPath $temporaryDirectory -Recurse -File -Filter "sd-server.exe" | Select-Object -First 1
	if (-not $server) { throw "The verified archives did not contain sd-server.exe." }
	$payloadDirectory = $server.Directory.FullName
	if (Test-Path -LiteralPath $installDirectory) {
		if (-not $Force) { throw "Install directory already exists: $installDirectory" }
		Remove-Item -LiteralPath $installDirectory -Recurse -Force
	}
	New-Item -ItemType Directory -Path $InstallRoot -Force | Out-Null
	Move-Item -LiteralPath $payloadDirectory -Destination $installDirectory
	[ordered]@{
		product = "stable-diffusion.cpp sd-server"; release = $release; backend = "cuda"; cuda = $cuda
		architecture = "x64"; source = "https://github.com/leejet/stable-diffusion.cpp/releases/tag/$release"
		installedAtUtc = [DateTime]::UtcNow.ToString("o")
	} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $installDirectory "ofxIC-install.json") -Encoding utf8
	if (-not (Test-Path -LiteralPath $executable)) { throw "Installation did not produce $executable" }
	Write-Output "Installed sd-server: $executable"
} finally {
	if (Test-Path -LiteralPath $temporaryDirectory) { Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force }
}
