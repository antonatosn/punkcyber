# ============================================================================
#  pc_stats_server.ps1 — PUNKCYBER Clock Display Controller
# ============================================================================
#  PC is the brain. Builds a 48-byte framebuffer and sends to ESP via UDP.
#  ESP is a thin display pipe — just writes bytes to TM1680.
#
#  USAGE:
#    powershell -ExecutionPolicy Bypass -File pc_stats_server.ps1
#    powershell -ExecutionPolicy Bypass -File pc_stats_server.ps1 -WeatherApiKey "YOUR_KEY" -WeatherCity "London,UK"
# ============================================================================

param(
    [int]$Port = 8889,
    [string]$WeatherApiKey = "",
    [string]$WeatherCity = ""
)

$ErrorActionPreference = "Continue"

# ═══════════════════════════════════════════════════════════════════════════════
#  DISPLAY MAP — All LED positions for the TM1680
# ═══════════════════════════════════════════════════════════════════════════════

# ── 7-Segment Clock Digits (6 positions) ─────────────────────────────────────
# Bit layout: 0=e, 1=f, 2=g, 3=[icon], 4=a, 5=b, 6=c, 7=d
$DIGIT_BYTES = @(0x0C, 0x0E, 0x10, 0x12, 0x14, 0x16)

$SEG_FONT = @(
    0xF3,  # 0: a,b,c,d,e,f     = bits 4,5,6,7,0,1
    0x60,  # 1: b,c             = bits 5,6
    0xB5,  # 2: a,b,d,e,g       = bits 4,5,7,0,2
    0xF4,  # 3: a,b,c,d,g       = bits 4,5,6,7,2
    0x66,  # 4: b,c,f,g         = bits 5,6,1,2
    0xD6,  # 5: a,c,d,f,g       = bits 4,6,7,1,2
    0xD7,  # 6: a,c,d,e,f,g     = bits 4,6,7,0,1,2
    0x70,  # 7: a,b,c           = bits 4,5,6
    0xF7,  # 8: a,b,c,d,e,f,g   = all
    0xF6   # 9: a,b,c,d,f,g     = bits 4,5,6,7,1,2
)

# ── Colons (between digit pairs) ─────────────────────────────────────────────
# Bit 3 of digit bytes = icon/colon
$COLON1_BYTE = 0x0E; $COLON1_BIT = 0x08   # Between digits 2-3 (HH:MM)
$COLON2_BYTE = 0x12; $COLON2_BIT = 0x08   # Between digits 4-5 (MM:SS)

# ── Icons (flat parallel arrays: byte[], mask[]) ─────────────────────────────
# WiFi and Computer are ESP-managed, don't touch those
$ICON_AUDIO_B  = @(0x01);                         $ICON_AUDIO_M  = @(0x08)
$ICON_WATCH_B  = @(0x07, 0x08, 0x09, 0x09);       $ICON_WATCH_M  = @(0x08, 0x08, 0x04, 0x08)
$ICON_SUNNY_B  = @(0x1F, 0x1F);                    $ICON_SUNNY_M  = @(0x02, 0x04)
$ICON_CLOUD_B  = @(0x1F, 0x21);                    $ICON_CLOUD_M  = @(0x08, 0x02)
$ICON_RAIN_B   = @(0x21, 0x21);                    $ICON_RAIN_M   = @(0x04, 0x08)

# ── Day of Week (flat: byte[], mask[]) ────────────────────────────────────────
# 0=Monday..6=Sunday
$DAY_B = @(0x18, 0x18, 0x18, 0x18, 0x19, 0x19, 0x19)
$DAY_M = @(0x30, 0xC0, 0x03, 0x0C, 0x30, 0xC0, 0x01)
$SUN_EXTRA_BYTE = 0x01; $SUN_EXTRA_BIT = 0x04

# ── VU Labels + % ─────────────────────────────────────────────────────────────
$CPU_LABEL_BYTE = 0x22; $CPU_LABEL_BIT = 0x10
$CPU_PCT_BYTE   = 0x28; $CPU_PCT_BIT   = 0x02
$GPU_LABEL_BYTE = 0x24; $GPU_LABEL_BIT = 0x10
$GPU_PCT_BYTE   = 0x29; $GPU_PCT_BIT   = 0x80
$RAM_LABEL_BYTE = 0x26; $RAM_LABEL_BIT = 0x10
$RAM_PCT_BYTE   = 0x2A; $RAM_PCT_BIT   = 0x20

