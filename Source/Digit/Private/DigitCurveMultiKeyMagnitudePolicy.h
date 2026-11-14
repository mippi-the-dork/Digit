// Copyright Mippithedork 2026, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Containers/UnrealString.h"
#include <cmath>

// Multi-key Value Ladder: scrub a relative offset from the initial key
// snapshot. Changing rungs never rescales steps already applied. The same
// offset drives the visual feedback, live property updates and native Enter.
namespace DigitCurveMultiKeyMagnitudePolicy
{
    constexpr int32 OriginPlace = 0;
    constexpr int32 Radius = 4;
    constexpr double MaximumOffset = 1000000000.0;

    inline double StepForPlace(const int32 Place)
    {
        return FMath::Pow(10.0, static_cast<double>(Place));
    }

    inline bool ConsumeVerticalRungs(int32& SelectedPlace, float& Travel,
        const float DeltaY, const float Sensitivity,
        const int32 Origin = OriginPlace)
    {
        if (!FMath::IsFinite(DeltaY) || !FMath::IsFinite(Sensitivity)
            || Sensitivity < 0.049f || Sensitivity > 5.01f)
        {
            return false;
        }
        constexpr float RowHeight = 17.0f;
        constexpr float Threshold = RowHeight * 0.5f;
        // The shared ladder applies the native 0.5x scale to the public
        // [0.1, 10] setting, so valid effective values are [0.05, 5].
        // Rejecting values below 0.1 silently disabled every vertical rung
        // at the default/project minimum setting of 0.1.
        Travel += DeltaY * Sensitivity;
        bool bChanged = false;
        while (Travel <= -Threshold && SelectedPlace < Origin + Radius)
        {
            ++SelectedPlace;
            Travel += RowHeight;
            bChanged = true;
        }
        while (Travel >= Threshold && SelectedPlace > Origin - Radius)
        {
            --SelectedPlace;
            Travel -= RowHeight;
            bChanged = true;
        }
        if (SelectedPlace >= Origin + Radius)
        {
            Travel = FMath::Max(Travel, -Threshold);
        }
        if (SelectedPlace <= Origin - Radius)
        {
            Travel = FMath::Min(Travel, Threshold);
        }
        return bChanged;
    }

    inline bool TryAccumulate(const double Previous, const int32 Steps,
        const int32 SelectedPlace, double& OutOffset,
        const int32 Origin = OriginPlace)
    {
        if (!FMath::IsFinite(Previous) || Steps < -32 || Steps > 32
            || SelectedPlace < Origin - Radius || SelectedPlace > Origin + Radius)
        {
            return false;
        }
        const double Next = Previous + static_cast<double>(Steps) * StepForPlace(SelectedPlace);
        if (!FMath::IsFinite(Next) || FMath::Abs(Next) > MaximumOffset)
        {
            return false;
        }
        // The smallest supported rung is 0.0001. Eliminate binary summation
        // dust so reversing a gesture to zero restores exactly the snapshot.
        OutOffset = std::round(Next * 100000000.0) / 100000000.0;
        return true;
    }

    inline FString FormatSignedOffset(const double Offset)
    {
        return FMath::Abs(Offset) < 0.000000001
            ? TEXT("+0") : FString::Printf(TEXT("%+.12g"), Offset);
    }

    inline bool TryStageMixed(const FString& Original, const double Offset,
        FString& OutExpression)
    {
        OutExpression.Reset();
        if (Original != TEXT("Multiple Values") || !FMath::IsFinite(Offset)
            || FMath::Abs(Offset) < 0.000000001
            || FMath::Abs(Offset) > MaximumOffset)
        {
            return false;
        }
        // Original is UE's verified English undetermined-value token. Native
        // SPropertyEditorNumeric substitutes each selected value into this
        // expression, allowing fractional relative updates per object.
        OutExpression = FString::Printf(TEXT("%s %s %.12g"), *Original,
            Offset > 0.0 ? TEXT("+") : TEXT("-"), FMath::Abs(Offset));
        return true;
    }

    inline bool TryStageEqual(const FString& Original, const double Offset,
        const int32 FinestPlace, FString& OutValue)
    {
        OutValue.Reset();
        double Parsed = 0.0;
        if (!LexTryParseString(Parsed, *Original) || !FMath::IsFinite(Parsed)
            || !FMath::IsFinite(Offset) || FMath::Abs(Offset) > MaximumOffset)
        {
            return false;
        }
        const double Result = Parsed + Offset;
        if (!FMath::IsFinite(Result) || FMath::Abs(Result) > 1.0e12)
        {
            return false;
        }
        const int32 DecimalIndex = Original.Find(TEXT("."));
        const int32 ExistingDecimals = DecimalIndex == INDEX_NONE
            ? 0 : Original.Len() - DecimalIndex - 1;
        const int32 Decimals = FMath::Clamp(FMath::Max(ExistingDecimals,
            -FinestPlace), 0, 9);
        OutValue = FString::Printf(TEXT("%.*f"), Decimals, Result);
        return true;
    }
}
