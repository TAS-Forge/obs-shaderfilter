[CmdletBinding()]
param(
    [string]$BuildRoot = (Join-Path $PSScriptRoot '../work/windows-hdr'),
    [string]$Generator = 'Visual Studio 18 2026',
    [string]$Toolset = 'version=14.51,host=x64'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$obsRevision = 'ba2f32bdf791005443988a4955e963663e16b1ed' # peeled OBS 32.2.2 tag
$pluginRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
$obsSource = Join-Path $BuildRoot 'obs-studio'
$obsBuild = Join-Path $BuildRoot 'build'
$package = Join-Path $BuildRoot 'package'

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed ($LASTEXITCODE)" }
}

$cmakeOutput = & cmake --version
$cmakeExitCode = $LASTEXITCODE
$cmakeVersion = $cmakeOutput | Select-Object -First 1
if ($cmakeExitCode -ne 0 -or $cmakeVersion -ne 'cmake version 4.3.1') {
    throw "Expected CMake 4.3.1; found: $cmakeVersion"
}
if (-not (Test-Path -LiteralPath $obsSource)) {
    Invoke-Checked -Program git -Arguments @('clone', '--branch', '32.2.2', '--depth', '1',
        'https://github.com/obsproject/obs-studio.git', $obsSource)
}
$head = (& git -C $obsSource rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $head -ne $obsRevision) { throw "OBS revision mismatch: $head" }
$obsStatus = & git -C $obsSource status --porcelain --untracked-files=no
if ($LASTEXITCODE -ne 0 -or $obsStatus) { throw 'OBS source must have no tracked modifications' }

# Use OBS's own build system and hash-checked 32.2.2 dependency manifest.
# DEFER adds only this plugin after OBS defines its targets and helper functions.
$pluginCmakePath = $pluginRoot.Replace('\','/')
$binaryCmakePath = (Join-Path $obsBuild 'plugins/obs-shaderfilter').Replace('\','/')
$inject = Join-Path $BuildRoot 'add-shaderfilter.cmake'
@"
cmake_language(DEFER CALL add_subdirectory "$pluginCmakePath" "$binaryCmakePath")
"@ | Set-Content -LiteralPath $inject -Encoding utf8
Invoke-Checked -Program cmake -Arguments @('-S', $obsSource, '-B', $obsBuild, '-G', $Generator, '-A', 'x64', '-T', $Toolset,
    "-DCMAKE_PROJECT_obs-studio_INCLUDE=$inject", '-DENABLE_FRONTEND=OFF', '-DENABLE_PLUGINS=OFF',
    '-DENABLE_SCRIPTING=OFF', '-DENABLE_BROWSER=OFF', '-DENABLE_CCACHE=OFF')
Invoke-Checked -Program cmake -Arguments @('--build', $obsBuild, '--config', 'RelWithDebInfo', '--target', 'obs-shaderfilter', '--parallel')

$dlls = @(Get-ChildItem -LiteralPath $obsBuild -Recurse -File -Filter 'obs-shaderfilter.dll')
if ($dlls.Count -eq 0) { throw 'No built Shaderfilter DLL found' }
$dllHashes = @($dlls | Get-FileHash -Algorithm SHA256 | Select-Object -ExpandProperty Hash -Unique)
if ($dllHashes.Count -ne 1) { throw 'Ambiguous build outputs with different hashes' }
if (Test-Path -LiteralPath $package) { throw "Package directory already exists: $package. Use a fresh BuildRoot." }
New-Item -ItemType Directory -Path (Join-Path $package 'obs-plugins/64bit'), (Join-Path $package 'data/obs-plugins') -Force | Out-Null
Copy-Item -LiteralPath $dlls[0].FullName -Destination (Join-Path $package 'obs-plugins/64bit/obs-shaderfilter.dll')
Copy-Item -LiteralPath (Join-Path $pluginRoot 'data') -Destination (Join-Path $package 'data/obs-plugins/obs-shaderfilter') -Recurse
$manifest = [ordered]@{
    obs_revision = $head
    shaderfilter_base = '4b48d4f7c41d991903061c30e119e020cfe02de4'
    plugin_head = (& git -C $pluginRoot rev-parse HEAD)
    plugin_diff_sha256 = $null
    cmake = $cmakeVersion
    generator = $Generator
    requested_toolset = $Toolset
    signed = $false
    runtime_hdr_validated = $false
    dll_sha256 = $dllHashes[0]
    filter_source_sha256 = (Get-FileHash -LiteralPath (Join-Path $pluginRoot 'obs-shaderfilter.c') -Algorithm SHA256).Hash
    output_effect_sha256 = (Get-FileHash -LiteralPath (Join-Path $pluginRoot 'data/internal/render_output.effect') -Algorithm SHA256).Hash
    fisheye_shader_sha256 = (Get-FileHash -LiteralPath (Join-Path $pluginRoot 'data/examples/fisheye-xy.shader') -Algorithm SHA256).Hash
}
Invoke-Checked -Program git -Arguments @('-C', $pluginRoot, 'diff', '--binary', "--output=$(Join-Path $BuildRoot 'build-source.diff')")
$manifest.plugin_diff_sha256 = (Get-FileHash -LiteralPath (Join-Path $BuildRoot 'build-source.diff') -Algorithm SHA256).Hash
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $package 'build-manifest.json') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $obsBuild 'CMakeCache.txt') -Destination $package
Get-ChildItem -LiteralPath (Join-Path $obsBuild 'CMakeFiles') -Recurse -File -Filter 'CMakeCCompiler.cmake' |
    Copy-Item -Destination $package
Write-Output "Unsigned test package: $package"
Write-Output 'This package is not authorization or assurance that Windows Application Control will load it.'
