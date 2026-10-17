# shot.ps1 <saida.png> - captura a tela SO se a janela em primeiro plano for do azimuth.exe (nao captura a area de trabalho)
param([string]$out)
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class Fg { [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid); }
'@
$h = [Fg]::GetForegroundWindow(); $procId = 0; [void][Fg]::GetWindowThreadProcessId($h, [ref]$procId)
$p = Get-Process -Id $procId -ErrorAction SilentlyContinue
if (-not $p -or $p.ProcessName -ne 'azimuth') { Write-Output "primeiro plano: $($p.ProcessName) - sem captura"; exit 1 }
$b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
$g = [System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png); Write-Output "ok $out"
