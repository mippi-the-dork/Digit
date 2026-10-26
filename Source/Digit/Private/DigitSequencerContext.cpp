// Copyright Mippithedork 2026, Inc. All Rights Reserved.
#include "DigitSequencerContext.h"
#include "DigitTemporalTimecode.h"

#include "Framework/Application/SlateApplication.h"
#include "ISequencer.h"
#include "ISequencerModule.h"
#include "Layout/WidgetPath.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/NumericTypeInterface.h"
#include "Widgets/SWidget.h"

namespace
{
    TArray<TWeakPtr<ISequencer>> KnownSequencers;
    FDelegateHandle SequencerCreatedHandle;

    void OnSequencerCreated(TSharedRef<ISequencer> Sequencer)
    {
        KnownSequencers.RemoveAll([](const TWeakPtr<ISequencer>& Item)
        {
            return !Item.IsValid();
        });
        KnownSequencers.Add(Sequencer.ToWeakPtr());
    }

    bool OwnsWidgetPath(const ISequencer& Sequencer, const FWidgetPath& Path)
    {
        // Never match merely by window title: multiple Sequencers can be open.
        const SWidget* RootWidget = &Sequencer.GetSequencerWidget().Get();
        for (int32 Index = 0; Index < Path.Widgets.Num(); ++Index)
        {
            if (&Path.Widgets[Index].Widget.Get() == RootWidget)
            {
                return true;
            }
        }
        return false;
    }

    bool TryHostNumericInterface(const TSharedPtr<INumericTypeInterface<double>>& Interface,
        const FString& Text, double NativeValue, double NativeTicksPerFrame,
        double NativeTicksPerSecond, double& OutNativeUnit,
        FString& OutZeroSignature, FString& OutOneSignature)
    {
        if (!Interface.IsValid() || Interface->ToString(NativeValue) != Text)
        {
            return false;
        }

        // ToString must agree with the actual widget. Probe the host interface
        // rather than an inferred NativeValue / DisplayValue ratio: that ratio
        // is undefined at zero and inaccurate when text is rounded.
        const TOptional<double> Zero = Interface->FromString(TEXT("0"), NativeValue);
        const TOptional<double> One = Interface->FromString(TEXT("1"), NativeValue);
        const TOptional<double> Two = Interface->FromString(TEXT("2"), NativeValue);
        if (!Zero.IsSet() || !One.IsSet() || !Two.IsSet())
        {
            return false;
        }

        const double Unit = One.GetValue() - Zero.GetValue();
        // The host may have an arbitrary numeric interface. Require an
        // affine step across two adjacent display units, not just a matching
        // 0->1 slope that could be coincidental for a nonlinear converter.
        const double SecondUnit = Two.GetValue() - One.GetValue();
        if (!FMath::IsFinite(Unit) || Unit <= 0.0
            || !FMath::IsFinite(SecondUnit)
            || FMath::Abs(Unit - SecondUnit)
                > FMath::Max(1e-7, FMath::Abs(Unit) * 1e-5))
        {
            return false;
        }

        // Accept ONLY frame or second mappings demonstrated by the current
        // Sequencer tick/display rates. Reject arbitrary custom numeric units
        // that happen to format a value identically.
        const auto Matches = [Unit](double Expected)
        {
            return FMath::IsFinite(Expected) && Expected > 0.0
                && FMath::Abs(Unit - Expected) <= FMath::Max(1e-7, Expected * 1e-5);
        };
        if (!Matches(NativeTicksPerFrame) && !Matches(NativeTicksPerSecond))
        {
            return false;
        }

        OutNativeUnit = Unit;
        OutZeroSignature = Interface->ToString(Zero.GetValue());
        OutOneSignature = Interface->ToString(One.GetValue());
        return true;
    }
}

void FDigitSequencerContext::Initialize()
{
    if (SequencerCreatedHandle.IsValid())
    {
        return;
    }
    ISequencerModule& Module = FModuleManager::LoadModuleChecked<ISequencerModule>(TEXT("Sequencer"));
    SequencerCreatedHandle = Module.RegisterOnSequencerCreated(
        FOnSequencerCreated::FDelegate::CreateStatic(&OnSequencerCreated));
}

