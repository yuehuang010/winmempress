#requires -Version 7.0

# Renders the shipped app mark (navy chip silhouette over four pressure bars)
# into the PNG asset set an MSIX package needs: tile logos, Store logo, and the
# taskbar/Start target-size variants. Output goes to packaging/Assets.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

try {
    Add-Type -AssemblyName System.Drawing
}
catch {
    throw 'System.Drawing is required. Run this script on Windows with PowerShell 7.'
}

$repoRoot = Split-Path -Parent ([System.IO.Path]::GetFullPath($PSScriptRoot))
$assetDirectory = Join-Path $repoRoot 'packaging\Assets'
$null = New-Item -ItemType Directory -Path $assetDirectory -Force

$designSize = 256.0

$navy = [System.Drawing.Color]::FromArgb(255, 25, 45, 68)
$green = [System.Drawing.Color]::FromArgb(255, 42, 181, 125)
$yellow = [System.Drawing.Color]::FromArgb(255, 245, 195, 66)
$orange = [System.Drawing.Color]::FromArgb(255, 239, 126, 58)
$red = [System.Drawing.Color]::FromArgb(255, 218, 65, 72)

function New-MarkRectangle {
    param(
        [double]$X,
        [double]$Y,
        [double]$Width,
        [double]$Height,
        [double]$Scale,
        [double]$OffsetX,
        [double]$OffsetY
    )

    return [System.Drawing.RectangleF]::new(
        [float]($OffsetX + $X * $Scale),
        [float]($OffsetY + $Y * $Scale),
        [float]($Width * $Scale),
        [float]($Height * $Scale))
}

function Fill-RoundedRectangle {
    param(
        [System.Drawing.Graphics]$Graphics,
        [System.Drawing.Brush]$Brush,
        [System.Drawing.RectangleF]$Rectangle,
        [double]$Radius
    )

    $radiusValue = [float]$Radius
    $diameter = $radiusValue * 2.0
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    try {
        $path.AddArc($Rectangle.X, $Rectangle.Y, $diameter, $diameter, 180, 90)
        $path.AddArc($Rectangle.Right - $diameter, $Rectangle.Y, $diameter, $diameter, 270, 90)
        $path.AddArc($Rectangle.Right - $diameter, $Rectangle.Bottom - $diameter, $diameter, $diameter, 0, 90)
        $path.AddArc($Rectangle.X, $Rectangle.Bottom - $diameter, $diameter, $diameter, 90, 90)
        $path.CloseFigure()
        $Graphics.FillPath($Brush, $path)
    }
    finally {
        $path.Dispose()
    }
}

function Draw-Mark {
    param(
        [System.Drawing.Graphics]$Graphics,
        [double]$Scale,
        [double]$OffsetX,
        [double]$OffsetY
    )

    $chipBrush = [System.Drawing.SolidBrush]::new($navy)
    $barBrushes = @(
        [System.Drawing.SolidBrush]::new($green),
        [System.Drawing.SolidBrush]::new($yellow),
        [System.Drawing.SolidBrush]::new($orange),
        [System.Drawing.SolidBrush]::new($red)
    )
    try {
        # Chip pins: four along each edge of the body.
        $pins = @()
        foreach ($offset in 67, 96, 125, 154) {
            $pins += , @(38, $offset, 22, 14)
            $pins += , @(196, $offset, 22, 14)
            $pins += , @($offset, 38, 14, 22)
            $pins += , @($offset, 196, 14, 22)
        }
        foreach ($pin in $pins) {
            $Graphics.FillRectangle(
                $chipBrush,
                (New-MarkRectangle $pin[0] $pin[1] $pin[2] $pin[3] $Scale $OffsetX $OffsetY))
        }

        Fill-RoundedRectangle $Graphics $chipBrush `
            (New-MarkRectangle 54 54 148 148 $Scale $OffsetX $OffsetY) (20 * $Scale)

        $barTops = 72, 104, 136, 168
        for ($index = 0; $index -lt $barTops.Count; $index++) {
            Fill-RoundedRectangle $Graphics $barBrushes[$index] `
                (New-MarkRectangle 72 $barTops[$index] 112 24 $Scale $OffsetX $OffsetY) (6 * $Scale)
        }
    }
    finally {
        $chipBrush.Dispose()
        foreach ($brush in $barBrushes) { $brush.Dispose() }
    }
}

function Write-Asset {
    param(
        [string]$FileName,
        [int]$Width,
        [int]$Height
    )

    $bitmap = [System.Drawing.Bitmap]::new(
        $Width,
        $Height,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.Clear([System.Drawing.Color]::Transparent)

        # The mark is square; on wide tiles it is centered with the tile height
        # driving its size, matching how Windows renders a square logo wide.
        $markSize = [Math]::Min($Width, $Height)
        Draw-Mark $graphics ($markSize / $designSize) (($Width - $markSize) / 2.0) (($Height - $markSize) / 2.0)

        $bitmap.Save((Join-Path $assetDirectory $FileName), [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

# Scale-qualified tile and Store logos. Sizes are the Microsoft-documented
# values for 100/125/150/200/400 percent.
$scaledAssets = @(
    @{ Name = 'Square44x44Logo'; Width = 44; Height = 44 },
    @{ Name = 'Square150x150Logo'; Width = 150; Height = 150 },
    @{ Name = 'Wide310x150Logo'; Width = 310; Height = 150 },
    @{ Name = 'StoreLogo'; Width = 50; Height = 50 }
)
$scales = @(
    @{ Percent = 100; Factor = 1.00 },
    @{ Percent = 125; Factor = 1.25 },
    @{ Percent = 150; Factor = 1.50 },
    @{ Percent = 200; Factor = 2.00 },
    @{ Percent = 400; Factor = 4.00 }
)

$written = 0
foreach ($asset in $scaledAssets) {
    foreach ($scale in $scales) {
        $width = [int][Math]::Ceiling($asset.Width * $scale.Factor)
        $height = [int][Math]::Ceiling($asset.Height * $scale.Factor)
        Write-Asset "$($asset.Name).scale-$($scale.Percent).png" $width $height
        $written++
    }
}

# Target-size variants drive the taskbar, Start list, and Alt+Tab. The unplated
# form is the one shown without the accent-colored backplate.
foreach ($size in 16, 24, 32, 48, 256) {
    Write-Asset "Square44x44Logo.targetsize-$size.png" $size $size
    Write-Asset "Square44x44Logo.targetsize-${size}_altform-unplated.png" $size $size
    $written += 2
}

# Unqualified fallbacks so the package remains renderable if resource
# resolution ever falls back to the literal manifest paths.
Write-Asset 'Square44x44Logo.png' 44 44
Write-Asset 'Square150x150Logo.png' 150 150
Write-Asset 'Wide310x150Logo.png' 310 150
Write-Asset 'StoreLogo.png' 50 50
$written += 4

foreach ($file in Get-ChildItem -LiteralPath $assetDirectory -Filter '*.png') {
    if ($file.Length -le 0) { throw "Generated asset is empty: $($file.FullName)" }
}

Write-Output "Generated $written store assets in $assetDirectory"
