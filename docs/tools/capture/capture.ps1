# Takes the manual's interface screenshots in a real 3ds Max.
#
#   powershell -File capture.ps1 -Out <shots dir> -Langs en,de [-MaxVer 2027] [-Only settings,importer]
#
# Max runs capture.ms with its own 3dsMax.ini (-i) whose PlugCFG points to a
# private folder, so switching the WhiteoutDex language never touches the
# user's settings. capture.ms opens one dialog after the other; before each it
# writes <Out>\_want.txt ("<lang>|<shot id>|modal" or "...|self") and waits
# for _armed.txt; this side notes the open windows, arms, waits for the new
# window, saves <lang>\<id>.png and <lang>\<id>.json (every child
# control: id, class, text, rectangle) and answers with _done.txt. A modal
# dialog is closed from here (Cancel); a non-modal one closes itself.
param(
  [Parameter(Mandatory)][string]$Out,
  [string[]]$Langs = @('en'),
  [int]$MaxVer = 2027,
  [string[]]$Only = @(),
  [int]$MaxMinutes = 60,
  [int]$WaitSeconds = 90   # how long a window may take to appear
)
$ErrorActionPreference = 'Continue'
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System; using System.Text; using System.Collections.Generic;
using System.Runtime.InteropServices; using System.Drawing; using System.Drawing.Imaging;
public static class Cap {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr p, EnumProc f, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] static extern int GetDlgCtrlID(IntPtr h);
  [DllImport("user32.dll")] static extern IntPtr GetParent(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, string l);
  // WM_SETTEXT is marshalled across processes, so this works on Max's controls.
  public static void SetText(IntPtr dlg, int id, string text) { SendMessage(GetDlgItem(dlg, id), 0x000C, IntPtr.Zero, text); }
  // Shows `to` instead of `from` in every child control whose text holds it
  // (display only). Returns how many controls changed.
  [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] static extern IntPtr SendMessageSB(IntPtr h, uint m, IntPtr w, StringBuilder l);
  [DllImport("user32.dll", EntryPoint="SendMessageW")] static extern IntPtr SendMessageI(IntPtr h, uint m, IntPtr w, IntPtr l);
  // The text of a control in another process. GetWindowText does not reach
  // into another process's edit controls; WM_GETTEXT is marshalled and does.
  public static string TextMsg(IntPtr h) {
    int n = (int)SendMessageI(h, 0x000E, IntPtr.Zero, IntPtr.Zero);   // WM_GETTEXTLENGTH
    if (n <= 0) return "";
    var sb = new StringBuilder(n + 1);
    SendMessageSB(h, 0x000D, (IntPtr)(n + 1), sb);                     // WM_GETTEXT
    return sb.ToString();
  }
  // Shows `to` instead of `from` in every child control whose text holds it
  // (display only). Returns how many controls changed.
  public static int Mask(IntPtr win, string from, string to) {
    int n = 0;
    EnumChildWindows(win, (h, l) => {
      var t = TextMsg(h);
      if (t.IndexOf(from, StringComparison.OrdinalIgnoreCase) >= 0) {
        SendMessage(h, 0x000C, IntPtr.Zero, System.Text.RegularExpressions.Regex.Replace(t, System.Text.RegularExpressions.Regex.Escape(from), to.Replace("$", "$$"), System.Text.RegularExpressions.RegexOptions.IgnoreCase));
        n++;
      }
      return true; }, IntPtr.Zero);
    return n;
  }
  // A button click as the dialog sees it: WM_COMMAND(BN_CLICKED, id, hwnd).
  public static void Click(IntPtr dlg, int id) { PostMessage(dlg, 0x0111, (IntPtr)id, GetDlgItem(dlg, id)); }
  // A left mouse click inside a child control (client pixels).
  public static void ClickAt(IntPtr dlg, int id, int x, int y) {
    IntPtr c = GetDlgItem(dlg, id); IntPtr lp = (IntPtr)((y << 16) | (x & 0xFFFF));
    PostMessage(c, 0x0201, (IntPtr)1, lp); PostMessage(c, 0x0202, IntPtr.Zero, lp);
  }
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  public struct RECT { public int L, T, R, B; }
  // Probe: every descendant window inside the rectangle x >= minX, with its
  // handle, parent handle and rectangle (screen coordinates).
  public static List<string> Tree(IntPtr win, int minX) {
    var r0 = new List<string>();
    EnumChildWindows(win, (h, l) => {
      RECT r; GetWindowRect(h, out r);
      if (r.L >= minX && IsWindowVisible(h)) r0.Add(h.ToInt64() + "|" + GetParent(h).ToInt64() + "|" + Class(h) + "|" + Text(h).Replace("|", "/") + "|" + r.L + "," + r.T + "," + (r.R - r.L) + "," + (r.B - r.T));
      return true; }, IntPtr.Zero);
    return r0;
  }
  // The first visible descendant whose text is exactly `title`.
  public static IntPtr FindChild(IntPtr win, string title) {
    IntPtr found = IntPtr.Zero;
    EnumChildWindows(win, (h, l) => { if (IsWindowVisible(h) && Text(h) == title) { found = h; return false; } return true; }, IntPtr.Zero);
    return found;
  }
  // The control id of the first visible child whose text is `text` (0 if none).
  public static int IdOfText(IntPtr win, string text) {
    IntPtr h = FindChild(win, text);
    return h == IntPtr.Zero ? 0 : GetDlgCtrlID(h);
  }
  // All texts of a window's controls (WM_GETTEXT), for diagnostics.
  public static string AllTexts(IntPtr win) {
    var sb = new StringBuilder();
    EnumChildWindows(win, (h, l) => { var t = TextMsg(h); if (t.Length > 0 && t.Length < 300) sb.Append(" | " + t.Replace('\r', ' ').Replace('\n', ' ')); return true; }, IntPtr.Zero);
    var r = sb.ToString(); return r.Length > 600 ? r.Substring(0, 600) : r;
  }
  // Presses the button with this text (BM_CLICK), e.g. "&Continue" on a .NET error box.
  public static bool ClickText(IntPtr win, string text) {
    bool hit = false;
    EnumChildWindows(win, (h, l) => { if (TextMsg(h) == text) { SendMessageI(h, 0x00F5, IntPtr.Zero, IntPtr.Zero); hit = true; return false; } return true; }, IntPtr.Zero);
    return hit;
  }
  public static int LeftOf(IntPtr h) { RECT r; GetWindowRect(h, out r); return r.L; }
  public static long Area(IntPtr h) { RECT r; if (!GetWindowRect(h, out r)) return 0; return (long)(r.R - r.L) * (r.B - r.T); }

  // ---- Command panel rollouts -------------------------------------------
  // The command panel shows only part of a long object's rollouts and
  // PrintWindow sees only what is on screen. Each rollout is a native child
  // window of one scroll viewport, and its full height is known even where it
  // is scrolled out of view. So: wheel-scroll the viewport, copy the visible
  // rows of the rollout out of a picture of the main window, repeat until
  // every row is filled, and write one picture (and control list) per rollout.
  static Bitmap ShotOf(IntPtr h) {
    RECT r; GetWindowRect(h, out r);
    var bmp = new Bitmap(r.R - r.L, r.B - r.T, PixelFormat.Format32bppArgb);
    using (var g = Graphics.FromImage(bmp)) { IntPtr dc = g.GetHdc(); PrintWindow(h, dc, 2); g.ReleaseHdc(dc); }
    return bmp;
  }
  static RECT RectOf(IntPtr h) { RECT r; GetWindowRect(h, out r); return r; }
  static bool HasWin32Child(IntPtr h) {
    bool found = false;
    EnumChildWindows(h, (k, l) => { var c = Class(k); if (!c.StartsWith("Qt") && IsWindowVisible(k)) { found = true; return false; } return true; }, IntPtr.Zero);
    return found;
  }
  static void Wheel(IntPtr viewport, int notches) {
    RECT v = RectOf(viewport);
    int x = v.L + 30, y = v.T + 30;
    IntPtr lp = (IntPtr)((y << 16) | (x & 0xFFFF));
    for (int i = 0; i < Math.Abs(notches); i++) {
      int delta = notches < 0 ? -120 : 120;
      PostMessage(viewport, 0x020A, (IntPtr)(delta << 16), lp);   // WM_MOUSEWHEEL
    }
    System.Threading.Thread.Sleep(350);
  }
  public static string Rollouts(IntPtr main, int minX, string dir, string name) {
    var log = new StringBuilder();
    // Candidate rollouts: native Qt windows in the panel that hold Win32 controls;
    // the viewport is the parent most of them share.
    if (minX <= 0) minX = RectOf(main).R - 450;   // the command panel sits at the right
    var byParent = new Dictionary<IntPtr, List<IntPtr>>();
    EnumChildWindows(main, (h, l) => {
      RECT r = RectOf(h);
      if (r.L < minX || !IsWindowVisible(h) || !Class(h).StartsWith("Qt")) return true;
      if (!HasWin32Child(h)) return true;
      // No rollouts: the modifier stack (its list box has id 1689) and Max's
      // bottom panels (view controls, time and animation buttons, status).
      bool isOther = false;
      EnumChildWindows(h, (w, l2) => {
        var wc = Class(w);
        if ((GetDlgCtrlID(w) == 1689 && wc == "ListBox") || wc == "ViewControlPanel" || wc == "TimePanel" ||
            wc == "AnimatePanel" || wc == "StatusPanel" || wc == "TimeSlider") { isOther = true; return false; }
        return true; }, IntPtr.Zero);
      if (isOther) return true;
      IntPtr p = GetParent(h);
      if (!byParent.ContainsKey(p)) byParent[p] = new List<IntPtr>();
      byParent[p].Add(h);
      return true; }, IntPtr.Zero);
    // A candidate that holds other candidates is a container (the panel, the
    // scroll viewport), not a rollout: drop it, else an object with two
    // rollouts ties with the panel's own two parts.
    var all = new HashSet<IntPtr>();
    foreach (var kv in byParent) foreach (var h in kv.Value) all.Add(h);
    var containers = new HashSet<IntPtr>();
    foreach (var h in all) for (IntPtr q = GetParent(h); q != IntPtr.Zero; q = GetParent(q)) if (all.Contains(q)) containers.Add(q);
    foreach (var kv in byParent) kv.Value.RemoveAll(h => containers.Contains(h));
    IntPtr viewport = IntPtr.Zero; int best = 0;
    foreach (var kv in byParent) if (kv.Value.Count > best) { best = kv.Value.Count; viewport = kv.Key; }
    if (viewport == IntPtr.Zero) return "no rollouts found";
    var rolls = byParent[viewport];
    rolls.Sort((a, b) => RectOf(a).T.CompareTo(RectOf(b).T));
    RECT main0 = RectOf(main);
    // Back to the top first.
    for (int i = 0; i < 40; i++) { int t0 = RectOf(rolls[0]).T; Wheel(viewport, 5); if (RectOf(rolls[0]).T == t0) break; }
    int k = 0;
    foreach (var roll in rolls) {
      k++;
      RECT r = RectOf(roll);
      int w = r.R - r.L, hgt = r.B - r.T;
      var full = new Bitmap(w, hgt, PixelFormat.Format32bppArgb);
      var done = new bool[hgt];
      int filled = 0, still = 0, lastTop = int.MinValue;
      for (int step = 0; step < 200 && filled < hgt; step++) {
        r = RectOf(roll);
        RECT v = RectOf(viewport);
        int top = Math.Max(r.T, v.T), bottom = Math.Min(r.B, v.B);
        if (bottom > top) {
          using (var shot = ShotOf(main))
          using (var g = Graphics.FromImage(full)) {
            for (int y = top; y < bottom; y++) {
              int ry = y - r.T;
              if (ry < 0 || ry >= hgt || done[ry]) continue;
              done[ry] = true; filled++;
            }
            g.DrawImage(shot, new Rectangle(0, top - r.T, w, bottom - top),
                        new Rectangle(r.L - main0.L, top - main0.T, w, bottom - top), GraphicsUnit.Pixel);
          }
        }
        if (filled >= hgt) break;
        // Not complete: if the rollout starts below the viewport top it has
        // to come up; scroll one notch down and look again.
        if (r.T == lastTop) { if (++still > 2) break; } else still = 0;
        lastTop = r.T;
        Wheel(viewport, -1);
      }
      string file = System.IO.Path.Combine(dir, name + "_r" + k + ".png");
      full.Save(file, ImageFormat.Png);
      full.Dispose();
      // Controls relative to the rollout (scrolling moves them together).
      System.IO.File.WriteAllText(System.IO.Path.Combine(dir, name + "_r" + k + ".json"),
        "{\"title\":\"\",\"controls\":" + Controls(roll) + "}", new UTF8Encoding(false));
      log.AppendFormat("r{0}: {1}x{2} filled {3}/{2}\n", k, w, hgt, filled);
    }
    return log.ToString();
  }
  public static string Text(IntPtr h) { var s = new StringBuilder(1024); GetWindowText(h, s, 1024); return s.ToString(); }
  public static string Class(IntPtr h) { var s = new StringBuilder(256); GetClassName(h, s, 256); return s.ToString(); }
  public static List<IntPtr> Tops(uint pid) {
    var list = new List<IntPtr>();
    EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid && IsWindowVisible(h)) list.Add(h); return true; }, IntPtr.Zero);
    return list;
  }
  public static string Esc(string s) {
    var b = new StringBuilder();
    foreach (var c in s) {
      if (c == '"' || c == '\\') b.Append('\\').Append(c);
      else if (c < 32) b.Append(string.Format("\\u{0:x4}", (int)c));
      else b.Append(c);
    }
    return b.ToString();
  }
  // Every visible child control, relative to the window, as a JSON array.
  public static string Controls(IntPtr win) {
    RECT w; GetWindowRect(win, out w);
    var sb = new StringBuilder("[");
    bool first = true;
    EnumChildWindows(win, (h, l) => {
      if (!IsWindowVisible(h)) return true;
      RECT r; GetWindowRect(h, out r);
      if (r.R - r.L <= 0 || r.B - r.T <= 0) return true;
      if (!first) sb.Append(",\n"); first = false;
      sb.AppendFormat("{{\"id\":{0},\"parent_id\":{1},\"class\":\"{2}\",\"text\":\"{3}\",\"x\":{4},\"y\":{5},\"w\":{6},\"h\":{7}}}",
        GetDlgCtrlID(h), GetDlgCtrlID(GetParent(h)), Esc(Class(h)), Esc(Text(h)),
        r.L - w.L, r.T - w.T, r.R - r.L, r.B - r.T);
      return true; }, IntPtr.Zero);
    sb.Append("]");
    return sb.ToString();
  }
  public static bool Shot(IntPtr h, string path) {
    RECT r; if (!GetWindowRect(h, out r)) return false;
    int w = r.R - r.L, ht = r.B - r.T; if (w <= 0 || ht <= 0) return false;
    using (var bmp = new Bitmap(w, ht, PixelFormat.Format32bppArgb)) {
      using (var g = Graphics.FromImage(bmp)) { IntPtr dc = g.GetHdc(); PrintWindow(h, dc, 2); g.ReleaseHdc(dc); }
      bmp.Save(path, ImageFormat.Png);
    }
    return true;
  }
}
'@
[Cap]::SetProcessDPIAware() | Out-Null

