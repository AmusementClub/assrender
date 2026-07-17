[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("static", "shared")]
    [string] $LibraryType,

    [Parameter(Mandatory = $true)]
    [string] $InstallPrefix,

    [string] $WorkingDirectory = (Get-Location).Path,

    [string] $DependencyPrefix,

    [string] $PackageOutputDirectory,

    [ValidateSet("x64")]
    [string] $Architecture = "x64"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$versionsFile = Join-Path $repositoryRoot ".github/dependency-versions.env"

foreach ($line in Get-Content -LiteralPath $versionsFile) {
    if ($line -match '^([A-Z][A-Z0-9_]*)=(.+)$') {
        [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], "Process")
    }
}

$requiredVersions = @(
    "MSVC_ABI_VERSION",
    "MSVC_RUNTIME",
    "NASM_MIN_VERSION",
    "MESON_VERSION",
    "FREETYPE_VERSION",
    "HARFBUZZ_VERSION",
    "FRIBIDI_VERSION",
    "LIBPNG_VERSION",
    "ZLIB_VERSION",
    "LIBASS_VERSION"
)

foreach ($name in $requiredVersions) {
    if ([string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($name))) {
        throw "Missing $name in $versionsFile"
    }
}

if (-not [string]::IsNullOrWhiteSpace($PackageOutputDirectory) -and $LibraryType -ne "shared") {
    throw "Packaging is only supported for shared libass builds."
}
function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Command,

        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]] $Arguments
    )

    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Command failed with exit code $LASTEXITCODE"
    }
}

function Initialize-GitSource {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Repository,

        [Parameter(Mandatory = $true)]
        [string] $Revision,

        [Parameter(Mandatory = $true)]
        [string] $Destination
    )

    if (Test-Path -LiteralPath (Join-Path $Destination ".git")) {
        $currentRevision = (& git -C $Destination describe --tags --exact-match 2>$null)
        if ($LASTEXITCODE -ne 0 -or $currentRevision -ne $Revision) {
            throw "Existing checkout at $Destination is not revision $Revision."
        }
        return
    }

    if (Test-Path -LiteralPath $Destination) {
        throw "Source destination exists but is not a Git checkout: $Destination"
    }

    $cloneArguments = @(
        "-c",
        "core.longpaths=true",
        "clone",
        "--quiet",
        "--depth=1",
        "--branch",
        $Revision,
        $Repository,
        $Destination
    )
    Invoke-CheckedCommand git @cloneArguments
}

function Assert-WrapVersion {
    param(
        [Parameter(Mandatory = $true)]
        [string] $WrapFile,

        [Parameter(Mandatory = $true)]
        [string] $DirectoryName
    )

    $content = Get-Content -LiteralPath $WrapFile -Raw
    $expected = "(?m)^directory\s*=\s*$([Regex]::Escape($DirectoryName))\s*$"
    if ($content -notmatch $expected) {
        throw "$WrapFile does not describe $DirectoryName. Update it together with dependency-versions.env."
    }
}

$mesonVersion = (& meson --version).Trim()
if ($LASTEXITCODE -ne 0 -or $mesonVersion -ne $env:MESON_VERSION) {
    throw "Expected Meson $env:MESON_VERSION; found '$mesonVersion'."
}

$nasmCommand = Get-Command nasm -CommandType Application -ErrorAction SilentlyContinue
if ($null -eq $nasmCommand) {
    throw "NASM >= $env:NASM_MIN_VERSION is required but was not found in PATH."
}
$nasmVersionOutput = (& $nasmCommand.Source -v | Out-String).Trim()
if ($LASTEXITCODE -ne 0) {
    throw "Failed to query NASM at '$($nasmCommand.Source)'."
}
$nasmVersionMatch = [Regex]::Match($nasmVersionOutput, '(?i)\bNASM version\s+(?<version>\d+(?:\.\d+){1,2})\b')
if (-not $nasmVersionMatch.Success) {
    throw "Could not parse NASM version from '$nasmVersionOutput'."
}
$nasmVersionText = $nasmVersionMatch.Groups['version'].Value
$nasmVersion = [Version]::Parse($nasmVersionText)
$minimumNasmVersion = [Version]::Parse($env:NASM_MIN_VERSION)
if ($nasmVersion -lt $minimumNasmVersion) {
    throw "NASM >= $env:NASM_MIN_VERSION is required; found $nasmVersionText at '$($nasmCommand.Source)'."
}
Write-Output "Using NASM $nasmVersionText from '$($nasmCommand.Source)'."

