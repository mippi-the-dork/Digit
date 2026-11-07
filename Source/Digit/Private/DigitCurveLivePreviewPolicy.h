// Copyright Mippithedork 2026, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

// Proposed numeric changes always originate from the mouse-down snapshot,
// never from values previously written during the same drag.
namespace DigitCurveLivePreviewPolicy
{
    inline FString SignedOffset(const double Delta)
    {
        return Delta == 0.0 ? TEXT("+0") : FString::Printf(TEXT("%+.12g"), Delta);
    }

    inline bool BuildValues(const TArray<double>& Originals, double Offset,
        TArray<FString>& Out)
    {
        Out.Reset();
        if (!FMath::IsFinite(Offset)) return false;
        for (const double Original : Originals)
        {
            const double Value = Original + Offset;
            if (!FMath::IsFinite(Value)) return false;
            Out.Add(FString::Printf(TEXT("%.17g"), Value));
        }
        return !Out.IsEmpty();
    }
}
