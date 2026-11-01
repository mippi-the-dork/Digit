// Copyright Mippithedork 2026, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

// Pure, strictly bounded text staging for the Curve Editor key-detail VALUE
// text box. This is NOT a property setter or a replacement for its native
// OnTextCommitted/Undo path. Only use after confirming the focused widget's
// exact SCurveKeyDetailPanel Value branch and editability.
namespace DigitCurveEditableValue
{
    inline bool TryStepSelectedPlace(const FString& Source, int32 SelectedIndex,
        int32 Direction, FString& OutText, int32& OutSelectedIndex)
    {
        OutText.Reset();
        OutSelectedIndex = INDEX_NONE;
        if ((Direction != 1 && Direction != -1)
            || Source.IsEmpty() || Source.Len() > 32
            || !Source.IsValidIndex(SelectedIndex))
        {
            return false;
        }

        // Accept only plain, ASCII fixed-point numbers. No exponent, NaN,
        // localized grouping, unit suffix, expression, or time representation.
        const bool bNegative = Source[0] == TEXT('-');
        const int32 Start = bNegative ? 1 : 0;
        if (SelectedIndex < Start)
        {
            return false;
        }
        int32 Decimal = INDEX_NONE;
        int32 IntegerDigits = 0;
        int32 FractionDigits = 0;
        int32 DigitsAfterSelected = 0;
        int64 Magnitude = 0;
        int32 DigitCount = 0;
        for (int32 Index = Start; Index < Source.Len(); ++Index)
        {
            const TCHAR C = Source[Index];
            if (C == TEXT('.') && Decimal == INDEX_NONE)
            {
                Decimal = Index;
                continue;
            }
            if (C < TEXT('0') || C > TEXT('9'))
            {
                return false;
            }
            if (++DigitCount > 15)
            {
                return false; // Reject BEFORE integer multiplication can overflow.
            }
            Magnitude = Magnitude * 10 + static_cast<int64>(C - TEXT('0'));
            if (Decimal == INDEX_NONE)
            {
                ++IntegerDigits;
            }
            else
            {
                ++FractionDigits;
            }
            if (Index > SelectedIndex)
            {
                ++DigitsAfterSelected;
            }
        }
        // Keep every intermediate integral and within 15 decimal digits.
        // This protects both UE float-backed values and int64 conversion.
        const int32 Count = IntegerDigits + FractionDigits;
        if (Count == 0 || Count > 15 || IntegerDigits == 0
            || FractionDigits > 9
            || (Decimal != INDEX_NONE && FractionDigits == 0)
            || Source[SelectedIndex] < TEXT('0') || Source[SelectedIndex] > TEXT('9'))
        {
            return false;
        }
        int64 SelectedStep = 1;
        for (int32 Place = 0; Place < DigitsAfterSelected; ++Place)
        {
            SelectedStep *= 10;
        }
        const int64 ScaledValue = bNegative ? -Magnitude : Magnitude;
        const int64 NextValue = ScaledValue + static_cast<int64>(Direction) * SelectedStep;
        // Reject a value that cannot be represented in the same conservative
        // decimal envelope, instead of silently rounding or saturating it.
        constexpr int64 MaxMagnitude = 999999999999999LL;
        if (NextValue < -MaxMagnitude || NextValue > MaxMagnitude)
        {
            return false;
        }

        const bool bNextNegative = NextValue < 0;
        const int64 Absolute = bNextNegative ? -NextValue : NextValue;
        FString Digits = FString::Printf(TEXT("%lld"), static_cast<long long>(Absolute));
        const int32 MinimumWidth = IntegerDigits + FractionDigits;
        while (Digits.Len() < MinimumWidth)
        {
            Digits = TEXT("0") + Digits;
        }
        if (FractionDigits > 0)
        {
            const int32 DecimalPosition = Digits.Len() - FractionDigits;
            Digits = Digits.Left(DecimalPosition) + TEXT(".")
                + Digits.Mid(DecimalPosition);
        }
        OutText = bNextNegative ? (FString(TEXT("-")) + Digits) : Digits;

        // A carry can grow the integer width; crossing zero can add/remove
        // a sign. Preserve the selected DECIMAL PLACE, not a stale offset.
        int32 RemainingDigits = DigitsAfterSelected;
        for (int32 Index = OutText.Len() - 1; Index >= 0; --Index)
        {
            if (OutText[Index] >= TEXT('0') && OutText[Index] <= TEXT('9'))
            {
                if (RemainingDigits-- == 0)
                {
                    OutSelectedIndex = Index;
                    return true;
                }
            }
        }
        OutText.Reset();
        return false;
    }
}
