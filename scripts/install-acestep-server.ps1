[CmdletBinding()]
param(
	[string] $InstallRoot = (Join-Path $env:LOCALAPPDATA "ofxIC\servers"),
	[string] $ModelDirectory = $(if (Test-Path -LiteralPath "G:\Models" -PathType Container) { "G:\Models" } else { Join-Path $env:LOCALAPPDATA "ofxIC\models\acestep.cpp" }),
	[switch] $Force,
	[switch] $Plan
)

$ErrorActionPreference = "Stop"
$commit = "9761469d95fc204b5468623c68a1a2203e50b1f9"
$commitPrefix = $commit.Substring(0, 7)
$installDirectory = Join-Path $InstallRoot "acestep.cpp-$commitPrefix-cuda-13"
$sourceDirectory = Join-Path $installDirectory "source"
$buildDirectory = Join-Path $installDirectory "build"
$server = Join-Path $installDirectory "ace-server.exe"

function Find-AceServer([string] $Root) {
	Get-ChildItem -LiteralPath $Root -Recurse -File -Filter "ace-server.exe" -ErrorAction SilentlyContinue |
		Select-Object -First 1 -ExpandProperty FullName
}

function Test-ModelSet([string] $Root) {
	if (-not (Test-Path -LiteralPath $Root -PathType Container)) { return $false }
	$names = Get-ChildItem -LiteralPath $Root -File -Filter "*.gguf" | ForEach-Object { $_.Name.ToLowerInvariant() }
	$hasLm = @($names | Where-Object { $_ -like "acestep-5hz-lm-*.gguf" }).Count -gt 0
	$hasEmbedding = @($names | Where-Object { $_ -like "qwen3-embedding-*.gguf" }).Count -gt 0
	$hasDit = @($names | Where-Object { $_ -like "acestep-v15-*.gguf" }).Count -gt 0
	$hasVae = @($names | Where-Object { $_ -like "vae-*.gguf" }).Count -gt 0
	return $hasLm -and $hasEmbedding -and $hasDit -and $hasVae
}

if ($Plan) {
	Write-Output "ACE-Step runtime: acestep.cpp commit $commit"
	Write-Output "Backend: native CUDA, built locally with the installed CUDA toolkit"
	Write-Output "Install directory: $installDirectory"
	Write-Output "Server: $server"
	Write-Output "Model directory: $ModelDirectory"
	Write-Output "Model set complete: $(Test-ModelSet $ModelDirectory)"
	Write-Output "Source: https://github.com/ServeurpersoCom/acestep.cpp.git"
	return
}

if ($env:PROCESSOR_ARCHITECTURE -ne "AMD64") {
	throw "The acestep.cpp setup requires 64-bit Windows on x64."
}
$git = (Get-Command git.exe -ErrorAction SilentlyContinue).Source
$cmake = (Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
$nvcc = (Get-Command nvcc.exe -ErrorAction SilentlyContinue).Source
if (-not $git) { throw "git.exe was not found on PATH." }
if (-not $cmake) { throw "cmake.exe was not found on PATH." }
if (-not $nvcc) { throw "nvcc.exe was not found on PATH. Install the CUDA toolkit first." }

if ((Test-Path -LiteralPath $server -PathType Leaf) -and -not $Force) {
	Write-Output "Native ACE-Step server is already installed: $server"
	Write-Output "Model directory: $ModelDirectory (complete: $(Test-ModelSet $ModelDirectory))"
	return
}
if (Test-Path -LiteralPath $installDirectory) {
	if (-not $Force) { throw "Install directory already exists but is incomplete: $installDirectory" }
	Remove-Item -LiteralPath $installDirectory -Recurse -Force
}
New-Item -ItemType Directory -Path $installDirectory -Force | Out-Null

try {
	& $git clone --recurse-submodules https://github.com/ServeurpersoCom/acestep.cpp.git $sourceDirectory
	if ($LASTEXITCODE -ne 0) { throw "git clone failed with exit code $LASTEXITCODE" }
	& $git -C $sourceDirectory checkout --detach $commit
	if ($LASTEXITCODE -ne 0) { throw "git checkout failed with exit code $LASTEXITCODE" }
	& $git -C $sourceDirectory submodule update --init --recursive
	if ($LASTEXITCODE -ne 0) { throw "git submodule update failed with exit code $LASTEXITCODE" }
	$resolvedCommit = (& $git -C $sourceDirectory rev-parse HEAD).Trim()
	if ($resolvedCommit -ne $commit) { throw "Expected commit $commit but checked out $resolvedCommit" }

	& $cmake -S $sourceDirectory -B $buildDirectory -DGGML_CUDA=ON -DGGML_NATIVE=OFF
	if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }
	& $cmake --build $buildDirectory --config Release --target ace-server --parallel
	if ($LASTEXITCODE -ne 0) { throw "ACE-Step CUDA build failed with exit code $LASTEXITCODE" }

	$builtServer = Find-AceServer $buildDirectory
	if (-not $builtServer) { throw "The build completed without producing ace-server.exe" }
	$runtimeDirectory = Split-Path -Parent $builtServer
	Get-ChildItem -LiteralPath $runtimeDirectory -File | Where-Object {
		$_.Extension -in ".exe", ".dll"
	} | Copy-Item -Destination $installDirectory -Force
	if (-not (Test-Path -LiteralPath $server -PathType Leaf)) {
		throw "The runtime copy did not produce $server"
	}

	[ordered]@{
		product = "acestep.cpp native API server"
		commit = $resolvedCommit
		backend = "cuda"
		cudaCompiler = (& $nvcc --version | Select-Object -Last 1)
		source = "https://github.com/ServeurpersoCom/acestep.cpp"
		modelDirectory = $ModelDirectory
		modelSetComplete = (Test-ModelSet $ModelDirectory)
		installedAtUtc = [DateTime]::UtcNow.ToString("o")
	} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $installDirectory "ofxIC-install.json") -Encoding utf8
	Write-Output "Installed native ACE-Step server: $server"
	Write-Output "Model directory: $ModelDirectory (complete: $(Test-ModelSet $ModelDirectory))"
} catch {
	Write-Warning "ACE-Step installation failed; removing incomplete native runtime."
	if (Test-Path -LiteralPath $installDirectory) {
		Remove-Item -LiteralPath $installDirectory -Recurse -Force
	}
	throw
}