$here = $PSScriptRoot
$Out = (New-Item -ItemType Directory -Force $Out).FullName
$log = Join-Path $Out '_capture.log'
function Say($m) { Add-Content $log "$(Get-Date -Format T) $m" -Encoding utf8 }

# ---- private Max config: a copy of the user's 3dsMax.ini with its own PlugCFG ----
$userIni = Join-Path $env:LOCALAPPDATA "Autodesk\3dsMax\$MaxVer - 64bit\ENU\3dsMax.ini"
$userCfg = Join-Path $env:LOCALAPPDATA "Autodesk\3dsMax\$MaxVer - 64bit\ENU\en-US\plugcfg"
$cfgRoot = Join-Path $Out "_maxcfg$MaxVer"
$cfg = Join-Path $cfgRoot 'plugcfg'
New-Item -ItemType Directory -Force $cfg | Out-Null
$ini = Join-Path $cfgRoot '3dsMax.ini'
$text = [IO.File]::ReadAllText($userIni)   # UTF-16 with BOM
$text = [regex]::Replace($text, '(?m)^PlugCFG=.*$', "PlugCFG=$cfg")
[IO.File]::WriteAllText($ini, $text, [Text.Encoding]::Unicode)
# Game paths and MPQ order come along so the dialogs show real data. The
# import and export options do not: the manual shows their defaults.
foreach ($n in 'WhiteoutDex_Settings.ini', 'WhiteoutDexMPQ.ini') {
  $src = Join-Path $userCfg $n
  if ((Test-Path $src) -and -not (Test-Path (Join-Path $cfg $n))) { Copy-Item $src $cfg }
}
foreach ($n in 'MDLXExporter.ini', 'WhiteoutDexImporter.ini', 'WhiteoutDex_MaterialFix.ini') { Remove-Item (Join-Path $cfg $n) -ErrorAction SilentlyContinue }

