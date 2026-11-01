// Copyright Mippithedork 2026, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DigitCurveEditableValue.h"
#include <cmath>

// Pure drag quantization and atomic VALUE staging. TIME staging must pass
// through the owning Sequencer's native rate/format conversion, in the caller.
namespace DigitCurveTextMouse
{
    // Preserve sub-step pixel movement; never discard overshoot. Process a
    // bounded batch per pointer event so unexpectedly large deltas cannot
    // block Slate's input thread.
    inline int32 ConsumeSteps(double& AccumulatedPixels,
        double PixelsPerStep, int32 MaximumSteps = 12)
    {
        if (!FMath::IsFinite(AccumulatedPixels)
            || !FMath::IsFinite(PixelsPerStep)
            || PixelsPerStep < 1.0 || MaximumSteps < 1 || MaximumSteps > 32)
        {
            AccumulatedPixels = 0.0;
            return 0;
        }
        const int32 Direction = AccumulatedPixels > 0.0 ? 1 : -1;
        const double Magnitude = FMath::Abs(AccumulatedPixels);
        const int32 Steps = static_cast<int32>(FMath::Min(
            std::floor(Magnitude / PixelsPerStep),
            static_cast<double>(MaximumSteps)));
        if (Steps <= 0)
        {
            return 0;
        }
        AccumulatedPixels -= Direction * Steps * PixelsPerStep;
        return Direction * Steps;
    }

    // Native text widgets own transactions. Only a deliberate, unchanged,
    // opt-in gesture may ask Slate to deliver Enter on mouse release.
    inline bool ShouldRequestNativeCommit(bool bEnabled, bool bStaged,
        bool bOwnsCapture, bool bSourceUnchanged)
    {
        return bEnabled && bStaged && bOwnsCapture && bSourceUnchanged;
    }

    // A pending commit is a one-shot request, not proof that the host has
    // accepted a keyframe transaction. Recheck these conditions AFTER Slate
    // processes mouse-up, just before forwarding Enter to the original host.
    inline bool CanDispatchDeferredCommit(bool bPreviewEnabled,
        bool bOriginalFieldFocused, bool bOriginalFieldAttached,
        bool bSelectedDigitStable, bool bStagedBufferUnchanged,
        bool bPointerCaptureReleased)
    {
        return bPreviewEnabled && bOriginalFieldFocused
            && bOriginalFieldAttached && bSelectedDigitStable
            && bStagedBufferUnchanged && bPointerCaptureReleased;
    }

    // A drag may only continue from its own last staged buffer and the
    // current selected digit. Never transform a value updated by another
    // editor event, a selection change, or a rebuild of the source field.
    // Carries are supported: the caller updates ExpectedIndex after each
    // successful selected-place remapping.
    inline bool IsUninterruptedEdit(const FString& LiveText,
        const FString& InitialText, const FString& LastStagedText,
        int32 SelectedIndex, int32 ExpectedIndex)
    {
        if (ExpectedIndex == INDEX_NONE || SelectedIndex != ExpectedIndex
            || LiveText.IsEmpty() || LiveText.Len() > 128)
        {
            return false;
        }
        return LiveText == (LastStagedText.IsEmpty() ? InitialText : LastStagedText);
    }

    // Shift intentionally changes horizontal *distance* per step, not the
    // numeric magnitude. The existing step batch limit remains in force,
    // including while dragging timecode through a Sequencer parser.
    inline double GetPixelsPerStep(double Sensitivity, bool bShiftDown)
    {
        if (!FMath::IsFinite(Sensitivity))
        {
            Sensitivity = 1.0;
        }
        return FMath::Max(1.0, 8.0 /
            (FMath::Clamp(Sensitivity, 0.1, 10.0) * (bShiftDown ? 4.0 : 1.0)));
    }

    inline bool TryStageValueSteps(const FString& Source, int32 SourceIndex,
        int32 SignedSteps, FString& OutText, int32& OutIndex)
    {
        OutText.Reset();
        OutIndex = INDEX_NONE;
        if (SignedSteps == 0 || SignedSteps < -32 || SignedSteps > 32)
        {
            return false;
        }
        FString Current = Source;
        int32 Index = SourceIndex;
        for (int32 Step = 0; Step < FMath::Abs(SignedSteps); ++Step)
        {
            FString Candidate;
            int32 CandidateIndex = INDEX_NONE;
            if (!DigitCurveEditableValue::TryStepSelectedPlace(Current, Index,
                SignedSteps > 0 ? 1 : -1, Candidate, CandidateIndex))
            {
                return false; // No partially staged edit.
            }
            Current = MoveTemp(Candidate);
            Index = CandidateIndex;
        }
        OutText = MoveTemp(Current);
        OutIndex = Index;
        return true;
    }
}
