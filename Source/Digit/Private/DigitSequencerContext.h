// Copyright Mippithedork 2026, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Misc/FrameRate.h"

class ISequencer;
class FWidgetPath;

// A deliberately narrow bridge. Digit never guesses a frame rate from the
// displayed glyphs or edits the timeline directly. It asks the owning
// Sequencer's own numeric interface to prove a reversible unit conversion.
struct FDigitTemporalSpinContext
{
    TWeakPtr<ISequencer> Sequencer;
    FFrameRate TickResolution;
    FFrameRate DisplayRate;
    double NativePerDisplayedUnit = 0.0;
    // Revalidate against the same reference the host saw at hover time.
    // Some numeric interfaces interpret relative input using this context.
    double ProbeNativeValue = 0.0;
    // Snapshots of the host's rendering. Detect frame/seconds format changes
    // even in the unusual case where the two formats have identical scales.
    FString ZeroDisplaySignature;
    FString OneDisplaySignature;
    bool bDurationIntent = false;
    // A verified structured-timecode spin box uses integral native ticks,
    // not a decimal display-unit multiplier. Its hovered digit is fixed
    // for the duration of the native capture; the Value Ladder is excluded.
    bool bNonDropTimecode = false;
    int32 TimecodeCharacterIndex = INDEX_NONE;
    int64 NativeTimecodeTickStep = 0;
    FString TimecodeReferenceSignature;
};

class FDigitSequencerContext final
{
public:
    static void Initialize();
    static void Shutdown();
    static bool Resolve(const FWidgetPath& Path, const FString& DisplayText,
        double NativeValue, FDigitTemporalSpinContext& OutContext);
    static bool ResolveNonDropTimecode(const FWidgetPath& Path, const FString& DisplayText,
        double NativeValue, int32 CharacterIndex, FDigitTemporalSpinContext& OutContext);
    // Text-based Curve Editor Time fields use native SEditableTextBox commit
    // callbacks. This function computes and validates only the next displayed
    // value; the editor remains responsible for committing through its own
    // property handle on Enter or focus loss.
    static bool TryStepEditableNonDropTimecode(const FWidgetPath& Path,
        const FString& DisplayText, int32 CharacterIndex, int32 Direction,
        FString& OutDisplayText);
    static bool IsStillCurrent(const FDigitTemporalSpinContext& Context);
};
