param(
    [Parameter(Mandatory = $true)]
    [string]$QtQmakePath,

    [string]$BuildDir = "build",

    [ValidateSet("release", "debug", "profiling")]
    [string]$Config = "release",

    [ValidateSet("True", "False")]
    [string]$WithCrashReporting = "True",

    [ValidateSet("True", "False")]
    [string]$WithXerces = "True",

    [ValidateSet("True", "False")]
    [string]$WithICU = "True",

    [ValidateSet("True", "False")]
    [string]$WithICONV = "False",

    [ValidateSet("True", "False")]
    [string]$WithBigCodecs = "False",

    [ValidateSet("True", "False")]
    [string]$EnablePCH = "False",

    [int]$Jobs = 1,

    [string]$QbsProfile = "qt6",

    [string]$ToolchainProfile = "MSVC0-x64",

    [string]$ConanProfile = "share/ci/conan/profiles/windows",

    [switch]$RunTests
)

$ErrorActionPreference = "Stop"

function Require-Command {
    param([string]$Name)

    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command not found: $Name"
    }
}

function Invoke-Step {
    param(
        [string]$Title,
        [string[]]$Command
    )

    Write-Host ""
    Write-Host "==> $Title" -ForegroundColor Cyan
    Write-Host ($Command -join " ")
    if ($Command.Length -eq 1) {
        & $Command[0]
    } else {
        & $Command[0] $Command[1..($Command.Length - 1)]
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Step failed: $Title"
    }
}

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $repoRoot

if (-not (Test-Path $QtQmakePath)) {
    throw "Qt qmake.exe not found at: $QtQmakePath"
}

Require-Command "qbs"
Require-Command "qbs-setup-toolchains"
Require-Command "qbs-setup-qt"
Require-Command "qbs-config"
Require-Command "conan"

$resolvedBuildDir = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))
$installRoot = Join-Path $resolvedBuildDir "$Config\install-root\valentina"

$conanBuildType = "Release"
$conanRuntimeType = "Release"
if ($Config -eq "debug") {
    $conanBuildType = "Debug"
    $conanRuntimeType = "Debug"
} elseif ($Config -eq "profiling") {
    $conanBuildType = "RelWithDebInfo"
    $conanRuntimeType = "Release"
}

Invoke-Step -Title "Detecting toolchains" -Command @(
    "qbs-setup-toolchains",
    "--detect"
)

Invoke-Step -Title "Registering Qt profile" -Command @(
    "qbs-setup-qt",
    $QtQmakePath,
    $QbsProfile
)

Invoke-Step -Title "Setting default Qbs profile" -Command @(
    "qbs-config",
    "defaultProfile",
    $QbsProfile
)

Invoke-Step -Title "Setting Qbs base toolchain profile" -Command @(
    "qbs-config",
    "profiles.$QbsProfile.baseProfile",
    $ToolchainProfile
)

Invoke-Step -Title "Installing Conan dependencies" -Command @(
    "conan",
    "install",
    ".",
    "--build=missing",
    "-s", "build_type=$conanBuildType",
    "-s", "compiler.runtime_type=$conanRuntimeType",
    "-o", "&:with_crash_reporting=$WithCrashReporting",
    "-o", "&:with_xerces=$WithXerces",
    "-o", "&:with_icu=$WithICU",
    "-o", "&:with_iconv=$WithICONV",
    "-pr:a=$ConanProfile"
)

$commonQbsArgs = @(
    "--jobs", $Jobs,
    "-f", "valentina.qbs",
    "-d", $resolvedBuildDir,
    "config:$Config",
    "qbs.installRoot:$installRoot",
    "profile:$QbsProfile",
    "project.enableConan:true",
    "project.conanWithCrashReporting:$WithCrashReporting",
    "project.conanWithXerces:$WithXerces",
    "project.conanWithICU:$WithICU",
    "project.conanWithICONV:$WithICONV",
    "project.conanProfiles:$ConanProfile",
    "project.withICUCodecs:$WithICU",
    "project.withICONVCodecs:$WithICONV",
    "project.withBigCodecs:$WithBigCodecs",
    "modules.buildconfig.enablePCH:$EnablePCH",
    "modules.windeployqt.compilerRuntime:true",
    "modules.windeployqt.noCompilerRuntime:false"
)

$resolveArgs = @("qbs", "resolve") + $commonQbsArgs
$buildArgs = @("qbs", "build") + $commonQbsArgs

Invoke-Step -Title "Resolving Qbs project" -Command $resolveArgs

Invoke-Step -Title "Building Valentina" -Command $buildArgs

if ($RunTests) {
    $testArgs = @(
        "-p", "autotest-runner",
        "-f", "valentina.qbs",
        "-d", $resolvedBuildDir,
        "config:$Config",
        "qbs.installRoot:$installRoot",
        "profile:$QbsProfile",
        "project.enableConan:true",
        "project.conanWithCrashReporting:$WithCrashReporting",
        "project.conanWithXerces:$WithXerces",
        "project.conanWithICU:$WithICU",
        "project.conanWithICONV:$WithICONV",
        "project.conanProfiles:$ConanProfile",
        "project.withICUCodecs:$WithICU",
        "project.withICONVCodecs:$WithICONV",
        "project.withBigCodecs:$WithBigCodecs"
    )

    $runTestArgs = @("qbs") + $testArgs
    Invoke-Step -Title "Running tests" -Command $runTestArgs
}

Write-Host ""
Write-Host "Build completed." -ForegroundColor Green
Write-Host "Install root: $installRoot"
