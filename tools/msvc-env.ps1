$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Visual Studio C++ x64 tools are required.' }
$dev = Join-Path $vs 'Common7/Tools/VsDevCmd.bat'
$lines = & $env:ComSpec /d /c "`"$dev`" -no_logo -arch=x64 -host_arch=x64 -vcvars_ver=14.44 && set"
if ($LASTEXITCODE -ne 0) { throw 'VsDevCmd failed.' }
foreach ($line in $lines) { if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') } }
