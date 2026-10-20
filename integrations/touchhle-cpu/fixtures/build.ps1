param(
    [Parameter(Mandatory=$true)][string]$Clang,
    [Parameter(Mandatory=$true)][string]$SDK,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
$ErrorActionPreference='Stop'
$compiler=(Resolve-Path -LiteralPath $Clang).Path
$sdkRoot=(Resolve-Path -LiteralPath $SDK).Path
$linker=Join-Path $sdkRoot 'usr/bin/ld.exe'
if (!(Test-Path -LiteralPath $linker)) { $linker=Join-Path $sdkRoot 'usr/bin/ld' }
$outputRoot=[IO.Path]::GetFullPath($OutputDirectory)
$repoRoot=(Resolve-Path "$PSScriptRoot/../../..").Path
$allowedRoot=[IO.Path]::GetFullPath((Join-Path $repoRoot 'work'))+[IO.Path]::DirectorySeparatorChar
if (!$outputRoot.StartsWith($allowedRoot,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Fixture output must be inside this checkout/work.'
}
$bundle=Join-Path $outputRoot 'InterpreterSmoke.app'
New-Item -ItemType Directory -Force -Path $bundle | Out-Null
$env:TEMP=$outputRoot; $env:TMP=$outputRoot; $env:TMPDIR=$outputRoot
$common=@('-target','arm-apple-ios3.0','-arch','armv6','-isysroot',$sdkRoot,
    '-Wno-incompatible-sysroot','-fno-stack-protector','-mfpu=vfp','-mfloat-abi=softfp',
    '-O1','-Wall','-Wextra','-Werror')
& $compiler @common -marm -c "$PSScriptRoot/smoke.c" -o "$outputRoot/smoke.o"
if ($LASTEXITCODE) { throw 'ARM fixture compile failed' }
& $compiler @common -mthumb -c "$PSScriptRoot/thumb.c" -o "$outputRoot/thumb.o"
if ($LASTEXITCODE) { throw 'Thumb fixture compile failed' }
& $linker -arch armv6 -iphoneos_version_min 3.0 -syslibroot $sdkRoot `
    -undefined dynamic_lookup -o "$bundle/InterpreterSmoke" "$sdkRoot/usr/lib/crt1.o" `
    "$outputRoot/smoke.o" "$outputRoot/thumb.o" -lSystem
if ($LASTEXITCODE) { throw 'Fixture link failed' }
Copy-Item -LiteralPath "$PSScriptRoot/Info.plist" -Destination "$bundle/Info.plist"
$visual=Join-Path $outputRoot 'HLEVisual.app'
New-Item -ItemType Directory -Force -Path $visual | Out-Null
& $compiler @common -marm -c "$PSScriptRoot/visual.m" -o "$outputRoot/visual.o"
if ($LASTEXITCODE) { throw 'Visual fixture compile failed' }
& $linker -arch armv6 -iphoneos_version_min 3.0 -syslibroot $sdkRoot `
    -undefined dynamic_lookup -o "$visual/HLEVisual" "$sdkRoot/usr/lib/crt1.o" `
    "$outputRoot/visual.o" -lSystem -lobjc -framework Foundation -framework UIKit -framework OpenAL
if ($LASTEXITCODE) { throw 'Visual fixture link failed' }
Copy-Item -LiteralPath "$PSScriptRoot/visual.plist" -Destination "$visual/Info.plist"
Write-Output $bundle
