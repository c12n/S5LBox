param(
    [Parameter(Mandatory=$true)][string]$Clang,
    [Parameter(Mandatory=$true)][string]$SDK,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$CMake='cmake'
)
# Build guest ARMv6 libraries from pinned public sources and the community SDK.
# Outputs stay in work; this does not replace the checked-in runtime libraries.
$ErrorActionPreference='Stop'
$repoRoot=(Resolve-Path "$PSScriptRoot/..").Path
$output=[IO.Path]::GetFullPath($OutputDirectory)
$allowed=[IO.Path]::GetFullPath((Join-Path $repoRoot 'work'))+[IO.Path]::DirectorySeparatorChar
if (!$output.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Library build output must be inside this checkout/work.'
}
New-Item -ItemType Directory -Force -Path $output | Out-Null
$env:TEMP=$output; $env:TMP=$output; $env:TMPDIR=$output
$compiler=(Resolve-Path -LiteralPath $Clang).Path.Replace('\','/')
$sdkRoot=(Resolve-Path -LiteralPath $SDK).Path.Replace('\','/')
$sourcePrefix=$output.Replace('\','/')
$linker="$sdkRoot/usr/bin/ld"
if(Test-Path -LiteralPath "$linker.exe") { $linker="$linker.exe" }
$patch=Join-Path $repoRoot 'integrations/touchhle-cpu/armv6-dylibs.patch'
$specs=@(
    @{Name='zlib'; Revision='1e0e03ce0040815b678d96f5c6e8a2f08e98fe2f'; Built='libz.1.2.3.dylib'; Output='libz.1.2.3.dylib'},
    @{Name='sqlite'; Revision='5d960d99b632b08cb11af9196ad374919ade59ed'; Built='libsqlite.3.6.10.dylib'; Output='libsqlite3.dylib'},
    @{Name='libxml2'; Revision='4b51e8c1ac1afacc983b72ac107069435a71dbc9'; Built='libxml2.2.dylib'; Output='libxml2.2.dylib'}
)
$resultDirectory=Join-Path $output 'dylibs'
New-Item -ItemType Directory -Force -Path $resultDirectory | Out-Null
foreach($spec in $specs) {
    $source=Join-Path $output $spec.Name
    if (!(Test-Path -LiteralPath $source)) {
        & git -c core.autocrlf=false clone --no-checkout --depth 1 "https://github.com/touchHLE/$($spec.Name)-dylib.git" $source
        if($LASTEXITCODE) { throw 'Library source clone failed' }
        & git -C $source config core.autocrlf false
        & git -C $source fetch --depth 1 origin $spec.Revision
        if($LASTEXITCODE) { throw 'Pinned source fetch failed' }
        & git -C $source checkout --detach $spec.Revision
        if($LASTEXITCODE) { throw 'Pinned source checkout failed' }
        & git -C $source -c core.autocrlf=false submodule update --init --depth 1
        if($LASTEXITCODE) { throw 'Pinned submodule fetch failed' }
    }
    $revision=(& git -C $source rev-parse HEAD).Trim()
    if($LASTEXITCODE -or $revision -ne $spec.Revision) {
        throw "Unexpected source revision in $source; use a fresh output directory."
    }
    # Keep the upstream build recipe, changing only its CPU architecture.
    if([IO.File]::ReadAllText((Join-Path $source 'CMakeLists.txt')).Contains('OSX_ARCHITECTURES "armv6"')) {
        & git -C $source apply --reverse --check $patch
        if($LASTEXITCODE) { throw 'Unexpected ARMv6 source changes' }
    } else {
        & git -C $source apply --check $patch
        if($LASTEXITCODE) { throw 'Source changes conflict with the ARMv6 patch' }
        & git -C $source apply $patch
        if($LASTEXITCODE) { throw 'ARMv6 patch failed' }
    }
    if($spec.Name -eq 'libxml2') {
        Copy-Item -LiteralPath (Join-Path $resultDirectory 'libz.1.2.3.dylib') -Destination $source
    }
    $build=Join-Path $output "build-$($spec.Name)"
    & $CMake -S $source -B $build -G Ninja `
        "-DCMAKE_TOOLCHAIN_FILE=$sdkRoot/cmake/Toolchain/common-3.0.cmake" `
        "-DCMAKE_C_COMPILER=$compiler" `
        "-DCMAKE_C_FLAGS=-B$sdkRoot/usr/bin -Wno-incompatible-sysroot -mlinker-version=253 -mfpu=vfpv2 -ffile-prefix-map=$sourcePrefix=." `
        "-DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=$linker" `
        '-DCMAKE_C_STANDARD_LIBRARIES=-lgcc_s.1'
    if($LASTEXITCODE) { throw "Configuration failed: $($spec.Name)" }
    & $CMake --build $build -j 2
    if($LASTEXITCODE) { throw "Build failed: $($spec.Name)" }
    $binary=Join-Path $build $spec.Built
    $bytes=[IO.File]::ReadAllBytes($binary)
    if([BitConverter]::ToUInt32($bytes,0) -ne [uint32]4277009102 -or
       [BitConverter]::ToInt32($bytes,4) -ne 12 -or
       [BitConverter]::ToInt32($bytes,8) -ne 6) {
        throw "Expected an ARMv6 Mach-O library: $binary"
    }
    Copy-Item -LiteralPath $binary -Destination (Join-Path $resultDirectory $spec.Output)
}
Get-ChildItem -LiteralPath $resultDirectory -Filter '*.dylib' | Get-FileHash -Algorithm SHA256
