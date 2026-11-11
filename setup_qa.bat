@echo off
setlocal

rem Detect test directory location
set "TARGET_DIR=tests"
if not exist "%TARGET_DIR%" (
    if exist "Barcode-Scanner\tests" (
        set "TARGET_DIR=Barcode-Scanner\tests"
    ) else if exist "..\tests" (
        set "TARGET_DIR=..\tests"
    ) else (
        echo [INFO] Creating directory: %TARGET_DIR%
        mkdir "%TARGET_DIR%"
    )
)

echo ======================================================================
echo   QA Environment Setup - Barcode Scanner Test Suite
echo ======================================================================
echo [INFO] Target Directory: %TARGET_DIR%
echo [INFO] Generating valid 6-digit ITF barcode test suite...

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
"$patterns = @('NNWWN', 'WNNNW', 'NWNNW', 'WWNNN', 'NNWNW', 'WNWNN', 'NWWNN', 'NNNWW', 'WNNWN', 'NWNWN');" ^
"function Get-W([char]$c) { if ($c -eq [char]'W') { 3 } else { 1 } };" ^
"function Encode-ITF([string]$d) {" ^
"    $w = New-Object System.Collections.Generic.List[int];" ^
"    $w.Add(1); $w.Add(1); $w.Add(1); $w.Add(1);" ^
"    for ($p = 0; $p -lt 3; $p++) {" ^
"        $b = $patterns[[int][char]$d[2 * $p] - 48];" ^
"        $s = $patterns[[int][char]$d[2 * $p + 1] - 48];" ^
"        for ($i = 0; $i -lt 5; $i++) { $w.Add((Get-W $b[$i])); $w.Add((Get-W $s[$i])); }" ^
"    };" ^
"    $w.Add(3); $w.Add(1); $w.Add(1); return ,$w.ToArray();" ^
"};" ^
"function Make-Bmp([string]$path, [string]$code, [int]$mod = 3, [int]$h = 120, [string]$variant = 'normal') {" ^
"    $widths = Encode-ITF $code; $tot = 20; foreach ($w in $widths) { $tot += $w };" ^
"    $w = $tot * $mod; $rowSize = [int][Math]::Ceiling($w / 4.0) * 4; $imgSize = $rowSize * $h; $fileSize = 1078 + $imgSize;" ^
"    $pix = New-Object byte[] ($w * $h); for ($i = 0; $i -lt $pix.Length; $i++) { $pix[$i] = 255 };" ^
"    $x = 10 * $mod; $isBar = $true;" ^
"    foreach ($wid in $widths) {" ^
"        $px = $wid * $mod;" ^
"        if ($isBar) { for ($y = 0; $y -lt $h; $y++) { for ($i = 0; $i -lt $px; $i++) { $pix[$y * $w + $x + $i] = 0 } } };" ^
"        $x += $px; $isBar = -not $isBar;" ^
"    };" ^
"    if ($variant -eq 'blurry') {" ^
"        $bl = New-Object byte[] $pix.Length; [Array]::Copy($pix, $bl, $pix.Length);" ^
"        for ($y = 0; $y -lt $h; $y++) {" ^
"            for ($col = 2; $col -lt ($w - 2); $col++) {" ^
"                $bl[$y * $w + $col] = [byte][Math]::Min(230, [Math]::Max(40, [int](($pix[$y * $w + $col - 2] + $pix[$y * $w + $col - 1] + $pix[$y * $w + $col] + $pix[$y * $w + $col + 1] + $pix[$y * $w + $col + 2]) / 5)))" ^
"            }" ^
"        }; $pix = $bl" ^
"    } elseif ($variant -eq 'noisy') {" ^
"        $rnd = New-Object System.Random 42; for ($i = 0; $i -lt $pix.Length; $i++) { if ($rnd.NextDouble() -lt 0.03) { $pix[$i] = if ($pix[$i] -eq 0) { 255 } else { 0 } } }" ^
"    } elseif ($variant -eq 'inverted') {" ^
"        $inv = New-Object byte[] $pix.Length;" ^
"        for ($y = 0; $y -lt $h; $y++) { for ($col = 0; $col -lt $w; $col++) { $inv[$y * $w + $col] = $pix[($h - 1 - $y) * $w + ($w - 1 - $col)] } };" ^
"        $pix = $inv" ^
"    };" ^
"    $b = New-Object byte[] $fileSize; $b[0] = 0x42; $b[1] = 0x4D;" ^
"    [BitConverter]::GetBytes([uint32]$fileSize).CopyTo($b, 2);" ^
"    [BitConverter]::GetBytes([uint32]1078).CopyTo($b, 10);" ^
"    [BitConverter]::GetBytes([uint32]40).CopyTo($b, 14);" ^
"    [BitConverter]::GetBytes([int32]$w).CopyTo($b, 18);" ^
"    [BitConverter]::GetBytes([int32]$h).CopyTo($b, 22);" ^
"    [BitConverter]::GetBytes([uint16]1).CopyTo($b, 26);" ^
"    [BitConverter]::GetBytes([uint16]8).CopyTo($b, 28);" ^
"    [BitConverter]::GetBytes([uint32]$imgSize).CopyTo($b, 34);" ^
"    [BitConverter]::GetBytes([int32]2835).CopyTo($b, 38);" ^
"    [BitConverter]::GetBytes([int32]2835).CopyTo($b, 42);" ^
"    [BitConverter]::GetBytes([uint32]256).CopyTo($b, 46);" ^
"    for ($c = 0; $c -lt 256; $c++) { $o = 54 + ($c * 4); $b[$o] = [byte]$c; $b[$o + 1] = [byte]$c; $b[$o + 2] = [byte]$c };" ^
"    for ($y = 0; $y -lt $h; $y++) { [Array]::Copy($pix, (($h - 1 - $y) * $w), $b, (1078 + ($y * $rowSize)), $w) };" ^
"    [System.IO.File]::WriteAllBytes($path, $b);" ^
"    Write-Host ('  [OK] ' + (Split-Path $path -Leaf) + ' (Code: ' + $code + ')');" ^
"};" ^
"$codes = @('123456', '987654', '100000', '555555', '999999', '112233', '246810', '314159', '765432', '888888', '543210', '654321', '102030', '908070', '456789', '135790', '876543', '234567', '345678', '678901', '789012', '890123', '901234', '192837', '462819');" ^
"foreach ($c in $codes) { Make-Bmp (Join-Path '%TARGET_DIR%' ('barcode_' + $c + '.bmp')) $c 3 120 'normal' };" ^
"Make-Bmp (Join-Path '%TARGET_DIR%' 'test_normal.bmp') '123456' 3 120 'normal';" ^
"Make-Bmp (Join-Path '%TARGET_DIR%' 'test_rotated.bmp') '987654' 3 120 'inverted';" ^
"Make-Bmp (Join-Path '%TARGET_DIR%' 'test_blurry.bmp') '555555' 3 120 'blurry';" ^
"Make-Bmp (Join-Path '%TARGET_DIR%' 'test_noisy.bmp') '888888' 3 120 'noisy';" ^
"Make-Bmp (Join-Path '%TARGET_DIR%' 'test_small.bmp') '314159' 2 80 'normal';" ^
"Make-Bmp (Join-Path '%TARGET_DIR%' 'test_large.bmp') '654321' 4 160 'normal';" ^
"Make-Bmp (Join-Path '%TARGET_DIR%' 'test_inverted.bmp') '765432' 3 120 'inverted';"

if errorlevel 1 (
    echo [ERROR] Failed to generate test barcode images.
    exit /b 1
)

echo.
echo ======================================================================
echo [SUCCESS] QA Barcode test suite successfully primed in %TARGET_DIR%.
echo ======================================================================
exit /b 0
