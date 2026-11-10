// Copyright Mippithedork 2026, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

// Pure policy shared by the Curve Editor multi-value text field and the
// existing Value Ladder. The UI never substitutes an invented value for
// Unreal's "Multiple Values" marker.
namespace DigitCurveUnifiedGesturePolicy
{
    enum class EActivation : uint8 { NativeText, HoveredEqualDigit, AltLadder, LegacyCtrlAlt };

    inline EActivation Classify(const bool bMixed, const bool bAlt,
        const bool bCtrl, const bool bDigitHovered)
    {
        if (bAlt) return bCtrl ? EActivation::LegacyCtrlAlt : EActivation::AltLadder;
        return !bMixed && bDigitHovered
            ? EActivation::HoveredEqualDigit : EActivation::NativeText;
    }

    // Ctrl+Alt is retained as a compatibility gesture: holding Ctrl from
    // the start must not pin the ladder at ones forever. Starting with Alt
    // alone lets Ctrl lock the chosen rung mid-gesture, like single-key mode.
    inline bool ShouldLockMagnitude(bool bSetting, bool bCtrlDown,
        bool bCtrlUsedForActivation, bool bLadderDrag, bool bRowLocked)
    {
        return bLadderDrag && !bRowLocked && bSetting && bCtrlDown
            && !bCtrlUsedForActivation;
    }

    inline bool ShouldUseLadder(const EActivation Activation)
    {
        return Activation == EActivation::AltLadder
            || Activation == EActivation::LegacyCtrlAlt;
    }
}
