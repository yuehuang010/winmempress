#requires -Version 7.0

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

try {
    Add-Type -AssemblyName System.Drawing
}
catch {
    throw 'System.Drawing is required. Run this script on Windows with PowerShell 7.'
}

$scriptRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$outputDirectory = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot '..\assets\icon-candidates'))
$null = New-Item -ItemType Directory -Path $outputDirectory -Force

$designSize = 256.0
$iconSizes = @(16, 32, 48, 256)

# The palette is intentionally small so every icon remains clear at 16x16.
$navy = [System.Drawing.Color]::FromArgb(255, 25, 45, 68)
$green = [System.Drawing.Color]::FromArgb(255, 42, 181, 125)
$yellow = [System.Drawing.Color]::FromArgb(255, 245, 195, 66)
$orange = [System.Drawing.Color]::FromArgb(255, 239, 126, 58)
$red = [System.Drawing.Color]::FromArgb(255, 218, 65, 72)
$darkGray = [System.Drawing.Color]::FromArgb(255, 67, 76, 88)
$white = [System.Drawing.Color]::FromArgb(255, 248, 250, 252)

function Scale-Value {
    param(
        [double]$Value,
        [int]$CanvasSize
    )

    return [float]($Value * $CanvasSize / $designSize)
}

function New-Brush {
    param(
        [System.Drawing.Color]$Color
    )

    return [System.Drawing.SolidBrush]::new($Color)
}

function New-Pen {
    param(
        [System.Drawing.Color]$Color,
        [double]$Width,
        [int]$CanvasSize
    )

    $pen = [System.Drawing.Pen]::new($Color, (Scale-Value $Width $CanvasSize))
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    return $pen
}

function New-RoundedRectanglePath {
    param(
        [System.Drawing.RectangleF]$Rectangle,
        [double]$Radius
    )

    $radiusValue = [float]$Radius
    $diameter = $radiusValue * 2.0
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $path.AddArc($Rectangle.X, $Rectangle.Y, $diameter, $diameter, 180, 90)
    $path.AddArc($Rectangle.Right - $diameter, $Rectangle.Y, $diameter, $diameter, 270, 90)
    $path.AddArc($Rectangle.Right - $diameter, $Rectangle.Bottom - $diameter, $diameter, $diameter, 0, 90)
    $path.AddArc($Rectangle.X, $Rectangle.Bottom - $diameter, $diameter, $diameter, 90, 90)
    $path.CloseFigure()
    return $path
}

function Get-Rectangle {
    param(
        [double]$X,
        [double]$Y,
        [double]$Width,
        [double]$Height,
        [int]$CanvasSize
    )

    return [System.Drawing.RectangleF]::new(
        (Scale-Value $X $CanvasSize),
        (Scale-Value $Y $CanvasSize),
        (Scale-Value $Width $CanvasSize),
        (Scale-Value $Height $CanvasSize))
}

function Get-Point {
    param(
        [double]$X,
        [double]$Y,
        [int]$CanvasSize
    )

    return [System.Drawing.PointF]::new(
        (Scale-Value $X $CanvasSize),
        (Scale-Value $Y $CanvasSize))
}

function Fill-RoundedRectangle {
    param(
        [System.Drawing.Graphics]$Graphics,
        [System.Drawing.Brush]$Brush,
        [System.Drawing.RectangleF]$Rectangle,
        [double]$Radius
    )

    $path = New-RoundedRectanglePath $Rectangle $Radius
    try {
        $Graphics.FillPath($Brush, $path)
    }
    finally {
        $path.Dispose()
    }
}

function Draw-RoundedRectangle {
    param(
        [System.Drawing.Graphics]$Graphics,
        [System.Drawing.Pen]$Pen,
        [System.Drawing.RectangleF]$Rectangle,
        [double]$Radius
    )

    $path = New-RoundedRectanglePath $Rectangle $Radius
    try {
        $Graphics.DrawPath($Pen, $path)
    }
    finally {
        $path.Dispose()
    }
}

