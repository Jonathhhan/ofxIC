param(
	[string] $Executable = (Join-Path $PSScriptRoot "../ofxICExample/bin/ofxICExample.exe"),
	[switch] $Live
)
$ErrorActionPreference = "Stop"
$executablePath = [IO.Path]::GetFullPath($Executable)
$evidence = Join-Path ([IO.Path]::GetTempPath()) ("ofxIC-web-import-" + [Guid]::NewGuid())
New-Item -ItemType Directory -Path $evidence | Out-Null
$previous = @{}
Get-ChildItem Env: | Where-Object Name -Like "OFXIC_*" | ForEach-Object { $previous[$_.Name] = $_.Value }
function Clear-TestEnvironment {
	Get-ChildItem Env: | Where-Object Name -Like "OFXIC_*" | ForEach-Object {
		[Environment]::SetEnvironmentVariable($_.Name, $null, "Process")
	}
}
$cases = @("private")
if ($Live) { $cases += @("preview", "accept") }
try {
	foreach ($case in $cases) {
		Clear-TestEnvironment
		$env:OFXIC_SETTINGS_PATH = Join-Path $evidence "$case.settings"
		$env:OFXIC_HISTORY_PATH = Join-Path $evidence "$case.history"
		$env:OFXIC_WEB_IMPORT_AUTORUN = if ($case -eq "private") { "http://127.0.0.1:9/" } else { "https://example.com/?ofxic=import-test" }
		$env:OFXIC_WEB_IMPORT_ACCEPT = if ($case -eq "accept") { "1" } else { "0" }
		$resultPath = Join-Path $evidence "$case.result.txt"
		$env:OFXIC_WEB_IMPORT_RESULT_PATH = $resultPath
		$example = Start-Process -FilePath $executablePath -WindowStyle Hidden -PassThru `
			-WorkingDirectory (Split-Path $executablePath) `
			-RedirectStandardOutput (Join-Path $evidence "$case.stdout.log") `
			-RedirectStandardError (Join-Path $evidence "$case.stderr.log")
		try {
			$deadline = [DateTime]::UtcNow.AddSeconds(75)
			while (-not (Test-Path -LiteralPath $resultPath)) {
				if ($example.HasExited -or [DateTime]::UtcNow -ge $deadline) { throw "$case : no import result" }
				Start-Sleep -Milliseconds 100
			}
			$result = Get-Content -LiteralPath $resultPath -Raw
			$before = [int]([regex]::Match($result, '(?m)^documents_before=(\d+)').Groups[1].Value)
			$after = [int]([regex]::Match($result, '(?m)^documents=(\d+)').Groups[1].Value)
			if ($case -eq "private") {
				if ($result -notmatch "non-public address" -or $before -ne $after) { throw "Private URL was not rejected: $result" }
			} elseif ($case -eq "preview") {
				if ($result -notmatch "Preview ready" -or $result -notmatch 'preview_bytes=[1-9]' -or $before -ne $after) { throw "Preview changed document index or failed: $result" }
			} else {
				if ($result -notmatch "Webpage added" -or $after -ne ($before + 1)) { throw "Explicit acceptance failed: $result" }
			}
			Write-Output "$case : passed"
		} finally {
			if (-not $example.HasExited) {
				$example.CloseMainWindow() | Out-Null
				if (-not $example.WaitForExit(5000)) {
					Stop-Process -Id $example.Id -Force
					throw "Example did not close gracefully."
				}
			}
			$example.Dispose()
		}
	}
} finally {
	Clear-TestEnvironment
	foreach ($name in $previous.Keys) { [Environment]::SetEnvironmentVariable($name, $previous[$name], "Process") }
	Write-Output "Evidence retained: $evidence"
}
