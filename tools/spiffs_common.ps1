# =============================================================================
#  spiffs_common.ps1 - Shared helpers for make_spiffs.ps1 / flash_spiffs.ps1
#
#  Do not run this file directly; it is dot-sourced by the two scripts above.
#
#  Everything here is ASCII only so that Windows PowerShell 5.1 and cmd.exe
#  never show mojibake, whatever code page the console is using.
# =============================================================================

# SPIFFS geometry used by arduino-esp32 (CONFIG_SPIFFS_PAGE_SIZE=256,
# CONFIG_SPIFFS_BLOCK_SIZE=4096). Only the partition offset/size come from
# partitions.csv.
$script:SpiffsPageSize  = 256
$script:SpiffsBlockSize = 4096

function Get-RepoRootPath {
  return (Split-Path -Parent $PSScriptRoot)
}

# Parse "0x960000", "9830400" or "9M" style numbers used in partitions.csv.
function ConvertTo-ImageNumber {
  param([string]$Text)
  $t = "$Text".Trim()
  if (-not $t) { return $null }

  if ($t -match '^0[xX][0-9a-fA-F]+$') {
    try { return [uint64][Convert]::ToUInt64($t.Substring(2), 16) } catch { return $null }
  }
  if ($t -match '^\d+[kKmM]$') {
    $mult = 1024
    if ($t[-1] -match '[mM]') { $mult = 1024 * 1024 }
    $num = [uint64]$t.Substring(0, $t.Length - 1)
    return ($num * $mult)
  }
  try { return [uint64]$t } catch { return $null }
}

function Format-ImageSize {
  param([uint64]$Bytes)
  return ("{0:N0} bytes ({1:N3} MB)" -f $Bytes, ($Bytes / 1MB))
}

function Read-PartitionRows {
  param([string]$CsvPath)

  $rows = @()
  if (-not (Test-Path -LiteralPath $CsvPath)) { return $rows }

  foreach ($raw in (Get-Content -LiteralPath $CsvPath)) {
    $line = "$raw"
    $hash = $line.IndexOf('#')
    if ($hash -ge 0) { $line = $line.Substring(0, $hash) }
    $line = $line.Trim()
    if (-not $line) { continue }

    $cells = @($line.Split(',') | ForEach-Object { $_.Trim() })
    if ($cells.Count -lt 5) { continue }
    if ($cells[0] -ieq 'Name' -or $cells[1] -ieq 'Type') { continue }

    $offset = ConvertTo-ImageNumber $cells[3]
    $size   = ConvertTo-ImageNumber $cells[4]
    if ($null -eq $size -or $size -eq 0) { continue }

    $rows += [pscustomobject]@{
      Name    = $cells[0]
      Type    = $cells[1]
      SubType = $cells[2]
      Offset  = $offset
      Size    = $size
      CsvPath = $CsvPath
    }
  }
  return $rows
}

function Get-SpiffsPartition {
  param([string]$CsvPath)

  foreach ($row in (Read-PartitionRows -CsvPath $CsvPath)) {
    if ($row.SubType -ieq 'spiffs' -or $row.Name -ieq 'spiffs') { return $row }
  }
  return $null
}

# The sketch's own partitions.csv wins; a copy in the repo root is accepted as
# a fallback (see README "Partition table").
function Find-SpiffsPartition {
  param([string]$RepoRoot)

  $candidates = @(
    (Join-Path $RepoRoot 'LILYGO-T-Display-S3_Nav\partitions.csv'),
    (Join-Path $RepoRoot 'partitions.csv')
  )
  foreach ($path in $candidates) {
    $row = Get-SpiffsPartition -CsvPath $path
    if ($row -and $null -ne $row.Offset -and $row.Size -gt 0) { return $row }
  }

  throw ("Cannot find a 'spiffs' partition row. Looked in:`n  " + ($candidates -join "`n  "))
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

function Find-Esptool {
  if ($env:ESPTOOL -and (Test-Path -LiteralPath $env:ESPTOOL)) {
    return (Resolve-Path -LiteralPath $env:ESPTOOL).Path
  }

  $cmd = Get-Command esptool -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }

  $bases = @()
  if ($env:ARDUINO15)    { $bases += $env:ARDUINO15 }
  if ($env:LOCALAPPDATA) { $bases += (Join-Path $env:LOCALAPPDATA 'Arduino15') }
  if ($env:USERPROFILE) {
    $bases += (Join-Path $env:USERPROFILE 'AppData\Local\Arduino15')
    $bases += (Join-Path $env:USERPROFILE '.arduino15')
  }

  foreach ($base in ($bases | Select-Object -Unique)) {
    $dir = Join-Path $base 'packages\esp32\tools\esptool_py'
    if (-not (Test-Path -LiteralPath $dir)) { continue }

    $exe = Get-ChildItem -LiteralPath $dir -Recurse -File -Filter 'esptool.exe' -ErrorAction SilentlyContinue |
      Sort-Object FullName -Descending |
      Select-Object -First 1
    if ($exe) { return $exe.FullName }
  }
  return $null
}

