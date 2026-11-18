# Hovers the REAL mouse cursor over a creature of a QA game on this PC and saves a picture of the game window with the
# cursor drawn in (the game's own `shot` has no cursor; the console's `move` is a virtual cursor that lasts 5 s).
#   tools/qa_win/hover.ps1 -Instance 7 -Pattern 'class 9' -Out out\shots\hover.png [-MatingCall]
# -Pattern: a regex for the creature's line in `coop.evolve mates` (its "screen x y" is hovered, 22 px above the feet).
# The window is brought to the front and "clicked into" on its title bar (no game input); -MatingCall presses Backspace.
# Moves the owner's real mouse: only with their OK (docs/06-testing.md#testing-two-pc-msi).
# -At 'x,y': hover that client point instead (a game without the co-op mod has no `coop.evolve mates`).
param([Parameter(Mandatory)][int]$Instance, [string]$Pattern = 'class 9', [Parameter(Mandatory)][string]$Out,
      [switch]$MatingCall, [int]$Up = 22, [string]$At = '')
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System; using System.Drawing; using System.Runtime.InteropServices;
public static class QaHover {
    [StructLayout(LayoutKind.Sequential)] public struct CURSORINFO { public int cbSize, flags; public IntPtr hCursor; public Point pos; }
    [StructLayout(LayoutKind.Sequential)] public struct ICONINFO { public bool fIcon; public int xHot, yHot; public IntPtr mask, color; }
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte sc, uint f, UIntPtr e);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref Point p);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out Rectangle r);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out Rectangle r);
    [DllImport("user32.dll")] public static extern bool GetCursorInfo(ref CURSORINFO ci);
    [DllImport("user32.dll")] public static extern bool GetIconInfo(IntPtr h, out ICONINFO ii);
    [DllImport("user32.dll")] public static extern bool DrawIconEx(IntPtr dc, int x, int y, IntPtr icon, int w, int h, int step, IntPtr brush, int flags);
    // the window's client area as on screen, with the cursor drawn where it is
    public static string Capture(IntPtr hwnd, string path) {
        Rectangle c; GetClientRect(hwnd, out c); Point o = new Point(0, 0); ClientToScreen(hwnd, ref o);
        using (Bitmap b = new Bitmap(c.Width, c.Height)) using (Graphics g = Graphics.FromImage(b)) {
            g.CopyFromScreen(o.X, o.Y, 0, 0, b.Size);
            CURSORINFO ci = new CURSORINFO(); ci.cbSize = Marshal.SizeOf(typeof(CURSORINFO));
            string note = "no cursor";
            if (GetCursorInfo(ref ci) && ci.flags == 1) {
                ICONINFO ii; GetIconInfo(ci.hCursor, out ii);
                IntPtr dc = g.GetHdc();
                DrawIconEx(dc, ci.pos.X - o.X - ii.xHot, ci.pos.Y - o.Y - ii.yHot, ci.hCursor, 0, 0, 0, IntPtr.Zero, 3);
                g.ReleaseHdc(dc);
                note = "cursor handle " + ci.hCursor.ToInt64().ToString("X") + " at client " + (ci.pos.X - o.X) + " " + (ci.pos.Y - o.Y);
            }
            b.Save(path, System.Drawing.Imaging.ImageFormat.Png);
            return note;
        }
    }
}
'@
function Qa([string]$cmd) { & py (Join-Path $root 'tools\qa_win\qa.py') $Instance $cmd.Split(' ') }
$port = 47800 + $Instance - 1
$gamePid = (Get-NetTCPConnection -LocalPort $port -State Listen).OwningProcess | Select-Object -First 1
$h = (Get-Process -Id $gamePid).MainWindowHandle
$r = New-Object System.Drawing.Rectangle; [QaHover]::GetWindowRect($h, [ref]$r) | Out-Null
[QaHover]::SetForegroundWindow($h) | Out-Null
[QaHover]::SetCursorPos($r.X + 250, $r.Y + 12) | Out-Null; Start-Sleep -Milliseconds 200   # the title bar: tabs in
[QaHover]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 80; [QaHover]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Seconds 6                                                                      # a virtual cursor has run out
if ([QaHover]::GetForegroundWindow() -ne $h) { throw 'the game window is not in front' }
function Target {
    if ($At) {
        $p = New-Object System.Drawing.Point ([int]$At.Split(',')[0]), ([int]$At.Split(',')[1])
        [QaHover]::ClientToScreen($h, [ref]$p) | Out-Null
        return $p
    }
    $l = (Qa 'coop.evolve mates' | Select-String $Pattern | Select-Object -First 1).Line
    if ($l -and $l -notmatch 'behind' -and $l -match 'screen (\d+) (\d+)') {
        $p = New-Object System.Drawing.Point ([int]$Matches[1]), ([int]$Matches[2] - $Up)
        [QaHover]::ClientToScreen($h, [ref]$p) | Out-Null
        return $p
    }
    return $null
}
# the cursor into the world first: over the title bar or the HUD the game rates every creature class 0
$g = New-Object System.Drawing.Point 40, 260; [QaHover]::ClientToScreen($h, [ref]$g) | Out-Null
[QaHover]::SetCursorPos($g.X, $g.Y) | Out-Null; Start-Sleep -Milliseconds 500
if ($MatingCall) {
    [QaHover]::keybd_event(8, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 120; [QaHover]::keybd_event(8, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Seconds 4
}
$p = Target
if (-not $p) { throw "no creature matching '$Pattern' on screen (turn the camera first)" }
for ($i = 8; $i -ge 0; $i--) { [QaHover]::SetCursorPos($p.X + 5 * $i, $p.Y - 4 * $i) | Out-Null; Start-Sleep -Milliseconds 60 }
Start-Sleep -Seconds 1
$p2 = Target
if ($p2) { [QaHover]::SetCursorPos($p2.X + 2, $p2.Y) | Out-Null; Start-Sleep -Milliseconds 100; [QaHover]::SetCursorPos($p2.X, $p2.Y) | Out-Null }
Start-Sleep -Seconds 2
$full = if ([IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path $root $Out }
if (-not $At) { (Qa 'coop.evolve mates' | Select-String $Pattern | Select-Object -First 1).Line }
(Qa 'cursor')
[QaHover]::Capture($h, $full)
$full