function Draw-CandidateOne {
    param(
        [System.Drawing.Graphics]$Graphics,
        [int]$CanvasSize
    )

    # RAM silhouette with three bold contact slots and a pressure gauge overlay.
    $moduleBrush = New-Brush $navy
    $greenBrush = New-Brush $green
    $redPen = New-Pen $red 14 $CanvasSize
    $greenPen = New-Pen $green 14 $CanvasSize
    try {
        Fill-RoundedRectangle $Graphics $moduleBrush (Get-Rectangle 30 70 196 116 $CanvasSize) 18

        $Graphics.FillRectangle($greenBrush, (Get-Rectangle 53 158 22 14 $CanvasSize))
        $Graphics.FillRectangle($greenBrush, (Get-Rectangle 91 158 22 14 $CanvasSize))
        $Graphics.FillRectangle($greenBrush, (Get-Rectangle 129 158 22 14 $CanvasSize))

        $Graphics.DrawArc($greenPen, (Get-Rectangle 143 43 76 76 $CanvasSize), 205, 130)
        $Graphics.DrawLine($redPen, (Get-Point 181 81 $CanvasSize), (Get-Point 199 62 $CanvasSize))
        $Graphics.FillEllipse($greenBrush, (Get-Rectangle 173 73 16 16 $CanvasSize))
    }
    finally {
        $moduleBrush.Dispose()
        $greenBrush.Dispose()
        $redPen.Dispose()
        $greenPen.Dispose()
    }
}

function Draw-CandidateTwo {
    param(
        [System.Drawing.Graphics]$Graphics,
        [int]$CanvasSize
    )

    # Rounded-square gauge using the four pressure states as its complete palette.
    $greenPen = New-Pen $green 14 $CanvasSize
    $yellowPen = New-Pen $yellow 14 $CanvasSize
    $orangePen = New-Pen $orange 14 $CanvasSize
    $redPen = New-Pen $red 14 $CanvasSize
    $redBrush = New-Brush $red
    try {
        Draw-RoundedRectangle $Graphics $greenPen (Get-Rectangle 28 28 200 200 $CanvasSize) 40

        $gaugeBounds = Get-Rectangle 61 61 134 134 $CanvasSize
        $Graphics.DrawArc($greenPen, $gaugeBounds, 135, 45)
        $Graphics.DrawArc($yellowPen, $gaugeBounds, 180, 45)
        $Graphics.DrawArc($orangePen, $gaugeBounds, 225, 45)
        $Graphics.DrawArc($redPen, $gaugeBounds, 270, 45)

        $Graphics.DrawLine($redPen, (Get-Point 128 128 $CanvasSize), (Get-Point 174 88 $CanvasSize))
        $Graphics.FillEllipse($redBrush, (Get-Rectangle 117 117 22 22 $CanvasSize))
    }
    finally {
        $greenPen.Dispose()
        $yellowPen.Dispose()
        $orangePen.Dispose()
        $redPen.Dispose()
        $redBrush.Dispose()
    }
}

function Draw-CandidateThree {
    param(
        [System.Drawing.Graphics]$Graphics,
        [int]$CanvasSize
    )

    # Chip silhouette with chunky pins and a single pressure-status dot.
    $chipBrush = New-Brush $navy
    $statusBrush = New-Brush $green
    try {
        $pinRectangles = @(
            (Get-Rectangle 38 67 22 14 $CanvasSize),
            (Get-Rectangle 38 96 22 14 $CanvasSize),
            (Get-Rectangle 38 125 22 14 $CanvasSize),
            (Get-Rectangle 196 67 22 14 $CanvasSize),
            (Get-Rectangle 196 96 22 14 $CanvasSize),
            (Get-Rectangle 196 125 22 14 $CanvasSize),
            (Get-Rectangle 67 38 14 22 $CanvasSize),
            (Get-Rectangle 96 38 14 22 $CanvasSize),
            (Get-Rectangle 125 38 14 22 $CanvasSize),
            (Get-Rectangle 67 196 14 22 $CanvasSize),
            (Get-Rectangle 96 196 14 22 $CanvasSize),
            (Get-Rectangle 125 196 14 22 $CanvasSize)
        )
        foreach ($pinRectangle in $pinRectangles) {
            $Graphics.FillRectangle($chipBrush, $pinRectangle)
        }

        Fill-RoundedRectangle $Graphics $chipBrush (Get-Rectangle 54 54 148 148 $CanvasSize) 20
        $Graphics.FillEllipse($statusBrush, (Get-Rectangle 161 68 34 34 $CanvasSize))
    }
    finally {
        $chipBrush.Dispose()
        $statusBrush.Dispose()
    }
}

