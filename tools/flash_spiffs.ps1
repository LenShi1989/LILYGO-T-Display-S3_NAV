# =============================================================================
#  flash_spiffs.ps1 - Write build\spiffs.bin to the spiffs partition over USB
#
#  Usage (from the repo root):
#    tools\flash_spiffs.bat                 # build if needed, then flash
#    tools\flash_spiffs.bat COM5            # pick the serial port
#    tools\flash_spiffs.bat -Rebuild        # rebuild the image first
#    tools\flash_spiffs.bat -DryRun         # only print the esptool command
#
#  The offset comes from LILYGO-T-Display-S3_Nav\partitions.csv, so it can
#  never drift away from the partition table the firmware is built with.
# =============================================================================
param(
  [Parameter(Position = 0)][string]$Image = '',
  [Parameter(Position = 1)][string]$Port = '',
  [string]$PartitionCsv = '',
  [int]$Baud = 921600,
  [switch]$Rebuild,
  [switch]$DryRun,
  [switch]$NoReset
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'spiffs_common.ps1')

$Root    = Get-RepoRootPath
$Default = Join-Path $Root 'LILYGO-T-Display-S3_Nav\build\spiffs.bin'

if ([string]::IsNullOrWhiteSpace($Image)) {
  $Image = $Default
} elseif (-not [System.IO.Path]::IsPathRooted($Image)) {
  $Image = Join-Path $Root $Image
}

# A bare "COM5" passed as the first argument is treated as the port.
if ($Image -match '^COM\d+$' -and [string]::IsNullOrWhiteSpace($Port)) {
  $Port  = $Image
  $Image = $Default
}

if ($Rebuild -or -not (Test-Path -LiteralPath $Image)) {
  Write-Host 'Building spiffs.bin first...'
  & (Join-Path $PSScriptRoot 'make_spiffs.ps1') -OutFile $Image -PartitionCsv $PartitionCsv
}

if ($PartitionCsv) {
  if (-not [System.IO.Path]::IsPathRooted($PartitionCsv)) {
    $PartitionCsv = Join-Path $Root $PartitionCsv
  }
  $part = Get-SpiffsPartition -CsvPath $PartitionCsv
  if (-not $part) { throw "No 'spiffs' row in $PartitionCsv" }
} else {
  $part = Find-SpiffsPartition -RepoRoot $Root
}

if ($null -eq $part.Offset) {
  throw ('Partition "{0}" has no offset in {1}; add one before flashing.' -f $part.Name, $part.CsvPath)
}
$Offset = [uint64]$part.Offset
$Size   = [uint64]$part.Size

# --- verify the image before touching the flash -----------------------------
$problems = @(Test-SpiffsImage -Image $Image -Size $Size)
if ($problems.Count -gt 0) {
  Write-Host ''
  Write-Host ('Cannot flash "{0}":' -f $Image) -ForegroundColor Red
  foreach ($p in $problems) { Write-Host ('  - ' + $p) -ForegroundColor Red }
  Write-Host 'Run tools\make_spiffs.bat to rebuild it.'
  exit 1
}

# --- serial port ------------------------------------------------------------
function Get-EspPort {
  param([string]$Preferred)

  if ($Preferred) { return $Preferred }
  if ($env:ESP_PORT) { return $env:ESP_PORT }

  $ports = @([System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object -Unique)
  if ($ports.Count -eq 0) {
    throw 'No serial port found. Connect the board over USB or pass -Port COM5.'
  }
  if ($ports.Count -eq 1) { return $ports[0] }

  $usb = @()
  foreach ($p in $ports) {
    $label = ''
    try {
      $dev = Get-CimInstance -ClassName Win32_PnPEntity -ErrorAction Stop |
        Where-Object { $_.Name -like "*($p)*" } |
        Select-Object -First 1
      if ($dev) { $label = $dev.Name }
    } catch { }
    if ($label -match 'USB|JTAG|CH34|CH910|CP210|Silicon|Espressif') { $usb += $p }
    Write-Host ('  {0}  {1}' -f $p, $label)
  }
  if ($usb.Count -eq 1) { return $usb[0] }

  throw ('Several serial ports found (' + ($ports -join ', ') + '). Pass the right one, e.g. tools\flash_spiffs.bat COM5')
}

$port = Get-EspPort -Preferred $Port

# --- esptool ----------------------------------------------------------------
$esptoolExec = @()
$esptool = Find-Esptool
if ($esptool) {
  $esptoolExec = @($esptool)
} else {
  $py = Get-Command python -ErrorAction SilentlyContinue
  if ($py) {
    & $py.Source -c "import esptool" 2>$null
    if ($LASTEXITCODE -eq 0) { $esptoolExec = @($py.Source, '-m', 'esptool') }
  }
}
if ($esptoolExec.Count -eq 0) {
  throw @"
esptool was not found.
Install the ESP32 board core (Arduino IDE > Boards Manager), or set:
  set ESPTOOL=C:\path\to\esptool.exe
"@
}

$after = 'hard-reset'
if ($NoReset) { $after = 'no-reset' }

$esptoolArgs = @(
  '--chip', 'esp32s3',
  '--port', $port,
  '--baud', "$Baud",
  '--before', 'default-reset',
  '--after', $after,
  'write-flash',
  '--flash-mode', 'dio',
  '--flash-freq', '80m',
  '--flash-size', '16MB',
  ('0x{0:X}' -f $Offset),
  $Image
)

Write-Host ''
Write-Host 'LILYGO T-Display-S3 NAV - flash SPIFFS image over USB'
Write-Host ('  board   : esp32s3 (16MB flash)')
Write-Host ('  port    : {0} @ {1} baud' -f $port, $Baud)
Write-Host ('  image   : {0}' -f $Image)
Write-Host ('  layout  : {0} offset 0x{1:X}, {2}' -f $part.Name, $Offset, (Format-ImageSize $Size))
$displayArgs = $esptoolArgs | ForEach-Object { if ($_ -match '\s') { '"{0}"' -f $_ } else { $_ } }
Write-Host ('  command : {0} {1}' -f $esptoolExec[0], ($displayArgs -join ' '))
Write-Host ''

if ($DryRun) {
  Write-Host 'Dry run: nothing was written.' -ForegroundColor Yellow
  exit 0
}

$exe = $esptoolExec[0]
$prefix = @()
if ($esptoolExec.Count -gt 1) {
  $prefix = $esptoolExec[1..($esptoolExec.Count - 1)]
}

& $exe @prefix @esptoolArgs
$code = $LASTEXITCODE

Write-Host ''
if ($code -eq 0) {
  Write-Host 'Done: spiffs.bin written and verified. The board was reset.' -ForegroundColor Green
  Write-Host 'Open http://nav.local (or the device IP) and check the web UI.'
} else {
  Write-Host ('esptool failed with exit code {0}.' -f $code) -ForegroundColor Red
  Write-Host 'Checklist:'
  Write-Host '  - Use a data-capable USB cable and the USB port wired to the ESP32-S3.'
  Write-Host '  - Close the Arduino IDE serial monitor (COM port must be free).'
  Write-Host '  - If connect fails, hold BOOT, tap RESET, release BOOT, then retry.'
  Write-Host '  - Try a slower baud rate:  tools\flash_spiffs.bat -Baud 115200'
}
exit $code
