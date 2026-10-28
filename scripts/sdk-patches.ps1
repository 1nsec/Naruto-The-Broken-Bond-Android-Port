# The project's ReXGlue SDK patches, in the order they apply, and how they are
# applied to the sdk submodule. Dot-sourced by build-sdk.ps1 and
# build-android.ps1; scripts/android/apply-sdk-patches.sh does the same in CI.
#
# rexglue-android.patch only changes code behind Android checks, so the
# Windows SDK build is the same with it applied.

$SdkPatchNames = @(
    'rexglue-enable-usbcam.patch',
    'rexglue-discovery.patch',
    'rexglue-xma-decoder.patch',
    'rexglue-mnk-modifiers.patch',
    'rexglue-pc-features.patch',
    'rexglue-direct-mouse.patch',
    'rexglue-android.patch'
)

function Test-SdkPatch {
    param([string]$Sdk, [string]$Patch, [switch]$Reverse)
    # git writes to stderr when the check fails; Windows PowerShell turns that
    # into a terminating error under 'Stop'.
    $ErrorActionPreference = 'Continue'
    $arguments = @('-C', $Sdk, 'apply', '--check')
    if ($Reverse) { $arguments += '--reverse' }
    & git @arguments $Patch 2>&1 | Out-Null
    return $LASTEXITCODE -eq 0
}

# Applies the patches that are not applied yet. A later patch can change the
# lines next to an earlier one, and then the earlier one no longer checks in
# reverse: the stack is checked from the top, and every patch below the
# highest one found applied counts as applied.
function Sync-SdkPatches {
    param([Parameter(Mandatory)][string]$ProjectRoot)
    $sdk = Join-Path $ProjectRoot 'sdk'
    $paths = @($SdkPatchNames | ForEach-Object { Join-Path $ProjectRoot "patches/$_" })
    $applied = 0
    for ($i = $paths.Count; $i -gt 0; $i--) {
        if (Test-SdkPatch -Sdk $sdk -Patch $paths[$i - 1] -Reverse) {
            $applied = $i
            break
        }
    }
    for ($i = $applied; $i -lt $paths.Count; $i++) {
        if (-not (Test-SdkPatch -Sdk $sdk -Patch $paths[$i])) {
            throw "SDK patch $($SdkPatchNames[$i]) cannot be applied or verified. Existing SDK changes were preserved."
        }
        & git -C $sdk apply $paths[$i]
        if ($LASTEXITCODE -ne 0) { throw "Applying SDK patch $($SdkPatchNames[$i]) failed." }
        Write-Host "Applied SDK patch $($SdkPatchNames[$i])"
    }
}
