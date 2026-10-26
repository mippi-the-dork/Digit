// Copyright Mippithedork 2026, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Misc/FrameRate.h"
#include <cmath>
#include <limits>

// A unit-only bridge for *non-drop-frame* timecode. It deliberately does not
// provide a Slate editing mechanism. Text-based frame-number customizations
// still own their value commits and transactions; Digit must not bypass them.
// Drop-frame minutes are non-uniform, so they cannot use the linear digit
// magnitudes calculated here.
namespace DigitTemporalTimecode
{
    struct FDigitStep
    {
        int32 ComponentIndex = INDEX_NONE; // 0 hours, 1 minutes, 2 seconds, 3 frames
        int32 DecimalPlaceWithinComponent = INDEX_NONE;
        int64 DisplayFrameStep = 0;
        int64 NativeTickStep = 0;
    };

    inline bool TryGetNonDropFrameDigitStep(
        const FString& DisplayText, int32 CharacterIndex,
        const FFrameRate& TickResolution, const FFrameRate& DisplayRate,
        FDigitStep& OutStep)
    {
        OutStep = FDigitStep();

        // UE 5.8.3 FFrameNumberInterface formats non-drop timecode as
        // [HH:MM:SS:FF]. A semicolon is a drop-frame marker and an asterisk
        // indicates a subframe. Do not reinterpret either as a base-10 field.
        if (DisplayText.Len() != 13 || DisplayText[0] != TEXT('[')
            || DisplayText[12] != TEXT(']')
            || DisplayText[3] != TEXT(':') || DisplayText[6] != TEXT(':')
            || DisplayText[9] != TEXT(':'))
        {
            return false;
        }

        constexpr int32 GroupStarts[4] = { 1, 4, 7, 10 };
        int32 Components[4] = { 0, 0, 0, 0 };
        int32 SelectedComponent = INDEX_NONE;
        int32 SelectedPlace = INDEX_NONE;
        for (int32 Component = 0; Component < 4; ++Component)
        {
            const int32 Start = GroupStarts[Component];
            for (int32 Offset = 0; Offset < 2; ++Offset)
            {
                const TCHAR Character = DisplayText[Start + Offset];
                if (Character < TEXT('0') || Character > TEXT('9'))
                {
                    return false;
                }
            }
            Components[Component] = (DisplayText[Start] - TEXT('0')) * 10
                + DisplayText[Start + 1] - TEXT('0');
            if (CharacterIndex == Start || CharacterIndex == Start + 1)
            {
                SelectedComponent = Component;
                SelectedPlace = CharacterIndex == Start ? 1 : 0;
            }
        }
        if (SelectedComponent == INDEX_NONE
            || TickResolution.Numerator <= 0 || TickResolution.Denominator <= 0
            || DisplayRate.Numerator <= 0 || DisplayRate.Denominator <= 0)
        {
            return false;
        }

        const double FramesPerSecond = DisplayRate.AsDecimal();
        const int32 NominalFramesPerSecond = FMath::RoundToInt(FramesPerSecond);
        // Non-drop timecode counts nominal integer frames even for fractional
        // frame rates like 30000/1001. Unusual rates require a separate probe.
        if (NominalFramesPerSecond < 1 || NominalFramesPerSecond > 120
            || FMath::Abs(FramesPerSecond - NominalFramesPerSecond) > 0.1
            || Components[1] >= 60 || Components[2] >= 60
            || Components[3] >= NominalFramesPerSecond)
        {
            return false;
        }

        // Temporal values are stored in integral FFrameNumber ticks. Avoid a
        // silently rounded native step when the focused rate combination
        // cannot represent exactly one display frame.
        const double TicksPerFrame = TickResolution.AsDecimal() / FramesPerSecond;
        const double RoundedTicksPerFrame = std::round(TicksPerFrame);
        if (!FMath::IsFinite(TicksPerFrame) || RoundedTicksPerFrame < 1.0
            || FMath::Abs(TicksPerFrame - RoundedTicksPerFrame) > 1e-7
            || RoundedTicksPerFrame > static_cast<double>(std::numeric_limits<int32>::max()))
        {
            return false;
        }

        const int64 GroupFrameMultipliers[4] = {
            static_cast<int64>(NominalFramesPerSecond) * 3600,
            static_cast<int64>(NominalFramesPerSecond) * 60,
            static_cast<int64>(NominalFramesPerSecond),
            1
        };
        const int64 DecimalMultiplier = SelectedPlace == 1 ? 10 : 1;
        const int64 DisplayFrameStep = GroupFrameMultipliers[SelectedComponent] * DecimalMultiplier;
        const int64 TicksPerWholeFrame = static_cast<int64>(RoundedTicksPerFrame);
        // This bound prevents overflow when an extreme editor tick rate is used.
        if (DisplayFrameStep > std::numeric_limits<int64>::max() / TicksPerWholeFrame)
        {
            return false;
        }
        OutStep.ComponentIndex = SelectedComponent;
        OutStep.DecimalPlaceWithinComponent = SelectedPlace;
        OutStep.DisplayFrameStep = DisplayFrameStep;
        OutStep.NativeTickStep = DisplayFrameStep * TicksPerWholeFrame;
        return true;
    }
}
