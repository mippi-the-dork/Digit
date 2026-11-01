// Copyright Mippithedork 2026, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include <cmath>

// Pure policy used by Digit's native SSpinBox scrub path. These controls are
// intentionally separate from Curve Editor's text-only Ctrl+Alt gesture.
namespace DigitScrubDisplayPolicy
{
    inline double NativeModifierMultiplier(bool bShift, bool bControl, bool bAlt)
    {
        // Ctrl+Alt belongs to the selected-text gesture. When the widget is
        // actually a native spin box, it must not silently add two more decimal
        // places (and then trigger its own precision-change safety guard).
        // Preserve Shift's ordinary coarse multiplier if also held.
        if (bControl && bAlt)
        {
            return bShift ? 10.0 : 1.0;
        }
        if (bShift)
        {
            return bAlt ? 100.0 : 10.0;
        }
        if (bControl)
        {
            return 0.1;
        }
        return 1.0;
    }

    inline int32 MaximumSelfGeneratedFractionDigits(
        int32 InitialFractionDigits, int32 FormatterMaximum, double VisibleStep)
    {
        if (FormatterMaximum < InitialFractionDigits || !FMath::IsFinite(VisibleStep)
            || VisibleStep == 0.0)
        {
            return InitialFractionDigits;
        }
        // A finer ladder rung, Ctrl (0.1x) or fractional sensitivity may
        // *legitimately* add decimals to the native display. Only authorize
        // precision that this drag requested, bounded by the host formatter.
        const double Required = -std::log10(std::abs(VisibleStep));
        if (!FMath::IsFinite(Required))
        {
            return InitialFractionDigits;
        }
        // Avoid a floating rounding artifact classifying 0.01 as 3 digits.
        const double RoundedUp = std::ceil(Required - 1.e-8);
        const int32 Requested = static_cast<int32>(FMath::Clamp(
            RoundedUp, 0.0, static_cast<double>(FormatterMaximum)));
        return FMath::Max(InitialFractionDigits, Requested);
    }

    inline bool IsExpectedFractionalWidth(
        int32 InitialFractionDigits, int32 CurrentFractionDigits,
        int32 AuthorizedFractionDigits, bool bHadDigitMovement)
    {
        if (CurrentFractionDigits == InitialFractionDigits)
        {
            return true;
        }
        // Any contraction is still suspicious: it may have removed the
        // selected digit. Increased precision is fine ONLY after Digit itself
        // requested a representable movement using that finer magnitude.
        return bHadDigitMovement && CurrentFractionDigits > InitialFractionDigits
            && CurrentFractionDigits <= AuthorizedFractionDigits;
    }
}