void FDigitSequencerContext::Shutdown()
{
    if (SequencerCreatedHandle.IsValid() && FModuleManager::Get().IsModuleLoaded(TEXT("Sequencer")))
    {
        ISequencerModule& Module = FModuleManager::GetModuleChecked<ISequencerModule>(TEXT("Sequencer"));
        Module.UnregisterOnSequencerCreated(SequencerCreatedHandle);
    }
    SequencerCreatedHandle.Reset();
    KnownSequencers.Reset();
}

bool FDigitSequencerContext::Resolve(const FWidgetPath& Path, const FString& DisplayText,
    double NativeValue, FDigitTemporalSpinContext& OutContext)
{
    if (!FMath::IsFinite(NativeValue))
    {
        return false;
    }

    for (int32 Index = KnownSequencers.Num() - 1; Index >= 0; --Index)
    {
        const TSharedPtr<ISequencer> Sequencer = KnownSequencers[Index].Pin();
        if (!Sequencer.IsValid())
        {
            KnownSequencers.RemoveAtSwap(Index);
            continue;
        }
        if (!OwnsWidgetPath(*Sequencer, Path))
        {
            continue;
        }

        const FFrameRate Tick = Sequencer->GetFocusedTickResolution();
        const FFrameRate Display = Sequencer->GetFocusedDisplayRate();
        if (Tick.Numerator <= 0 || Tick.Denominator <= 0
            || Display.Numerator <= 0 || Display.Denominator <= 0)
        {
            return false;
        }

        const double TicksPerSecond = Tick.AsDecimal();
        const double TicksPerFrame = TicksPerSecond / Display.AsDecimal();
        double Unit = 0.0;
        FString ZeroSignature;
        FString OneSignature;
        const bool bPosition = TryHostNumericInterface(
            Sequencer->GetNumericTypeInterface(UE::Sequencer::ENumericIntent::Position),
            DisplayText, NativeValue, TicksPerFrame, TicksPerSecond, Unit,
            ZeroSignature, OneSignature);
        const bool bDuration = !bPosition && TryHostNumericInterface(
            Sequencer->GetNumericTypeInterface(UE::Sequencer::ENumericIntent::Duration),
            DisplayText, NativeValue, TicksPerFrame, TicksPerSecond, Unit,
            ZeroSignature, OneSignature);
        if (bPosition || bDuration)
        {
            OutContext.Sequencer = Sequencer;
            OutContext.TickResolution = Tick;
            OutContext.DisplayRate = Display;
            OutContext.NativePerDisplayedUnit = Unit;
            OutContext.ProbeNativeValue = NativeValue;
            OutContext.ZeroDisplaySignature = MoveTemp(ZeroSignature);
            OutContext.OneDisplaySignature = MoveTemp(OneSignature);
            OutContext.bDurationIntent = bDuration;
            return true;
        }
        // We found the owner, but its numeric interface does not prove a safe
        // temporal conversion. Do not silently fall back to guessing units.
        return false;
    }
    return false;
}

