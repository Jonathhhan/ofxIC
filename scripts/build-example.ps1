param(
	[ValidatePattern('^[A-Za-z0-9._-]+$')]
	[string] $Example = "ofxICExample",
	[ValidateSet("Debug", "Release")]
	[string] $Configuration = "Release",
	[ValidateSet("x64")]
	[string] $Platform = "x64",
	[string] $MsBuild = "",
	[string] $ProjectGenerator = "",
	[switch] $Incremental,
	[switch] $UpdateProject,
	[switch] $Clean
)

$ErrorActionPreference = "Stop"
$repository = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$openFrameworksRoot = [System.IO.Path]::GetFullPath((Join-Path $repository "..\.."))
$exampleDirectory = Join-Path $repository $Example
$solution = Join-Path $exampleDirectory ($Example + ".sln")
$executable = Join-Path $exampleDirectory ("bin\" + $Example + ".exe")

function Resolve-ProjectGenerator {
	if ($ProjectGenerator) {
		$resolved = [System.IO.Path]::GetFullPath($ProjectGenerator)
		if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
			throw "ProjectGenerator was not found: $resolved"
		}
		return $resolved
	}
	$candidates = @(
		(Join-Path $openFrameworksRoot "projectGenerator\resources\app\app\projectGenerator.exe"),
		(Join-Path $openFrameworksRoot "projectGenerator\projectGenerator.exe"))
	foreach ($candidate in $candidates) {
		if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
	}
	throw "openFrameworks projectGenerator was not found below $openFrameworksRoot"
}

function Resolve-MsBuild {
	if ($MsBuild) {
		$resolved = [System.IO.Path]::GetFullPath($MsBuild)
		if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
			throw "MSBuild was not found: $resolved"
		}
		return $resolved
	}
	$command = Get-Command "MSBuild.exe" -ErrorAction SilentlyContinue
	if ($command) { return $command.Source }
	$programFilesX86 = [Environment]::GetFolderPath(
		[Environment+SpecialFolder]::ProgramFilesX86)
	$vswhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
	if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
		$candidate = & $vswhere -latest -products "*" -requires Microsoft.Component.MSBuild `
			-find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1
		if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
			return $candidate
		}
	}
	$programFiles = [Environment]::GetFolderPath(
		[Environment+SpecialFolder]::ProgramFiles)
	$visualStudioRoot = Join-Path $programFiles "Microsoft Visual Studio"
	if (Test-Path -LiteralPath $visualStudioRoot -PathType Container) {
		$candidate = Get-ChildItem -LiteralPath $visualStudioRoot -Recurse `
			-Filter "MSBuild.exe" -ErrorAction SilentlyContinue |
			Where-Object { $_.FullName -match '\\MSBuild\\Current\\Bin\\MSBuild\.exe$' } |
			Sort-Object FullName -Descending | Select-Object -First 1
		if ($candidate) { return $candidate.FullName }
	}
	throw "MSBuild was not found. Install Visual Studio with Desktop development with C++."
}

function Get-CompatibleRelativePath([string] $BaseDirectory, [string] $TargetPath) {
	$base = [System.IO.Path]::GetFullPath($BaseDirectory)
	if (-not $base.EndsWith([System.IO.Path]::DirectorySeparatorChar.ToString())) {
		$base += [System.IO.Path]::DirectorySeparatorChar
	}
	$baseUri = New-Object System.Uri($base)
	$targetUri = New-Object System.Uri([System.IO.Path]::GetFullPath($TargetPath))
	return [System.Uri]::UnescapeDataString($baseUri.MakeRelativeUri($targetUri).ToString()).Replace('/', '\')
}

if (-not (Test-Path -LiteralPath (Join-Path $openFrameworksRoot "libs") -PathType Container)) {
	throw "The addon is not inside a complete openFrameworks checkout: $openFrameworksRoot"
}
if (-not (Test-Path -LiteralPath (Join-Path $exampleDirectory "addons.make") -PathType Leaf)) {
	throw "Example addons.make is missing: $exampleDirectory"
}

if ($UpdateProject) {
	$generator = Resolve-ProjectGenerator
	Write-Output "Updating generated project with $generator"
	& $generator ("-o" + $openFrameworksRoot) "-pwinvs" $exampleDirectory
	if ($LASTEXITCODE -ne 0) {
		throw "projectGenerator failed with exit code $LASTEXITCODE"
	}
}
if (-not (Test-Path -LiteralPath $solution -PathType Leaf)) {
	throw "Generated solution is missing: $solution"
}
$project = Join-Path $exampleDirectory ($Example + ".vcxproj")
if (-not (Test-Path -LiteralPath $project -PathType Leaf)) {
	throw "Generated Visual Studio project is missing: $project"
}
$projectText = [System.IO.File]::ReadAllText($project).Replace('/', '\')
$missingSources = @(
	Get-ChildItem -LiteralPath (Join-Path $exampleDirectory "src"),
		(Join-Path $repository "src") -Recurse -File -Filter "*.cpp" |
		ForEach-Object {
			$relative = Get-CompatibleRelativePath $exampleDirectory $_.FullName
			if (-not $projectText.Contains('Include="' + $relative + '"')) { $relative }
		})
if ($missingSources.Count -gt 0) {
	throw ("Generated project is stale and omits source files: " +
		($missingSources -join ", ") + ". Run this script with -UpdateProject after " +
		"repairing the local openFrameworks Project Generator.")
}
Write-Output "Verified generated project source membership"

$runningExamples = @(
	Get-CimInstance Win32_Process -Filter ("Name = '" + $Example + ".exe'") `
		-ErrorAction SilentlyContinue |
		Where-Object {
			$_.ExecutablePath -and
			[System.IO.Path]::GetFullPath($_.ExecutablePath).Equals(
				[System.IO.Path]::GetFullPath($executable),
				[System.StringComparison]::OrdinalIgnoreCase)
		})
if ($runningExamples.Count -gt 0) {
	$pids = ($runningExamples | ForEach-Object { $_.ProcessId }) -join ", "
	throw "Close the running $Example before rebuilding (PID: $pids)."
}

$builder = Resolve-MsBuild
$target = if ($Incremental -and -not $Clean) { "Build" } else { "Rebuild" }
$started = [DateTime]::UtcNow
Write-Output "$target $Configuration|$Platform with $builder"
& $builder $solution ("/t:" + $target) ("/p:Configuration=" + $Configuration) `
	("/p:Platform=" + $Platform) "/m" "/v:minimal"
if ($LASTEXITCODE -ne 0) {
	throw "Example $target failed with exit code $LASTEXITCODE"
}
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
	throw "Build succeeded but the expected executable is missing: $executable"
}
$artifact = Get-Item -LiteralPath $executable
if ($target -eq "Rebuild" -and $artifact.LastWriteTimeUtc -lt $started.AddSeconds(-2)) {
	throw "Rebuild did not refresh the example executable timestamp"
}
Write-Output ("Built: " + $artifact.FullName)
Write-Output ("Timestamp: " + $artifact.LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss"))
Write-Output ("Bytes: " + $artifact.Length)
if ($Example -eq "ofxICExample") {
	& (Join-Path $PSScriptRoot "stage-installed-runtimes.ps1")
	if ($LASTEXITCODE -ne 0) { throw "Runtime staging failed with exit code $LASTEXITCODE" }
}