function Draw-CandidateFour {
    param(
        [System.Drawing.Graphics]$Graphics,
        [int]$CanvasSize
    )

    # Four-level vertical meter beside a chip outline. The outline reuses green
    # so the icon has exactly the four pressure-state colors.
    $greenBrush = New-Brush $green
    $yellowBrush = New-Brush $yellow
    $orangeBrush = New-Brush $orange
    $redBrush = New-Brush $red
    $greenPen = New-Pen $green 12 $CanvasSize
    try {
        Fill-RoundedRectangle $Graphics $greenBrush (Get-Rectangle 27 37 58 42 $CanvasSize) 10
        Fill-RoundedRectangle $Graphics $yellowBrush (Get-Rectangle 27 85 58 42 $CanvasSize) 10
        Fill-RoundedRectangle $Graphics $orangeBrush (Get-Rectangle 27 133 58 42 $CanvasSize) 10
        Fill-RoundedRectangle $Graphics $redBrush (Get-Rectangle 27 181 58 38 $CanvasSize) 10

        Draw-RoundedRectangle $Graphics $greenPen (Get-Rectangle 121 57 96 142 $CanvasSize) 18
        foreach ($y in @(76, 108, 140, 172)) {
            $Graphics.DrawLine($greenPen, (Get-Point 106 $y $CanvasSize), (Get-Point 121 $y $CanvasSize))
            $Graphics.DrawLine($greenPen, (Get-Point 217 $y $CanvasSize), (Get-Point 232 $y $CanvasSize))
        }
    }
    finally {
        $greenBrush.Dispose()
        $yellowBrush.Dispose()
        $orangeBrush.Dispose()
        $redBrush.Dispose()
        $greenPen.Dispose()
    }
}

function Draw-CandidateFive {
    param(
        [System.Drawing.Graphics]$Graphics,
        [int]$CanvasSize
    )

    # A two-stop green-to-red ring keeps the mark subtle while preserving the
    # requested gradient. The bold navy M remains readable at icon sizes.
    $ringPath = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $ringBrush = $null
    $mPen = New-Pen $navy 16 $CanvasSize
    try {
        $outer = Get-Rectangle 28 28 200 200 $CanvasSize
        $inner = Get-Rectangle 54 54 148 148 $CanvasSize
        $ringPath.FillMode = [System.Drawing.Drawing2D.FillMode]::Alternate
        $ringPath.AddEllipse($outer)
        $ringPath.AddEllipse($inner)

        $ringBrush = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
            $outer,
            $green,
            $red,
            [System.Drawing.Drawing2D.LinearGradientMode]::Horizontal)
        $Graphics.FillPath($ringBrush, $ringPath)

        $mPoints = [System.Drawing.PointF[]]@(
            (Get-Point 84 171 $CanvasSize),
            (Get-Point 84 85 $CanvasSize),
            (Get-Point 128 137 $CanvasSize),
            (Get-Point 172 85 $CanvasSize),
            (Get-Point 172 171 $CanvasSize)
        )
        $Graphics.DrawLines($mPen, $mPoints)
    }
    finally {
        $ringPath.Dispose()
        if ($null -ne $ringBrush) {
            $ringBrush.Dispose()
        }
        $mPen.Dispose()
    }
}

