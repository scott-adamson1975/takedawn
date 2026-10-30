# Guided mapping of a racing wheel's buttons and hat switch for Takedawn.
#
# It asks for each control in turn, waits for you to press it, and prints the
# RECOMP_WHEEL_* lines to use. Results are also saved to
# %USERPROFILE%\Desktop\wheel-buttons.txt.
#
# Run (the execution policy blocks plain script files on most machines):
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\win\wheel-buttons.ps1
#
# It reads the wheel through Windows' joystick API (winmm), so it only needs
# PowerShell. winmm shows up to 32 buttons and one hat. Button numbers here are
# counted from 0 and are meant to match SDL's joystick button index, which is
# what the game uses; check them in the game with RECOMP_WHEEL_LOG=1.
param(
    [int]$Timeout = 15,                                  # seconds to wait for each control
    [switch]$All,                                        # listen to every device, not just the one with most buttons
    [string]$Out = "$env:USERPROFILE\Desktop\wheel-buttons.txt"
)

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class WmBtn {
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
  public struct CAPS {
    public ushort mid, pid;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string name;
    public uint xmin,xmax,ymin,ymax,zmin,zmax,nbtn,minper,maxper,rmin,rmax,umin,umax,vmin,vmax,caps,maxaxes,naxes,maxbtn;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string regkey;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst=260)] public string oem;
  }
  [StructLayout(LayoutKind.Sequential)]
  public struct INFOEX { public uint size, flags, x, y, z, r, u, v, buttons, btnnum, pov, r1, r2; }
  [DllImport("winmm.dll", CharSet=CharSet.Unicode)] public static extern uint joyGetDevCapsW(ulong id, ref CAPS c, uint sz);
  [DllImport("winmm.dll")] public static extern uint joyGetPosEx(uint id, ref INFOEX i);
  [DllImport("winmm.dll")] public static extern uint joyGetNumDevs();
}
'@

Start-Transcript -Path $Out -Force | Out-Null
$infoSize = [Runtime.InteropServices.Marshal]::SizeOf([Type][WmBtn+INFOEX])
$capsSize = [Runtime.InteropServices.Marshal]::SizeOf([Type][WmBtn+CAPS])
$CENTRED = 65535

# Every joystick that is plugged in and has buttons or a hat: id -> name, vid:pid.
function Get-Devices {
    $list = @()
    for ($i = 0; $i -lt [WmBtn]::joyGetNumDevs(); $i++) {
        $c = New-Object WmBtn+CAPS
        if ([WmBtn]::joyGetDevCapsW([uint64]$i, [ref]$c, $capsSize) -ne 0) { continue }
        $p = New-Object WmBtn+INFOEX; $p.size = $infoSize; $p.flags = 0xFF
        if ([WmBtn]::joyGetPosEx($i, [ref]$p) -ne 0) { continue }
        if ($c.nbtn -eq 0) { continue }                  # the pedals: no buttons
        $list += [pscustomobject]@{ Id = $i; Key = ('{0:X4}:{1:X4}' -f $c.mid, $c.pid); Buttons = $c.nbtn }
    }
    $list
}

function Read-State($id) {
    $p = New-Object WmBtn+INFOEX; $p.size = $infoSize; $p.flags = 0xFF
    if ([WmBtn]::joyGetPosEx([uint32]$id, [ref]$p) -ne 0) { return $null }
    [pscustomobject]@{ Buttons = [uint32]$p.buttons; Pov = [uint32]$p.pov }
}

$devs = @(Get-Devices)
if ($devs.Count -eq 0) {
    Write-Host "No joystick with buttons found. Is the wheel plugged in and on?"
    Stop-Transcript | Out-Null
    Read-Host "Press Enter to close"
    return
}
Write-Host "Devices with buttons:"
$devs | ForEach-Object { Write-Host ("  {0}  ({1} buttons)" -f $_.Key, $_.Buttons) }
# Only the device with the most buttons is listened to (the wheel base). Another
# device in the list (044F:B660 on one machine) held or toggled a button on its
# own and would be taken for a press. -All listens to every device.
if (-not $All -and $devs.Count -gt 1) {
    $devs = @($devs | Sort-Object Buttons -Descending | Select-Object -First 1)
    Write-Host ("Listening to {0} only (use -All to listen to every device)." -f $devs[0].Key)
}

# Wait until nothing is held, so the last control's release is not taken as the next press.
function Wait-Released {
    $end = (Get-Date).AddSeconds(5)
    while ((Get-Date) -lt $end) {
        $held = $false
        foreach ($d in $devs) {
            $s = Read-State $d.Id
            if ($s -and $s.Pov -ne $CENTRED) { $held = $true }
        }
        if (-not $held) { return }
        Start-Sleep -Milliseconds 50
    }
}

