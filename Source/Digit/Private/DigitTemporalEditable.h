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

    // Resolve a decimal place from a single, explicitly selected glyph in a
    // canonical frame/seconds label. The caller MUST separately prove the
    // owning Sequencer numeric interface and exact native-tick conversion.
    // This helper alone does not establish that a string represents time.
    inline bool TryGetPlainNumericDigitStep(const FString& DisplayText,
        int32 CharacterIndex, double& OutDisplayedStep)
    {
        OutDisplayedStep = 0.0;
        if (DisplayText.IsEmpty() || DisplayText.Len() > 64)
        {
            return false;
        }

        const int32 Start = DisplayText[0] == TEXT('-') ? 1 : 0;
        const int32 End = DisplayText.EndsWith(TEXT("s"))
            ? DisplayText.Len() - 1 : DisplayText.Len();
        if (Start >= End || CharacterIndex < Start || CharacterIndex >= End)
        {
            return false;
        }
        int32 Point = INDEX_NONE;
        int32 IntegerDigits = 0;
        int32 FractionDigits = 0;
        for (int32 Index = Start; Index < End; ++Index)
        {
            const TCHAR C = DisplayText[Index];
            if (C == TEXT('.') && Point == INDEX_NONE)
            {
                Point = Index;
                continue;
            }
            if (C < TEXT('0') || C > TEXT('9'))
            {
                // Reject exponent notation, thousands separators, timecode,
                // subframe markers, localized or ambiguous units.
                return false;
            }
            if (Point == INDEX_NONE)
            {
                ++IntegerDigits;
            }
            else
            {
                ++FractionDigits;
            }
        }
        if (IntegerDigits == 0 || (Point != INDEX_NONE && FractionDigits == 0)
            || DisplayText[CharacterIndex] < TEXT('0')
            || DisplayText[CharacterIndex] > TEXT('9'))
        {
            return false;
        }

        const int32 Place = Point == INDEX_NONE
            ? End - CharacterIndex - 1
            : (CharacterIndex < Point ? Point - CharacterIndex - 1
                : Point - CharacterIndex);
        // A double's decimal magnitude is unsafe to infer at arbitrarily
        // large and small exponents. The native tick and int32 gates apply
        // additional, editor-specific restrictions.
        if (Place < -9 || Place > 9)
        {
            return false;
        }
        OutDisplayedStep = FMath::Pow(10.0, static_cast<double>(Place));
        return FMath::IsFinite(OutDisplayedStep) && OutDisplayedStep > 0.0;
    }
}