function Draw-CandidateSix {
    param(
        [System.Drawing.Graphics]$Graphics,
        [int]$CanvasSize
    )

    # The candidate-two gauge is placed on a filled navy tile with no outer
    # frame, so the pressure arc is the only geometry around the center.
    $navyBrush = New-Brush $navy
    $greenPen = New-Pen $green 14 $CanvasSize
    $yellowPen = New-Pen $yellow 14 $CanvasSize
    $orangePen = New-Pen $orange 14 $CanvasSize
    $redPen = New-Pen $red 14 $CanvasSize
    $redBrush = New-Brush $red
    try {
        Fill-RoundedRectangle $Graphics $navyBrush (Get-Rectangle 28 28 200 200 $CanvasSize) 40

        $gaugeBounds = Get-Rectangle 61 61 134 134 $CanvasSize
        $Graphics.DrawArc($greenPen, $gaugeBounds, 135, 45)
        $Graphics.DrawArc($yellowPen, $gaugeBounds, 180, 45)
        $Graphics.DrawArc($orangePen, $gaugeBounds, 225, 45)
        $Graphics.DrawArc($redPen, $gaugeBounds, 270, 45)

        $Graphics.DrawLine($redPen, (Get-Point 128 128 $CanvasSize), (Get-Point 174 88 $CanvasSize))
        $Graphics.FillEllipse($redBrush, (Get-Rectangle 117 117 22 22 $CanvasSize))
    }
    finally {
        $navyBrush.Dispose()
        $greenPen.Dispose()
        $yellowPen.Dispose()
        $orangePen.Dispose()
        $redPen.Dispose()
        $redBrush.Dispose()
    }
}

function Draw-CandidateSeven {
    param(
        [System.Drawing.Graphics]$Graphics,
        [int]$CanvasSize
    )

    # A wider, more assertive gauge uses a neutral frame and a three-quarter
    # pressure arc, while retaining the four pressure-state colors.
    $framePen = New-Pen $darkGray 14 $CanvasSize
    $greenPen = New-Pen $green 18 $CanvasSize
    $yellowPen = New-Pen $yellow 18 $CanvasSize
    $orangePen = New-Pen $orange 18 $CanvasSize
    $redPen = New-Pen $red 18 $CanvasSize
    $redBrush = New-Brush $red
    try {
        Draw-RoundedRectangle $Graphics $framePen (Get-Rectangle 28 28 200 200 $CanvasSize) 40

        $gaugeBounds = Get-Rectangle 54 54 148 148 $CanvasSize
        $Graphics.DrawArc($greenPen, $gaugeBounds, 135, 67.5)
        $Graphics.DrawArc($yellowPen, $gaugeBounds, 202.5, 67.5)
        $Graphics.DrawArc($orangePen, $gaugeBounds, 270, 67.5)
        $Graphics.DrawArc($redPen, $gaugeBounds, 337.5, 67.5)

        $needlePen = New-Pen $red 18 $CanvasSize
        try {
            $Graphics.DrawLine($needlePen, (Get-Point 128 128 $CanvasSize), (Get-Point 177 102 $CanvasSize))
        }
        finally {
            $needlePen.Dispose()
        }
        $Graphics.FillEllipse($redBrush, (Get-Rectangle 114 114 28 28 $CanvasSize))
    }
    finally {
        $framePen.Dispose()
        $greenPen.Dispose()
        $yellowPen.Dispose()
        $orangePen.Dispose()
        $redPen.Dispose()
        $redBrush.Dispose()
    }
}

function Draw-CandidateEight {
    param(
        [System.Drawing.Graphics]$Graphics,
        [int]$CanvasSize
    )

    # A speedometer-like half gauge sits on a green tile. The navy baseline
    # gives the white needle a clean, high-contrast center point.
    $greenBrush = New-Brush $green
    $navyPen = New-Pen $navy 14 $CanvasSize
    $whitePen = New-Pen $white 14 $CanvasSize
    $whiteBrush = New-Brush $white
    try {
        Fill-RoundedRectangle $Graphics $greenBrush (Get-Rectangle 28 28 200 200 $CanvasSize) 40

        $gaugeBounds = Get-Rectangle 54 54 148 148 $CanvasSize
        $Graphics.DrawArc($navyPen, $gaugeBounds, 180, 180)
        $Graphics.DrawLine($navyPen, (Get-Point 54 128 $CanvasSize), (Get-Point 202 128 $CanvasSize))
        $Graphics.DrawLine($whitePen, (Get-Point 128 128 $CanvasSize), (Get-Point 169 87 $CanvasSize))
        $Graphics.FillEllipse($whiteBrush, (Get-Rectangle 114 114 28 28 $CanvasSize))
    }
    finally {
        $greenBrush.Dispose()
        $navyPen.Dispose()
        $whitePen.Dispose()
        $whiteBrush.Dispose()
    }
}