# Files that mkspiffs reports inside the image: path -> size.
function Get-ImageFileList {
  param(
    [string]$Mkspiffs,
    [string]$Image,
    [uint64]$Size
  )

  $raw = & $Mkspiffs -l -s ('0x{0:X}' -f $Size) -b $script:SpiffsBlockSize `
    -p $script:SpiffsPageSize $Image
  if ($LASTEXITCODE -ne 0) {
    throw "mkspiffs -l failed with exit code $LASTEXITCODE"
  }

  $map = @{}
  foreach ($line in $raw) {
    $text = "$line"
    $m = [regex]::Match($text, '^\s*(\d+)\s+(\S.*?)\s*$')
    if ($m.Success) { $map[$m.Groups[2].Value] = [uint64]$m.Groups[1].Value }
  }
  return $map
}

# Files that should end up in the image: "/index.html" -> size.
function Get-SourceFileList {
  param([string]$DataDir)

  $map = @{}
  $prefixLen = $DataDir.TrimEnd('\').Length
  foreach ($file in (Get-ChildItem -LiteralPath $DataDir -Recurse -File -Force)) {
    $rel = $file.FullName.Substring($prefixLen).Replace('\', '/')
    $map[$rel] = [uint64]$file.Length
  }
  return $map
}

# Structural checks that do not need mkspiffs: exact partition size, SPIFFS
# magic in the first blocks and the last block, and not a firmware image.
function Test-SpiffsImage {
  param(
    [string]$Image,
    [uint64]$Size
  )

  $problems = @()
  if (-not (Test-Path -LiteralPath $Image)) {
    return @("image not found: $Image")
  }

  $len = (Get-Item -LiteralPath $Image).Length
  if ($len -ne $Size) {
    $problems += ("size is {0:N0} bytes but the spiffs partition is {1:N0} bytes" -f $len, $Size)
  }
  if (($len % $script:SpiffsBlockSize) -ne 0) {
    $problems += ("size {0:N0} is not a multiple of the block size {1}" -f $len, $script:SpiffsBlockSize)
  }

  $fs = [System.IO.File]::Open($Image, [System.IO.FileMode]::Open,
                               [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
  try {
    $head = New-Object byte[] 4
    $fs.Position = 0
    if ($fs.Read($head, 0, 4) -ge 1 -and $head[0] -eq 0xE9) {
      $problems += 'file starts with 0xE9, which is a firmware image, not a SPIFFS image'
    }

    $blocks = [int][Math]::Floor($len / $script:SpiffsBlockSize)
    if ($blocks -le 0) { return $problems }

    $checkBlocks = @(0, 1, 2, ($blocks - 1)) |
      Where-Object { $_ -ge 0 -and $_ -lt $blocks } |
      Select-Object -Unique

    $pair = New-Object byte[] 2
    foreach ($bix in $checkBlocks) {
      # The magic lives in the block's last object-lookup page, right before
      # the 2-byte erase counter and after a 2-byte padding entry.
      $off = [int64]$bix * $script:SpiffsBlockSize + $script:SpiffsPageSize - 4
      $fs.Position = $off
      if ($fs.Read($pair, 0, 2) -ne 2) {
        $problems += ("block {0}: cannot read SPIFFS magic at 0x{1:X}" -f $bix, $off)
        continue
      }

      $got  = ([int]$pair[0]) -bor (([int]$pair[1]) -shl 8)
      $want = (0x20140529 -bxor $script:SpiffsPageSize -bxor ($blocks - $bix)) -band 0xFFFF
      if ($got -ne $want) {
        $problems += ("block {0}: SPIFFS magic 0x{1:X4} != 0x{2:X4} (wrong page / block / partition size?)" -f $bix, $got, $want)
      }
    }
  } finally {
    $fs.Dispose()
  }

  return $problems
}

# Compare the file list stored in the image with the data/ folder.
function Test-SpiffsContents {
  param(
    [string]$Mkspiffs,
    [string]$Image,
    [uint64]$Size,
    [string]$DataDir
  )

  $problems = @()
  $inside = Get-ImageFileList -Mkspiffs $Mkspiffs -Image $Image -Size $Size
  $source = Get-SourceFileList -DataDir $DataDir

  foreach ($name in ($source.Keys | Sort-Object)) {
    if (-not $inside.ContainsKey($name)) {
      $problems += "missing in image: $name"
    } elseif ($inside[$name] -ne $source[$name]) {
      $problems += ("size mismatch for {0}: image {1:N0} / source {2:N0}" -f $name, $inside[$name], $source[$name])
    }
  }
  foreach ($name in ($inside.Keys | Sort-Object)) {
    if (-not $source.ContainsKey($name)) {
      $problems += "unexpected file in image: $name"
    }
  }

  return $problems
}