bool FDigitSequencerContext::ResolveNonDropTimecode(const FWidgetPath& Path,
    const FString& DisplayText, double NativeValue, int32 CharacterIndex,
    FDigitTemporalSpinContext& OutContext)
{
    if (!FMath::IsFinite(NativeValue))
    {
        return false;
    }

    for (int32 Index = KnownSequencers.Num() - 1; Index >= 0; --Index)
    {
        const TSharedPtr<ISequencer> Sequencer = KnownSequencers[Index].Pin();
        if (!Sequencer.IsValid())
        {
            KnownSequencers.RemoveAtSwap(Index);
            continue;
        }
        if (!OwnsWidgetPath(*Sequencer, Path))
        {
            continue;
        }

        const FFrameRate Tick = Sequencer->GetFocusedTickResolution();
        const FFrameRate Display = Sequencer->GetFocusedDisplayRate();
        DigitTemporalTimecode::FDigitStep Step;
        if (!DigitTemporalTimecode::TryGetNonDropFrameDigitStep(
            DisplayText, CharacterIndex, Tick, Display, Step))
        {
            return false;
        }

        // The format of the *actual* hovered native spin box must agree with
        // the owning Sequencer, and moving by the proposed native ticks must
        // remain in the same structured format. Never infer this from text
        // alone or from the Sequencer's rate settings in isolation.
        for (int32 IntentIndex = 0; IntentIndex < 2; ++IntentIndex)
        {
            const bool bDuration = IntentIndex == 1;
            const auto Intent = bDuration
                ? UE::Sequencer::ENumericIntent::Duration
                : UE::Sequencer::ENumericIntent::Position;
            const TSharedPtr<INumericTypeInterface<double>> Interface =
                Sequencer->GetNumericTypeInterface(Intent);
            if (!Interface.IsValid() || Interface->ToString(NativeValue) != DisplayText)
            {
                continue;
            }
            const double NextNativeValue = NativeValue + static_cast<double>(Step.NativeTickStep);
            if (!FMath::IsFinite(NextNativeValue)
                || NextNativeValue > static_cast<double>(MAX_int32)
                || NextNativeValue < static_cast<double>(MIN_int32))
            {
                continue;
            }
            const FString NextDisplay = Interface->ToString(NextNativeValue);
            DigitTemporalTimecode::FDigitStep NextStep;
            if (!DigitTemporalTimecode::TryGetNonDropFrameDigitStep(
                    NextDisplay, CharacterIndex, Tick, Display, NextStep)
                || NextStep.NativeTickStep != Step.NativeTickStep)
            {
                continue;
            }

            OutContext = FDigitTemporalSpinContext();
            OutContext.Sequencer = Sequencer;
            OutContext.TickResolution = Tick;
            OutContext.DisplayRate = Display;
            OutContext.ProbeNativeValue = NativeValue;
            OutContext.bDurationIntent = bDuration;
            OutContext.bNonDropTimecode = true;
            OutContext.TimecodeCharacterIndex = CharacterIndex;
            OutContext.NativeTimecodeTickStep = Step.NativeTickStep;
            OutContext.TimecodeReferenceSignature = DisplayText;
            return true;
        }
        return false;
    }
    return false;
}

bool FDigitSequencerContext::TryStepEditableNonDropTimecode(
    const FWidgetPath& Path, const FString& DisplayText, int32 CharacterIndex,
    int32 Direction, FString& OutDisplayText)
{
    OutDisplayText.Reset();
    if (Direction != -1 && Direction != 1)
    {
        return false;
    }

    for (int32 Index = KnownSequencers.Num() - 1; Index >= 0; --Index)
    {
        const TSharedPtr<ISequencer> Sequencer = KnownSequencers[Index].Pin();
        if (!Sequencer.IsValid())
        {
            KnownSequencers.RemoveAtSwap(Index);
            continue;
        }
        if (!OwnsWidgetPath(*Sequencer, Path))
        {
            continue;
        }

        const FFrameRate Tick = Sequencer->GetFocusedTickResolution();
        const FFrameRate Display = Sequencer->GetFocusedDisplayRate();
        DigitTemporalTimecode::FDigitStep Step;
        if (!DigitTemporalTimecode::TryGetNonDropFrameDigitStep(
            DisplayText, CharacterIndex, Tick, Display, Step))
        {
            return false;
        }

        for (int32 IntentIndex = 0; IntentIndex < 2; ++IntentIndex)
        {
            const auto Intent = IntentIndex == 1
                ? UE::Sequencer::ENumericIntent::Duration
                : UE::Sequencer::ENumericIntent::Position;
            const TSharedPtr<INumericTypeInterface<double>> Interface =
                Sequencer->GetNumericTypeInterface(Intent);
            if (!Interface.IsValid())
            {
                continue;
            }
            const TOptional<double> Parsed = Interface->FromString(DisplayText, 0.0);
            if (!Parsed.IsSet() || !FMath::IsFinite(Parsed.GetValue())
                || Parsed.GetValue() < static_cast<double>(MIN_int32)
                || Parsed.GetValue() > static_cast<double>(MAX_int32))
            {
                continue;
            }
            // FFrameNumber's storage is int32; never silently quantize a
            // fractional tick or use an interface which only coincidentally
            // accepts the displayed glyphs.
            const double NativeValue = Parsed.GetValue();
            if (FMath::Abs(NativeValue - std::round(NativeValue)) > 1e-7
                || Interface->ToString(NativeValue) != DisplayText)
            {
                continue;
            }
            const int64 NativeInteger = static_cast<int64>(NativeValue);
            // Guard before adding. A legal digit magnitude can exceed int32,
            // and overflowing int64 would be undefined even if rejected later.
            if ((Direction > 0
                    && Step.NativeTickStep > static_cast<int64>(MAX_int32) - NativeInteger)
                || (Direction < 0
                    && Step.NativeTickStep > NativeInteger - static_cast<int64>(MIN_int32)))
            {
                continue;
            }
            const int64 Proposed = NativeInteger
                + static_cast<int64>(Direction) * Step.NativeTickStep;
            const FString Candidate = Interface->ToString(static_cast<double>(Proposed));
            DigitTemporalTimecode::FDigitStep CandidateStep;
            if (!DigitTemporalTimecode::TryGetNonDropFrameDigitStep(
                    Candidate, CharacterIndex, Tick, Display, CandidateStep)
                || CandidateStep.NativeTickStep != Step.NativeTickStep)
            {
                continue;
            }
            const TOptional<double> Reparsed = Interface->FromString(Candidate, NativeValue);
            if (!Reparsed.IsSet() || !FMath::IsFinite(Reparsed.GetValue())
                || FMath::Abs(Reparsed.GetValue() - static_cast<double>(Proposed)) > 1e-7)
            {
                continue;
            }
            OutDisplayText = Candidate;
            return true;
        }
        // Do not try a different Sequencer or infer units when a matching
        // owner has a custom formatter that we cannot verify.
        return false;
    }
    return false;
}