function Draw-ChipPins {
    param(
        [System.Drawing.Graphics]$Graphics,
        [System.Drawing.Brush]$Brush,
        [int]$CanvasSize
    )

    $pinRectangles = @(
        (Get-Rectangle 38 67 22 14 $CanvasSize),
        (Get-Rectangle 38 96 22 14 $CanvasSize),
        (Get-Rectangle 38 125 22 14 $CanvasSize),
        (Get-Rectangle 38 154 22 14 $CanvasSize),
        (Get-Rectangle 196 67 22 14 $CanvasSize),
        (Get-Rectangle 196 96 22 14 $CanvasSize),
        (Get-Rectangle 196 125 22 14 $CanvasSize),
        (Get-Rectangle 196 154 22 14 $CanvasSize),
        (Get-Rectangle 67 38 14 22 $CanvasSize),
        (Get-Rectangle 96 38 14 22 $CanvasSize),
        (Get-Rectangle 125 38 14 22 $CanvasSize),
        (Get-Rectangle 154 38 14 22 $CanvasSize),
        (Get-Rectangle 67 196 14 22 $CanvasSize),
        (Get-Rectangle 96 196 14 22 $CanvasSize),
        (Get-Rectangle 125 196 14 22 $CanvasSize),
        (Get-Rectangle 154 196 14 22 $CanvasSize)
    )
    foreach ($pinRectangle in $pinRectangles) {
        $Graphics.FillRectangle($Brush, $pinRectangle)
    }
}

function Draw-CandidateNine {
    param(
        [System.Drawing.Graphics]$Graphics,
        [int]$CanvasSize
    )

    # Four horizontal pressure bars are contained by a dark chip silhouette.
    $chipBrush = New-Brush $navy
    $greenBrush = New-Brush $green
    $yellowBrush = New-Brush $yellow
    $orangeBrush = New-Brush $orange
    $redBrush = New-Brush $red
    try {
        Draw-ChipPins $Graphics $chipBrush $CanvasSize
        Fill-RoundedRectangle $Graphics $chipBrush (Get-Rectangle 54 54 148 148 $CanvasSize) 20

        Fill-RoundedRectangle $Graphics $greenBrush (Get-Rectangle 72 72 112 24 $CanvasSize) 6
        Fill-RoundedRectangle $Graphics $yellowBrush (Get-Rectangle 72 104 112 24 $CanvasSize) 6
        Fill-RoundedRectangle $Graphics $orangeBrush (Get-Rectangle 72 136 112 24 $CanvasSize) 6
        Fill-RoundedRectangle $Graphics $redBrush (Get-Rectangle 72 168 112 24 $CanvasSize) 6
    }
    finally {
        $chipBrush.Dispose()
        $greenBrush.Dispose()
        $yellowBrush.Dispose()
        $orangeBrush.Dispose()
        $redBrush.Dispose()
    }
}

function Draw-CandidateFourChipPins {
    param(
        [System.Drawing.Graphics]$Graphics,
        [System.Drawing.Brush]$Brush,
        [int]$CanvasSize
    )

    $pinRectangles = @(
        (Get-Rectangle 106 76 15 12 $CanvasSize),
        (Get-Rectangle 106 108 15 12 $CanvasSize),
        (Get-Rectangle 106 140 15 12 $CanvasSize),
        (Get-Rectangle 106 172 15 12 $CanvasSize),
        (Get-Rectangle 217 76 15 12 $CanvasSize),
        (Get-Rectangle 217 108 15 12 $CanvasSize),
        (Get-Rectangle 217 140 15 12 $CanvasSize),
        (Get-Rectangle 217 172 15 12 $CanvasSize)
    )
    foreach ($pinRectangle in $pinRectangles) {
        $Graphics.FillRectangle($Brush, $pinRectangle)
    }
}

