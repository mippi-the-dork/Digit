// Copyright Mippithedork 2026, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Framework/Text/TextLayout.h"
#include <initializer_list>

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

    // Move among digits of the SAME native editor text buffer. Do not
    // reinterpret punctuation, timecode separators, or marker-only subframes
    // as numeric positions. Native value commits are not involved.
    inline bool TryNavigateSelectedDigit(const FString& DisplayText,
        int32 CurrentIndex, int32 Direction, int32& OutIndex)
    {
        OutIndex = INDEX_NONE;
        if ((Direction != 1 && Direction != -1)
            || DisplayText.IsEmpty() || DisplayText.Len() > 128
            || !DisplayText.IsValidIndex(CurrentIndex)
            || DisplayText[CurrentIndex] < TEXT('0')
            || DisplayText[CurrentIndex] > TEXT('9'))
        {
            return false;
        }
        for (int32 Index = CurrentIndex + Direction;
             DisplayText.IsValidIndex(Index); Index += Direction)
        {
            if (DisplayText[Index] >= TEXT('0') && DisplayText[Index] <= TEXT('9'))
            {
                OutIndex = Index;
                return true;
            }
        }
        return false;
    }

    // Native Time formatting can widen a frame number (0009 -> 0010 or
    // 9 -> 10), or change its sign. A stale character offset then selects
    // the WRONG magnitude on the next keypress. Track the ordinal digit from
    // the right, as the Value adapter already does. Refuse remapping if the
    // native formatter changed the fractional precision or token type.
    inline bool TryRemapSelectedDigit(const FString& Source,
        int32 SourceIndex, const FString& Target, int32& OutIndex)
    {
        OutIndex = INDEX_NONE;
        if (Source.IsEmpty() || Target.IsEmpty()
            || Source.Len() > 128 || Target.Len() > 128
            || !Source.IsValidIndex(SourceIndex)
            || Source[SourceIndex] < TEXT('0')
            || Source[SourceIndex] > TEXT('9'))
        {
            return false;
        }
        // Do not cross between seconds, timecode, and subframe display
        // families during a staged edit. Their decimal places are distinct.
        for (const TCHAR* Punctuation : {TEXT("."), TEXT("*"), TEXT(":"),
                 TEXT(";"), TEXT("["), TEXT("]"), TEXT("s")})
        {
            if (Source.Contains(Punctuation) != Target.Contains(Punctuation))
            {
                return false;
            }
        }
        const int32 SourcePoint = Source.Find(TEXT("."));
        const int32 TargetPoint = Target.Find(TEXT("."));
        if ((SourcePoint != INDEX_NONE || TargetPoint != INDEX_NONE))
        {
            int32 SourceFractionDigits = 0;
            int32 TargetFractionDigits = 0;
            for (int32 Index = SourcePoint + 1; Index < Source.Len(); ++Index)
            {
                if (Source[Index] >= TEXT('0') && Source[Index] <= TEXT('9'))
                {
                    ++SourceFractionDigits;
                }
            }
            for (int32 Index = TargetPoint + 1; Index < Target.Len(); ++Index)
            {
                if (Target[Index] >= TEXT('0') && Target[Index] <= TEXT('9'))
                {
                    ++TargetFractionDigits;
                }
            }
            if (SourceFractionDigits != TargetFractionDigits)
            {
                return false;
            }
        }
        int32 DigitsToRight = 0;
        for (int32 Index = SourceIndex + 1; Index < Source.Len(); ++Index)
        {
            if (Source[Index] >= TEXT('0') && Source[Index] <= TEXT('9'))
            {
                ++DigitsToRight;
            }
        }
        for (int32 Index = Target.Len() - 1; Index >= 0; --Index)
        {
            if (Target[Index] >= TEXT('0') && Target[Index] <= TEXT('9'))
            {
                if (DigitsToRight-- == 0)
                {
                    OutIndex = Index;
                    return true;
                }
            }
        }
        return false;
    }

}
