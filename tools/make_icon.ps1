# BootHop icon (B3: power ring, amber "1" with a foot) drawn with GDI+ at each size, packed as PNG entries in one .ico
param([string]$Out, [string]$Preview)
Add-Type -AssemblyName System.Drawing
$sizes = 16, 20, 24, 32, 40, 48, 64, 256
$blue = [System.Drawing.Color]::FromArgb(255, 0x2f, 0x7c, 0xf6)
$amber = [System.Drawing.Color]::FromArgb(255, 0xf2, 0xa3, 0x3a)

function Draw([int]$n) {
    $bmp = New-Object System.Drawing.Bitmap $n, $n, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'; $g.PixelOffsetMode = 'HighQuality'; $g.Clear([System.Drawing.Color]::Transparent)
    $k = $n / 64.0
    $g.ScaleTransform($k, $k); $g.TranslateTransform(0, 0.75)
    $round = [System.Drawing.Drawing2D.LineCap]::Round
    $ring = New-Object System.Drawing.Pen $blue, 6.5; $ring.StartCap = $round; $ring.EndCap = $round
    $g.DrawArc($ring, 11, 14, 42, 42, -50, 280)
    $one = New-Object System.Drawing.Pen $amber, 6.5; $one.StartCap = $round; $one.EndCap = $round; $one.LineJoin = 'Round'
    $pts = [System.Drawing.PointF[]]@((New-Object System.Drawing.PointF 26, 11.5), (New-Object System.Drawing.PointF 32, 6.5), (New-Object System.Drawing.PointF 32, 38))
    $g.DrawLines($one, $pts)
    $foot = New-Object System.Drawing.Pen $amber, 5.5; $foot.StartCap = $round; $foot.EndCap = $round
    $g.DrawLine($foot, 25, 39, 39, 39)
    $g.Dispose()
    return $bmp
}

$images = @()
foreach ($n in $sizes) {
    $bmp = Draw $n
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $images += , @($n, $ms.ToArray())
    $bmp.Dispose()
}

# ICONDIR + ICONDIRENTRY per image + the PNG data
$fs = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter $fs
$w.Write([UInt16]0); $w.Write([UInt16]1); $w.Write([UInt16]$images.Count)
$offset = 6 + 16 * $images.Count
foreach ($im in $images) {
    $n = $im[0]; $data = $im[1]
    $b = if ($n -ge 256) { 0 } else { $n }
    $w.Write([byte]$b); $w.Write([byte]$b); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([UInt16]1); $w.Write([UInt16]32); $w.Write([UInt32]$data.Length); $w.Write([UInt32]$offset)
    $offset += $data.Length
}
foreach ($im in $images) { $w.Write([byte[]]$im[1]) }
$w.Flush()
[System.IO.File]::WriteAllBytes($Out, $fs.ToArray())

# a preview sheet: every size on light and on dark
$W = [int]40 + ($sizes | Measure-Object -Sum).Sum + 16 * $sizes.Count
$sheet = New-Object System.Drawing.Bitmap ([int]$W), 600
$g = [System.Drawing.Graphics]::FromImage($sheet)
$g.Clear([System.Drawing.Color]::White)
$g.FillRectangle((New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 0x20, 0x28, 0x33))), 0, 300, $W, 300)
$x = 20
foreach ($n in $sizes) {
    $bmp = Draw $n
    $g.DrawImage($bmp, $x, 280 - $n); $g.DrawImage($bmp, $x, 580 - $n)
    $x += $n + 16; $bmp.Dispose()
}
$g.Dispose()
$sheet.Save($Preview, [System.Drawing.Imaging.ImageFormat]::Png)
"wrote $Out ($((Get-Item $Out).Length) bytes)"
