// Copyright Mippithedork 2026, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DigitTemporalEditable.h"

// Native FrameNumberInterface's asterisk signifies an explicit fractional
// frame, not a decimal point in the whole frame count.  No host conversion
// is assumed here: owning Sequencer and exact tick round-trips are mandatory.
namespace DigitTemporalSubframe
{
    inline bool TryGetFrameDigitStep(const FString& DisplayText,
        int32 CharacterIndex, double& OutFrames)
    {
        OutFrames = 0.0;
        const int32 Separator = DisplayText.Find(TEXT("*"));
        if (Separator <= 0 || Separator == DisplayText.Len() - 1
            || DisplayText.Find(TEXT("*"), ESearchCase::CaseSensitive,
                ESearchDir::FromStart, Separator + 1) != INDEX_NONE)
        {
            return false;
        }
        const FString Whole = DisplayText.Left(Separator);
        const FString Fraction = DisplayText.Mid(Separator + 1);
        double Step = 0.0;
        if (CharacterIndex < Separator)
        {
            if (!DigitTemporalEditable::TryGetPlainNumericDigitStep(
                    Whole, CharacterIndex, Step))
            {
                return false;
            }
        }
        else if (CharacterIndex > Separator)
        {
            // The native suffix is a 0.x fraction of one display frame.
            if (!Fraction.StartsWith(TEXT("0."))
                || !DigitTemporalEditable::TryGetPlainNumericDigitStep(
                    Fraction, CharacterIndex - Separator - 1, Step)
                || Step >= 1.0)
            {
                return false;
            }
        }
        else
        {
            return false;
        }
        OutFrames = Step;
        return true;
    }
}
