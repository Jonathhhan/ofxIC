$ErrorActionPreference = "Stop"
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ("ofxic-sam-installer-" + [Guid]::NewGuid())
$runtime = Join-Path $fixtureRoot "servers/sam-python-1-cuda-13"
$installer = Join-Path $PSScriptRoot "../scripts/install-sam-server.ps1"
try {
	New-Item -ItemType Directory -Path (Join-Path $runtime ".venv/Scripts") -Force | Out-Null
	New-Item -ItemType File -Path (Join-Path $runtime ".venv/Scripts/python.exe") | Out-Null
	New-Item -ItemType File -Path (Join-Path $runtime "sam-python-runner.py") | Out-Null
	$model = Join-Path $fixtureRoot "selected.pth"
	New-Item -ItemType File -Path $model | Out-Null
	& $installer -InstallRoot (Join-Path $fixtureRoot "servers") -ExistingModel $model
	$metadataPath = Join-Path $runtime "ofxIC-install.json"
	$metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
	if ($metadata.model -ne $model) { throw "Existing runtime ignored ExistingModel." }
	$models = Join-Path $fixtureRoot "models"
	New-Item -ItemType Directory -Path $models | Out-Null
	$defaultModel = Join-Path $models "sam_vit_b_01ec64.pth"
	New-Item -ItemType File -Path $defaultModel | Out-Null
	& $installer -InstallRoot (Join-Path $fixtureRoot "servers") -ModelRoot $models -DownloadModel
	$metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
	if ($metadata.model -ne $defaultModel) { throw "Existing runtime ignored DownloadModel." }
	$rejected = $false
	try { & $installer -InstallRoot (Join-Path $fixtureRoot "servers") -ExistingModel (Join-Path $fixtureRoot "missing.pth") }
	catch { $rejected = $true }
	if (-not $rejected) { throw "Missing checkpoint was accepted." }
	if (-not (Test-Path -LiteralPath (Join-Path $runtime ".venv/Scripts/python.exe"))) { throw "Model failure removed existing runtime." }
	Write-Output "SAM installer model selection tests passed."
} finally {
	$resolvedFixture = [IO.Path]::GetFullPath($fixtureRoot)
	$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
	if (-not $resolvedFixture.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase)) { throw "Unsafe fixture cleanup path." }
	if (Test-Path -LiteralPath $resolvedFixture) { Remove-Item -LiteralPath $resolvedFixture -Recurse -Force }
}