if ($env:VCToolsVersion -notmatch '^(\d+)\.') {
    throw "VCToolsVersion is unavailable; initialize the MSVC build environment first."
}
if ($Matches[1] -ne $env:MSVC_ABI_VERSION) {
    throw "Expected MSVC ABI family $env:MSVC_ABI_VERSION; found toolset $env:VCToolsVersion."
}
if ($env:MSVC_RUNTIME -ne "MT") {
    throw "Unsupported MSVC runtime '$env:MSVC_RUNTIME'; expected MT."
}

$workingRoot = [System.IO.Path]::GetFullPath($WorkingDirectory)
$installRoot = [System.IO.Path]::GetFullPath($InstallPrefix)
$dependencyRoot = if ([string]::IsNullOrWhiteSpace($DependencyPrefix)) {
    $null
} else {
    [System.IO.Path]::GetFullPath($DependencyPrefix)
}
$sourceRoot = Join-Path $workingRoot "libass-$env:LIBASS_VERSION"
$subprojectsRoot = Join-Path $sourceRoot "subprojects"
$buildRoot = Join-Path $sourceRoot "build-$LibraryType"
$freetypeTag = "VER-$($env:FREETYPE_VERSION.Replace('.', '-'))"
$ltoEnabled = if ($LibraryType -eq "shared") { "true" } else { "false" }

New-Item -ItemType Directory -Force -Path $workingRoot | Out-Null
Initialize-GitSource https://github.com/libass/libass.git $env:LIBASS_VERSION $sourceRoot

if ($null -eq $dependencyRoot) {
    New-Item -ItemType Directory -Force -Path $subprojectsRoot | Out-Null
    Initialize-GitSource https://github.com/freetype/freetype.git $freetypeTag (Join-Path $subprojectsRoot "freetype2")
    Assert-WrapVersion (Join-Path $subprojectsRoot "freetype2/subprojects/libpng.wrap") "libpng-$env:LIBPNG_VERSION"
    Assert-WrapVersion (Join-Path $subprojectsRoot "freetype2/subprojects/zlib.wrap") "zlib-$env:ZLIB_VERSION"
    Initialize-GitSource https://github.com/harfbuzz/harfbuzz.git $env:HARFBUZZ_VERSION (Join-Path $subprojectsRoot "harfbuzz")
    Initialize-GitSource https://github.com/fribidi/fribidi.git "v$env:FRIBIDI_VERSION" (Join-Path $subprojectsRoot "fribidi")
} else {
    $dependencyPkgConfig = Join-Path $dependencyRoot "lib/pkgconfig"
    if (-not (Test-Path -LiteralPath (Join-Path $dependencyPkgConfig "freetype2.pc"))) {
        throw "Prebuilt dependency package is incomplete: $dependencyRoot"
    }
    [Environment]::SetEnvironmentVariable("PKG_CONFIG_PATH", $dependencyPkgConfig, "Process")
}

$setupArguments = @(
    "setup",
    $buildRoot,
    $sourceRoot,
    "--prefix=$installRoot",
    "--buildtype=release",
    "--default-library=$LibraryType",
    "-Dc_std=c11",
    "-Db_lto=$ltoEnabled",
    "-Db_ndebug=true",
    "-Db_vscrt=static_from_buildtype",
    "-Dasm=enabled",
    "-Dlarge-tiles=false",
    "-Dfontconfig=disabled",
    "-Ddirectwrite=enabled",
    "-Dlibunibreak=disabled",
    "-Dtest=disabled",
    "-Dcompare=disabled",
    "-Dprofile=disabled",
    "-Dfuzz=disabled",
    "-Dcheckasm=disabled"
)