# ── VU Meters (flat parallel: byte[], mask[]) ────────────────────────────────
$CPU_VU_B = @(0x22,0x22,0x22,0x22,0x22,0x22,0x22, 0x23,0x23,0x23,0x23,0x23,0x23,0x23,0x23, 0x28,0x28,0x28,0x28,0x28)
$CPU_VU_M = @(0x20,0x40,0x80,0x01,0x02,0x04,0x08, 0x10,0x20,0x40,0x80,0x01,0x02,0x04,0x08, 0x10,0x20,0x40,0x80,0x01)

$GPU_VU_B = @(0x24,0x24,0x24,0x24,0x24,0x24,0x24, 0x25,0x25,0x25,0x25,0x25,0x25,0x25,0x25, 0x28,0x28, 0x29,0x29,0x29)
$GPU_VU_M = @(0x20,0x40,0x80,0x01,0x02,0x04,0x08, 0x10,0x20,0x40,0x80,0x01,0x02,0x04,0x08, 0x04,0x08, 0x10,0x20,0x40)

$RAM_VU_B = @(0x26,0x26,0x26,0x26,0x26,0x26,0x26, 0x27,0x27,0x27,0x27,0x27,0x27,0x27,0x27, 0x29,0x29,0x29,0x29, 0x2A)
$RAM_VU_M = @(0x20,0x40,0x80,0x01,0x02,0x04,0x08, 0x10,0x20,0x40,0x80,0x01,0x02,0x04,0x08, 0x01,0x02,0x04,0x08, 0x10)

$VERT_L_B = @(0x17,0x17,0x17,0x17, 0x15,0x15,0x15,0x15, 0x14,0x13,0x13,0x13)
$VERT_L_M = @(0x04,0x01,0x40,0x10, 0x08,0x02,0x80,0x20, 0x08,0x04,0x01,0x40)

$VERT_R_B = @(0x17,0x17,0x17,0x17, 0x16,0x15,0x15,0x15, 0x15,0x13,0x13,0x13)
$VERT_R_M = @(0x08,0x02,0x80,0x20, 0x08,0x04,0x01,0x40, 0x10,0x08,0x02,0x80)

# ── PUNKCYBER Logo ──────────────────────────────────────────────────────────
$LOGO_BYTES = @(0x1E, 0x1F, 0x20, 0x21)
$LOGO_MASK  = @(0xFF, 0xF1, 0xFF, 0xF1)
$LOGO_DATA  = @(0xFF, 0xF1, 0xFF, 0xF1)

# ── 7x7 LED Matrix (49 pixels, row-major: row0=top, col0=left) ──────────────
# Index = row*7 + col
$MTX_B = @(
    0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,  # Row 0 (top)
    0x0C,0x0D,0x0D,0x0D,0x0D,0x0D,0x0D,  # Row 1
    0x0D,0x0D,0x19,0x19,0x19,0x1A,0x1A,  # Row 2
    0x1A,0x1A,0x1A,0x1A,0x1A,0x1A,0x1B,  # Row 3
    0x1B,0x1B,0x1B,0x1B,0x1B,0x1B,0x1B,  # Row 4
    0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,0x1C,  # Row 5
    0x1C,0x1D,0x1D,0x1D,0x1D,0x1D,0x1D   # Row 6 (bottom)
)
$MTX_M = @(
    0x20,0x40,0x80,0x01,0x02,0x04,0x08,  # Row 0
    0x08,0x10,0x20,0x40,0x80,0x01,0x02,  # Row 1
    0x04,0x08,0x02,0x04,0x08,0x10,0x20,  # Row 2
    0x40,0x80,0x01,0x02,0x04,0x08,0x10,  # Row 3
    0x20,0x40,0x80,0x01,0x02,0x04,0x08,  # Row 4
    0x10,0x20,0x40,0x80,0x01,0x02,0x04,  # Row 5
    0x08,0x10,0x20,0x40,0x80,0x01,0x02   # Row 6
)

# ── Starburst Digit Positions (6 digits, each = outer + inner byte) ──────────
# Outer byte: bit0=e, 1=f, 2=g, 3=icon, 4=a, 5=b, 6=c, 7=d
# Inner byte: bit0=l(↓), 1=m(↘), 4=h(↘upper), 5=i(↓upper), 6=j(↗upper), 7=k(↗lower)
$STAR_OUTER = @(0x00, 0x02, 0x04, 0x06, 0x08, 0x0A)  # Even bytes
$STAR_INNER = @(0x01, 0x03, 0x05, 0x07, 0x09, 0x0B)  # Odd bytes
$STAR_SEG_MASK = 0xF7  # Exclude bit3 (icon)

