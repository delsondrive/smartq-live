<#
.SYNOPSIS
  Configure and build Smart Q Live with the Visual Studio developer shell, optionally running the tests.
.PARAMETER Target
  CMake target to build. Defaults to sqlive_tests when -Test is given, otherwise everything.
.PARAMETER Test
  Catch2 filter (for example "[smoke]"). Runs build/tests/Release/sqlive_tests.exe with it.
#>
param(
    [string]$Target = "",
    [string]$Test = ""
)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found at $vswhere" }
$vsPath = & $vswhere -latest -products * -property installationPath
if (-not $vsPath) { throw "No Visual Studio or Build Tools installation found" }

& (Join-Path $vsPath "Common7\Tools\Launch-VsDevShell.ps1") -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null

Push-Location $repo
try {
    $extra = @()
    # Optional: reuse an existing JUCE or Catch2 checkout instead of downloading (set in your own environment, never committed).
    if ($env:SQLIVE_JUCE_SRC) { $extra += "-DFETCHCONTENT_SOURCE_DIR_JUCE=$env:SQLIVE_JUCE_SRC" }
    if ($env:SQLIVE_CATCH2_SRC) { $extra += "-DFETCHCONTENT_SOURCE_DIR_CATCH2=$env:SQLIVE_CATCH2_SRC" }
    cmake --preset default @extra
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)" }

    if (-not $Target) { $Target = if ($Test) { "sqlive_tests" } else { "ALL_BUILD" } }
    cmake --build build --config Release --target $Target --parallel
    if ($LASTEXITCODE -ne 0) { throw "CMake build failed ($LASTEXITCODE)" }

    if ($Test) {
        $exe = Join-Path $repo "build\tests\Release\sqlive_tests.exe"
        if (-not (Test-Path $exe)) { throw "Test executable not found: $exe (build with -Target sqlive_tests)" }
        & $exe $Test
        if ($LASTEXITCODE -ne 0) { throw "Tests failed ($LASTEXITCODE)" }
    }
}
finally {
    Pop-Location
}
