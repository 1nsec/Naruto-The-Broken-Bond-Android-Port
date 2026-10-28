[CmdletBinding()]
param(
    [string]$PlumeSource,
    [string]$XenosRecompThirdparty,
    [string]$SdkPrefix,
    [string]$AndroidSdk,
    [string]$SpirvVal,
    [string]$Version = '2.5-android-alpha',
    [string]$ShaderCache,
    [string]$AdrenotoolsSource,
    [string]$TurnipDriver,
    [switch]$StandInGameCode,
    [switch]$Install,
    [ValidateRange(1, 128)]
    [int]$Jobs = 6
)

# Builds the Android APK (arm64-v8a) of the port from your own game files, on
# the Windows PC that builds the PC version:
#   1. checks game/ and the generated C++ (scripts/build-game.ps1), and applies
#      the SDK patches with rexglue-android.patch;
#   2. builds the SPIR-V shader cache from game/xeshader.sdb on this PC (the
#      shader tools run on Windows only) in build/android-shaders;
#   3. cross-compiles the game, the ReXGlue runtime and SDL3 with the Android
#      NDK in build/android and stages them in android/app/src/main/jniLibs;
#   4. packages the app with Gradle and copies the APK to dist/.
# The APK holds code generated from your default.xex and shaders from your
# xeshader.sdb, like the PC build's executable, but no disc image: the app
# asks for the ISO on first start. See docs/android.md.
#
# -ShaderCache <folder>: use the shader cache of an earlier build (its
# shader_cache_spirv.bin and shader_manifest.json) instead of building it.
# -AdrenotoolsSource <folder>: libadrenotools (default out/libadrenotools,
# fetched at the revision of tools/android/dependencies.json when missing), which
# loads a Vulkan driver bundled with the app.
# -TurnipDriver <libvulkan_freedreno.so>: Turnip to bundle (scripts/android/
# build-turnip.sh on Linux, or the "turnip" artifact of the Android workflows);
# without it the app runs on the system driver only, which on older Adreno
# GPUs (Snapdragon 865 and earlier) lacks what the renderer needs.
# -StandInGameCode: CI only. Builds against the stand-ins of
# scripts/android/make-stub-generated.py instead of your game files (needs
# -ShaderCache); the APK cannot play anything.
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
# NDK r29: the Clang of r28 and older lacks #embed. The CI build checks the same
# version (.github/workflows/android.yml).
$ndkVersion = '29.0.14206865'
$androidPlatform = 29

