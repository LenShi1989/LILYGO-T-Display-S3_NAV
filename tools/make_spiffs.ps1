# =============================================================================
#  make_spiffs.ps1 - Build + verify spiffs.bin from LILYGO-T-Display-S3_Nav/data
#
#  Default output:
#    <repo>\LILYGO-T-Display-S3_Nav\build\spiffs.bin
#
#  Usage (from the repo root):
#    powershell -ExecutionPolicy Bypass -File tools\make_spiffs.ps1
#    powershell -ExecutionPolicy Bypass -File tools\make_spiffs.ps1 D:\out\spiffs.bin
#    powershell -ExecutionPolicy Bypass -File tools\make_spiffs.ps1 -Flash
#
#  Or simply double-click tools\make_spiffs.bat.
#
#  The image size and the flash offset are read from partitions.csv, so the
#  generated file always matches the installed partition table. The result is
#  verified (exact size, SPIFFS magic, file list) before it is reported as ok.
# =============================================================================
param(
  [Parameter(Position = 0)][string]$OutFile = '',
  [string]$PartitionCsv = '',
  [switch]$Flash,
  [string]$Port = '',
  [int]$Baud = 921600
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'spiffs_common.ps1')

$Root    = Get-RepoRootPath
$DataDir = Join-Path $Root 'LILYGO-T-Display-S3_Nav\data'
$Default = Join-Path $Root 'LILYGO-T-Display-S3_Nav\build\spiffs.bin'

if ([string]::IsNullOrWhiteSpace($OutFile)) {
  $OutFile = $Default
} elseif (-not [System.IO.Path]::IsPathRooted($OutFile)) {
  $OutFile = Join-Path $Root $OutFile
}

if (-not (Test-Path -LiteralPath $DataDir)) {
  throw "Data folder not found: $DataDir"
}

# --- partition layout -------------------------------------------------------
if ($PartitionCsv) {
  if (-not [System.IO.Path]::IsPathRooted($PartitionCsv)) {
    $PartitionCsv = Join-Path $Root $PartitionCsv
  }
  if (-not (Test-Path -LiteralPath $PartitionCsv)) {
    throw "Partition CSV not found: $PartitionCsv"
  }
  $part = Get-SpiffsPartition -CsvPath $PartitionCsv
  if (-not $part) { throw "No 'spiffs' row in $PartitionCsv" }
} else {
  $part = Find-SpiffsPartition -RepoRoot $Root
}

$SpiffsSize = [uint64]$part.Size
if ($null -ne $part.Offset) {
  $offsetText = '0x{0:X}' -f [uint64]$part.Offset
} else {
  $offsetText = '(not set in CSV)'
}

# --- tools ------------------------------------------------------------------
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

Write-Host ''
Write-Host 'LILYGO T-Display-S3 NAV - SPIFFS image'
Write-Host ('  mkspiffs  : {0}' -f $mkspiffs)
Write-Host ('  source    : {0}' -f $DataDir)
Write-Host ('  partition : {0}, offset {1}, size {2}' -f $part.Name, $offsetText, (Format-ImageSize $SpiffsSize))
Write-Host ('  csv       : {0}' -f $part.CsvPath)
Write-Host ('  output    : {0}' -f $OutFile)
Write-Host ''

Write-Host ('  block {0} / page {1}' -f $script:SpiffsBlockSize, $script:SpiffsPageSize)

# --- build ------------------------------------------------------------------
& $mkspiffs -c $DataDir -p $script:SpiffsPageSize -b $script:SpiffsBlockSize `
  -s ('0x{0:X}' -f $SpiffsSize) $OutFile
if ($LASTEXITCODE -ne 0) {
  throw "mkspiffs failed with exit code $LASTEXITCODE"
}
if (-not (Test-Path -LiteralPath $OutFile)) {
  throw "Output file was not created: $OutFile"
}

$size = (Get-Item -LiteralPath $OutFile).Length
if ($size -ne $SpiffsSize) {
  throw ("mkspiffs produced {0:N0} bytes, but the spiffs partition is {1:N0} bytes" -f $size, $SpiffsSize)
}

# --- verify -----------------------------------------------------------------
$problems = @()
$problems += @(Test-SpiffsImage -Image $OutFile -Size $SpiffsSize)
$problems += @(Test-SpiffsContents -Mkspiffs $mkspiffs -Image $OutFile -Size $SpiffsSize -DataDir $DataDir)

if ($problems.Count -gt 0) {
  Write-Host ''
  Write-Host 'Image validation FAILED:' -ForegroundColor Red
  foreach ($p in $problems) { Write-Host ("  - " + $p) -ForegroundColor Red }
  throw 'spiffs.bin does not match the partition layout / data folder.'
}

$fileCount = (Get-SourceFileList -DataDir $DataDir).Count
Write-Host ''
Write-Host ('Verified: {0} bytes, SPIFFS magic ok, {1} file(s) match data/' -f $size, $fileCount) -ForegroundColor Green

# --- flash hints ------------------------------------------------------------
if ($null -ne $part.Offset) {
  $esptool = Find-Esptool
  Write-Host ''
  Write-Host 'Flash over USB (whole 9.375 MB file, offset from partitions.csv):'
  Write-Host '  tools\flash_spiffs.bat'
  Write-Host '  (or)'
  if ($esptool) {
    Write-Host ('  "{0}" --chip esp32s3 --port COM5 --baud 921600 write-flash 0x{1:X} "{2}"' -f $esptool, [uint64]$part.Offset, $OutFile)
  } else {
    Write-Host ('  esptool --chip esp32s3 --port COM5 --baud 921600 write-flash 0x{0:X} "{1}"' -f [uint64]$part.Offset, $OutFile)
  }
}
Write-Host ''
Write-Host 'Or upload it from the web UI: OTA update > file system (spiffs.bin).'

if ($Flash) {
  Write-Host ''
  & (Join-Path $PSScriptRoot 'flash_spiffs.ps1') -Image $OutFile -Port $Port -Baud $Baud
  exit $LASTEXITCODE
}
