param([string]$ToolchainRoot=$env:PAPA_TOOLCHAIN_ROOT,[int]$Jobs=4)
$ErrorActionPreference='Stop'
if(!$ToolchainRoot){throw 'Pass -ToolchainRoot with the portable devkitPro folder, or set PAPA_TOOLCHAIN_ROOT.'}
$toolchain=(Resolve-Path -LiteralPath $ToolchainRoot).Path
$dkp=(Join-Path $toolchain 'root/opt/devkitpro').Replace('\','/')
$cmake=Join-Path $toolchain 'cmake/cmake-4.4.5-windows-x86_64/bin/cmake.exe'
$ninja=(Join-Path $toolchain 'ninja/ninja.exe').Replace('\','/')
foreach($required in @($cmake,$ninja,"$dkp/devkitA64/bin/aarch64-none-elf-g++.exe","$dkp/tools/bin/elf2nro.exe")){if(!(Test-Path -LiteralPath $required)){throw "Missing build tool: $required"}}
Add-Type -AssemblyName System.Drawing
$image=[Drawing.Image]::FromFile((Join-Path $PSScriptRoot 'assets/icon-master.png'))
$bitmap=[Drawing.Bitmap]::new(256,256)
$graphics=[Drawing.Graphics]::FromImage($bitmap)
try{$graphics.InterpolationMode=[Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic;$graphics.DrawImage($image,0,0,256,256);$bitmap.Save((Join-Path $PSScriptRoot 'assets/icon.jpg'),[Drawing.Imaging.ImageFormat]::Jpeg)}finally{$graphics.Dispose();$bitmap.Dispose();$image.Dispose()}
# Stage sources next to the portable toolchain: its compiler tools expect an ASCII build path.
$stage=Join-Path (Split-Path $toolchain) 'papa-walkman-build/source'
$build=Join-Path (Split-Path $toolchain) 'papa-walkman-build/build'
if($stage -match '[^\x00-\x7f]'){throw 'The toolchain parent must have an ASCII path for this Windows compiler.'}
New-Item -ItemType Directory -Path $stage,$build -Force | Out-Null
foreach($folder in @('source','third_party','assets')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $folder) -Destination $stage -Recurse -Force}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'CMakeLists.txt') -Destination $stage -Force
$env:DEVKITPRO=$dkp;$env:DEVKITA64="$dkp/devkitA64";$env:PATH="$dkp/devkitA64/bin;$dkp/tools/bin;"+$env:PATH
& $cmake -S $stage -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" -DCMAKE_BUILD_TYPE=Release
if($LASTEXITCODE -ne 0){throw 'Configuration failed'}
& $cmake --build $build --parallel $Jobs
if($LASTEXITCODE -ne 0){throw 'NRO compilation failed'}
# Preserve UTF-8 metadata regardless of the Windows process code page.
$nacp=Join-Path $build 'papa-walkman.nacp';$bytes=[IO.File]::ReadAllBytes($nacp)
$title=[Text.Encoding]::UTF8.GetBytes('爬爬的随身听');$author=[Text.Encoding]::UTF8.GetBytes('Papa')
for($language=0;$language -lt 16;$language++){[Array]::Clear($bytes,$language*768,768);[Array]::Copy($title,0,$bytes,$language*768,$title.Length);[Array]::Copy($author,0,$bytes,$language*768+512,$author.Length)}
[IO.File]::WriteAllBytes($nacp,$bytes)
& "$dkp/tools/bin/elf2nro.exe" (Join-Path $build 'papa-walkman.elf') (Join-Path $build 'papa-walkman.nro') "--icon=$(Join-Path $stage 'assets/icon.jpg')" "--nacp=$nacp"
if($LASTEXITCODE -ne 0){throw 'NRO packaging failed'}
New-Item -ItemType Directory -Path (Join-Path $PSScriptRoot 'build') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $build 'papa-walkman.nro') -Destination (Join-Path $PSScriptRoot 'build/papa-walkman.nro') -Force
Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $PSScriptRoot 'build/papa-walkman.nro')