# ── Starburst 14-Segment Font (outer, inner) ─────────────────────────────────
# Layout:   aaaa          Outer: a=4,b=5,c=6,d=7,e=0,f=1,g=2
#          f\h|i/j b      Inner: h=4,i=5,j=6,k=7,l=0,m=1
#            gg
#          e/k|l\m c
#            dddd
$STAR_FONT = @{
    '0' = @(0xF3, 0xC0)  # a,b,c,d,e,f + j,k (with slash)
    '1' = @(0x60, 0x00)  # b,c
    '2' = @(0xB5, 0x00)  # a,b,d,e,g
    '3' = @(0xF4, 0x00)  # a,b,c,d,g
    '4' = @(0x66, 0x00)  # b,c,f,g
    '5' = @(0xD6, 0x00)  # a,c,d,f,g
    '6' = @(0xD7, 0x00)  # a,c,d,e,f,g
    '7' = @(0x70, 0x00)  # a,b,c
    '8' = @(0xF7, 0x00)  # a,b,c,d,e,f,g
    '9' = @(0xF6, 0x00)  # a,b,c,d,f,g
    'A' = @(0x77, 0x00)  # a,b,c,e,f,g
    'B' = @(0xF4, 0x21)  # a,b,c,d,g + i,l
    'C' = @(0x93, 0x00)  # a,d,e,f
    'D' = @(0xF0, 0x21)  # a,b,c,d + i,l
    'E' = @(0x97, 0x00)  # a,d,e,f,g
    'F' = @(0x17, 0x00)  # a,e,f,g
    'G' = @(0xD3, 0x00)  # a,c,d,e,f
    'H' = @(0x67, 0x00)  # b,c,e,f,g
    'I' = @(0x90, 0x21)  # a,d + i,l
    'J' = @(0xE1, 0x00)  # b,c,d,e
    'K' = @(0x07, 0x42)  # e,f,g + j,m
    'L' = @(0x83, 0x00)  # d,e,f
    'M' = @(0x63, 0x50)  # b,c,e,f + h,j (not possible perfectly, use h,i)
    'N' = @(0x63, 0x12)  # b,c,e,f + h,m
    'O' = @(0xF3, 0x00)  # a,b,c,d,e,f
    'P' = @(0x37, 0x00)  # a,b,e,f,g
    'Q' = @(0xF3, 0x02)  # a,b,c,d,e,f + m
    'R' = @(0x37, 0x02)  # a,b,e,f,g + m
    'S' = @(0xD6, 0x00)  # a,c,d,f,g
    'T' = @(0x10, 0x21)  # a + i,l
    'U' = @(0xE3, 0x00)  # b,c,d,e,f
    'V' = @(0x03, 0xC0)  # e,f + j,k
    'W' = @(0x63, 0x82)  # b,c,e,f + k,m
    'X' = @(0x00, 0xD2)  # h,j,k,m
    'Y' = @(0x00, 0x51)  # h,j,l
    'Z' = @(0x94, 0xC0)  # a,d,g + j,k
    ' ' = @(0x00, 0x00)  # Blank
    '-' = @(0x04, 0x00)  # g only
    '.' = @(0x80, 0x00)  # d only (bottom dot)
    '_' = @(0x80, 0x00)  # d
    '!' = @(0x00, 0x21)  # i,l (vertical bar)
}


