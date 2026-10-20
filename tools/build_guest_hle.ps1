param(
    [Parameter(Mandatory=$true)][string]$Clang,
    [Parameter(Mandatory=$true)][string]$SDK,
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][string]$Python
)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path "$PSScriptRoot/..").Path
$build=[IO.Path]::GetFullPath($BuildDirectory)
$allowed=[IO.Path]::GetFullPath((Join-Path $repo 'work'))+[IO.Path]::DirectorySeparatorChar
if (!$build.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) { throw 'Build output must stay inside checkout/work.' }
New-Item -ItemType Directory -Force -Path $build | Out-Null
$env:TEMP=$build; $env:TMP=$build; $env:TMPDIR=$build; $env:PYTHONDONTWRITEBYTECODE='1'
$sdkRoot=(Resolve-Path -LiteralPath $SDK).Path
$compiler=(Resolve-Path -LiteralPath $Clang).Path
$linker=Join-Path $sdkRoot 'usr/bin/ld.exe'
if (!(Test-Path -LiteralPath $linker)) { $linker=Join-Path $sdkRoot 'usr/bin/ld' }
$common=@('-target','arm-apple-ios3.0','-arch','armv6','-isysroot',$sdkRoot,
    '-Wno-incompatible-sysroot','-Os','-Wall','-Wextra','-Werror','-I',"$repo/tools")
foreach ($source in @('guest_hle_springboard.m','guest_hle_setup.c','guest_hle_routes.c','guest_hle_route_service.c','guest_hle_launch_guard.c')) {
    & $compiler @common -c "$repo/tools/$source" -o "$build/$source.o"
    if ($LASTEXITCODE) { throw "Failed to compile $source" }
}
$library='/usr/lib/s5lbox-hle-launch-v1.dylib'
& $linker -arch armv6 -iphoneos_version_min 3.0 -syslibroot $sdkRoot -undefined dynamic_lookup `
    -dylib -install_name $library -o "$build/s5lbox-hle-launch-v1.dylib" `
    "$sdkRoot/usr/lib/dylib1.o" "$build/guest_hle_springboard.m.o" "$build/guest_hle_routes.c.o" -lSystem -lobjc -framework CoreFoundation
if ($LASTEXITCODE) { throw 'Failed to link guest launch guard' }
& $linker -arch armv6 -iphoneos_version_min 3.0 -syslibroot $sdkRoot -undefined dynamic_lookup `
    -o "$build/s5lbox-hle-setup-v1" "$sdkRoot/usr/lib/crt1.o" "$build/guest_hle_setup.c.o" -lSystem -framework CoreFoundation
if ($LASTEXITCODE) { throw 'Failed to link guest launch setup' }
& $linker -arch armv6 -iphoneos_version_min 3.0 -syslibroot $sdkRoot -undefined dynamic_lookup `
    -o "$build/s5lbox-hle-routes-v1" "$sdkRoot/usr/lib/crt1.o" "$build/guest_hle_route_service.c.o" `
    "$build/guest_hle_launch_guard.c.o" -lSystem -framework CoreFoundation
if ($LASTEXITCODE) { throw 'Failed to link guest icon routing service' }
& $Python -B "$repo/tools/package_guest_hle.py" $build "$build/s5lbox-hle-launch.deb"
if ($LASTEXITCODE) { throw 'Failed to package guest launch guard' }
