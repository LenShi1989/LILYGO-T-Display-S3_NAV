# =============================================================================
#  make_spiffs.ps1 - Build spiffs.bin from LILYGO-T-Display-S3_Nav/data
#
#  Default output:
#    <repo>\LILYGO-T-Display-S3_Nav\build\spiffs.bin
#
#  Usage (from the repo root):
#    powershell -ExecutionPolicy Bypass -File tools\make_spiffs.ps1
#    powershell -ExecutionPolicy Bypass -File tools\make_spiffs.ps1 D:\out\spiffs.bin
#
#  Or simply double-click tools\make_spiffs.bat.
#
#  mkspiffs lookup order:
#    1. MKSPIFFS environment variable
#    2. mkspiffs found on PATH
#    3. mkspiffs bundled with the ESP32 core in Arduino15
# =============================================================================
param(
  [string]$OutFile = ''
)

$ErrorActionPreference = 'Stop'

$Root       = Split-Path -Parent $PSScriptRoot
$DataDir    = Join-Path $Root 'LILYGO-T-Display-S3_Nav\data'
$Default    = Join-Path $Root 'LILYGO-T-Display-S3_Nav\build\spiffs.bin'
$SpiffsSize = '0x960000'   # 9.375 MB, must match partitions.csv
$BlockSize  = '4096'
$PageSize   = '256'

if ([string]::IsNullOrWhiteSpace($OutFile)) {
  $OutFile = $Default
} elseif (-not [System.IO.Path]::IsPathRooted($OutFile)) {
  $OutFile = Join-Path $Root $OutFile
}

function Find-Mkspiffs {
  if ($env:MKSPIFFS -and (Test-Path -LiteralPath $env:MKSPIFFS)) {
    return (Resolve-Path -LiteralPath $env:MKSPIFFS).Path
  }

  $cmd = Get-Command mkspiffs -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }

  $bases = @()
  if ($env:ARDUINO15)    { $bases += $env:ARDUINO15 }
  if ($env:LOCALAPPDATA) { $bases += (Join-Path $env:LOCALAPPDATA 'Arduino15') }
  if ($env:USERPROFILE) {
    $bases += (Join-Path $env:USERPROFILE 'AppData\Local\Arduino15')
    $bases += (Join-Path $env:USERPROFILE '.arduino15')
  }

  foreach ($base in ($bases | Select-Object -Unique)) {
    $dir = Join-Path $base 'packages\esp32\tools\mkspiffs'
    if (-not (Test-Path -LiteralPath $dir)) { continue }

    $exe = Get-ChildItem -LiteralPath $dir -Recurse -File -Filter 'mkspiffs.exe' -ErrorAction SilentlyContinue |
      Sort-Object FullName -Descending |
      Select-Object -First 1
    if ($exe) { return $exe.FullName }
  }
  return $null
}

if (-not (Test-Path -LiteralPath $DataDir)) {
  throw "Data folder not found: $DataDir"
}

$mkspiffs = Find-Mkspiffs
if (-not $mkspiffs) {
  throw @"
mkspiffs.exe was not found.
Install the ESP32 board core (Arduino IDE > Boards Manager) or set:
  set MKSPIFFS=C:\path\to\mkspiffs.exe
"@
}

$outDir = Split-Path -Parent $OutFile
if (-not (Test-Path -LiteralPath $outDir)) {
  New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}

Write-Host ""
Write-Host "mkspiffs : $mkspiffs"
Write-Host "source   : $DataDir"
Write-Host "image    : $SpiffsSize (block $BlockSize / page $PageSize)"
Write-Host "output   : $OutFile"
Write-Host ""

& $mkspiffs -c $DataDir -p $PageSize -b $BlockSize -s $SpiffsSize $OutFile
if ($LASTEXITCODE -ne 0) {
  throw "mkspiffs failed with exit code $LASTEXITCODE"
}
if (-not (Test-Path -LiteralPath $OutFile)) {
  throw "Output file was not created: $OutFile"
}

$size = (Get-Item -LiteralPath $OutFile).Length
Write-Host ""
Write-Host ("Done: {0} ({1:N0} bytes)" -f $OutFile, $size)
Write-Host "Upload it from the web UI: OTA update > spiffs.bin"
