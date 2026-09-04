param([string]$Chrome = $env:FRAMEBRIDGE_CHROME)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$config=Join-Path $root '.local/tcw005r-tools.json'
if (!$Chrome -and (Test-Path -LiteralPath $config)) { $Chrome=(Get-Content -Raw $config | ConvertFrom-Json).chrome }
if (!(Test-Path -LiteralPath $Chrome)) { throw 'Pass -Chrome <installed Chrome executable>.' }
$exe=Join-Path $root 'build/tcw008/framebridge-native-mirror.exe'
if (!(Test-Path -LiteralPath $exe)) { throw 'Build the current native demo first; see docs/tcw009-dlss-closeout.md.' }
$node=(Get-Command node).Source
$vite=Start-Process -FilePath $node -ArgumentList 'apps/demo-web/node_modules/vite/bin/vite.js apps/demo-web --host 127.0.0.1 --port 5173 --strictPort' -WorkingDirectory $root -WindowStyle Hidden -PassThru
try {
  Start-Process -FilePath $Chrome -ArgumentList '--new-window http://127.0.0.1:5173'
  Write-Host 'Paste the READY port and token into the webpage, then Connect mirror. Keep this console out of screenshots.'
  Write-Host 'Expected window: FrameBridge Native Mirror. Press Enter here or close that window to stop.'
  & $exe
  if($LASTEXITCODE -ne 0) { throw "Native receiver exit $LASTEXITCODE" }
} finally { if(!$vite.HasExited) { Stop-Process -Id $vite.Id } }
