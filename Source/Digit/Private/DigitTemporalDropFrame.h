// Copyright Mippithedork 2026, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Misc/FrameRate.h"

// SMPTE drop-frame *label* arithmetic, not dropped video frames.  This is
// intentionally separate from non-drop timecode's constant-tick magnitude:
// the difference between two display labels varies at skipped-minute borders.
// The caller must still prove the original and candidate with the exact
// owning Sequencer numeric interface before touching an editable widget.
namespace DigitTemporalDropFrame
{
    struct FLabelStep
    {
        int64 SourceFrame = 0;
        int64 TargetFrame = 0;
        FString TargetLabel;
    };

    inline bool GetRate(const FFrameRate& Rate, int32& OutNominal, int32& OutDropped)
    {
        if (Rate.Numerator > 0 && Rate.Denominator > 0
            && static_cast<int64>(Rate.Numerator) * 1001 ==
                static_cast<int64>(30000) * Rate.Denominator)
        {
            OutNominal = 30;
            OutDropped = 2;
            return true;
        }
        if (Rate.Numerator > 0 && Rate.Denominator > 0
            && static_cast<int64>(Rate.Numerator) * 1001 ==
                static_cast<int64>(60000) * Rate.Denominator)
        {
            OutNominal = 60;
            OutDropped = 4;
            return true;
        }
        return false;
    }

    inline bool IsDroppedLabel(int64 VirtualFrame, int32 Nominal, int32 Dropped)
    {
        const int64 FramesPerMinute = static_cast<int64>(Nominal) * 60;
        const int64 Minute = VirtualFrame / FramesPerMinute;
        const int64 InsideMinute = VirtualFrame % FramesPerMinute;
        return Minute % 10 != 0 && InsideMinute < Dropped;
    }

    inline int64 ToPhysicalFrame(int64 VirtualFrame, int32 Nominal, int32 Dropped)
    {
        const int64 TotalMinutes = VirtualFrame / (static_cast<int64>(Nominal) * 60);
        return VirtualFrame - static_cast<int64>(Dropped)
            * (TotalMinutes - TotalMinutes / 10);
    }

    inline bool TryStepLabel(const FString& Label, int32 CharacterIndex,
        int32 Direction, const FFrameRate& Rate, FLabelStep& OutStep)
    {
        OutStep = FLabelStep();
        int32 Nominal = 0;
        int32 Dropped = 0;
        if (!GetRate(Rate, Nominal, Dropped) || (Direction != -1 && Direction != 1)
            || Label.Len() != 13 || Label[0] != TEXT('[') || Label[12] != TEXT(']')
            || Label[3] != TEXT(':') || Label[6] != TEXT(':')
            || Label[9] != TEXT(';'))
        {
            return false;
        }
        constexpr int32 Starts[4] = { 1, 4, 7, 10 };
        int32 Parts[4] = {};
        int32 Component = INDEX_NONE;
        int32 Place = 0;
        for (int32 Part = 0; Part < 4; ++Part)
        {
            const int32 Start = Starts[Part];
            if (Label[Start] < TEXT('0') || Label[Start] > TEXT('9')
                || Label[Start + 1] < TEXT('0') || Label[Start + 1] > TEXT('9'))
            {
                return false;
            }
            Parts[Part] = 10 * (Label[Start] - TEXT('0'))
                + Label[Start + 1] - TEXT('0');
            if (CharacterIndex == Start || CharacterIndex == Start + 1)
            {
                Component = Part;
                Place = CharacterIndex == Start ? 10 : 1;
            }
        }
        if (Component == INDEX_NONE || Parts[0] > 23 || Parts[1] >= 60
            || Parts[2] >= 60 || Parts[3] >= Nominal)
        {
            return false;
        }
        const int64 Multipliers[4] = {
            static_cast<int64>(Nominal) * 3600,
            static_cast<int64>(Nominal) * 60,
            static_cast<int64>(Nominal), 1
        };
        const int64 MaxVirtualFrame = static_cast<int64>(24) * 3600 * Nominal;
        const int64 VirtualSource = Parts[0] * Multipliers[0]
            + Parts[1] * Multipliers[1]
            + Parts[2] * Multipliers[2] + Parts[3];
        if (IsDroppedLabel(VirtualSource, Nominal, Dropped))
        {
            return false;
        }
        int64 VirtualTarget = VirtualSource
            + static_cast<int64>(Direction) * Place * Multipliers[Component];
        if (VirtualTarget < 0 || VirtualTarget >= MaxVirtualFrame)
        {
            return false;
        }
        // A requested label can land in a skipped code. Advance or retreat
        // to the next real displayed frame in the requested direction.
        while (IsDroppedLabel(VirtualTarget, Nominal, Dropped))
        {
            VirtualTarget += Direction;
            if (VirtualTarget < 0 || VirtualTarget >= MaxVirtualFrame)
            {
                return false;
            }
        }
        int64 Remainder = VirtualTarget;
        const int32 H = static_cast<int32>(Remainder / Multipliers[0]);
        Remainder %= Multipliers[0];
        const int32 M = static_cast<int32>(Remainder / Multipliers[1]);
        Remainder %= Multipliers[1];
        const int32 S = static_cast<int32>(Remainder / Multipliers[2]);
        const int32 F = static_cast<int32>(Remainder % Multipliers[2]);
        OutStep.SourceFrame = ToPhysicalFrame(VirtualSource, Nominal, Dropped);
        OutStep.TargetFrame = ToPhysicalFrame(VirtualTarget, Nominal, Dropped);
        OutStep.TargetLabel = FString::Printf(TEXT("[%02d:%02d:%02d;%02d]"), H, M, S, F);
        return OutStep.SourceFrame != OutStep.TargetFrame;
    }
}
