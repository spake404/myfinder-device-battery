param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'Manifest.Common.ps1')
$release=Read-DeviceBatteryRelease $projectRoot
$buildOutput = if ($OutputDirectory) { $OutputDirectory } else { Join-Path $projectRoot 'outputs' }
$compiler = 'C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (-not (Test-Path -LiteralPath $compiler)) { throw 'Windows .NET Framework compiler is unavailable.' }
New-Item -ItemType Directory -Path $buildOutput -Force | Out-Null
$widgetSources = Join-Path $projectRoot 'native\batteryhub'
$nativeCompiler = if($env:MYFINDER_MINGW_CXX){$env:MYFINDER_MINGW_CXX}else{
    $mingw = Get-Command g++.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if($mingw){$mingw.Source}else{'C:\msys64\ucrt64\bin\g++.exe'}
}
if(-not (Test-Path -LiteralPath $nativeCompiler -PathType Leaf)){throw 'Install x64 MinGW-w64 or set MYFINDER_MINGW_CXX to its g++.exe.'}
$widgetLibrary = Join-Path $buildOutput 'MyFinderTaskbar.dll'
$originalPath = $env:Path
try {
    $env:Path = (Split-Path -Parent $nativeCompiler)+';'+$originalPath
    & $nativeCompiler -std=c++17 -Os -shared -static -s -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 `
        -o $widgetLibrary (Join-Path $widgetSources 'widget.cpp') (Join-Path $widgetSources 'brands.cpp') (Join-Path $widgetSources 'bridge.cpp') (Join-Path $widgetSources 'reserve.cpp') `
        -lcomctl32 -lgdi32 -lole32 -loleaut32 -ladvapi32 -luuid
    if($LASTEXITCODE -ne 0){throw 'Taskbar renderer build failed.'}
    & $nativeCompiler -std=c++17 -Os -shared -static -s -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 `
        -o (Join-Path $buildOutput 'MyFinderTaskbarReserve.dll') (Join-Path $widgetSources 'reserve_tap.cpp') `
        -lole32 -loleaut32 -luuid
    if($LASTEXITCODE -ne 0){throw 'Taskbar reserve build failed.'}
} finally {
    $env:Path = $originalPath
}
$hidSource = Join-Path $PSScriptRoot 'HidBattery.cs'
$headsetSource = Join-Path $PSScriptRoot 'HeadsetBattery.cs'
$audioSource = Join-Path $PSScriptRoot 'AudioControl.cs'
$visualSource = Join-Path $PSScriptRoot 'VisualDesign.cs'
$traySource = Join-Path $PSScriptRoot 'TrayGlyph.cs'
$settingsSource = Join-Path $PSScriptRoot 'DeviceSettings.cs'
$iconRelative = 'assets/app-icon.ico'
$iconPath = Join-Path $projectRoot $iconRelative
$manifestRelative = 'assets/app.manifest'
$manifestPath = Join-Path $projectRoot $manifestRelative
if (-not (Test-Path -LiteralPath $iconPath -PathType Leaf)) { throw 'The application icon is missing. Restore assets/app-icon.ico or rebuild it with tools/Build-AppIcon.cjs.' }
& $compiler /nologo /target:exe /platform:x64 /optimize+ "/out:$(Join-Path $buildOutput 'AtkBatteryProbe.exe')" $hidSource (Join-Path $PSScriptRoot 'Probe.cs') (Join-Path $PSScriptRoot 'Tests.cs')
if ($LASTEXITCODE -ne 0) { throw 'Probe build failed.' }
$assembly=Join-Path $buildOutput 'AssemblyInfo.generated.cs'
$metadata=@'
using System.Reflection;
[assembly: AssemblyTitle("MyFinder Device Battery")]
[assembly: AssemblyProduct("MyFinder Device Battery")]
[assembly: AssemblyDescription("Local mouse, headset and keyboard battery cards for Windows")]
[assembly: AssemblyVersion("{ASSEMBLY}")]
[assembly: AssemblyFileVersion("{ASSEMBLY}")]
[assembly: AssemblyInformationalVersion("{VERSION}")]
'@
[IO.File]::WriteAllText($assembly,$metadata.Replace('{ASSEMBLY}',$release.assemblyVersion).Replace('{VERSION}',$release.version),(New-Object Text.UTF8Encoding($false)))
$uiSources=@(Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.cs' -File | Where-Object {$_.Name -notin @('Probe.cs','Tests.cs','AssemblyInfo.cs')} | Sort-Object Name | ForEach-Object {$_.FullName})
$uiSources+=@('/reference:System.IO.Compression.dll','/reference:System.IO.Compression.FileSystem.dll')
$uiSources+=('/resource:'+(Join-Path $projectRoot 'engine\device_pack_schema.json')+',DevicePackSchema')
$uiSources+=('/resource:'+(Join-Path $projectRoot 'tools\folia-bridge\mod.json')+',FoliaBridgeModJson')
$uiSources+=('/resource:'+(Join-Path $projectRoot 'tools\folia-bridge\main.cjs')+',FoliaBridgeMainCjs')
$uiSources+=('/resource:'+(Join-Path $projectRoot 'tools\folia-bridge\client.mjs')+',FoliaBridgeClientCjs')
& $compiler /nologo /target:winexe /platform:x64 /optimize+ "/win32icon:$iconPath" "/win32manifest:$manifestPath" /reference:System.Windows.Forms.dll /reference:System.Drawing.dll /reference:Accessibility.dll /reference:System.Runtime.Serialization.dll "/out:$(Join-Path $buildOutput 'AtkMouseBattery.exe')" $uiSources $assembly
if ($LASTEXITCODE -ne 0) { throw 'Tray build failed.' }
& (Join-Path $buildOutput 'AtkBatteryProbe.exe') --self-test
if ($LASTEXITCODE -ne 0) { throw 'Offline tests failed.' }
& (Join-Path $buildOutput 'AtkMouseBattery.exe') --preview $buildOutput
if ($LASTEXITCODE -ne 0) { throw 'Appearance preview failed.' }
try {
    $env:Path = (Join-Path $projectRoot 'engine\runtime')+';'+(Split-Path -Parent $nativeCompiler)+';'+$originalPath
    & (Join-Path $projectRoot 'tests\Run.ps1') -OutputDirectory (Join-Path $buildOutput 'tests')
    if(-not $?){throw 'Regression checks failed.'}
} finally {
    $env:Path = $originalPath
}
$buildInfo=[ordered]@{schema=2;version=$release.version;releaseSha256=(Get-FileHash -LiteralPath (Join-Path $projectRoot 'release.json') -Algorithm SHA256).Hash.ToLowerInvariant();
    files=@('AtkMouseBattery.exe','AtkBatteryProbe.exe','MyFinderTaskbar.dll','MyFinderTaskbarReserve.dll' | ForEach-Object {[pscustomobject]@{path=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $buildOutput $_) -Algorithm SHA256).Hash.ToLowerInvariant()}});
    sources=@(Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.cs' -File | Sort-Object Name | ForEach-Object {[pscustomobject]@{path=('scripts/'+$_.Name);sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}})+@(Get-ChildItem -LiteralPath $widgetSources -File | Sort-Object Name | ForEach-Object {[pscustomobject]@{path=('native/batteryhub/'+$_.Name);sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}})+@($iconRelative,$manifestRelative,'engine/device_pack_schema.json' | ForEach-Object {[pscustomobject]@{path=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $projectRoot $_) -Algorithm SHA256).Hash.ToLowerInvariant()}})}
[IO.File]::WriteAllText((Join-Path $buildOutput 'build-info.json'),($buildInfo | ConvertTo-Json -Depth 6),(New-Object Text.UTF8Encoding($false)))
