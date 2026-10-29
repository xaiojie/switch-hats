param([string]$Python='C:/Users/22969/AppData/Local/Programs/Python/Python311/python.exe',
 [string]$CurlInclude='C:/Users/22969/Documents/Codex/2026-10-28/new-chat-2/work/toolchain/root/opt/devkitpro/portlibs/switch/include',
 [string]$CurlBin='C:/Program Files/Git/mingw64/bin')
$ErrorActionPreference='Stop'
Push-Location (Join-Path $PSScriptRoot '..')
$server=$null
$oldTemp=$env:TEMP;$oldTmp=$env:TMP;$oldPath=$env:PATH
try {
 New-Item -ItemType Directory -Force build-host/stream | Out-Null
 $exports=@('curl_global_init','curl_easy_init','curl_easy_setopt','curl_easy_perform','curl_easy_getinfo','curl_easy_cleanup','curl_easy_strerror')
 [IO.File]::WriteAllLines((Join-Path (Get-Location) 'build-host/stream/curl.def'),(@('LIBRARY libcurl-4.dll','EXPORTS')+$exports))
 $vc='C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
 $command='call "'+$vc+'" >nul && lib /nologo /machine:X64 /def:build-host\stream\curl.def /out:build-host\stream\curl.lib && cl /nologo /std:c++17 /EHsc /utf-8 /O2 /Isource /Ithird_party/minimp3 /I"'+$CurlInclude+'" tests/stream_tests.cpp source/stream.cpp source/decoder.cpp source/engine.cpp source/core.cpp /Fo:build-host/stream/ /Fe:build-host/stream-tests.exe /link build-host/stream/curl.lib'
 & cmd /d /c $command
 if($LASTEXITCODE -ne 0){throw 'Stream compilation failed'}
 $listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0);$listener.Start();$port=$listener.LocalEndpoint.Port;$listener.Stop()
 $server=Start-Process -FilePath $Python -ArgumentList @('tests/stream_server.py','tests/tone.mp3',"$port") -WindowStyle Hidden -PassThru
 New-Item -ItemType Directory -Force build-host/stream/cache | Out-Null
 $env:TEMP=(Resolve-Path build-host/stream/cache).Path;$env:TMP=$env:TEMP
 $env:PATH="$CurlBin;$env:PATH"
 for($i=0;$i -lt 50;$i++){try{$c=[Net.Sockets.TcpClient]::new('127.0.0.1',$port);$c.Dispose();break}catch{Start-Sleep -Milliseconds 50}}
 & ./build-host/stream-tests.exe "http://127.0.0.1:$port"
 if($LASTEXITCODE -ne 0){throw 'Stream tests failed'}
} finally {$env:TEMP=$oldTemp;$env:TMP=$oldTmp;$env:PATH=$oldPath;if($server){Stop-Process -Id $server.Id -ErrorAction SilentlyContinue};Pop-Location}
