param(
	[string] $InstallRoot = "$env:LOCALAPPDATA\ofxIC",
	[string] $Destination = ""
)

$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
if (-not $Destination) {
	$Destination = Join-Path $repo "ofxICExample\bin\runtime"
}

if (-not ("OfxIC.NativeLinks" -as [type])) {
	Add-Type -TypeDefinition @"
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
namespace OfxIC {
    public static class NativeLinks {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool CreateHardLinkW(string link, string target, IntPtr reserved);
        private static string Extended(string path) {
            string full = System.IO.Path.GetFullPath(path);
            return full.StartsWith(@"\\?\") ? full : @"\\?\" + full;
        }
        public static void Create(string link, string target) {
            if (!CreateHardLinkW(Extended(link), Extended(target), IntPtr.Zero))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "Could not create hardlink: " + link);
        }
    }
}
"@
}

function Mirror-HardLinks([string] $Source, [string] $Target) {
	if (-not (Test-Path -LiteralPath $Source -PathType Container)) { return $false }
	$stamp = Join-Path $Target ".ofxic-hardlink-stage.complete"
	if (Test-Path -LiteralPath $stamp -PathType Leaf) {
		$stagedSource = [IO.File]::ReadAllText($stamp)
		if ($stagedSource -eq [IO.Path]::GetFullPath($Source)) { return $true }
	}
	Get-ChildItem -LiteralPath $Source -Directory -Recurse | ForEach-Object {
		$relative = $_.FullName.Substring($Source.Length).TrimStart('\')
		New-Item -ItemType Directory -Force -Path (Join-Path $Target $relative) | Out-Null
	}
	New-Item -ItemType Directory -Force -Path $Target | Out-Null
	Get-ChildItem -LiteralPath $Source -File -Recurse | ForEach-Object {
		$sourceFile = $_.FullName
		$sourceLength = $_.Length
		$sourceTimestamp = $_.LastWriteTimeUtc
		$relative = $sourceFile.Substring($Source.Length).TrimStart('\')
		$targetFile = Join-Path $Target $relative
		New-Item -ItemType Directory -Force -Path (Split-Path -Parent $targetFile) | Out-Null
		if (Test-Path -LiteralPath $targetFile) {
			$existing = Get-Item -LiteralPath $targetFile
			if ($existing.Length -eq $sourceLength -and $existing.LastWriteTimeUtc -eq $sourceTimestamp) {
				return
			}
			Remove-Item -LiteralPath $targetFile -Force
		}
		try {
			New-Item -ItemType HardLink -Path $targetFile -Target $sourceFile -ErrorAction Stop | Out-Null
		} catch {
			[OfxIC.NativeLinks]::Create($targetFile, $sourceFile)
		}
		(Get-Item -LiteralPath $targetFile).LastWriteTimeUtc = $sourceTimestamp
	}
	[IO.File]::WriteAllText($stamp, [IO.Path]::GetFullPath($Source))
	return $true
}

$serverRoot = Join-Path $InstallRoot "servers"
$staged = 0
foreach ($directory in @(
	"llama.cpp-b10516-cuda-13.3",
	"stable-diffusion.cpp-master-829-0a565f2-cuda12",
	"whisper.cpp-b4938-cuda-12.4",
	"acestep.cpp-9761469-cuda-13",
	"sam-python-1-cuda-13"
)) {
	if (Mirror-HardLinks (Join-Path $serverRoot $directory) (Join-Path $Destination "servers\$directory")) {
		$staged++
	}
}

$whisperModel = Join-Path $InstallRoot "models\whisper.cpp\ggml-base-q5_1.bin"
if (Test-Path -LiteralPath $whisperModel -PathType Leaf) {
	$modelTarget = Join-Path $Destination "models\whisper.cpp\ggml-base-q5_1.bin"
	New-Item -ItemType Directory -Force -Path (Split-Path -Parent $modelTarget) | Out-Null
	if (-not (Test-Path -LiteralPath $modelTarget)) {
		New-Item -ItemType HardLink -Path $modelTarget -Target $whisperModel | Out-Null
	}
}

$llamaModel = Join-Path $InstallRoot "models\llama.cpp\qwen2.5-1.5b-instruct-q4_k_m.gguf"
if (Test-Path -LiteralPath $llamaModel -PathType Leaf) {
	$modelTarget = Join-Path $Destination "models\llama.cpp\qwen2.5-1.5b-instruct-q4_k_m.gguf"
	New-Item -ItemType Directory -Force -Path (Split-Path -Parent $modelTarget) | Out-Null
	if (-not (Test-Path -LiteralPath $modelTarget)) {
		New-Item -ItemType HardLink -Path $modelTarget -Target $llamaModel | Out-Null
	}
}

Write-Output "Staged $staged runtime directories at $Destination"
