param([string]$Compiler='C:\MinGW\bin\g++.exe')
$ErrorActionPreference='Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
 New-Item -ItemType Directory -Force build-host | Out-Null
 & $Compiler -std=gnu++17 -O2 -Wall -Wextra -Isource -Ithird_party/yyjson tests/media_tests.cpp source/media.cpp third_party/yyjson/yyjson.c -o build-host/media-tests.exe
 if($LASTEXITCODE -ne 0){throw 'Media compilation failed'}
 & ./build-host/media-tests.exe
 if($LASTEXITCODE -ne 0){throw 'Media tests failed'}
} finally {Pop-Location}
