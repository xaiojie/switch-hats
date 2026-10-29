param(
    [string]$ToolchainRoot = (Join-Path $PSScriptRoot '../../../toolchain'),
    [string]$SourceRoot = (Join-Path $PSScriptRoot '../../../upstream/sphaira'),
    [int]$Jobs = 6
)
$ErrorActionPreference = 'Stop'
$ToolchainRoot = (Resolve-Path -LiteralPath $ToolchainRoot).Path
$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
$dkp = (Join-Path $ToolchainRoot 'root/opt/devkitpro').Replace('\','/')
$cmake = Join-Path $ToolchainRoot 'cmake/cmake-4.4.5-windows-x86_64/bin/cmake.exe'
$ninja = (Join-Path $ToolchainRoot 'ninja/ninja.exe').Replace('\','/')
$git = (Get-Command git.exe -ErrorAction Stop).Source.Replace('\','/')
$env:DEVKITPRO = $dkp
$env:DEVKITA64 = "$dkp/devkitA64"
$env:PATH = "$dkp/devkitA64/bin;$dkp/tools/bin;" + $env:PATH
$build = Join-Path $SourceRoot 'build/SwitchLink'

# Native Windows CMake needs these two adaptations in its local toolchain copy.
$guard = Join-Path $dkp 'cmake/dkp-initialize-path.cmake'
$text = [IO.File]::ReadAllText($guard).Replace('message(FATAL_ERROR "CMake must be installed and launched from msys2: pacman -S cmake")', 'message(STATUS "Switch Link portable native Windows toolchain")')
[IO.File]::WriteAllText($guard, $text)
$common = Join-Path $dkp 'cmake/dkp-toolchain-common.cmake'
$text = [IO.File]::ReadAllText($common).Replace('string(REPLACE ":" ";" CMAKE_SYSTEM_PROGRAM_PATH "$ENV{PATH}")', 'set(CMAKE_SYSTEM_PROGRAM_PATH "$ENV{PATH}")')
[IO.File]::WriteAllText($common, $text)

$options = @(
    '-DCMAKE_BUILD_TYPE=MinSizeRel', '-DLTO=OFF', '-DENABLE_FTPSRV=ON', '-DFETCHCONTENT_UPDATES_DISCONNECTED=ON',
    '-DENABLE_NSZ=OFF', '-DENABLE_LIBUSBHSFS=OFF', '-DENABLE_NTFS=OFF',
    '-DENABLE_LIBUSBDVD=OFF', '-DENABLE_LIBHAZE=OFF',
    '-DENABLE_AUDIO_MP3=OFF', '-DENABLE_AUDIO_OGG=OFF', '-DENABLE_AUDIO_WAV=OFF', '-DENABLE_AUDIO_FLAC=OFF',
    '-DENABLE_DEVOPTAB_NFS=OFF', '-DENABLE_DEVOPTAB_SMB2=OFF', '-DENABLE_DEVOPTAB_FTP=OFF',
    '-DENABLE_DEVOPTAB_SFTP=OFF', '-DENABLE_DEVOPTAB_WEBDAV=OFF',
    "-DCMAKE_MAKE_PROGRAM=$ninja", "-DGIT_EXECUTABLE=$git",
    "-DPKG_CONFIG_EXECUTABLE=$dkp/portlibs/switch/bin/aarch64-none-elf-pkg-config"
)
# Reuse already-patched dependency sources during incremental builds. This also
# avoids reapplying the upstream ftpsrv patch when CMake changes update modes.
if (Test-Path -LiteralPath (Join-Path $build '_deps')) {
    foreach ($dependency in Get-ChildItem -LiteralPath (Join-Path $build '_deps') -Directory -Filter '*-src') {
        $key = $dependency.Name.Substring(0, $dependency.Name.Length - 4).ToUpperInvariant()
        $options += "-DFETCHCONTENT_SOURCE_DIR_${key}=$($dependency.FullName.Replace('\','/'))"
    }
}
& $cmake -S $SourceRoot -B $build -G Ninja @options
if ($LASTEXITCODE -ne 0) { throw 'Console configuration failed.' }
& $cmake --build $build --parallel $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Console build failed.' }
Copy-Item -LiteralPath (Join-Path $build 'sphaira.nro') -Destination (Join-Path $PSScriptRoot 'switch-link.nro') -Force
Write-Host 'Built switch-link.nro'
