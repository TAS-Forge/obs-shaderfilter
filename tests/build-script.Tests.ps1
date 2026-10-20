# Argument and packaging smoke test with fake git/cmake. No compiler or DLL execution.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$testRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../work/build-script-test-$([guid]::NewGuid())"))
New-Item -ItemType Directory -Path (Join-Path $testRoot 'obs-studio') -Force | Out-Null
$global:shaderfilterSmokeConfigureSeen = $false
$global:shaderfilterSmokeBuildSeen = $false
Remove-Variable -Name LASTEXITCODE -Scope Global -ErrorAction SilentlyContinue
function git {
    $global:LASTEXITCODE = 0
    if ($args -contains 'rev-parse') { return 'ba2f32bdf791005443988a4955e963663e16b1ed' }
    if ($args -contains 'status') { return }
    if ($args -contains 'diff') {
        $out = @($args | Where-Object { $_ -like '--output=*' })
        if ($out.Count -ne 1) { throw 'Missing diff output argument' }
        'fake diff for packaging test' | Set-Content -LiteralPath $out[0].Substring(9)
        return
    }
    throw "Unexpected git arguments: $args"
}
function cmake {
    if ($args.Count -eq 1 -and $args[0] -eq '--version') {
        Write-Output 'cmake version 4.3.1'
        Write-Output ''
        Write-Output 'CMake suite maintained and supported by Kitware.'
        $global:LASTEXITCODE = 0
        return
    }
    $global:LASTEXITCODE = 0
    if ($args[0] -eq '-S') {
        foreach ($expected in @('-B','-G','Visual Studio 18 2026','-A','x64','-T','version=14.51,host=x64',
                                '-DENABLE_FRONTEND=OFF','-DENABLE_PLUGINS=OFF','-DENABLE_BROWSER=OFF')) {
            if ($args -notcontains $expected) { throw "Lost configure argument: $expected" }
        }
        $global:shaderfilterSmokeConfigureSeen = $true
        $build = $args[([array]::IndexOf($args,'-B')+1)]
        New-Item -ItemType Directory -Path (Join-Path $build 'CMakeFiles/mock') -Force | Out-Null
        'mock cache' | Set-Content -LiteralPath (Join-Path $build 'CMakeCache.txt')
        'mock compiler' | Set-Content -LiteralPath (Join-Path $build 'CMakeFiles/mock/CMakeCCompiler.cmake')
        return
    }
    if ($args[0] -eq '--build') {
        if ($args -notcontains 'obs-shaderfilter' -or $args -notcontains 'RelWithDebInfo') { throw 'Lost build arguments' }
        $global:shaderfilterSmokeBuildSeen = $true
        'This is a packaging fixture, not a DLL' | Set-Content -LiteralPath (Join-Path $args[1] 'obs-shaderfilter.dll')
        return
    }
    throw "Unexpected cmake arguments: $args"
}
& (Join-Path $PSScriptRoot '../tools/build-windows-hdr.ps1') -BuildRoot $testRoot
if (-not $global:shaderfilterSmokeConfigureSeen -or -not $global:shaderfilterSmokeBuildSeen) { throw 'Build stages were not invoked' }
$manifest = Get-Content -Raw -LiteralPath (Join-Path $testRoot 'package/build-manifest.json') | ConvertFrom-Json
if ($manifest.runtime_hdr_validated -or $manifest.signed) { throw 'Invalid validation or signing claim' }
$effect = Join-Path $testRoot 'package/data/obs-plugins/obs-shaderfilter/internal/render_output.effect'
if ((Get-FileHash -LiteralPath $effect).Hash -ne $manifest.output_effect_sha256) { throw 'Effect packaging mismatch' }
Write-Output 'PASS: build arguments, matching effect resources, unsigned manifest, and package layout (mock tools only)'
