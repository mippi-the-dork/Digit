// Copyright Mippithedork 2026, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// A UI-only classifier. Do not infer a frame rate or a native tick value from
// text alone. The source time-rate and conversion interface belong to the
// owning Sequencer instance.
namespace DigitTemporalDisplay
{
    inline bool IsStructuredTimeDisplay(const FString& Text)
    {
        // FFrameNumberInterface emits bracketed timecode, colons (or a
        // semicolon for drop-frame), and a configurable subframe marker which
        // defaults to '*'. Do not interpret any of these as base-10 digits.
        return Text.Contains(TEXT(":"))
            || Text.Contains(TEXT(";"))
            || Text.Contains(TEXT("["))
            || Text.Contains(TEXT("]"))
            || Text.Contains(TEXT("*"));
    }
}
