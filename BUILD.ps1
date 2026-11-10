param([string]$Python = 'python', [string]$ArduinoCli = '')
$ErrorActionPreference = 'Stop'
$buildArgs = @((Join-Path $PSScriptRoot 'tools\build.py'))
if ($ArduinoCli) { $buildArgs += @('--cli', $ArduinoCli) }
& $Python @buildArgs
if ($LASTEXITCODE -ne 0) { throw 'Firmware build failed' }
