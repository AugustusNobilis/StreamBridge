param([string]$Repository = 'AugustusNobilis/StreamBridge', [switch]$IntegrationTests, [switch]$WidgetFixture)
$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path "$PSScriptRoot/..").Path
$cef = Join-Path $workspace '.deps/cef/cef_binary_154.0.34+g14c5a08+chromium-154.0.8037.98_windows64_minimal'
$integration = if ($IntegrationTests) { 'ON' } else { 'OFF' }
$fixture = if ($WidgetFixture) { 'ON' } else { 'OFF' }
& "$PSScriptRoot/cmake.ps1" -CMakeArgs @('-S',$workspace,'-B',"$workspace/build",'-G','Visual Studio 17 2022','-A','x64',
  '-DBUILD_TESTING=ON',"-DSTREAMBRIDGE_INTEGRATION_TESTS=$integration","-DSTREAMBRIDGE_GITHUB_REPOSITORY=$Repository")
if ($LASTEXITCODE -ne 0) { throw 'Plugin configure failed' }
& "$PSScriptRoot/cmake.ps1" -CMakeArgs @('--build',"$workspace/build",'--config','Release','--parallel','4')
if ($LASTEXITCODE -ne 0) { throw 'Plugin build failed' }
& "$PSScriptRoot/cmake.ps1" -CMakeArgs @('-S',"$workspace/helper",'-B',"$workspace/build-helper",'-G','Visual Studio 17 2022','-A','x64',
  "-DCEF_ROOT=$cef","-DSTREAMBRIDGE_TEST_FIXTURE=$fixture")
if ($LASTEXITCODE -ne 0) { throw 'CEF configure failed' }
& "$PSScriptRoot/cmake.ps1" -CMakeArgs @('--build',"$workspace/build-helper",'--config','Release','--parallel','4')
if ($LASTEXITCODE -ne 0) { throw 'CEF build failed' }
