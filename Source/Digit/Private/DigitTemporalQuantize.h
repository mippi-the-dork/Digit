// Copyright Mippithedork 2026, Inc. All Rights Reserved.
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

// Pure arithmetic used by the UE-backed timecode spin box interaction. Keep
// this independent of Slate so scrubbing/reversal/overflow can be tested by a
// standalone C++ runner as well as by Unreal automation.
namespace DigitTemporalQuantize
{
    inline bool TryWholeSelectedSteps(double AccumulatedNativeTicks,
        std::int64_t SelectedNativeTickStep, std::int64_t& OutTickOffset)
    {
        OutTickOffset = 0;
        if (!std::isfinite(AccumulatedNativeTicks) || SelectedNativeTickStep <= 0)
        {
            return false;
        }
        const double WholeStepCount = std::trunc(
            AccumulatedNativeTicks / static_cast<double>(SelectedNativeTickStep));
        const double SafeStepCount = static_cast<double>(
            std::numeric_limits<std::int64_t>::max() / SelectedNativeTickStep);
        if (!std::isfinite(WholeStepCount)
            || WholeStepCount > SafeStepCount || WholeStepCount < -SafeStepCount)
        {
            return false;
        }
        OutTickOffset = static_cast<std::int64_t>(WholeStepCount)
            * SelectedNativeTickStep;
        return true;
    }
}