# ═══════════════════════════════════════════════════════════════════════════════
#  AUDIO COM INTEROP
# ═══════════════════════════════════════════════════════════════════════════════
try {
Add-Type -ErrorAction Ignore -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public enum EDataFlow { eRender = 0, eCapture = 1, eAll = 2 }
public enum ERole     { eConsole = 0, eMultimedia = 1, eCommunications = 2 }
public enum CLSCTX    { CLSCTX_INPROC_SERVER = 0x1 }

[ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"),
 InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IMMDeviceEnumerator
{
    int NotImpl_EnumAudioEndpoints();
    int GetDefaultAudioEndpoint(EDataFlow dataFlow, ERole role, out IMMDevice ppDevice);
}

[ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"),
 InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IMMDevice
{
    int Activate(ref Guid iid, int dwClsCtx, IntPtr pActivationParams,
                 [MarshalAs(UnmanagedType.IUnknown)] out object ppInterface);
}

[ComImport, Guid("C02216F6-8C67-4B5B-9D00-D008E73E0064"),
 InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IAudioMeterInformation
{
    int GetPeakValue(out float pfPeak);
    int GetMeteringChannelCount(out int pnChannelCount);
    int GetChannelsPeakValues(int u32ChannelCount,
        [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 0)] float[] afPeakValues);
}

[ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")]
public class MMDeviceEnumeratorClass { }

public static class AudioPeakMeter
{
    private static IAudioMeterInformation _meter;
    private static bool _initialized, _failed;
    private static int _channelCount;

    private static void EnsureInit()
    {
        if (_initialized || _failed) return;
        try
        {
            var enumerator = (IMMDeviceEnumerator)new MMDeviceEnumeratorClass();
            IMMDevice device;
            int hr = enumerator.GetDefaultAudioEndpoint(EDataFlow.eRender, ERole.eConsole, out device);
            if (hr != 0 || device == null) { _failed = true; return; }
            Guid iidMeter = typeof(IAudioMeterInformation).GUID;
            object meterObj;
            hr = device.Activate(ref iidMeter, (int)CLSCTX.CLSCTX_INPROC_SERVER, IntPtr.Zero, out meterObj);
            if (hr != 0 || meterObj == null) { _failed = true; return; }
            _meter = (IAudioMeterInformation)meterObj;
            _meter.GetMeteringChannelCount(out _channelCount);
            if (_channelCount < 1) _channelCount = 1;
            _initialized = true;
        }
        catch { _failed = true; }
    }

    public static int[] GetStereoLevels()
    {
        EnsureInit();
        if (_failed) return new int[] { 0, 0 };
        try
        {
            float[] peaks = new float[_channelCount];
            _meter.GetChannelsPeakValues(_channelCount, peaks);
            int left  = (int)Math.Round(peaks[0] * 100.0);
            int right = _channelCount >= 2 ? (int)Math.Round(peaks[1] * 100.0) : left;
            return new int[] { Math.Max(0, Math.Min(100, left)), Math.Max(0, Math.Min(100, right)) };
        }
        catch { return new int[] { 0, 0 }; }
    }
}
"@
} catch {
    if (-not ([AudioPeakMeter].GetMethod('GetStereoLevels'))) {
        Write-Host "`n  [ERROR] Open a NEW PowerShell window.`n" -ForegroundColor Red
        Read-Host "Press Enter to exit"; exit 1
    }
}

# ═══════════════════════════════════════════════════════════════════════════════
#  BANNER
# ═══════════════════════════════════════════════════════════════════════════════
Write-Host ""
Write-Host "  ╔══════════════════════════════════════════╗" -ForegroundColor DarkYellow
Write-Host "  ║   ⚡ PUNKCYBER Display Controller ⚡     ║" -ForegroundColor DarkYellow
Write-Host "  ╚══════════════════════════════════════════╝" -ForegroundColor DarkYellow
Write-Host ""

# ═══════════════════════════════════════════════════════════════════════════════
#  GPU DETECTION
# ═══════════════════════════════════════════════════════════════════════════════
$nvidiaSmi = $null
@("C:\Windows\System32\nvidia-smi.exe",
  "C:\Program Files\NVIDIA Corporation\NVSMI\nvidia-smi.exe") | ForEach-Object {
    if (!$nvidiaSmi -and (Test-Path $_)) { $nvidiaSmi = $_ }
}
$nv = Get-Command "nvidia-smi" -ErrorAction SilentlyContinue
if ($nv) { $nvidiaSmi = $nv.Source }
if ($nvidiaSmi) { Write-Host "  [GPU]  $nvidiaSmi" -ForegroundColor Green }
else { Write-Host "  [GPU]  Not found — GPU=0" -ForegroundColor Yellow }

# ── Audio test ───────────────────────────────────────────────────────────────
$at = [AudioPeakMeter]::GetStereoLevels()
Write-Host "  [AUD]  Stereo audio OK" -ForegroundColor Green

# ── Weather ──────────────────────────────────────────────────────────────────
if ($WeatherApiKey -and $WeatherCity) {
    Write-Host "  [WTH]  Weather: $WeatherCity" -ForegroundColor Green
} else {
    Write-Host "  [WTH]  No API key — weather disabled" -ForegroundColor Yellow
}

# ═══════════════════════════════════════════════════════════════════════════════
#  FRAMEBUFFER BUILDER
# ═══════════════════════════════════════════════════════════════════════════════

function Set-VU($fb, [int[]]$vuB, [int[]]$vuM, $level, $maxSegs) {
    $segs = [math]::Round(($level / 100.0) * $maxSegs)
    if ($segs -gt $maxSegs) { $segs = $maxSegs }
    for ($i = 0; $i -lt $segs; $i++) {
        $fb[$vuB[$i]] = $fb[$vuB[$i]] -bor $vuM[$i]
    }
}

function Set-Icon($fb, [int[]]$iB, [int[]]$iM) {
    for ($i = 0; $i -lt $iB.Count; $i++) {
        $fb[$iB[$i]] = $fb[$iB[$i]] -bor $iM[$i]
    }
}

function Set-Star($fb, [int]$pos, [string]$ch) {
    $ch = $ch.ToUpper()
    if (-not $STAR_FONT.ContainsKey($ch)) { $ch = ' ' }
    $glyph = $STAR_FONT[$ch]
    $outer = [int]($glyph[0]) -band $STAR_SEG_MASK
    $inner = [int]($glyph[1]) -band 0xF3  # exclude icon bits 2,3
    $fb[$STAR_OUTER[$pos]] = $fb[$STAR_OUTER[$pos]] -bor $outer
    $fb[$STAR_INNER[$pos]] = $fb[$STAR_INNER[$pos]] -bor $inner
}

function Set-StarText($fb, [string]$text) {
    $text = $text.PadRight(6).Substring(0, 6)
    for ($i = 0; $i -lt 6; $i++) {
        Set-Star $fb $i $text[$i]
    }
}

function Set-MatrixCol($fb, [int]$col, [int]$height) {
    # Draw column bottom-up: row6=bottom, row0=top
    for ($row = 6; $row -ge 0; $row--) {
        $barsFromBottom = 6 - $row
        if ($barsFromBottom -lt $height) {
            $idx = $row * 7 + $col
            $fb[$MTX_B[$idx]] = $fb[$MTX_B[$idx]] -bor $MTX_M[$idx]
        }
    }
}

# ── CPU History (circular buffer for matrix graph) ───────────────────────────
$script:cpuHistory = [int[]]::new(7)
$script:cpuHistIdx = 0
$script:lastHistUpdate = [DateTime]::MinValue

# ── Scrolling Text ───────────────────────────────────────────────────────────
$MONTH_NAMES = @('','JAN','FEB','MAR','APR','MAY','JUN','JUL','AUG','SEP','OCT','NOV','DEC')
$DOW_NAMES   = @('SUN','MON','TUE','WED','THU','FRI','SAT')
$script:starMessages = [System.Collections.ArrayList]@()
$script:scrollPos = 0
$script:lastScroll = [DateTime]::MinValue
$SCROLL_MS = 350  # Milliseconds per scroll step

function Build-Framebuffer {
    param($cpu, $ram, $gpu, $audioL, $audioR, $weatherCode)

    $fb = [byte[]]::new(48)

    # ── Time (local) ─────────────────────────────────────────────────────
    $now = Get-Date
    $h = $now.Hour; $m = $now.Minute; $s = $now.Second

    # Digits: HH MM SS
    $digits = @(
        [math]::Floor($h / 10), ($h % 10),
        [math]::Floor($m / 10), ($m % 10),
        [math]::Floor($s / 10), ($s % 10)
    )
    for ($i = 0; $i -lt 6; $i++) {
        $fb[$DIGIT_BYTES[$i]] = $fb[$DIGIT_BYTES[$i]] -bor $SEG_FONT[$digits[$i]]
    }

    # Colons (blink every second)
    if ($s % 2 -eq 0) {
        $fb[$COLON1_BYTE] = $fb[$COLON1_BYTE] -bor $COLON1_BIT
        $fb[$COLON2_BYTE] = $fb[$COLON2_BYTE] -bor $COLON2_BIT
    }

    # ── Day of Week ──────────────────────────────────────────────────────
    $dotnetDow = [int]$now.DayOfWeek
    $dow = if ($dotnetDow -eq 0) { 6 } else { $dotnetDow - 1 }
    $fb[$DAY_B[$dow]] = $fb[$DAY_B[$dow]] -bor $DAY_M[$dow]
    if ($dow -eq 6) { $fb[$SUN_EXTRA_BYTE] = $fb[$SUN_EXTRA_BYTE] -bor $SUN_EXTRA_BIT }

    # ── Starburst: Scrolling Text ────────────────────────────────────────
    $dayName = $DOW_NAMES[[int]$now.DayOfWeek]
    $dateStr = "{0:D2} {1}" -f $now.Day, $MONTH_NAMES[$now.Month]
    $scrollText = "      {0}   {1}   " -f $dayName, $dateStr
    # Append custom messages
    foreach ($cm in $script:starMessages) { $scrollText += "$cm   " }

    # Advance scroll position
    if (($now - $script:lastScroll).TotalMilliseconds -ge $SCROLL_MS) {
        $script:scrollPos = ($script:scrollPos + 1) % $scrollText.Length
        $script:lastScroll = $now
    }

    # Extract 6-char window from the looping scroll buffer
    $window = ""
    for ($i = 0; $i -lt 6; $i++) {
        $ci = ($script:scrollPos + $i) % $scrollText.Length
        $window += $scrollText[$ci]
    }
    Set-StarText $fb $window

    # ── VU Labels (always on) ────────────────────────────────────────────
    $fb[$CPU_LABEL_BYTE] = $fb[$CPU_LABEL_BYTE] -bor $CPU_LABEL_BIT
    $fb[$CPU_PCT_BYTE]   = $fb[$CPU_PCT_BYTE]   -bor $CPU_PCT_BIT
    $fb[$GPU_LABEL_BYTE] = $fb[$GPU_LABEL_BYTE] -bor $GPU_LABEL_BIT
    $fb[$GPU_PCT_BYTE]   = $fb[$GPU_PCT_BYTE]   -bor $GPU_PCT_BIT
    $fb[$RAM_LABEL_BYTE] = $fb[$RAM_LABEL_BYTE] -bor $RAM_LABEL_BIT
    $fb[$RAM_PCT_BYTE]   = $fb[$RAM_PCT_BYTE]   -bor $RAM_PCT_BIT

    # ── Horizontal VU Bars ───────────────────────────────────────────────
    Set-VU $fb $CPU_VU_B $CPU_VU_M $cpu 20
    Set-VU $fb $GPU_VU_B $GPU_VU_M $gpu 20
    Set-VU $fb $RAM_VU_B $RAM_VU_M $ram 20

    # ── Vertical VU (stereo audio) ───────────────────────────────────────
    Set-VU $fb $VERT_L_B $VERT_L_M $audioL 12
    Set-VU $fb $VERT_R_B $VERT_R_M $audioR 12

    # ── Audio icon (on when audio playing) ───────────────────────────────
    if ($audioL -gt 2 -or $audioR -gt 2) {
        Set-Icon $fb $ICON_AUDIO_B $ICON_AUDIO_M
    }

    # ── Watch icon (always on) ───────────────────────────────────────────
    Set-Icon $fb $ICON_WATCH_B $ICON_WATCH_M

    # ── Weather icons ────────────────────────────────────────────────────
    switch ($weatherCode) {
        1 { Set-Icon $fb $ICON_SUNNY_B $ICON_SUNNY_M }
        2 { Set-Icon $fb $ICON_SUNNY_B $ICON_SUNNY_M; Set-Icon $fb $ICON_CLOUD_B $ICON_CLOUD_M }
        3 { Set-Icon $fb $ICON_CLOUD_B $ICON_CLOUD_M }
        4 { Set-Icon $fb $ICON_CLOUD_B $ICON_CLOUD_M; Set-Icon $fb $ICON_RAIN_B $ICON_RAIN_M }
        5 { Set-Icon $fb $ICON_CLOUD_B $ICON_CLOUD_M
            if ($s % 2 -eq 0) { Set-Icon $fb $ICON_RAIN_B $ICON_RAIN_M } }
        6 { if ($s % 2 -eq 0) { Set-Icon $fb $ICON_CLOUD_B $ICON_CLOUD_M } }
    }

    # ── 7x7 Matrix: CPU History + Live Dot ──────────────────────────────
    # Update history every 2 seconds
    if (($now - $script:lastHistUpdate).TotalSeconds -ge 2) {
        $script:cpuHistory[$script:cpuHistIdx] = $cpu
        $script:cpuHistIdx = ($script:cpuHistIdx + 1) % 7
        $script:lastHistUpdate = $now
    }

    # Draw 7 bar columns (oldest left, newest right)
    for ($col = 0; $col -lt 7; $col++) {
        $histIdx = ($script:cpuHistIdx + $col) % 7
        $val = $script:cpuHistory[$histIdx]
        $barH = [math]::Round(($val / 100.0) * 7)
        if ($barH -gt 7) { $barH = 7 }
        if ($val -gt 0 -and $barH -lt 1) { $barH = 1 }  # Always show at least 1px
        Set-MatrixCol $fb $col $barH
    }

    # Corner frame markers (4 corners always lit)
    foreach ($corner in @(0, 6, 42, 48)) {
        if ($corner -lt 49) {
            $fb[$MTX_B[$corner]] = $fb[$MTX_B[$corner]] -bor $MTX_M[$corner]
        }
    }

    # Live CPU dot — bounces on top row proportional to current CPU
    $dotCol = [math]::Round(($cpu / 100.0) * 6)
    if ($dotCol -gt 6) { $dotCol = 6 }
    $dotIdx = $dotCol  # Row 0
    $fb[$MTX_B[$dotIdx]] = $fb[$MTX_B[$dotIdx]] -bor $MTX_M[$dotIdx]

    # ── PUNKCYBER Logo (always on, preserve icon bits) ───────────────────
    for ($i = 0; $i -lt $LOGO_BYTES.Count; $i++) {
        $b = $LOGO_BYTES[$i]
        $fb[$b] = ($fb[$b] -band (-bnot $LOGO_MASK[$i])) -bor $LOGO_DATA[$i]
    }

    return $fb
}

# ═══════════════════════════════════════════════════════════════════════════════
#  BACKGROUND STATS THREAD (CPU/RAM/GPU)
# ═══════════════════════════════════════════════════════════════════════════════
$sharedStats = [hashtable]::Synchronized(@{ cpu = 0; ram = 0; gpu = 0 })

$statsRunspace = [runspacefactory]::CreateRunspace()
$statsRunspace.Open()
$statsRunspace.SessionStateProxy.SetVariable('sharedStats', $sharedStats)
$statsRunspace.SessionStateProxy.SetVariable('nvidiaSmi', $nvidiaSmi)

$statsScript = [powershell]::Create().AddScript({
    while ($true) {
        try {
            $c = Get-CimInstance Win32_Processor -ErrorAction Stop |
                 Measure-Object -Property LoadPercentage -Average
            $sharedStats.cpu = [math]::Max(0, [math]::Min(100, [math]::Round($c.Average)))
        } catch { }
        try {
            $os = Get-CimInstance Win32_OperatingSystem -ErrorAction Stop
            $sharedStats.ram = [math]::Round((($os.TotalVisibleMemorySize - $os.FreePhysicalMemory) / $os.TotalVisibleMemorySize) * 100)
        } catch { }
        if ($nvidiaSmi) {
            try {
                $o = & $nvidiaSmi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>$null
                if ($o) { $sharedStats.gpu = [math]::Max(0, [math]::Min(100, [int]($o.Trim()))) }
            } catch { }
        }
        Start-Sleep -Seconds 1
    }
})
$statsScript.Runspace = $statsRunspace
$statsHandle = $statsScript.BeginInvoke()

# ═══════════════════════════════════════════════════════════════════════════════
#  BACKGROUND WEATHER THREAD
# ═══════════════════════════════════════════════════════════════════════════════
# Weather codes: 0=none, 1=clear, 2=partly, 3=cloudy, 4=rain, 5=storm, 6=snow
$sharedWeather = [hashtable]::Synchronized(@{ code = 0; temp = 0 })

if ($WeatherApiKey -and $WeatherCity) {
    $weatherRunspace = [runspacefactory]::CreateRunspace()
    $weatherRunspace.Open()
    $weatherRunspace.SessionStateProxy.SetVariable('sharedWeather', $sharedWeather)
    $weatherRunspace.SessionStateProxy.SetVariable('apiKey', $WeatherApiKey)
    $weatherRunspace.SessionStateProxy.SetVariable('city', $WeatherCity)

    $weatherScript = [powershell]::Create().AddScript({
        function Get-WeatherCode($id) {
            if ($id -ge 200 -and $id -lt 300) { return 5 }  # Storm
            if ($id -ge 300 -and $id -lt 600) { return 4 }  # Rain
            if ($id -ge 600 -and $id -lt 700) { return 6 }  # Snow
            if ($id -ge 700 -and $id -lt 800) { return 3 }  # Fog → cloudy
            if ($id -eq 800) { return 1 }                    # Clear
            if ($id -le 802) { return 2 }                    # Partly cloudy
            if ($id -le 804) { return 3 }                    # Cloudy
            return 0
        }
        while ($true) {
            try {
                $url = "http://api.openweathermap.org/data/2.5/weather?q=$city&appid=$apiKey&units=metric"
                $r = Invoke-RestMethod -Uri $url -TimeoutSec 10 -ErrorAction Stop
                $sharedWeather.code = Get-WeatherCode $r.weather[0].id
                $sharedWeather.temp = [math]::Round($r.main.temp)
            } catch { }
            Start-Sleep -Seconds 600  # Every 10 minutes
        }
    })
    $weatherScript.Runspace = $weatherRunspace
    $weatherHandle = $weatherScript.BeginInvoke()
}

# ═══════════════════════════════════════════════════════════════════════════════
#  UDP SETUP
# ═══════════════════════════════════════════════════════════════════════════════
Write-Host ""
Write-Host "  [INIT] Stats thread started" -ForegroundColor Green
Start-Sleep -Milliseconds 1500
Write-Host "  [INIT] CPU:$($sharedStats.cpu)%  RAM:$($sharedStats.ram)%  GPU:$($sharedStats.gpu)%" -ForegroundColor Green

$udpClient = New-Object System.Net.Sockets.UdpClient($Port)
$udpClient.Client.ReceiveTimeout = 33  # ~30Hz

$espIP = $null
$espPort = $Port
$lastHeartbeat = [DateTime]::MinValue

Write-Host ""
Write-Host "  [OK]   Listening on UDP port $Port (48-byte framebuffer mode)" -ForegroundColor Green
Write-Host "         Waiting for ESP heartbeat..." -ForegroundColor DarkGray
Write-Host "  Press Ctrl+C to stop." -ForegroundColor DarkGray
Write-Host "  ─────────────────────────────────────────────" -ForegroundColor DarkGray
Write-Host ""

# ═══════════════════════════════════════════════════════════════════════════════
#  MAIN LOOP (~30Hz)
# ═══════════════════════════════════════════════════════════════════════════════
$lastLog = [DateTime]::MinValue
try {
    while ($true) {
        # ── Listen for heartbeat ─────────────────────────────────────────
        try {
            $remoteEP = New-Object System.Net.IPEndPoint([System.Net.IPAddress]::Any, 0)
            $data = $udpClient.Receive([ref]$remoteEP)
            if ($data.Length -eq 1 -and $data[0] -eq 0xFF) {
                $newIP = $remoteEP.Address.ToString()
                if ($newIP -ne $espIP) {
                    $espIP = $newIP
                    $espPort = $remoteEP.Port
                    Write-Host "  [ESP]  Clock at ${espIP}:${espPort} — streaming display!" -ForegroundColor Cyan
                }
                $lastHeartbeat = [DateTime]::Now
            }
        } catch [System.Net.Sockets.SocketException] { }

        # ── Build & send framebuffer ─────────────────────────────────────
        if ($espIP -and ([DateTime]::Now - $lastHeartbeat).TotalSeconds -lt 5) {
            $audio = [AudioPeakMeter]::GetStereoLevels()
            $cpuVal = [int]$sharedStats.cpu
            $ramVal = [int]$sharedStats.ram
            $gpuVal = [int]$sharedStats.gpu
            $wxCode = [int]$sharedWeather.code

            $fb = Build-Framebuffer -cpu $cpuVal -ram $ramVal -gpu $gpuVal `
                                    -audioL $audio[0] -audioR $audio[1] `
                                    -weatherCode $wxCode

            try {
                $udpClient.Send($fb, 48, $espIP, $espPort) | Out-Null
            } catch { }

            # Log every 3 seconds
            if (([DateTime]::Now - $lastLog).TotalSeconds -ge 3) {
                $ts = Get-Date -Format "HH:mm:ss"
                $wx = @("--","Clear","Partly","Cloudy","Rain","Storm","Snow")[$wxCode]
                Write-Host "  [$ts]  CPU:${cpuVal}% RAM:${ramVal}% GPU:${gpuVal}%  AUD:$($audio[0])/$($audio[1])%  WX:$wx  -> $espIP" -ForegroundColor DarkGray
                $lastLog = [DateTime]::Now
            }
        } else {
            Start-Sleep -Milliseconds 100
            if (([DateTime]::Now - $lastLog).TotalSeconds -ge 10) {
                Write-Host "  [WAIT] Listening for ESP heartbeat on port $Port..." -ForegroundColor DarkYellow
                $lastLog = [DateTime]::Now
            }
        }
    }
} finally {
    $statsScript.Stop(); $statsRunspace.Close()
    if ($WeatherApiKey) { $weatherScript.Stop(); $weatherRunspace.Close() }
    $udpClient.Close()
    Write-Host ""; Write-Host "  [STOP] Controller stopped." -ForegroundColor Yellow
}
