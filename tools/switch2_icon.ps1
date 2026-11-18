param([Parameter(Mandatory=$true)][string]$Source,
      [Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$image = [System.Drawing.Image]::FromFile([System.IO.Path]::GetFullPath($Source))
try {
    if ($image.Width -ne 256 -or $image.Height -ne 256) { throw 'NRO icon must be 256 x 256' }
    $bitmap = New-Object System.Drawing.Bitmap 256,256,([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    try {
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try { $graphics.Clear([System.Drawing.Color]::Black); $graphics.DrawImageUnscaled($image,0,0) }
        finally { $graphics.Dispose() }
        $codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object MimeType -eq 'image/jpeg'
        $parameters = New-Object System.Drawing.Imaging.EncoderParameters 1
        try {
            $parameters.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter ([System.Drawing.Imaging.Encoder]::Quality),([long]90)
            $bitmap.Save([System.IO.Path]::GetFullPath($Output),$codec,$parameters)
        } finally { $parameters.Dispose() }
    } finally { $bitmap.Dispose() }
} finally { $image.Dispose() }