function Draw-CandidateTen {
    param(
        [System.Drawing.Graphics]$Graphics,
        [int]$CanvasSize
    )

    # Candidate four's side-by-side composition with chunkier bars and a
    # filled chip body.
    $chipBrush = New-Brush $navy
    $greenBrush = New-Brush $green
    $yellowBrush = New-Brush $yellow
    $orangeBrush = New-Brush $orange
    $redBrush = New-Brush $red
    try {
        Fill-RoundedRectangle $Graphics $greenBrush (Get-Rectangle 24 27 66 48 $CanvasSize) 10
        Fill-RoundedRectangle $Graphics $yellowBrush (Get-Rectangle 24 84 66 48 $CanvasSize) 10
        Fill-RoundedRectangle $Graphics $orangeBrush (Get-Rectangle 24 141 66 48 $CanvasSize) 10
        Fill-RoundedRectangle $Graphics $redBrush (Get-Rectangle 24 198 66 31 $CanvasSize) 10

        Draw-CandidateFourChipPins $Graphics $chipBrush $CanvasSize
        Fill-RoundedRectangle $Graphics $chipBrush (Get-Rectangle 121 57 96 142 $CanvasSize) 18
    }
    finally {
        $chipBrush.Dispose()
        $greenBrush.Dispose()
        $yellowBrush.Dispose()
        $orangeBrush.Dispose()
        $redBrush.Dispose()
    }
}

function Draw-CandidateEleven {
    param(
        [System.Drawing.Graphics]$Graphics,
        [int]$CanvasSize
    )

    # A simple four-column pressure meter is the sole foreground element on
    # the navy rounded square.
    $navyBrush = New-Brush $navy
    $greenBrush = New-Brush $green
    $yellowBrush = New-Brush $yellow
    $orangeBrush = New-Brush $orange
    $redBrush = New-Brush $red
    try {
        Fill-RoundedRectangle $Graphics $navyBrush (Get-Rectangle 28 28 200 200 $CanvasSize) 40
        Fill-RoundedRectangle $Graphics $greenBrush (Get-Rectangle 52 128 30 78 $CanvasSize) 6
        Fill-RoundedRectangle $Graphics $yellowBrush (Get-Rectangle 92 104 30 102 $CanvasSize) 6
        Fill-RoundedRectangle $Graphics $orangeBrush (Get-Rectangle 132 78 30 128 $CanvasSize) 6
        Fill-RoundedRectangle $Graphics $redBrush (Get-Rectangle 172 52 30 154 $CanvasSize) 6
    }
    finally {
        $navyBrush.Dispose()
        $greenBrush.Dispose()
        $yellowBrush.Dispose()
        $orangeBrush.Dispose()
        $redBrush.Dispose()
    }
}

function Render-Candidate {
    param(
        [int]$Candidate,
        [int]$CanvasSize
    )

    $bitmap = [System.Drawing.Bitmap]::new(
        $CanvasSize,
        $CanvasSize,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceOver
        $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.Clear([System.Drawing.Color]::Transparent)

        switch ($Candidate) {
            1 { Draw-CandidateOne $graphics $CanvasSize }
            2 { Draw-CandidateTwo $graphics $CanvasSize }
            3 { Draw-CandidateThree $graphics $CanvasSize }
            4 { Draw-CandidateFour $graphics $CanvasSize }
            5 { Draw-CandidateFive $graphics $CanvasSize }
            6 { Draw-CandidateSix $graphics $CanvasSize }
            7 { Draw-CandidateSeven $graphics $CanvasSize }
            8 { Draw-CandidateEight $graphics $CanvasSize }
            9 { Draw-CandidateNine $graphics $CanvasSize }
            10 { Draw-CandidateTen $graphics $CanvasSize }
            11 { Draw-CandidateEleven $graphics $CanvasSize }
            default { throw "Unknown candidate: $Candidate" }
        }

        return $bitmap
    }
    catch {
        $bitmap.Dispose()
        throw
    }
    finally {
        $graphics.Dispose()
    }
}

function Get-PngBytes {
    param(
        [System.Drawing.Bitmap]$Bitmap
    )

    $stream = [System.IO.MemoryStream]::new()
    try {
        $Bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
        return ,$stream.ToArray()
    }
    finally {
        $stream.Dispose()
    }
}

