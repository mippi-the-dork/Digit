// Copyright Mippithedork 2026, Inc. All Rights Reserved.
#pragma once

// Curve Editor shares a selected-key Value field across multiple keys. The
// native SSpinBox may remain editable while its slider is disabled. A gated
// preview can allow Unreal's own slider callbacks in that one verified field;
// Digit does not synthesize key changes or replace the Curve Editor delegates.
// Mixed-value annotations have no unambiguous digit/magnitude and are rejected.
namespace DigitCurveSharedValuePolicy
{
    inline bool CanPreviewNativeSlider(
        bool bPreviewSetting,
        bool bVerifiedCurveKeyValueBranch,
        bool bNativeWidgetEnabled,
        bool bNativeSliderEnabled,
        bool bNativeFloatOrDouble,
        bool bCanonicalNumericDisplay)
    {
        return bPreviewSetting && bVerifiedCurveKeyValueBranch
            && bNativeWidgetEnabled && !bNativeSliderEnabled
            && bNativeFloatOrDouble && bCanonicalNumericDisplay;
    }

    inline bool ShouldRestorePreviewSlider(
        bool bPreviewWasApplied,
        bool bStillInOriginalParentChain,
        bool bHostValueBindingRebound,
        bool bSliderStillEnabled)
    {
        // If the owner replaced its own slider state, don't touch it. If the
        // host has rebound or detached, the new row's settings are not ours.
        return bPreviewWasApplied && bStillInOriginalParentChain
            && !bHostValueBindingRebound && bSliderStillEnabled;
    }
}