bool FDigitSequencerContext::IsStillCurrent(const FDigitTemporalSpinContext& Context)
{
    const TSharedPtr<ISequencer> Sequencer = Context.Sequencer.Pin();
    if (!Sequencer.IsValid())
    {
        return false;
    }
    const FFrameRate Tick = Sequencer->GetFocusedTickResolution();
    const FFrameRate Display = Sequencer->GetFocusedDisplayRate();
    if (Tick.Numerator != Context.TickResolution.Numerator
        || Tick.Denominator != Context.TickResolution.Denominator
        || Display.Numerator != Context.DisplayRate.Numerator
        || Display.Denominator != Context.DisplayRate.Denominator)
    {
        return false;
    }
    // The user can switch Frames to Seconds without changing either rate.
    // Re-probe the selected numeric intent, not merely the displayed glyphs.
    const auto Intent = Context.bDurationIntent
        ? UE::Sequencer::ENumericIntent::Duration
        : UE::Sequencer::ENumericIntent::Position;
    const TSharedPtr<INumericTypeInterface<double>> Interface = Sequencer->GetNumericTypeInterface(Intent);
    if (!Interface.IsValid())
    {
        return false;
    }
    if (Context.bNonDropTimecode)
    {
        DigitTemporalTimecode::FDigitStep Step;
        return Interface->ToString(Context.ProbeNativeValue)
                == Context.TimecodeReferenceSignature
            && DigitTemporalTimecode::TryGetNonDropFrameDigitStep(
                Context.TimecodeReferenceSignature,
                Context.TimecodeCharacterIndex,
                Tick, Display, Step)
            && Step.NativeTickStep == Context.NativeTimecodeTickStep;
    }
    const TOptional<double> Zero = Interface->FromString(TEXT("0"), Context.ProbeNativeValue);
    const TOptional<double> One = Interface->FromString(TEXT("1"), Context.ProbeNativeValue);
    const TOptional<double> Two = Interface->FromString(TEXT("2"), Context.ProbeNativeValue);
    if (!Zero.IsSet() || !One.IsSet() || !Two.IsSet())
    {
        return false;
    }
    const double CurrentUnit = One.GetValue() - Zero.GetValue();
    const double SecondUnit = Two.GetValue() - One.GetValue();
    return FMath::IsFinite(CurrentUnit) && FMath::IsFinite(SecondUnit)
        && FMath::Abs(CurrentUnit - SecondUnit)
            <= FMath::Max(1e-7, Context.NativePerDisplayedUnit * 1e-5)
        && FMath::Abs(CurrentUnit - Context.NativePerDisplayedUnit)
            <= FMath::Max(1e-7, Context.NativePerDisplayedUnit * 1e-5)
        && Interface->ToString(Zero.GetValue()) == Context.ZeroDisplaySignature
        && Interface->ToString(One.GetValue()) == Context.OneDisplaySignature;
}