if ($null -eq $dependencyRoot) {
    $setupArguments += @(
    "--wrap-mode=forcefallback",
    "-Dfreetype2:default_library=static",
    "-Dfreetype2:b_lto=false",
    "-Dfreetype2:harfbuzz=disabled",
    "-Dfreetype2:brotli=disabled",
    "-Dfreetype2:bzip2=disabled",
    "-Dfreetype2:png=enabled",
    "-Dfreetype2:zlib=enabled",
    "-Dfribidi:default_library=static",
    "-Dfribidi:b_lto=false",
    "-Dfribidi:bin=false",
    "-Dfribidi:docs=false",
    "-Dfribidi:tests=false",
    "-Dharfbuzz:default_library=static",
    "-Dharfbuzz:b_lto=false",
    "-Dharfbuzz:tests=disabled",
    "-Dharfbuzz:introspection=disabled",
    "-Dharfbuzz:docs=disabled",
    "-Dharfbuzz:utilities=disabled",
    "-Dharfbuzz:cairo=disabled",
    "-Dharfbuzz:chafa=disabled",
    "-Dharfbuzz:glib=disabled",
    "-Dharfbuzz:gobject=disabled",
    "-Dharfbuzz:icu=disabled",
    "-Dharfbuzz:freetype=disabled",
    "-Dharfbuzz:png=disabled",
    "-Dharfbuzz:zlib=disabled",
    "-Dharfbuzz:subset=disabled",
    "-Dharfbuzz:raster=disabled",
    "-Dharfbuzz:vector=disabled",
    "-Dharfbuzz:gpu=disabled",
    "-Dzlib:default_library=static",
    "-Dzlib:b_lto=false",
    "-Dlibpng:default_library=static",
    "-Dlibpng:b_lto=false"
    )
} else {
    $setupArguments += "--wrap-mode=nofallback"
}

if (Test-Path -LiteralPath (Join-Path $buildRoot "meson-private")) {
    $setupArguments = @("setup", "--wipe") + $setupArguments[1..($setupArguments.Count - 1)]
}

Invoke-CheckedCommand meson @setupArguments
$compileArguments = @("compile", "-C", $buildRoot)
$installArguments = @("install", "-C", $buildRoot)
Invoke-CheckedCommand meson @compileArguments
Invoke-CheckedCommand meson @installArguments

$pkgConfigRoot = Join-Path $installRoot "lib/pkgconfig"
if (Test-Path -LiteralPath $pkgConfigRoot) {
    foreach ($pcFile in Get-ChildItem -LiteralPath $pkgConfigRoot -Filter "*.pc") {
        $content = Get-Content -LiteralPath $pcFile.FullName -Raw
        $content = [Regex]::Replace($content, '(?m)^prefix=.*$', 'prefix=${pcfiledir}/../..')
        Set-Content -LiteralPath $pcFile.FullName -Value $content -Encoding utf8NoBOM -NoNewline
    }
}

$installedLicenses = Join-Path $installRoot "LICENSES"
New-Item -ItemType Directory -Force -Path $installedLicenses | Out-Null
Copy-Item -LiteralPath (Join-Path $sourceRoot "COPYING") -Destination (Join-Path $installedLicenses "libass.txt") -Force
if ($null -eq $dependencyRoot) {
    Copy-Item -LiteralPath (Join-Path $sourceRoot "subprojects/freetype2/LICENSE.TXT") -Destination (Join-Path $installedLicenses "freetype.txt") -Force
    Copy-Item -LiteralPath (Join-Path $sourceRoot "subprojects/harfbuzz/COPYING") -Destination (Join-Path $installedLicenses "harfbuzz.txt") -Force
    Copy-Item -LiteralPath (Join-Path $sourceRoot "subprojects/fribidi/COPYING") -Destination (Join-Path $installedLicenses "fribidi.txt") -Force
    Copy-Item -LiteralPath (Join-Path $sourceRoot "subprojects/libpng-$env:LIBPNG_VERSION/LICENSE") -Destination (Join-Path $installedLicenses "libpng.txt") -Force
    Copy-Item -LiteralPath (Join-Path $sourceRoot "subprojects/zlib-$env:ZLIB_VERSION/LICENSE") -Destination (Join-Path $installedLicenses "zlib.txt") -Force
}

if ([string]::IsNullOrWhiteSpace($PackageOutputDirectory)) {
    return
}

$outputRoot = [System.IO.Path]::GetFullPath($PackageOutputDirectory)
$packageName = "libass-$env:LIBASS_VERSION-windows-$Architecture"
$packageRoot = Join-Path $outputRoot $packageName
$archivePath = Join-Path $outputRoot "$packageName.zip"

if (Test-Path -LiteralPath $packageRoot) {
    Remove-Item -LiteralPath $packageRoot -Recurse -Force
}
if (Test-Path -LiteralPath $archivePath) {
    Remove-Item -LiteralPath $archivePath -Force
}

