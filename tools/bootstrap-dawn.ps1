param(
  [string]$DawnRoot = (Join-Path $PSScriptRoot '../.local/dawn'),
  [string]$Python = 'python',
  [string]$GoBin = '',
  [switch]$SkipFetch,
  [int]$Jobs = 8
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/msvc-env.ps1"
if ($GoBin) { $env:PATH = "$GoBin;$env:PATH" }
$Python = (Get-Command $Python -ErrorAction Stop).Source
$pin = '34b1fca4d6c3d7025a2231d82b4fc719ca57fd71'
function Run([string]$Program, [string[]]$Arguments) {
  & $Program @Arguments
  if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit $LASTEXITCODE" }
}
if (!(Test-Path -LiteralPath $DawnRoot)) {
  Run git @('clone','--no-checkout','https://dawn.googlesource.com/dawn',$DawnRoot)
  Run git @('-C',$DawnRoot,'checkout','--detach',$pin)
}
$head = & git -C $DawnRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $head -ne $pin) { throw 'Dawn checkout is not the required pin; existing work is preserved.' }
if (& git -C $DawnRoot status --porcelain --untracked-files=no) { throw 'Dawn tracked source is dirty; existing work is preserved.' }
if (!$SkipFetch) { Run $Python @("$DawnRoot/tools/fetch_dawn_dependencies.py") }
$build = Join-Path $DawnRoot 'out/framebridge-relwithdebinfo'
Run cmake @('-S',$DawnRoot,'-B',$build,'-G','Ninja','-DCMAKE_BUILD_TYPE=RelWithDebInfo',
  "-DPython3_EXECUTABLE=$Python",'-DDAWN_FETCH_DEPENDENCIES=OFF','-DDAWN_BUILD_MONOLITHIC_LIBRARY=STATIC',
  '-DDAWN_BUILD_TESTS=OFF','-DDAWN_BUILD_SAMPLES=OFF','-DDAWN_BUILD_BENCHMARKS=OFF',
  '-DDAWN_ENABLE_D3D12=ON','-DDAWN_ENABLE_D3D11=OFF','-DDAWN_ENABLE_VULKAN=OFF',
  '-DDAWN_ENABLE_DESKTOP_GL=OFF','-DDAWN_ENABLE_OPENGLES=OFF','-DDAWN_ENABLE_NULL=OFF',
  '-DDAWN_USE_AGILITY_SDK=OFF','-DDAWN_USE_BUILT_DXC=OFF','-DDAWN_USE_GLFW=OFF',
  '-DTINT_BUILD_TESTS=OFF','-DTINT_BUILD_CMD_TOOLS=OFF','-DTINT_BUILD_SPV_READER=OFF',
  '-DTINT_BUILD_SPV_WRITER=OFF','-DTINT_BUILD_MSL_WRITER=OFF','-DTINT_BUILD_GLSL_WRITER=OFF')
Run cmake @('--build',$build,'--target','dawn_native','dawn_proc','--parallel',"$Jobs")
Write-Output "DAWN_BOOTSTRAP_PASS commit=$pin configuration=RelWithDebInfo"
