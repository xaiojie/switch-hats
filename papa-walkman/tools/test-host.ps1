param([string]$ToolchainRoot=$env:PAPA_TOOLCHAIN_ROOT,[switch]$SkipNetwork)
$ErrorActionPreference='Stop'
if(!$ToolchainRoot){$ToolchainRoot='C:/Users/22969/Documents/Codex/2026-10-28/new-chat-2/work/toolchain'}
$cmake=Join-Path $ToolchainRoot 'cmake/cmake-4.4.5-windows-x86_64/bin/cmake.exe'
$ctest=Join-Path (Split-Path $cmake) 'ctest.exe'
$curlInclude=Join-Path $ToolchainRoot 'root/opt/devkitpro/portlibs/switch/include'
$gitRoot=Split-Path (Split-Path (Get-Command git.exe).Source)
$curlBin=Join-Path $gitRoot 'mingw64/bin'
if(!(Test-Path -LiteralPath (Join-Path $curlBin 'libcurl-4.dll'))){$curlBin='C:/Program Files/Git/mingw64/bin'}
$vswhere='C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'Visual Studio 2022 C++ tools required for threaded verification.'}
$vc=Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat';$oldPath=$env:PATH
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Path build-host/fixtures/nested,build-host/common -Force | Out-Null
    foreach($name in @('Singer - Alpha.MP3','Zeta.mp3','.hidden.mp3','ignored.wav','nested/Beta.mp3')){[IO.File]::WriteAllText((Join-Path (Get-Location) "build-host/fixtures/$name"),'fixture')}
    $def=Join-Path (Get-Location) 'build-host/common/curl.def'
    [IO.File]::WriteAllLines($def,@('LIBRARY libcurl-4.dll','EXPORTS','curl_global_init','curl_easy_init','curl_easy_setopt','curl_easy_perform','curl_easy_getinfo','curl_easy_cleanup','curl_easy_strerror'))
    & cmd /d /c ('call "'+$vc+'" >nul && lib /nologo /machine:X64 /def:build-host\common\curl.def /out:build-host\common\curl.lib')
    if($LASTEXITCODE -ne 0){throw 'Host curl import library failed'}
    $curlLibrary=(Join-Path (Get-Location) 'build-host/common/curl.lib').Replace('\','/')
    & $cmake -S . -B build-host/windows -G 'Visual Studio 17 2022' -A x64 -DPAPA_HOST_TESTS=ON "-DPAPA_CURL_INCLUDE=$curlInclude" "-DPAPA_CURL_LIBRARY=$curlLibrary"
    if($LASTEXITCODE -ne 0){throw 'Host configuration failed'}
    & $cmake --build build-host/windows --config Release --parallel 4
    if($LASTEXITCODE -ne 0){throw 'Host compilation failed'}
    $env:PATH="$curlBin;$env:PATH"
    & $ctest --test-dir build-host/windows -C Release --output-on-failure
    if($LASTEXITCODE -ne 0){throw 'Host tests failed'}
    if(!$SkipNetwork){& ./tools/test-stream.ps1 -CurlInclude $curlInclude -CurlBin $curlBin;if($LASTEXITCODE -ne 0){throw 'HTTP verification failed'}}
}finally{$env:PATH=$oldPath;Pop-Location}
