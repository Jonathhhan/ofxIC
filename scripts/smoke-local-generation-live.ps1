param(
	[string] $Executable = (Join-Path $PSScriptRoot "../ofxICExample/bin/ofxICExample.exe"),
	[ValidateSet("chat", "image")]
	[string[]] $Task = @("chat", "image"),
	[string] $ChatModel = "",
	[string] $ImageModel = "",
	[ValidateRange(10, 1800)]
	[int] $TimeoutSeconds = 300
)

$ErrorActionPreference = "Stop"
if ($env:OFXIC_RUN_LIVE_LOCAL_GENERATION -ne "1") {
	throw "Set OFXIC_RUN_LIVE_LOCAL_GENERATION=1 to allow real local model inference."
}
$executablePath = [IO.Path]::GetFullPath($Executable)
if (-not (Test-Path -LiteralPath $executablePath -PathType Leaf)) { throw "Build the example first." }
foreach ($model in @($ChatModel, $ImageModel)) {
	if ($model -and -not (Test-Path -LiteralPath $model -PathType Leaf)) { throw "Model not found: $model" }
}
$evidence = Join-Path ([IO.Path]::GetTempPath()) ("ofxIC-generation-live-" + [Guid]::NewGuid())
New-Item -ItemType Directory -Path $evidence | Out-Null
$previous = @{}
Get-ChildItem Env: | Where-Object Name -Like "OFXIC_*" | ForEach-Object { $previous[$_.Name] = $_.Value }
$failures = [Collections.Generic.List[string]]::new()

function Clear-TestEnvironment {
	Get-ChildItem Env: | Where-Object Name -Like "OFXIC_*" | ForEach-Object {
		[Environment]::SetEnvironmentVariable($_.Name, $null, "Process")
	}
}
function Test-Listening([int] $Port) {
	return [Net.NetworkInformation.IPGlobalProperties]::GetIPGlobalProperties().GetActiveTcpListeners().Port -contains $Port
}

try {
	foreach ($kind in $Task) {
		$example = $null
		$port = if ($kind -eq "chat") { 8080 } else { 8081 }
		try {
			if (Test-Listening $port) { throw "Port $port is occupied; refusing to reuse an external server." }
			Clear-TestEnvironment
			$env:OFXIC_SETTINGS_PATH = Join-Path $evidence "$kind.settings"
			$env:OFXIC_HISTORY_PATH = Join-Path $evidence "$kind.history"
			$env:OFXIC_ENDPOINT_URL = "http://127.0.0.1:8080"
			$env:OFXIC_RUNTIME_START_TIMEOUT_SECONDS = $TimeoutSeconds.ToString()
			$resultPath = Join-Path $evidence "$kind.result.txt"
			if ($kind -eq "chat") {
				$env:OFXIC_DOCUMENT_PATH = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../docs/ARCHITECTURE.md"))
				$env:OFXIC_CHAT_STREAM = "0"
				$env:OFXIC_CHAT_AUTORUN = "Search the loaded documents for why ofxIC keeps inference behind a process boundary. Answer using the search result and cite its source identifier."
				$env:OFXIC_GUI_RESULT_PATH = $resultPath
				if ($ChatModel) { $env:OFXIC_LLAMA_MODEL = [IO.Path]::GetFullPath($ChatModel) }
			} else {
				$env:OFXIC_MEDIA_BACKEND = "sdcpp"
				$env:OFXIC_MEDIA_ENDPOINT_URL = "http://127.0.0.1:8081"
				$env:OFXIC_MEDIA_AUTORUN = "image"
				$env:OFXIC_MEDIA_PROMPT = "A red ceramic teapot on a white table, studio photograph"
				$env:OFXIC_MEDIA_WIDTH = "512"
				$env:OFXIC_MEDIA_HEIGHT = "512"
				$env:OFXIC_MEDIA_SEED = "42"
				$env:OFXIC_MEDIA_RESULT_PATH = $resultPath
				$env:OFXIC_GUI_HEARTBEAT_PATH = Join-Path $evidence "image.heartbeat"
				if ($ImageModel) { $env:OFXIC_SD_MODEL = [IO.Path]::GetFullPath($ImageModel) }
			}
			Write-Output "[$kind] Starting example-owned model-backed task"
			$stdout = Join-Path $evidence "$kind.stdout.log"
			$stderr = Join-Path $evidence "$kind.stderr.log"
			$example = Start-Process -FilePath $executablePath -WindowStyle Hidden -PassThru `
				-WorkingDirectory (Split-Path $executablePath) `
				-RedirectStandardOutput $stdout -RedirectStandardError $stderr
			$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
			while (-not (Test-Path -LiteralPath $resultPath)) {
				if ($example.HasExited) { throw "Example exited before writing a result." }
				if ([DateTime]::UtcNow -ge $deadline) { throw "Generation timed out." }
				Start-Sleep -Milliseconds 100
			}
			$result = Get-Content -LiteralPath $resultPath -Raw
			if ($kind -eq "chat") {
				if ($result -notmatch "Inference completed with [2-9][0-9]* model request\(s\)" -or
					$result -notmatch "ARCHITECTURE\.md#chunk-") { throw "Grounded tool path failed: $result" }
			} else {
				$match = [regex]::Match($result, '(?m)^Saved: (.+\.png)\r?$')
				$console = Get-Content -LiteralPath $stdout -Raw
				if ($console -notmatch "SD options loaded automatically: [1-9][0-9]* samplers, [1-9][0-9]* schedulers") {
					throw "SD option lists were not populated automatically before generation."
				}
				if (-not $match.Success -or $result -notmatch "completed") { throw "Image task failed: $result" }
				$imagePath = $match.Groups[1].Value
				$bytes = [IO.File]::ReadAllBytes($imagePath)
				if ($bytes.Length -lt 24 -or [BitConverter]::ToString($bytes, 0, 8) -ne "89-50-4E-47-0D-0A-1A-0A") { throw "Invalid PNG output." }
				$width = [Net.IPAddress]::NetworkToHostOrder([BitConverter]::ToInt32($bytes, 16))
				$height = [Net.IPAddress]::NetworkToHostOrder([BitConverter]::ToInt32($bytes, 20))
				if ($width -ne 512 -or $height -ne 512) { throw "Unexpected dimensions: ${width}x${height}" }
				Write-Output "Image: $imagePath"
			}
			Write-Output "[$kind] Model-backed task passed"
		} catch {
			$failures.Add("$kind : $_")
			Write-Warning $failures[$failures.Count - 1]
		} finally {
			if ($example) {
				if (-not $example.HasExited) {
					$example.CloseMainWindow() | Out-Null
					if (-not $example.WaitForExit(10000)) {
						$failures.Add("$kind : example did not close gracefully")
						Stop-Process -Id $example.Id -Force
						$example.WaitForExit(5000) | Out-Null
					}
				}
				$example.Dispose()
				if (Test-Listening $port) { $failures.Add("$kind : owned server remained listening on $port") }
			}
		}
	}
} finally {
	Clear-TestEnvironment
	foreach ($name in $previous.Keys) { [Environment]::SetEnvironmentVariable($name, $previous[$name], "Process") }
	Write-Output "Evidence retained: $evidence"
}
if ($failures.Count) { throw ($failures -join [Environment]::NewLine) }
