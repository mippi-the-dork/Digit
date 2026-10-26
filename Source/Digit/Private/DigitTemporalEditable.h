// Copyright Mippithedork 2026, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Framework/Text/TextLayout.h"

// An opt-in keyboard interaction, deliberately separate from native SSpinBox
// slider capture. Select exactly one digit in the original editor text field.
// With no selection, a caret location is ambiguous (before or after a digit).
namespace DigitTemporalEditable
{
    inline bool TryGetSingleSelectedDigit(const FTextSelection& Selection,
        const FString& DisplayText, int32& OutCharacterIndex)
    {
        OutCharacterIndex = INDEX_NONE;
        const FTextLocation& Start = Selection.GetBeginning();
        const FTextLocation& End = Selection.GetEnd();
        if (!Start.IsValid() || !End.IsValid()
            || Start.GetLineIndex() != 0 || End.GetLineIndex() != 0
            || End.GetOffset() != Start.GetOffset() + 1)
        {
            return false;
        }
        const int32 Index = Start.GetOffset();
        if (!DisplayText.IsValidIndex(Index)
            || DisplayText[Index] < TEXT('0') || DisplayText[Index] > TEXT('9'))
        {
            return false;
        }
        OutCharacterIndex = Index;
        return true;
    }
}