# Absolute, with forward slashes (CMake reads backslashes as escapes). Relative
# paths are taken from the current PowerShell location, not the process folder.
function ConvertTo-CMakePath([string]$Path) {
    if (-not [IO.Path]::IsPathRooted($Path)) { $Path = Join-Path (Get-Location).Path $Path }
    return [IO.Path]::GetFullPath($Path).Replace('\', '/')
}

if (-not $PlumeSource) { $PlumeSource = Join-Path $projectRoot 'out/plume' }
if (-not $XenosRecompThirdparty) { $XenosRecompThirdparty = Join-Path $projectRoot 'tools/xenosrecomp/thirdparty' }
if (-not $SdkPrefix) { $SdkPrefix = Join-Path $projectRoot 'build/sdk-install' }
foreach ($required in @(
        @($PlumeSource, 'Plume checkout (-PlumeSource); see docs/build-and-run.md, section 6'),
        @((Join-Path $XenosRecompThirdparty 'smol-v/source/smolv.cpp'), 'XenosRecomp dependencies (-XenosRecompThirdparty); see docs/build-and-run.md, section 6'),
        @((Join-Path $SdkPrefix 'bin/rexglue.exe'), 'host SDK: run scripts/build-sdk.ps1 first'))) {
    if (-not (Test-Path -LiteralPath $required[0])) { throw "Missing $($required[0]): $($required[1])." }
}
$PlumeSource = ConvertTo-CMakePath $PlumeSource
$XenosRecompThirdparty = ConvertTo-CMakePath $XenosRecompThirdparty
$SdkPrefix = ConvertTo-CMakePath $SdkPrefix
$rexglue = "$SdkPrefix/bin/rexglue.exe"

# 1. Game files, generated code and SDK patches.
$generated = Join-Path $projectRoot 'generated/default'
$standIn = Test-Path -LiteralPath (Join-Path $generated '.stub')
if ($StandInGameCode) {
    if (-not $standIn) { throw '-StandInGameCode needs the stand-ins of scripts/android/make-stub-generated.py in generated/default.' }
    if (-not $ShaderCache) { throw '-StandInGameCode needs -ShaderCache (there is no xeshader.sdb to build one from).' }
} else {
    $xex = Join-Path $projectRoot 'game/default.xex'
    if (-not (Test-Path -LiteralPath $xex)) { throw 'game/default.xex is missing: extract the game first (scripts/extract-game.ps1).' }
    if ((Get-FileHash -LiteralPath $xex -Algorithm SHA256).Hash -ne
        '8F70E79443E36B38E44B6A105DAD51FB8909FB615D982753CABAAC9B15F0D576') {
        throw 'The native renderer mapping only supports the documented Broken Bond executable.'
    }
    if (-not $ShaderCache -and -not (Test-Path -LiteralPath (Join-Path $projectRoot 'game/xeshader.sdb'))) {
        throw 'The shader cache needs game/xeshader.sdb; extract the game first.'
    }
    if ($standIn) {
        throw 'generated/default holds the CI stand-in, not your game: delete generated/ and run scripts/build-game.ps1.'
    }
}
if ($StandInGameCode) {
    # Ninja runs the codegen step of a new build folder once, to record its
    # dependencies; with the stand-ins there is no default.xex to run it on.
    $standInTool = Join-Path $projectRoot 'build/stand-in-codegen.cmd'
    New-Item -ItemType Directory -Force (Split-Path -Parent $standInTool) | Out-Null
    Set-Content -LiteralPath $standInTool -Value '@exit /b 0' -Encoding ascii
    $rexglue = ConvertTo-CMakePath $standInTool
}
if ($ShaderCache) {
    foreach ($file in 'shader_cache_spirv.bin', 'shader_manifest.json') {
        if (-not (Test-Path -LiteralPath (Join-Path $ShaderCache $file))) { throw "The shader cache folder has no $file`: $ShaderCache" }
    }
    $ShaderCache = ConvertTo-CMakePath $ShaderCache
}
if (-not (Test-Path -LiteralPath (Join-Path $generated 'broken_bond_init.cpp'))) {
    throw 'The generated C++ is missing: run scripts/build-game.ps1 first.'
}
$mspackSource = Join-Path $projectRoot 'sdk/thirdparty/libmspack/cabextract/mspack/lzxd.c'
if ((Get-Content -LiteralPath $mspackSource -TotalCount 1) -eq '../../libmspack/mspack/lzxd.c') {
    throw 'The libmspack checkout contains symlink placeholders. Initialize the SDK submodules with Git core.symlinks=true before building.'
}
. (Join-Path $PSScriptRoot 'sdk-patches.ps1')
Sync-SdkPatches -ProjectRoot $projectRoot

# Plume's Vulkan backend needs its volk, Vulkan-Headers and VMA submodules
# (the PC build only needs the D3D12 allocator).
foreach ($submodule in 'contrib/volk', 'contrib/Vulkan-Headers', 'contrib/VulkanMemoryAllocator') {
    if (-not (Test-Path -LiteralPath (Join-Path $PlumeSource "$submodule/.git"))) {
        & git -C $PlumeSource submodule update --init $submodule
        if ($LASTEXITCODE -ne 0) { throw "Fetching Plume's $submodule failed." }
    }
}

# libadrenotools, with its linkernsbypass submodule.
if (-not $AdrenotoolsSource) { $AdrenotoolsSource = Join-Path $projectRoot 'out/libadrenotools' }
if (-not (Test-Path -LiteralPath (Join-Path $AdrenotoolsSource 'CMakeLists.txt'))) {
    $android = Get-Content -Raw (Join-Path $projectRoot 'tools/android/dependencies.json') | ConvertFrom-Json
    & git clone -q $android.libadrenotools.url $AdrenotoolsSource
    if ($LASTEXITCODE -ne 0) { throw 'Cloning libadrenotools failed.' }
    & git -C $AdrenotoolsSource checkout -q $android.libadrenotools.revision
    if ($LASTEXITCODE -ne 0) { throw 'Checking out libadrenotools failed.' }
}
& git -C $AdrenotoolsSource submodule update -q --init --recursive
if ($LASTEXITCODE -ne 0) { throw 'Fetching the libadrenotools submodules failed.' }
$AdrenotoolsSource = ConvertTo-CMakePath $AdrenotoolsSource
$gpuAssets = Join-Path $projectRoot 'android/app/src/main/assets/gpu'
if ($TurnipDriver) {
    New-Item -ItemType Directory -Force $gpuAssets | Out-Null
    Copy-Item -LiteralPath $TurnipDriver -Destination (Join-Path $gpuAssets 'libvulkan_freedreno.so') -Force
}
if (-not (Test-Path -LiteralPath (Join-Path $gpuAssets 'libvulkan_freedreno.so'))) {
    Write-Warning 'No Turnip driver to bundle (-TurnipDriver): the APK will use the system Vulkan driver only.'
}

# 2. Android SDK, NDK and Java.
if (-not $AndroidSdk) {
    $AndroidSdk = @($env:ANDROID_HOME, $env:ANDROID_SDK_ROOT, (Join-Path $env:LOCALAPPDATA 'Android/Sdk')) |
        Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
}
if (-not $AndroidSdk -or -not (Test-Path -LiteralPath $AndroidSdk)) {
    throw 'Android SDK not found: install Android Studio, or pass its SDK folder with -AndroidSdk.'
}
$AndroidSdk = ConvertTo-CMakePath $AndroidSdk
if (-not $env:JAVA_HOME) {
    # Gradle needs a JDK 17 or newer; Android Studio ships one.
    $studioJdk = Join-Path $env:ProgramFiles 'Android/Android Studio/jbr'
    if (Test-Path -LiteralPath (Join-Path $studioJdk 'bin/java.exe')) { $env:JAVA_HOME = $studioJdk }
    elseif (-not (Get-Command java -ErrorAction SilentlyContinue)) {
        throw 'No JDK found: install Android Studio (it ships one) or a JDK 17+, or set JAVA_HOME.'
    }
}
$ndk = "$AndroidSdk/ndk/$ndkVersion"
if (-not (Test-Path -LiteralPath "$ndk/build/cmake/android.toolchain.cmake")) {
    $sdkManager = "$AndroidSdk/cmdline-tools/latest/bin/sdkmanager.bat"
    if (-not (Test-Path -LiteralPath $sdkManager)) {
        throw "Android NDK $ndkVersion is missing. In Android Studio: SDK Manager > SDK Tools, check 'Show Package Details', " +
              "install NDK (Side by side) $ndkVersion and Android SDK Command-line Tools."
    }
    Write-Host "Installing the Android NDK $ndkVersion"
    & $sdkManager --install "ndk;$ndkVersion"
    if ($LASTEXITCODE -ne 0) { throw "Installing the Android NDK $ndkVersion failed." }
}

# The SPIR-V of the shader cache is checked with spirv-val (Vulkan SDK).
if (-not $ShaderCache) {
    if (-not $SpirvVal) {
        $candidate = if ($env:VULKAN_SDK) { Join-Path $env:VULKAN_SDK 'Bin/spirv-val.exe' }
        if ($candidate -and (Test-Path -LiteralPath $candidate)) { $SpirvVal = $candidate }
        elseif ($command = Get-Command spirv-val -ErrorAction SilentlyContinue) { $SpirvVal = $command.Source }
        else { throw 'spirv-val not found: install the Vulkan SDK (https://vulkan.lunarg.com) or pass -SpirvVal.' }
    }
    $SpirvVal = ConvertTo-CMakePath $SpirvVal
}

if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $visualStudio) { throw 'Visual Studio Build Tools with the C++ workload are required.' }
    & (Join-Path $visualStudio 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
}