# The game folders of whoever runs this, to be shown as the standard folder.
$script:standardPath = 'C:\Program Files (x86)\Warcraft III'
$script:masks = @()          # game folders -> $standardPath
$script:maskPairs = @()      # other personal paths -> a neutral one
foreach ($pair in @(@('WhiteoutDexMPQ.ini', 'Directory'), @('WhiteoutDex_Settings.ini', 'W3Path'), @('WhiteoutDexImporter.ini', 'MPQDirectory'), @('WhiteoutDexImporter.ini', 'CASCDirectory'))) {
  $f = Join-Path $userCfg $pair[0]
  if (Test-Path $f) {
    foreach ($line in Get-Content $f) {
      if ($line -match ('^\s*' + $pair[1] + '\s*=\s*(.+?)\s*$')) {
        $v = $Matches[1].TrimEnd('\')
        if ($v -and $v -ne $script:standardPath -and $script:masks -notcontains $v) { $script:masks += $v }
      }
    }
  }
}

# The work folder of this run (where the sample model is imported from) and,
# as a last resort, the user's profile folder.
$script:maskPairs += ,@((Join-Path $Out '_work'), 'C:\Models')
$script:maskPairs += ,@($env:USERPROFILE, 'C:\Users\Name')

# ---- the job capture.ms reads ----
$job = @("out=$Out", "langs=$($Langs -join ',')", "only=$($Only -join ',')", "samples=$((Resolve-Path (Join-Path $here '..\..\..\samples')).Path)")
Set-Content (Join-Path $here 'capture_job.txt') $job -Encoding utf8
Remove-Item (Join-Path $Out '_want.txt'), (Join-Path $Out '_armed.txt'), (Join-Path $Out '_done.txt'), (Join-Path $Out '_finished.txt') -ErrorAction SilentlyContinue

# After a Max that was killed, the next start asks "Startup Failure
# Detection". Answer it like a person would want: Continue Without Restore
# (never Restore Factory Settings). The dialog is Qt, so UI Automation.
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
$script:lastStartupCheck = [DateTime]::MinValue
function ContinueStartup {
  if (((Get-Date) - $script:lastStartupCheck).TotalSeconds -lt 3) { return }
  $script:lastStartupCheck = Get-Date
  foreach ($h in [Cap]::Tops([uint32]$p.Id)) {
    if ([Cap]::Text($h) -notlike '*Startup Failure*') { continue }
    try {
      $A = [System.Windows.Automation.AutomationElement]
      $b = $A::FromHandle($h).FindFirst([System.Windows.Automation.TreeScope]::Descendants,
             (New-Object System.Windows.Automation.PropertyCondition($A::NameProperty, 'Continue Without Restore')))
      if ($b) { $b.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke(); Say 'Startup Failure Detection: Continue Without Restore' }
    } catch { Say "Startup Failure Detection: $_" }
  }
}
# A .NET "Unhandled exception" box blocks Max until someone answers it. In
# Russian, 3ds Max's own Scene Explorer (DevExpress) raises one after an
# import (seen 2026-10-10). Log it and press Continue, as a user would.
function DismissNetErrors {
  foreach ($h in [Cap]::Tops([uint32]$p.Id)) {
    if ([Cap]::Text($h) -ne 'Microsoft .NET') { continue }
    $msg = ([Cap]::AllTexts($h) -split '\|')[1]
    if ([Cap]::ClickText($h, '&Continue')) { Say ".NET error box answered with Continue:$msg" }
  }
}
function Snapshot { $h = @{}; foreach ($w in [Cap]::Tops([uint32]$p.Id)) { $h[$w] = $true }; $h }
function WaitNew($before) {
  $t0 = Get-Date
  while (((Get-Date) - $t0).TotalSeconds -lt $script:waitSeconds) {
    Start-Sleep -Milliseconds 300
    DismissNetErrors
    $new = @([Cap]::Tops([uint32]$p.Id) | Where-Object { -not $before.ContainsKey($_) -and [Cap]::Text($_) -ne 'Microsoft .NET' })
    if ($new.Count -gt 0) { return $new[-1] }
  }
  [IntPtr]::Zero
}
function SaveShot($win, $dir, $name) {
  Say "${lang} ${name}: window after $([int]((Get-Date) - $script:armTime).TotalSeconds) s"
  # Personal game paths become the standard one before the picture is taken.
  $masked = 0
  foreach ($m in $script:masks) { $masked += [Cap]::Mask($win, $m, $script:standardPath) }
  foreach ($pr in $script:maskPairs) { $masked += [Cap]::Mask($win, $pr[0], $pr[1]) }
  if ($masked) { Start-Sleep -Milliseconds 300 }
  [Cap]::Shot($win, (Join-Path $dir "$name.png")) | Out-Null
  $json = '{"title":"' + [Cap]::Esc([Cap]::Text($win)) + '","controls":' + [Cap]::Controls($win) + '}'
  [IO.File]::WriteAllText((Join-Path $dir "$name.json"), $json, (New-Object Text.UTF8Encoding $false))
  Say "${lang} ${name}: '$([Cap]::Text($win))' [$([Cap]::Class($win))]"
}
function CloseDlg($w) {
  [Cap]::PostMessage($w, 0x0111, [IntPtr]2, [IntPtr]::Zero) | Out-Null   # WM_COMMAND IDCANCEL
  Start-Sleep -Milliseconds 500
  if ([Cap]::IsWindow($w)) { [Cap]::PostMessage($w, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null }  # WM_CLOSE
  Start-Sleep -Milliseconds 300
}

$exe = "C:\Program Files\Autodesk\3ds Max $MaxVer\3dsmax.exe"
$ms = Join-Path $here 'capture.ms'
$p = Start-Process $exe -ArgumentList "-i `"$ini`" -U MAXScript `"$ms`"" -PassThru
Say "Max $MaxVer pid $($p.Id)"
$end = (Get-Date).AddMinutes($MaxMinutes)
$wantFile = Join-Path $Out '_want.txt'
$script:lastActivity = Get-Date
$script:waitSeconds = $WaitSeconds
while (-not $p.HasExited -and (Get-Date) -lt $end) {
  if (-not (Test-Path $wantFile)) {
    ContinueStartup
    DismissNetErrors
    # Nothing asked for a minute: log what Max shows (a popup that blocks it?).
    if (((Get-Date) - $script:lastActivity).TotalSeconds -gt 60) {
      $script:lastActivity = Get-Date
      Say "idle 60 s, windows of Max:"
      foreach ($h in [Cap]::Tops([uint32]$p.Id)) {
        Say "   [$([Cap]::Class($h))] '$([Cap]::Text($h))' $([Cap]::AllTexts($h))"
      }
    }
    Start-Sleep -Milliseconds 150; continue
  }
  $script:lastActivity = Get-Date
  $want = (Get-Content $wantFile -Raw -Encoding utf8).Trim()
  # <lang>|<shot>|modal or self|steps; steps are ";"-separated:
  #   tab:<button id>:<shot>    click a tab button, shoot the same window
  #   open:<button id>:<shot>   click a button, shoot the window it opens, close that
  #   clickat:<id>:<x>:<y>:<shot>  mouse click inside a control, shoot the window
  #   settext:<id>=<text>       before the first shot: show this text in a control
  #                             (only the display; used to hide personal paths)
  $f = $want.Split('|'); $lang = $f[0]; $id = $f[1]; $modal = $f[2] -eq 'modal'
  $steps = if ($f.Count -gt 3 -and $f[3]) { $f[3].Split(';') } else { @() }
  # Snapshot, then let capture.ms open the dialog: whatever window is new
  # afterwards is the dialog.
  $before = Snapshot
  Remove-Item $wantFile -ErrorAction SilentlyContinue
  Set-Content (Join-Path $Out '_armed.txt') 'ok'
  $script:armTime = Get-Date
  if ($f[2] -like 'existing=*') {
    # A window that is already open, found by its title; the steps act on it.
    $title = $f[2].Substring(9)
    $win = [Cap]::Tops([uint32]$p.Id) | Where-Object { [Cap]::Text($_) -eq $title } | Select-Object -First 1
    if (-not $win) { $win = [IntPtr]::Zero }
    $dir = (New-Item -ItemType Directory -Force (Join-Path $Out $lang)).FullName
    if ($win -ne [IntPtr]::Zero) {
      foreach ($st in $steps) {
        $k = $st.Split(':'); $name = $k[-1]
        $bid = 0; if (-not [int]::TryParse($k[1], [ref]$bid)) { $bid = [Cap]::IdOfText($win, $k[1]) }
        if ($k[0] -eq 'open') {
          $b2 = Snapshot
          [Cap]::Click($win, $bid)
          $sub = WaitNew $b2
          if ($sub -ne [IntPtr]::Zero) { Start-Sleep -Milliseconds 700; SaveShot $sub $dir $name; CloseDlg $sub } else { Say "${lang} ${name}: NO WINDOW after clicking $bid" }
        } elseif ($k[0] -eq 'tab') { [Cap]::Click($win, $bid); Start-Sleep -Milliseconds 800; SaveShot $win $dir $name }
      }
    } else { Say "${lang} ${id}: no open window '$title'" }
    # Every other window of Max (popups the test should know about)
    foreach ($h in [Cap]::Tops([uint32]$p.Id)) { if ([Cap]::Class($h) -eq '#32770') { Say "   popup [$([Cap]::Text($h))]: $(([Cap]::Controls($h)) -replace '\s+', ' ')" } }
    Set-Content (Join-Path $Out '_done.txt') 'ok'
    continue
  }
  if ($f[2] -eq 'panel') {
    # Command panel rollouts of the selected object (see Cap.Rollouts).
    Start-Sleep -Milliseconds 1500
    $mw = [Cap]::Tops([uint32]$p.Id) | Sort-Object { [Cap]::Area($_) } -Descending | Select-Object -First 1
    $pdir = (New-Item -ItemType Directory -Force (Join-Path $Out $lang)).FullName
    foreach ($m in $script:masks) { [Cap]::Mask($mw, $m, $script:standardPath) | Out-Null }
    foreach ($pr in $script:maskPairs) { [Cap]::Mask($mw, $pr[0], $pr[1]) | Out-Null }
    $res = [Cap]::Rollouts($mw, 0, $pdir, $id)
    Say "${lang} ${id} rollouts: $($res -replace "`n", '; ')"
    Set-Content (Join-Path $Out '_done.txt') 'ok'
    continue
  }
  if ($f[2] -like 'docked=*') {
    # A window docked into Max's main window, found by its title.
    Start-Sleep -Milliseconds 1500
    $mw = [Cap]::Tops([uint32]$p.Id) | Sort-Object { [Cap]::Area($_) } -Descending | Select-Object -First 1
    $title = $f[2].Substring(7)
    $cw = [Cap]::FindChild($mw, $title)
    $pdir = (New-Item -ItemType Directory -Force (Join-Path $Out $lang)).FullName
    if ($cw -ne [IntPtr]::Zero) { SaveShot $cw $pdir $id } else { Say "${lang} ${id}: no docked window '$title'" }
    Set-Content (Join-Path $Out '_done.txt') 'ok'
    continue
  }
  if ($f[2] -eq 'mtlpanel') {
    # Rollouts of the active slot in the Compact Material Editor.
    $mw = WaitNew $before
    if ($mw -eq [IntPtr]::Zero) { $mw = [Cap]::Tops([uint32]$p.Id) | Where-Object { [Cap]::Text($_) -like '*Material Editor*' } | Select-Object -First 1 }
    Start-Sleep -Milliseconds 1500
    $pdir = (New-Item -ItemType Directory -Force (Join-Path $Out $lang)).FullName
    if ($mw) {
      SaveShot $mw $pdir "${id}_window"
      $res = [Cap]::Rollouts($mw, [Cap]::LeftOf($mw), $pdir, $id)
      Say "${lang} ${id} rollouts: $($res -replace "`n", '; ')"
    } else { Say "${lang} ${id}: NO MATERIAL EDITOR" }
    Set-Content (Join-Path $Out '_done.txt') 'ok'
    continue
  }
  if ($f[2] -eq 'main') {
    # The Max main window itself (command panel, viewports): the largest
    # visible top-level window of the process.
    Start-Sleep -Milliseconds 1500
    $win = [Cap]::Tops([uint32]$p.Id) | Sort-Object { [Cap]::Area($_) } -Descending | Select-Object -First 1
  } else {
    $win = WaitNew $before
  }
  $dir = (New-Item -ItemType Directory -Force (Join-Path $Out $lang)).FullName
  if ($win -ne [IntPtr]::Zero) {
    Start-Sleep -Milliseconds 700   # let it paint
    foreach ($st in $steps | Where-Object { $_ -like 'settext:*' }) {
      $kv = $st.Substring(8); $eq = $kv.IndexOf('=')
      [Cap]::SetText($win, [int]$kv.Substring(0, $eq), $kv.Substring($eq + 1))
    }
    if ($steps -like 'settext:*') { Start-Sleep -Milliseconds 300 }
    SaveShot $win $dir $id
    foreach ($st in $steps | Where-Object { $_ -notlike 'settext:*' }) {
      # The button can be given by id or by its text ("open:Finish:shot").
      $k = $st.Split(':'); $name = $k[-1]
      $bid = 0; if (-not [int]::TryParse($k[1], [ref]$bid)) { $bid = [Cap]::IdOfText($win, $k[1]) }
      if ($k[0] -eq 'clickat') {
        [Cap]::ClickAt($win, $bid, [int]$k[2], [int]$k[3]); Start-Sleep -Milliseconds 800
        SaveShot $win $dir $name
      } elseif ($k[0] -eq 'tab') {
        [Cap]::Click($win, $bid); Start-Sleep -Milliseconds 800
        SaveShot $win $dir $name
      } elseif ($k[0] -eq 'open') {
        $b2 = Snapshot
        [Cap]::Click($win, $bid)
        $sub = WaitNew $b2
        if ($sub -ne [IntPtr]::Zero) {
          Start-Sleep -Milliseconds 700
          SaveShot $sub $dir $name
          CloseDlg $sub
        } else { Say "${lang} ${name}: NO WINDOW after clicking $bid" }
      }
    }
    # "modal" dialogs are closed here; "self" ones by capture.ms; "close"
    # ones (non-modal tool windows) here as well, after the last shot.
    if ($modal -or $f[2] -eq 'close') { CloseDlg $win }
  } else {
    Say "${lang} ${id}: NO WINDOW"
    # Every visible window of Max, to see what came up instead.
    foreach ($h in [Cap]::Tops([uint32]$p.Id)) { Say "   [$([Cap]::Class($h))] '$([Cap]::Text($h))'" }
  }
  Set-Content (Join-Path $Out '_done.txt') 'ok'
}
if (-not $p.HasExited) { Say 'timeout, Max killed'; Stop-Process -Id $p.Id -Force }
Say 'end'