New-Item -ItemType Directory -Force -Path (Join-Path $packageRoot "include/ass") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $packageRoot "bin") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $packageRoot "LICENSES") | Out-Null

Copy-Item -LiteralPath (Join-Path $installRoot "include/ass/ass.h") -Destination (Join-Path $packageRoot "include/ass/ass.h")
Copy-Item -LiteralPath (Join-Path $installRoot "include/ass/ass_types.h") -Destination (Join-Path $packageRoot "include/ass/ass_types.h")
$installedDlls = @(Get-ChildItem -LiteralPath (Join-Path $installRoot "bin") -Filter "ass*.dll")
if ($installedDlls.Count -ne 1) {
    throw "Expected exactly one installed libass DLL; found $($installedDlls.Count)."
}
Copy-Item -LiteralPath $installedDlls[0].FullName -Destination (Join-Path $packageRoot "bin/ass.dll")

Copy-Item -LiteralPath (Join-Path $installedLicenses "libass.txt") -Destination (Join-Path $packageRoot "LICENSES/libass.txt")
$dependencyLicenses = if ($null -eq $dependencyRoot) { $installedLicenses } else { Join-Path $dependencyRoot "LICENSES" }
Get-ChildItem -LiteralPath $dependencyLicenses -Filter "*.txt" |
    Where-Object Name -ne "libass.txt" |
    ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $packageRoot "LICENSES")
    }

@(
    "libass=$env:LIBASS_VERSION",
    "freetype=$env:FREETYPE_VERSION",
    "harfbuzz=$env:HARFBUZZ_VERSION",
    "fribidi=$env:FRIBIDI_VERSION",
    "libpng=$env:LIBPNG_VERSION",
    "zlib=$env:ZLIB_VERSION",
    "meson=$env:MESON_VERSION",
    "nasm=$nasmVersionText",
    "msvc_abi=$env:MSVC_ABI_VERSION",
    "msvc_toolset=$env:VCToolsVersion",
    "msvc_runtime=$env:MSVC_RUNTIME",
    "windows_sdk=$env:WindowsSDKVersion",
    "architecture=$Architecture",
    "configuration=release,libass-lto=$ltoEnabled,dependency-lto=false,nasm,directwrite,static-runtime,static-dependencies"
) | Set-Content -LiteralPath (Join-Path $packageRoot "VERSIONS.txt") -Encoding utf8

$dllPath = Join-Path $packageRoot "bin/ass.dll"
$handle = [Runtime.InteropServices.NativeLibrary]::Load($dllPath)
try {
    foreach ($export in @("ass_library_init", "ass_renderer_init", "ass_render_frame")) {
        $address = [IntPtr]::Zero
        if (-not [Runtime.InteropServices.NativeLibrary]::TryGetExport($handle, $export, [ref] $address)) {
            throw "Missing required export '$export' in $dllPath"
        }
    }
}
finally {
    [Runtime.InteropServices.NativeLibrary]::Free($handle)
}

$dependencies = (& dumpbin /nologo /dependents $dllPath | Out-String)
if ($LASTEXITCODE -ne 0) {
    throw "dumpbin failed while checking $dllPath"
}

$forbiddenDependencies = "freetype|harfbuzz|fribidi|libpng|zlib|libgcc|libstdc\+\+|msvcp|vcruntime|ucrtbase|api-ms-win-crt"
if ($dependencies -match $forbiddenDependencies) {
    throw "ass.dll has a forbidden dynamic dependency:`n$dependencies"
}

$packagedDlls = @(Get-ChildItem -LiteralPath $packageRoot -Recurse -Filter "*.dll")
if ($packagedDlls.Count -ne 1) {
    throw "The package must contain exactly one DLL; found $($packagedDlls.Count)."
}

Compress-Archive -Path (Join-Path $packageRoot "*") -DestinationPath $archivePath -CompressionLevel Optimal
Write-Output $archivePath

if (-not [string]::IsNullOrWhiteSpace($env:GITHUB_OUTPUT)) {
    "asset_path=$archivePath" | Add-Content -LiteralPath $env:GITHUB_OUTPUT
    "asset_name=$packageName.zip" | Add-Content -LiteralPath $env:GITHUB_OUTPUT
}
