# Construit tout le projet dans l'ordre :
#   1. firmware pedale  ->  2. integre dans le firmware E85  ->  3. firmware E85
#   4. integre dans l'appli  ->  5. APK signee
# Usage :  powershell -ExecutionPolicy Bypass -File tools\build_all.ps1
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$cli  = Join-Path $root "tools\arduino-cli\arduino-cli.exe"
if (-not (Test-Path $cli)) { $cli = "arduino-cli" }

$ver = (Select-String -Path "$root\firmware\E85_Master\protocol.h" -Pattern 'PROJECT_VERSION\s+"([^"]+)"').Matches[0].Groups[1].Value
$bld = (Select-String -Path "$root\firmware\E85_Master\protocol.h" -Pattern 'PROJECT_BUILD\s+(\d+)').Matches[0].Groups[1].Value
Write-Host "== Version $ver (build $bld) =="

Copy-Item "$root\firmware\E85_Master\protocol.h" "$root\firmware\Pedale\protocol.h" -Force

Write-Host "== 1. Firmware pedale =="
& $cli compile -b "esp32:esp32:esp32s3:CPUFreq=240,FlashSize=16M,CDCOnBoot=default" --output-dir "$root\build\Pedale" "$root\firmware\Pedale"
if ($LASTEXITCODE) { throw "compilation pedale" }

Write-Host "== 2. Integration pedale + interface =="
python -I "$root\tools\gen_pedal_fw.py" "$root\build\Pedale\Pedale.ino.bin"
python -I "$root\tools\gen_web_ui.py"

Write-Host "== 3. Firmware E85 =="
& $cli compile -b "esp32:esp32:esp32s3:CPUFreq=240,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,LoopCore=1,EventsCore=0,CDCOnBoot=default" --output-dir "$root\build\E85_Master" "$root\firmware\E85_Master"
if ($LASTEXITCODE) { throw "compilation E85" }

Write-Host "== 4. Integration dans l'appli =="
$assets = "$root\android\app\src\main\assets"
Copy-Item "$root\build\E85_Master\E85_Master.ino.bin" "$assets\e85.bin" -Force
"{`"version`":`"$ver`",`"build`":$bld}" | Set-Content -Encoding ascii "$assets\firmware.json"

Write-Host "== 5. APK =="
Push-Location "$root\android"
$gradle = Get-ChildItem "$env:USERPROFILE\.gradle\wrapper\dists\gradle-8.14.3-bin" -Recurse -Filter gradle.bat | Select -First 1 -Expand FullName
& $gradle assembleRelease "-PappVersion=$ver" "-PappBuild=$bld" --console=plain -q
if ($LASTEXITCODE) { Pop-Location; throw "compilation APK" }
Pop-Location

$out = "$root\livrables"
New-Item -ItemType Directory -Force "$out\E85_Master","$out\Pedale" | Out-Null
Copy-Item "$root\android\app\build\outputs\apk\release\app-release.apk" "$out\BoitierE85-$ver.apk" -Force
Copy-Item "$root\build\E85_Master\E85_Master.ino.merged.bin" "$out\E85_Master\E85_Master.ino.merged.bin" -Force
Copy-Item "$root\build\E85_Master\E85_Master.ino.bin" "$out\E85_Master\E85_Master.ino.bin" -Force
Copy-Item "$root\build\Pedale\Pedale.ino.merged.bin" "$out\Pedale\Pedale.ino.merged.bin" -Force
Copy-Item "$root\build\Pedale\Pedale.ino.bin" "$out\Pedale\Pedale.ino.bin" -Force
Write-Host "== OK : livrables\BoitierE85-$ver.apk =="