function Write-IcoFile {
    param(
        [string]$Path,
        [int[]]$Sizes,
        [byte[][]]$PngPayloads
    )

    $headerSize = 6 + (16 * $Sizes.Count)
    $offset = $headerSize
    $stream = [System.IO.MemoryStream]::new()
    $writer = [System.IO.BinaryWriter]::new($stream)
    try {
        # ICONDIR: reserved, type (1 = icon), image count.
        $writer.Write([uint16]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]$Sizes.Count)

        for ($index = 0; $index -lt $Sizes.Count; $index++) {
            $size = $Sizes[$index]
            $payload = $PngPayloads[$index]
            $dimension = if ($size -eq 256) { [byte]0 } else { [byte]$size }

            # ICONDIRENTRY: width, height, palette count, reserved, planes,
            # bits per pixel, PNG byte count, and PNG file offset.
            $writer.Write($dimension)
            $writer.Write($dimension)
            $writer.Write([byte]0)
            $writer.Write([byte]0)
            $writer.Write([uint16]1)
            $writer.Write([uint16]32)
            $writer.Write([uint32]$payload.Length)
            $writer.Write([uint32]$offset)
            $offset += $payload.Length
        }

        foreach ($payload in $PngPayloads) {
            $writer.Write([byte[]]$payload)
        }
        $writer.Flush()
        [System.IO.File]::WriteAllBytes($Path, $stream.ToArray())
    }
    finally {
        $writer.Dispose()
        $stream.Dispose()
    }
}

function Test-GeneratedFiles {
    param(
        [string]$Directory,
        [int[]]$Sizes
    )

    foreach ($candidate in 1..11) {
        $pngPath = Join-Path $Directory "candidate$candidate.png"
        $icoPath = Join-Path $Directory "candidate$candidate.ico"

        if (-not [System.IO.File]::Exists($pngPath)) {
            throw "Missing PNG: $pngPath"
        }
        if (-not [System.IO.File]::Exists($icoPath)) {
            throw "Missing ICO: $icoPath"
        }
        if ((Get-Item -LiteralPath $icoPath).Length -le 0) {
            throw "ICO is empty: $icoPath"
        }

        $image = [System.Drawing.Image]::FromFile($pngPath)
        try {
            if ($image.Width -ne 256 -or $image.Height -ne 256) {
                throw "PNG is not 256x256: $pngPath"
            }
        }
        finally {
            $image.Dispose()
        }

        $icoBytes = [System.IO.File]::ReadAllBytes($icoPath)
        if ($icoBytes.Length -lt 6) {
            throw "ICO header is truncated: $icoPath"
        }
        $count = [BitConverter]::ToUInt16($icoBytes, 4)
        if ($count -ne $Sizes.Count) {
            throw "ICO image count is $count, expected $($Sizes.Count): $icoPath"
        }

        for ($entryIndex = 0; $entryIndex -lt $count; $entryIndex++) {
            $entryOffset = 6 + (16 * $entryIndex)
            if ($icoBytes.Length -lt ($entryOffset + 16)) {
                throw "ICO entry table is truncated: $icoPath"
            }
            $payloadLength = [BitConverter]::ToUInt32($icoBytes, $entryOffset + 8)
            $payloadOffset = [BitConverter]::ToUInt32($icoBytes, $entryOffset + 12)
            if ($payloadLength -eq 0 -or $payloadOffset + $payloadLength -gt $icoBytes.Length) {
                throw "ICO entry points outside the file: $icoPath"
            }
            $pngSignature = @(0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A)
            for ($signatureIndex = 0; $signatureIndex -lt $pngSignature.Count; $signatureIndex++) {
                if ([int]$icoBytes[$payloadOffset + $signatureIndex] -ne $pngSignature[$signatureIndex]) {
                    throw "ICO entry is not PNG data: $icoPath"
                }
            }
        }
    }
}

foreach ($candidate in 1..11) {
    $pngPayloads = [System.Collections.Generic.List[byte[]]]::new()
    foreach ($size in $iconSizes) {
        $bitmap = Render-Candidate $candidate $size
        try {
            $pngPayloads.Add((Get-PngBytes $bitmap))
        }
        finally {
            $bitmap.Dispose()
        }
    }

    $pngPath = Join-Path $outputDirectory "candidate$candidate.png"
    $icoPath = Join-Path $outputDirectory "candidate$candidate.ico"
    [System.IO.File]::WriteAllBytes($pngPath, $pngPayloads[3])
    Write-IcoFile $icoPath $iconSizes ([byte[][]]$pngPayloads.ToArray())
}

Test-GeneratedFiles $outputDirectory $iconSizes
Write-Output "Generated and validated 11 icon candidates in $outputDirectory"
