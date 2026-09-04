param(
	[string] $Executable = (Join-Path $PSScriptRoot "..\ofxICExample\bin\ofxICExample.exe"),
	[string] $Endpoint = "http://127.0.0.1:8081",
	[int] $Width = 512,
	[int] $Height = 512,
	[int] $Frames = 17,
	[int] $Fps = 8,
	[int] $Seed = -1,
	[int] $Steps = 28,
	[double] $Guidance = 7.0,
	[string] $Sampler = "",
	[string] $Scheduler = "",
	[string] $OutputFormat = "",
	[int] $TimeoutSeconds = 1200,
	[int] $MinimumUniqueFrames = 2,
	[string] $Prompt = "A small paper sculpture slowly rotating on a clean studio background"
)

$ErrorActionPreference = "Stop"
$executablePath = [System.IO.Path]::GetFullPath($Executable)
$temporary = Join-Path ([System.IO.Path]::GetTempPath()) ("ofxIC-video-live-" + [guid]::NewGuid())
$resultPath = Join-Path $temporary "result.txt"

if ($env:OFXIC_RUN_LIVE_SDCPP_VIDEO -ne "1") {
	throw "Set OFXIC_RUN_LIVE_SDCPP_VIDEO=1 to allow the real model-backed video smoke"
}
if (-not (Test-Path -LiteralPath $executablePath)) {
	throw "Build the Release example first: $executablePath"
}
if ($Width -lt 64 -or $Height -lt 64 -or $Frames -lt 1 -or $Fps -lt 1) {
	throw "Width and height must be at least 64; frames and FPS must be positive"
}
if ($MinimumUniqueFrames -lt 2) {
	throw "MinimumUniqueFrames must be at least 2"
}
$ffprobe = Get-Command ffprobe.exe -ErrorAction SilentlyContinue
$ffmpeg = Get-Command ffmpeg.exe -ErrorAction SilentlyContinue
if (-not $ffprobe -or -not $ffmpeg) {
	throw "The live video smoke requires ffprobe.exe and ffmpeg.exe on PATH"
}

