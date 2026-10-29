param([string]$Compiler='g++')
$ErrorActionPreference='Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    New-Item -ItemType Directory -Path build-host/fixtures/nested -Force | Out-Null
    foreach($name in @('Singer - Alpha.MP3','Zeta.mp3','.hidden.mp3','ignored.wav','nested/Beta.mp3')) { [IO.File]::WriteAllText((Join-Path (Get-Location) "build-host/fixtures/$name"),'fixture') }
    & $Compiler -std=c++17 -O2 -Wall -Wextra -Isource tests/core_tests.cpp source/core.cpp -o build-host/core-tests.exe
    if($LASTEXITCODE -ne 0) {throw 'Core test compilation failed'}
    & ./build-host/core-tests.exe build-host/fixtures
    if($LASTEXITCODE -ne 0) {throw 'Core tests failed'}
    if(Test-Path -LiteralPath tests/tone.mp3) {
        & $Compiler -std=c++17 -O2 -Wall -Wextra -Isource -Ithird_party/minimp3 tests/decoder_tests.cpp source/decoder.cpp -o build-host/decoder-tests.exe
        if($LASTEXITCODE -ne 0) {throw 'Decoder test compilation failed'}
        & ./build-host/decoder-tests.exe tests/tone.mp3
        if($LASTEXITCODE -ne 0) {throw 'Decoder tests failed'}
        & $Compiler -std=c++17 -O2 -Wall -Wextra -Isource -Ithird_party/minimp3 tests/engine_tests.cpp source/engine.cpp source/decoder.cpp source/core.cpp -o build-host/engine-tests.exe
        if($LASTEXITCODE -ne 0){throw 'Engine test compilation failed'}
        & ./build-host/engine-tests.exe tests/tone.mp3
        if($LASTEXITCODE -ne 0){throw 'Engine tests failed'}
    } else { throw 'Decoder test fixture missing: tests/tone.mp3. Restore it or regenerate with tools/make_fixture.py.' }
    & $Compiler -std=c++17 -O2 -Wall -Wextra -Isource -Ithird_party/stb tests/ui_tests.cpp source/ui.cpp source/core.cpp -o build-host/ui-tests.exe
    if($LASTEXITCODE -ne 0){throw 'UI test compilation failed'}
    & ./build-host/ui-tests.exe
    if($LASTEXITCODE -ne 0){throw 'UI tests failed'}
} finally { Pop-Location }