# Waits for a button (or, when $hat is set, a hat direction) that was not down at the start.
# Returns @{ Kind='button'|'hat'; Key; Index; Pov } or $null on timeout.
function Ask($label, [bool]$hat, [bool]$optional) {
    $suffix = if ($optional) { " (optional: wait to skip)" } else { "" }
    Write-Host ""
    Write-Host ("==> {0}{1}" -f $label, $suffix) -ForegroundColor Cyan
    # Baseline: buttons already held (some devices hold one permanently) are ignored.
    $base = @{}
    foreach ($d in $devs) { $s = Read-State $d.Id; $base[$d.Id] = $s }
    $end = (Get-Date).AddSeconds($Timeout)
    while ((Get-Date) -lt $end) {
        foreach ($d in $devs) {
            $s = Read-State $d.Id
            if (-not $s) { continue }
            $b = $base[$d.Id]
            $new = $s.Buttons -band (-bnot $b.Buttons)
            if ($hat -and $s.Pov -ne $CENTRED -and $s.Pov -ne $b.Pov) {
                Write-Host ("    got hat {0} deg on {1}" -f ($s.Pov / 100), $d.Key) -ForegroundColor Green
                Wait-Released
                return @{ Kind = 'hat'; Key = $d.Key; Index = 0; Pov = $s.Pov }
            }
            if ($new -ne 0) {
                $idx = 0
                while ((($new -shr $idx) -band 1) -eq 0) { $idx++ }
                $extra = ($new -band (-bnot ([uint32]1 -shl $idx)))
                $warn = if ($extra -ne 0) { "  (several buttons at once; took the lowest)" } else { "" }
                Write-Host ("    got button {0}  (winmm button {1}) on {2}{3}" -f $idx, ($idx + 1), $d.Key, $warn) -ForegroundColor Green
                Wait-Released
                Start-Sleep -Milliseconds 300
                return @{ Kind = 'button'; Key = $d.Key; Index = $idx; Pov = 0 }
            }
        }
        Start-Sleep -Milliseconds 15
    }
    Write-Host "    nothing pressed" -ForegroundColor Yellow
    $null
}

Write-Host ""
Write-Host "Press the control you want for each prompt. Don't touch anything else."
Write-Host "You have $Timeout seconds for each."

$res = [ordered]@{}
$res.ACCEPT = Ask "ACCEPT: confirms in menus, and BOOST in a race (the game's A button)" $false $false
$res.BACK   = Ask "BACK: goes back in menus, and CRASHBREAKER in a crash (the game's B button)" $false $false
$res.PAUSE  = Ask "PAUSE: pauses a race, starts things (the game's Start button)" $false $false
$res.HAT_UP    = Ask "HAT: push UP" $true $false
$res.HAT_DOWN  = Ask "HAT: push DOWN" $true $false
$res.HAT_LEFT  = Ask "HAT: push LEFT" $true $false
$res.HAT_RIGHT = Ask "HAT: push RIGHT" $true $false
$res.LOOK_BACK   = Ask "LOOK BACK (the game's X button)" $false $true
$res.CHANGE_VIEW = Ask "CHANGE VIEW (the game's Y button)" $false $true

Write-Host ""
Write-Host "=== Result ===" -ForegroundColor Cyan
$names = @{ ACCEPT='ACCEPT'; BACK='BACK'; PAUSE='PAUSE'; LOOK_BACK='LOOKBACK'; CHANGE_VIEW='VIEW' }
foreach ($k in $res.Keys) {
    $r = $res[$k]
    if (-not $r) { Write-Host ("{0,-12} not set" -f $k); continue }
    if ($r.Kind -eq 'hat') {
        $dir = @{ 0='up'; 9000='right'; 18000='down'; 27000='left' }[[int]$r.Pov]
        $note = if ($dir) { "($dir)" } else { "(diagonal or odd value)" }
        Write-Host ("{0,-12} hat {1} deg {2} on {3}" -f $k, ($r.Pov / 100), $note, $r.Key)
    } else {
        Write-Host ("{0,-12} button {1} on {2}" -f $k, $r.Index, $r.Key)
    }
}

# The hat is one control: report it from the first direction that was a hat.
$hatDev = ($res.HAT_UP, $res.HAT_DOWN, $res.HAT_LEFT, $res.HAT_RIGHT | Where-Object { $_ -and $_.Kind -eq 'hat' } | Select-Object -First 1)
Write-Host ""
Write-Host "--- Settings (proposed names; the game support for them is not written yet) ---"
foreach ($k in 'ACCEPT','BACK','PAUSE','LOOK_BACK','CHANGE_VIEW') {
    $r = $res[$k]
    if ($r -and $r.Kind -eq 'button') {
        Write-Host ("set RECOMP_WHEEL_BTN_{0}={1}:{2}" -f $names[$k], $r.Key, $r.Index)
    }
}
if ($hatDev) {
    Write-Host ("set RECOMP_WHEEL_HAT={0}:0" -f $hatDev.Key)
} else {
    foreach ($k in 'HAT_UP','HAT_DOWN','HAT_LEFT','HAT_RIGHT') {
        $r = $res[$k]
        if ($r -and $r.Kind -eq 'button') {
            Write-Host ("set RECOMP_WHEEL_BTN_{0}={1}:{2}" -f $k.Replace('HAT_','DPAD_'), $r.Key, $r.Index)
        }
    }
}
Stop-Transcript | Out-Null
Write-Host ""
Write-Host "Saved to $Out"
Read-Host "Done. Press Enter to close"