$capabilities = Invoke-RestMethod -Uri ($Endpoint.TrimEnd('/') + "/sdcpp/v1/capabilities") `
	-Method Get -TimeoutSec 10
if ($capabilities.supported_modes -notcontains "vid_gen") {
	$modelName = if ($capabilities.model.name) { $capabilities.model.name } else { "<unknown>" }
	throw "Loaded sd-server model '$modelName' does not advertise vid_gen"
}

$previous = @{}
$environmentNames = @(
	"OFXIC_ENDPOINT_URL",
	"OFXIC_MEDIA_BACKEND",
	"OFXIC_MEDIA_ENDPOINT_URL",
	"OFXIC_MEDIA_AUTORUN",
	"OFXIC_MEDIA_KIND",
	"OFXIC_MEDIA_PROMPT",
	"OFXIC_MEDIA_WIDTH",
	"OFXIC_MEDIA_HEIGHT",
	"OFXIC_MEDIA_FRAMES",
	"OFXIC_MEDIA_FPS",
	"OFXIC_MEDIA_SEED",
	"OFXIC_MEDIA_STEPS",
	"OFXIC_MEDIA_GUIDANCE",
	"OFXIC_MEDIA_SAMPLER",
	"OFXIC_MEDIA_SCHEDULER",
	"OFXIC_MEDIA_OUTPUT_FORMAT",
	"OFXIC_MEDIA_RESULT_PATH",
	"OFXIC_SETTINGS_PATH")
foreach ($name in $environmentNames) {
	$previous[$name] = [Environment]::GetEnvironmentVariable($name)
}

$example = $null
try {
	New-Item -ItemType Directory -Path $temporary | Out-Null
	$env:OFXIC_ENDPOINT_URL = "http://127.0.0.1:1"
	$env:OFXIC_MEDIA_BACKEND = "sdcpp"
	$env:OFXIC_MEDIA_ENDPOINT_URL = $Endpoint
	$env:OFXIC_MEDIA_AUTORUN = "video"
	$env:OFXIC_MEDIA_KIND = "video"
	$env:OFXIC_MEDIA_PROMPT = $Prompt
	$env:OFXIC_MEDIA_WIDTH = $Width.ToString()
	$env:OFXIC_MEDIA_HEIGHT = $Height.ToString()
	$env:OFXIC_MEDIA_FRAMES = $Frames.ToString()
	$env:OFXIC_MEDIA_FPS = $Fps.ToString()
	$env:OFXIC_MEDIA_SEED = $Seed.ToString()
	$env:OFXIC_MEDIA_STEPS = $Steps.ToString()
	$env:OFXIC_MEDIA_GUIDANCE = $Guidance.ToString([Globalization.CultureInfo]::InvariantCulture)
	$env:OFXIC_MEDIA_SAMPLER = $Sampler
	$env:OFXIC_MEDIA_SCHEDULER = $Scheduler
	$env:OFXIC_MEDIA_OUTPUT_FORMAT = $OutputFormat
	$env:OFXIC_MEDIA_RESULT_PATH = $resultPath
	$env:OFXIC_SETTINGS_PATH = (Join-Path $temporary "settings")

	$example = Start-Process -FilePath $executablePath `
		-WorkingDirectory (Split-Path $executablePath) -WindowStyle Hidden -PassThru
	$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
	do {
		Start-Sleep -Milliseconds 250
		if ($example.HasExited) { throw "Example exited before producing video evidence" }
	} while (-not (Test-Path -LiteralPath $resultPath) -and [DateTime]::UtcNow -lt $deadline)
	if (-not (Test-Path -LiteralPath $resultPath)) {
		throw "Video GUI did not produce evidence within $TimeoutSeconds seconds"
	}

	$result = Get-Content -Raw -LiteralPath $resultPath
	$pathMatch = [regex]::Match($result, '(?m)^Saved: (.+\.(?:webm|webp|avi))\r?$')
	if ($result -notmatch "completed" -or -not $pathMatch.Success) {
		throw "Unexpected video GUI evidence: $result"
	}
	$generatedPath = $pathMatch.Groups[1].Value
	if (-not (Test-Path -LiteralPath $generatedPath)) {
		throw "Video GUI reported a missing output file: $generatedPath"
	}
	if ((Get-Item -LiteralPath $generatedPath).Length -lt 16) {
		throw "Generated video file is unexpectedly small: $generatedPath"
	}
	$probeLines = & $ffprobe.Source -v error -count_frames -select_streams v:0 `
		-show_entries stream=avg_frame_rate,nb_read_frames,duration `
		-of default=noprint_wrappers=1 $generatedPath
	if ($LASTEXITCODE -ne 0) { throw "ffprobe could not decode generated video: $generatedPath" }
	$probe = @{}
	foreach ($line in $probeLines) {
		$parts = $line -split '=', 2
		if ($parts.Count -eq 2) { $probe[$parts[0]] = $parts[1] }
	}
	$decodedFrames = if ($probe.nb_read_frames -match '^\d+$') { [int]$probe.nb_read_frames } else { 0 }
	if ($decodedFrames -lt 2) {
		throw "Generated output is not an animation: ffprobe decoded $decodedFrames frame(s)"
	}
	$frameHashes = & $ffmpeg.Source -v error -i $generatedPath -map 0:v:0 -f framemd5 - |
		Where-Object { $_ -match '^0,' } |
		ForEach-Object { ($_ -split ',')[-1].Trim() }
	if ($LASTEXITCODE -ne 0) { throw "ffmpeg could not decode generated video: $generatedPath" }
	$uniqueFrames = @($frameHashes | Sort-Object -Unique).Count
	if ($uniqueFrames -lt $MinimumUniqueFrames) {
		throw "Generated output contains $decodedFrames frames but only $uniqueFrames unique frame(s)"
	}
	Write-Output "Video GUI live smoke passed"
	Write-Output "Model: $($capabilities.model.name)"
	Write-Output "Decoded frames: $decodedFrames; unique frames: $uniqueFrames; rate: $($probe.avg_frame_rate); duration: $($probe.duration) s"
	Write-Output "Output: $generatedPath"
} finally {
	if ($example -and -not $example.HasExited) {
		$example.CloseMainWindow() | Out-Null
		if (-not $example.WaitForExit(3000)) {
			Stop-Process -Id $example.Id -Force -ErrorAction SilentlyContinue
		}
	}
	foreach ($name in $environmentNames) {
		[Environment]::SetEnvironmentVariable($name, $previous[$name])
	}
	if (Test-Path -LiteralPath $temporary) {
		Remove-Item -LiteralPath $temporary -Recurse -Force
	}
}
