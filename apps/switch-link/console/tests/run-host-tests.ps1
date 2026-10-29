param(
    [string]$Compiler = 'C:/MinGW/bin/gcc.exe',
    [string]$ToolchainRoot = (Join-Path $PSScriptRoot '../../../../toolchain'),
    [string]$SourceRoot = (Join-Path $PSScriptRoot '../../../../upstream/sphaira')
)
$ErrorActionPreference = 'Stop'
$overlay = (Resolve-Path (Join-Path $PSScriptRoot '../overlay')).Path
$yyjson = Join-Path (Resolve-Path $SourceRoot).Path 'build/SwitchLink/_deps/yyjson-src/src'
$scratch = Join-Path (Resolve-Path $ToolchainRoot).Path 'host-tests'
New-Item -ItemType Directory -Path $scratch -Force | Out-Null
$object = Join-Path $scratch 'yyjson.o'
& $Compiler -std=c11 -O1 -c (Join-Path $yyjson 'yyjson.c') -I $yyjson -o $object
if ($LASTEXITCODE -ne 0) { throw 'yyjson host compilation failed.' }
foreach ($case in @(@('task_progress', 'task-progress'), @('http_install', 'http-command'))) {
    $exe = Join-Path $scratch ($case[1] + '-tests.exe')
    & $Compiler -std=c11 -O1 -static -Wall -Wextra -Werror -I (Join-Path $overlay 'include') -I $yyjson (Join-Path $PSScriptRoot ($case[0] + '_test.c')) (Join-Path $overlay ('source/' + $case[0] + '.c')) $object -o $exe
    if ($LASTEXITCODE -ne 0) { throw ($case[0] + ' host compilation failed.') }
    & $exe
    if ($LASTEXITCODE -ne 0) { throw ($case[0] + ' host tests failed.') }
}
