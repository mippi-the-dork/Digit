// Copyright Mippithedork 2026, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include <cmath>

// A native SSpinBox scrub selects a DECIMAL PLACE, not a continuous rate of
// numeric change. Horizontal sensitivity controls the physical distance
// required for one complete increment of that place; it MUST NOT multiply
// the value delta itself (e.g. 0.7 * 10 = 7 would corrupt a tens-place scrub).
// This is distinct from Curve Editor multi-key text scrubbing, which already
// has its own per-step pixel accumulator and transaction lifecycle.
namespace DigitNativeStepPolicy
{
    inline int32 ConsumeWholePlaceSteps(double& FractionalSteps,
        double CursorDeltaX, double Sensitivity)
    {
        if (!FMath::IsFinite(FractionalSteps)
            || !FMath::IsFinite(CursorDeltaX)
            || !FMath::IsFinite(Sensitivity)
            || Sensitivity <= 0.0)
        {
            FractionalSteps = 0.0;
            return 0;
        }

        constexpr double MaxStepsPerEvent = 4096.0;
        const double Accumulated = FractionalSteps + CursorDeltaX * Sensitivity;
        if (!FMath::IsFinite(Accumulated))
        {
            FractionalSteps = 0.0;
            return 0;
        }

        // Truncation toward zero makes opposite motion cancel a previously
        // incomplete step. Carry remainder across pointer events, rather than
        // rounding the numeric VALUE to a smaller place on every event.
        // Floating-point summation of ten 0.7-pixel fractions can yield
        // 6.999999999999999 rather than seven. Stabilize only values already
        // within numeric dust of a whole step, without rounding meaningful
        // sub-step mouse travel.
        const double NearestWhole = std::round(Accumulated);
        const double StableAccumulated = FMath::Abs(Accumulated - NearestWhole) <= 1.e-9
            ? NearestWhole : Accumulated;
        const double WholeSteps = std::trunc(StableAccumulated);
        const double BoundedSteps = FMath::Clamp(
            WholeSteps, -MaxStepsPerEvent, MaxStepsPerEvent);
        FractionalSteps = StableAccumulated - BoundedSteps;
        if (FMath::Abs(FractionalSteps) > MaxStepsPerEvent)
        {
            // An anomalously large pointer event must not leave an
            // unbounded backlog that keeps changing values while stationary.
            FractionalSteps = std::fmod(FractionalSteps, 1.0);
        }
        return static_cast<int32>(BoundedSteps);
    }
}