Push-Location $projectRoot
try {
    # 3. The shader cache, built by the Windows game build (Vulkan backend
    #    only); only its shader target is built. The cache does not depend on
    #    the platform, and build/shader-store keeps the shaders between builds.
    if (-not $ShaderCache) {
        $shaderBuild = ConvertTo-CMakePath (Join-Path $projectRoot 'build/android-shaders')
        & cmake --preset windows-release -B $shaderBuild "-DCMAKE_PREFIX_PATH=$SdkPrefix" `
            "-Drexglue_DIR=$SdkPrefix/lib/cmake/rexglue" -DBROKENBOND_NATIVE_RENDERER=ON `
            -DBROKENBOND_NATIVE_D3D12=OFF -DBROKENBOND_NATIVE_VULKAN=ON -DBROKENBOND_BUILD_LAUNCHER=OFF `
            "-DNATIVE_RENDERER_PLUME_SOURCE=$PlumeSource" "-DBROKENBOND_XENOS_RECOMP_THIRDPARTY=$XenosRecompThirdparty" `
            "-DBROKENBOND_SPIRV_VAL=$SpirvVal"
        if ($LASTEXITCODE -ne 0) { throw 'Shader cache configuration failed.' }
        & cmake --build $shaderBuild --target broken_bond_native_shader_cache --parallel $Jobs
        if ($LASTEXITCODE -ne 0) { throw 'Shader cache build failed.' }
        $ShaderCache = "$shaderBuild/shader_cache"
    }

    # 4. The native libraries: the game (libmain.so), the runtime and SDL3,
    #    staged with libc++_shared.so in android/app/src/main/jniLibs/arm64-v8a.
    $androidBuild = ConvertTo-CMakePath (Join-Path $projectRoot 'build/android')
    & cmake -S . -B $androidBuild -G Ninja `
        "-DCMAKE_TOOLCHAIN_FILE=$ndk/build/cmake/android.toolchain.cmake" `
        -DANDROID_ABI=arm64-v8a "-DANDROID_PLATFORM=android-$androidPlatform" -DANDROID_STL=c++_shared `
        -DCMAKE_BUILD_TYPE=Release "-DREXSDK_DIR=$(ConvertTo-CMakePath (Join-Path $projectRoot 'sdk'))" "-DREXGLUE_HOST_TOOL=$rexglue" `
        -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_BUILD_TESTS=OFF `
        -DBROKENBOND_NATIVE_RENDERER=ON -DBROKENBOND_NATIVE_D3D12=OFF -DBROKENBOND_NATIVE_VULKAN=ON `
        "-DNATIVE_RENDERER_PLUME_SOURCE=$PlumeSource" "-DBROKENBOND_XENOS_RECOMP_THIRDPARTY=$XenosRecompThirdparty" `
        "-DBROKENBOND_PREBUILT_SHADER_CACHE=$ShaderCache" "-DBROKENBOND_VERSION=$Version" `
        "-DBROKENBOND_ADRENOTOOLS_SOURCE=$AdrenotoolsSource"
    if ($LASTEXITCODE -ne 0) { throw 'Android configuration failed.' }
    # CI (stand-ins) keeps going after an error, to report all of them in one run.
    $keepGoing = if ($StandInGameCode) { @('--', '-k', '0') } else { @() }
    & cmake --build $androidBuild --target android_jnilibs --parallel $Jobs @keepGoing
    if ($LASTEXITCODE -ne 0) { throw 'Android native build failed.' }

    # 5. The APK. Without a keystore (BROKENBOND_KEYSTORE, see
    #    android/app/build.gradle) it is signed with the debug key.
    $localProperties = Join-Path $projectRoot 'android/local.properties'
    if (-not (Test-Path -LiteralPath $localProperties)) {
        Set-Content -LiteralPath $localProperties -Value "sdk.dir=$AndroidSdk" -Encoding ascii
    }
    $env:ANDROID_HOME = $AndroidSdk
    $env:BROKENBOND_VERSION = $Version
    Push-Location (Join-Path $projectRoot 'android')
    try {
        & .\gradlew.bat --no-daemon assembleRelease
        if ($LASTEXITCODE -ne 0) { throw 'Gradle build of the APK failed.' }
    } finally {
        Pop-Location
    }
    $apk = Get-ChildItem (Join-Path $projectRoot 'android/app/build/outputs/apk/release') -Filter '*.apk' |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $apk) { throw 'Gradle finished without an APK.' }
    $dist = Join-Path $projectRoot 'dist'
    New-Item -ItemType Directory -Force -Path $dist | Out-Null
    $output = Join-Path $dist "NarutoBrokenBond-Android-$Version.apk"
    Copy-Item -LiteralPath $apk.FullName -Destination $output -Force
    Write-Host "APK: $output"

    if ($Install) {
        $adb = "$AndroidSdk/platform-tools/adb.exe"
        if (-not (Test-Path -LiteralPath $adb)) { throw 'adb not found: install Android SDK Platform-Tools.' }
        & $adb install -r $output
        if ($LASTEXITCODE -ne 0) { throw 'adb install failed: is the phone connected with USB debugging on?' }
    }
} finally {
    Pop-Location
}
