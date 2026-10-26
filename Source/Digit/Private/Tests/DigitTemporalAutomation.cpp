// Copyright Mippithedork 2026, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/FrameRate.h"
#include "FrameNumberNumericInterface.h"
#include "DigitTemporalDisplay.h"
#include "DigitTemporalTimecode.h"
#include "DigitTemporalQuantize.h"
#include "DigitTemporalEditable.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/NumericTypeInterface.h"

// Exercise Unreal's REAL time conversion interface in the editor build. A
// source-contract test cannot prove the active frame-rate implementation.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDigitTemporalNativeInterfaceTest,
    "Digit.Temporal.NativeFrameInterface",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDigitTemporalNativeInterfaceTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    const TAttribute<uint8> Padding(static_cast<uint8>(4));
    const TAttribute<FFrameRate> TickRate(FFrameRate(24000, 1));
    const TAttribute<FFrameRate> DisplayRate(FFrameRate(24, 1));

    FFrameNumberInterface Frames(
        TAttribute<EFrameNumberDisplayFormats>(EFrameNumberDisplayFormats::Frames),
        Padding, TickRate, DisplayRate);
    const FString FrameText = Frames.ToString(24000.0);
    TestEqual(TEXT("One second is 24 visible frames"), FrameText, FString(TEXT("0024")));
    const TOptional<double> ParsedFrames = Frames.FromString(TEXT("24"), 24000.0);
    TestTrue(TEXT("Frame input parses through the native numeric interface"), ParsedFrames.IsSet());
    if (ParsedFrames.IsSet())
    {
        TestTrue(TEXT("Frame conversion returns 24000 native ticks"),
            FMath::Abs(ParsedFrames.GetValue() - 24000.0) <= 1.0);
    }

    const FString SubframeText = Frames.ToString(1.0);
    TestTrue(TEXT("A subframe uses the native indicator"), SubframeText.Contains(TEXT("*")));
    TestTrue(TEXT("Digit rejects subframe text as decimal"),
        DigitTemporalDisplay::IsStructuredTimeDisplay(SubframeText));
    TestFalse(TEXT("Plain frame digits are not timecode punctuation"),
        DigitTemporalDisplay::IsStructuredTimeDisplay(FrameText));

    FFrameNumberInterface Seconds(
        TAttribute<EFrameNumberDisplayFormats>(EFrameNumberDisplayFormats::Seconds),
        Padding, TickRate, DisplayRate);
    const FString SecondsText = Seconds.ToString(24000.0);
    const TOptional<double> ParsedSeconds = Seconds.FromString(SecondsText, 24000.0);
    TestTrue(TEXT("Native seconds text parses at its owning tick rate"), ParsedSeconds.IsSet());
    if (ParsedSeconds.IsSet())
    {
        TestTrue(TEXT("One displayed second maps back to 24000 ticks"),
            FMath::Abs(ParsedSeconds.GetValue() - 24000.0) <= 1.0);
    }
    TestFalse(TEXT("A plain decimal second needs rate information, not a timecode rejection"),
        DigitTemporalDisplay::IsStructuredTimeDisplay(SecondsText));

    FFrameNumberInterface Timecode(
        TAttribute<EFrameNumberDisplayFormats>(EFrameNumberDisplayFormats::NonDropFrameTimecode),
        Padding, TickRate, DisplayRate);
    const FString TimecodeText = Timecode.ToString(24000.0);
    TestTrue(TEXT("Native timecode is bracketed"),
        TimecodeText.Contains(TEXT("[")) && TimecodeText.Contains(TEXT("]")));
    TestTrue(TEXT("Digit rejects native timecode as decimal"),
        DigitTemporalDisplay::IsStructuredTimeDisplay(TimecodeText));
    const TOptional<double> ParsedTimecode = Timecode.FromString(TimecodeText, 24000.0);
    TestTrue(TEXT("Native timecode round-trip parses"), ParsedTimecode.IsSet());
    if (ParsedTimecode.IsSet())
    {
        TestTrue(TEXT("Native timecode round-trip preserves one second"),
            FMath::Abs(ParsedTimecode.GetValue() - 24000.0) <= 1.0);
    }

    FFrameNumberInterface DropFrameTimecode(
        TAttribute<EFrameNumberDisplayFormats>(EFrameNumberDisplayFormats::DropFrameTimecode),
        Padding,
        TAttribute<FFrameRate>(FFrameRate(30000, 1)),
        TAttribute<FFrameRate>(FFrameRate(30000, 1001)));
    const FString DropFrameText = DropFrameTimecode.ToString(30000.0);
    TestTrue(TEXT("Drop-frame timecode is structured text"),
        DigitTemporalDisplay::IsStructuredTimeDisplay(DropFrameText));
    TestTrue(TEXT("Drop-frame timecode retains a timecode display"),
        DropFrameText.Contains(TEXT("[")) && DropFrameText.Contains(TEXT("]")));

    // At non-integer frame rates, one displayed frame is not necessarily an
    // integer number of ticks in other host configurations. Never assume an
    // implicit delta of one from the glyph alone.
    const FFrameTime FractionalRateFrame = FFrameRate::TransformTime(
        FFrameTime(FFrameNumber(1)), FFrameRate(30000, 1001), FFrameRate(24000, 1));
    TestTrue(TEXT("Fractional display rate requires conversion to native ticks"),
        FractionalRateFrame.AsDecimal() > 1.0);

    return true;
}

// Validate the same FromString(1)-FromString(0) probe used by Digit's
// Sequencer widget adapter. Importantly this works even when the field value
// is ZERO, where a display/native value ratio is undefined.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDigitTemporalHostRateBridgeTest,
    "Digit.Temporal.SequencerRateBridge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDigitTemporalHostRateBridgeTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    const TAttribute<uint8> Padding(static_cast<uint8>(4));

    const auto CheckUnits = [this, Padding](FFrameRate Tick, FFrameRate Display,
        EFrameNumberDisplayFormats Format, double ExpectedNativeUnit,
        const FString& Label)
    {
        FFrameNumberInterface Interface(
            TAttribute<EFrameNumberDisplayFormats>(Format),
            Padding,
            TAttribute<FFrameRate>(Tick),
            TAttribute<FFrameRate>(Display));
        const TOptional<double> Zero = Interface.FromString(TEXT("0"), 0.0);
        const TOptional<double> One = Interface.FromString(TEXT("1"), 0.0);
        const TOptional<double> Two = Interface.FromString(TEXT("2"), 0.0);
        TestTrue(*FString::Printf(TEXT("%s parses zero, one, and two"), *Label),
            Zero.IsSet() && One.IsSet() && Two.IsSet());
        if (Zero.IsSet() && One.IsSet() && Two.IsSet())
        {
            const double NativeStep = One.GetValue() - Zero.GetValue();
            const double SecondNativeStep = Two.GetValue() - One.GetValue();
            TestTrue(*FString::Printf(TEXT("%s native unit mapping"), *Label),
                FMath::IsNearlyEqual(NativeStep, ExpectedNativeUnit, 0.01));
            TestTrue(*FString::Printf(TEXT("%s linear adjacent frame increments"), *Label),
                FMath::IsNearlyEqual(NativeStep, SecondNativeStep, 0.01));
        }
        TestFalse(*FString::Printf(TEXT("%s starts from unstructured zero"), *Label),
            DigitTemporalDisplay::IsStructuredTimeDisplay(Interface.ToString(0.0)));
    };

    CheckUnits(FFrameRate(24000, 1), FFrameRate(24, 1),
        EFrameNumberDisplayFormats::Frames, 1000.0, TEXT("24fps frames"));
    CheckUnits(FFrameRate(24000, 1), FFrameRate(30, 1),
        EFrameNumberDisplayFormats::Frames, 800.0, TEXT("30fps frames"));
    CheckUnits(FFrameRate(30000, 1), FFrameRate(30000, 1001),
        EFrameNumberDisplayFormats::Frames, 1001.0, TEXT("29.97fps frames"));
    CheckUnits(FFrameRate(24000, 1), FFrameRate(24, 1),
        EFrameNumberDisplayFormats::Seconds, 24000.0, TEXT("seconds"));
    return true;
}


// A rate can have the same frame and second numerical scale (one display
// frame per second), so checking only FromString("1")-FromString("0") is not
// enough to notice the user switching formatting during a scrub.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDigitTemporalFormatSignatureTest,
    "Digit.Temporal.SequencerFormatSignature",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDigitTemporalFormatSignatureTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    const TAttribute<uint8> Padding(static_cast<uint8>(4));
    const TAttribute<FFrameRate> TickRate(FFrameRate(24000, 1));
    const TAttribute<FFrameRate> DisplayRate(FFrameRate(1, 1));
    FFrameNumberInterface Frames(
        TAttribute<EFrameNumberDisplayFormats>(EFrameNumberDisplayFormats::Frames),
        Padding, TickRate, DisplayRate);
    FFrameNumberInterface Seconds(
        TAttribute<EFrameNumberDisplayFormats>(EFrameNumberDisplayFormats::Seconds),
        Padding, TickRate, DisplayRate);

    const TOptional<double> FramesZero = Frames.FromString(TEXT("0"), 0.0);
    const TOptional<double> FramesOne = Frames.FromString(TEXT("1"), 0.0);
    const TOptional<double> SecondsZero = Seconds.FromString(TEXT("0"), 0.0);
    const TOptional<double> SecondsOne = Seconds.FromString(TEXT("1"), 0.0);
    TestTrue(TEXT("Both one-fps unit probes parse"),
        FramesZero.IsSet() && FramesOne.IsSet() && SecondsZero.IsSet() && SecondsOne.IsSet());
    if (FramesZero.IsSet() && FramesOne.IsSet() && SecondsZero.IsSet() && SecondsOne.IsSet())
    {
        const double FrameUnit = FramesOne.GetValue() - FramesZero.GetValue();
        const double SecondUnit = SecondsOne.GetValue() - SecondsZero.GetValue();
        TestTrue(TEXT("One-fps frames and seconds have the same native scale"),
            FMath::IsNearlyEqual(FrameUnit, SecondUnit, 0.01));
        // The current bridge also compares these rendered signatures. If a
        // future UE build renders the formats identically, this assertion
        // will tell us that text alone cannot detect the mode change.
        const bool bDifferentlyFormatted =
            Frames.ToString(FramesZero.GetValue()) != Seconds.ToString(SecondsZero.GetValue())
            || Frames.ToString(FramesOne.GetValue()) != Seconds.ToString(SecondsOne.GetValue());
        TestTrue(TEXT("One-fps frame and seconds formats have distinct signatures"),
            bDifferentlyFormatted);
    }
    return true;
}


// The first native test for Digit's *component-aware* non-drop-frame
// timecode magnitude resolver. This deliberately does not simulate mouse
// events: Slate text editors still need a transaction-preserving adapter.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDigitTemporalTimecodeDigitStepTest,
    "Digit.Temporal.TimecodeDigitSteps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDigitTemporalTimecodeDigitStepTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    const TAttribute<uint8> Padding(static_cast<uint8>(4));

    const auto CheckStep = [this, Padding](FFrameRate Tick, FFrameRate Display,
        double NativeStart, int32 CharacterIndex, int64 ExpectedNativeStep,
        const FString& Label)
    {
        FFrameNumberInterface Interface(
            TAttribute<EFrameNumberDisplayFormats>(EFrameNumberDisplayFormats::NonDropFrameTimecode),
            Padding,
            TAttribute<FFrameRate>(Tick),
            TAttribute<FFrameRate>(Display));
        const FString NativeText = Interface.ToString(NativeStart);
        DigitTemporalTimecode::FDigitStep Step;
        const bool bValid = DigitTemporalTimecode::TryGetNonDropFrameDigitStep(
            NativeText, CharacterIndex, Tick, Display, Step);
        TestTrue(*FString::Printf(TEXT("%s recognizes selected timecode digit"), *Label), bValid);
        if (bValid)
        {
            TestEqual(*FString::Printf(TEXT("%s returns correct native tick step"), *Label),
                Step.NativeTickStep, ExpectedNativeStep);
            const FString Advanced = Interface.ToString(NativeStart + Step.NativeTickStep);
            TestTrue(*FString::Printf(TEXT("%s produces another native timecode"), *Label),
                Advanced.StartsWith(TEXT("[")) && Advanced.EndsWith(TEXT("]")));
        }
    };

    // [00:00:59:23] + one displayed frame becomes [00:01:00:00].
    FFrameNumberInterface TwentyFour(
        TAttribute<EFrameNumberDisplayFormats>(EFrameNumberDisplayFormats::NonDropFrameTimecode),
        Padding,
        TAttribute<FFrameRate>(FFrameRate(24000, 1)),
        TAttribute<FFrameRate>(FFrameRate(24, 1)));
    const double BeforeMinute = (59.0 * 24.0 + 23.0) * 1000.0;
    const FString BeforeText = TwentyFour.ToString(BeforeMinute);
    DigitTemporalTimecode::FDigitStep FrameStep;
    const bool bFrameStep = DigitTemporalTimecode::TryGetNonDropFrameDigitStep(
        BeforeText, 11, FFrameRate(24000, 1), FFrameRate(24, 1), FrameStep);
    TestTrue(TEXT("24fps final frame identifies a frame-units digit"), bFrameStep);
    if (bFrameStep)
    {
        TestEqual(TEXT("24fps frame-units step is 1000 native ticks"),
            FrameStep.NativeTickStep, int64(1000));
        TestEqual(TEXT("24fps frame step carries into next minute"),
            TwentyFour.ToString(BeforeMinute + FrameStep.NativeTickStep),
            FString(TEXT("[00:01:00:00]")));
    }

    CheckStep(FFrameRate(24000, 1), FFrameRate(24, 1), 0.0,
        10, 10000, TEXT("24fps frame tens"));
    CheckStep(FFrameRate(24000, 1), FFrameRate(24, 1), 0.0,
        8, 24000, TEXT("24fps seconds units"));
    CheckStep(FFrameRate(24000, 1), FFrameRate(24, 1), 0.0,
        7, 240000, TEXT("24fps seconds tens"));
    CheckStep(FFrameRate(24000, 1), FFrameRate(24, 1), 0.0,
        5, 1440000, TEXT("24fps minutes units"));
    CheckStep(FFrameRate(24000, 1), FFrameRate(24, 1), 0.0,
        2, 86400000, TEXT("24fps hours units"));
    CheckStep(FFrameRate(30000, 1), FFrameRate(30000, 1001), 0.0,
        11, 1001, TEXT("29.97fps non-drop frame units"));

    // Drop-frame is intentionally excluded, because minute magnitudes are
    // not affine across dropped timecode labels.
    DigitTemporalTimecode::FDigitStep Rejected;
    TestFalse(TEXT("Drop-frame timecode cannot use a non-drop digit step"),
        DigitTemporalTimecode::TryGetNonDropFrameDigitStep(
            TEXT("[00:01:00;02]"), 11,
            FFrameRate(30000, 1), FFrameRate(30000, 1001), Rejected));
    TestFalse(TEXT("Subframe annotations must retain native editing"),
        DigitTemporalTimecode::TryGetNonDropFrameDigitStep(
            TEXT("[00:00:00:01*]"), 11,
            FFrameRate(24000, 1), FFrameRate(24, 1), Rejected));
    TestFalse(TEXT("Timecode punctuation is not an editable digit"),
        DigitTemporalTimecode::TryGetNonDropFrameDigitStep(
            TEXT("[00:00:00:00]"), 9,
            FFrameRate(24000, 1), FFrameRate(24, 1), Rejected));
    TestFalse(TEXT("Frame rate combinations with fractional ticks per frame are not silently rounded"),
        DigitTemporalTimecode::TryGetNonDropFrameDigitStep(
            TEXT("[00:00:00:00]"), 11,
            FFrameRate(24000, 1), FFrameRate(30000, 1001), Rejected));
    return true;
}


// Tests the quantization used by a timecode-capable Sequencer SSpinBox.
// Actual pointer capture, native key commits, and Undo/Redo require manual
// editor testing; this automation only covers tick math + UE formatting.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDigitTemporalTimecodeSpinQuantizationTest,
    "Digit.Temporal.TimecodeSpinQuantization",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDigitTemporalTimecodeSpinQuantizationTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    const TAttribute<uint8> Padding(static_cast<uint8>(4));
    const FFrameRate Tick(24000, 1);
    const FFrameRate Display(24, 1);
    FFrameNumberInterface Native(
        TAttribute<EFrameNumberDisplayFormats>(EFrameNumberDisplayFormats::NonDropFrameTimecode),
        Padding, TAttribute<FFrameRate>(Tick), TAttribute<FFrameRate>(Display));

    const double LastFrameOfMinute = (59.0 * 24.0 + 23.0) * 1000.0;
    const FString Original = Native.ToString(LastFrameOfMinute);
    TestEqual(TEXT("Starts at the last frame of minute zero"),
        Original, FString(TEXT("[00:00:59:23]")));
    DigitTemporalTimecode::FDigitStep Step;
    const bool bRecognized = DigitTemporalTimecode::TryGetNonDropFrameDigitStep(
        Original, 11, Tick, Display, Step);
    TestTrue(TEXT("Native frame-units digit resolves"), bRecognized);
    if (bRecognized)
    {
        std::int64_t Offset = 0;
        TestTrue(TEXT("Partial physical motion remains on the native frame"),
            DigitTemporalQuantize::TryWholeSelectedSteps(0.75 * Step.NativeTickStep,
                Step.NativeTickStep, Offset));
        TestEqual(TEXT("Partial frame motion does not introduce subframes"), Offset, int64(0));
        TestTrue(TEXT("Full-frame motion quantizes to the selected frame"),
            DigitTemporalQuantize::TryWholeSelectedSteps(1.0 * Step.NativeTickStep,
                Step.NativeTickStep, Offset));
        TestEqual(TEXT("One frame is 1000 ticks"), Offset, int64(1000));
        TestEqual(TEXT("Native formatting carries through minute rollover"),
            Native.ToString(LastFrameOfMinute + static_cast<double>(Offset)),
            FString(TEXT("[00:01:00:00]")));
        TestTrue(TEXT("Reverse movement is symmetric"),
            DigitTemporalQuantize::TryWholeSelectedSteps(-1.0 * Step.NativeTickStep,
                Step.NativeTickStep, Offset));
        TestEqual(TEXT("Reverse selected frame step"), Offset, int64(-1000));
    }

    const FFrameRate FractionalTick(30000, 1);
    const FFrameRate FractionalDisplay(30000, 1001);
    FFrameNumberInterface FractionalNative(
        TAttribute<EFrameNumberDisplayFormats>(EFrameNumberDisplayFormats::NonDropFrameTimecode),
        Padding, TAttribute<FFrameRate>(FractionalTick),
        TAttribute<FFrameRate>(FractionalDisplay));
    DigitTemporalTimecode::FDigitStep FractionalStep;
    const FString FractionalText = FractionalNative.ToString(0.0);
    const bool bFractional = DigitTemporalTimecode::TryGetNonDropFrameDigitStep(
        FractionalText, 11, FractionalTick, FractionalDisplay, FractionalStep);
    TestTrue(TEXT("Fractional-rate timecode resolves through UE formatter"), bFractional);
    if (bFractional)
    {
        std::int64_t Offset = 0;
        TestEqual(TEXT("29.97 non-drop native frame tick count"),
            FractionalStep.NativeTickStep, int64(1001));
        TestTrue(TEXT("Two fractional-rate frame steps quantize exactly"),
            DigitTemporalQuantize::TryWholeSelectedSteps(2002.0,
                FractionalStep.NativeTickStep, Offset));
        TestEqual(TEXT("Exact quantized tick offset"), Offset, int64(2002));
        TestEqual(TEXT("UE native formatter shows two frames"),
            FractionalNative.ToString(static_cast<double>(Offset)),
            FString(TEXT("[00:00:00:02]")));
    }
    std::int64_t RejectedOffset = 0;
    TestFalse(TEXT("Non-finite pointer movement must not write a timecode"),
        DigitTemporalQuantize::TryWholeSelectedSteps(
            std::numeric_limits<double>::infinity(), 1000, RejectedOffset));
    return true;
}


// Test the *actual* Slate editable-text staging API and Digit's explicit
// one-digit selection requirement. There is no synthetic property commit:
// Unreal's owning editor must perform its ordinary Enter/focus-loss callback.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDigitTemporalEditableTextStageTest,
    "Digit.Temporal.EditableTextStage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDigitTemporalEditableTextStageTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    const FString Initial(TEXT("[00:00:59:23]"));
    int32 SelectedDigit = INDEX_NONE;
    const FTextSelection FrameUnits(FTextLocation(0, 11), FTextLocation(0, 12));
    TestTrue(TEXT("A single frame digit is eligible"),
        DigitTemporalEditable::TryGetSingleSelectedDigit(
            FrameUnits, Initial, SelectedDigit));
    TestEqual(TEXT("Selected frame index"), SelectedDigit, 11);
    TestFalse(TEXT("Punctuation cannot be selected for adjustment"),
        DigitTemporalEditable::TryGetSingleSelectedDigit(
            FTextSelection(FTextLocation(0, 9), FTextLocation(0, 10)),
            Initial, SelectedDigit));
    TestFalse(TEXT("A multi-digit selection cannot be adjusted"),
        DigitTemporalEditable::TryGetSingleSelectedDigit(
            FTextSelection(FTextLocation(0, 10), FTextLocation(0, 12)),
            Initial, SelectedDigit));
    TestFalse(TEXT("An empty selection cannot be adjusted"),
        DigitTemporalEditable::TryGetSingleSelectedDigit(
            FTextSelection(FTextLocation(0, 11), FTextLocation(0, 11)),
            Initial, SelectedDigit));

    int32 Commits = 0;
    const TSharedRef<SEditableText> Field = SNew(SEditableText)
        .Text(FText::FromString(Initial))
        .OnTextCommitted(FOnTextCommitted::CreateLambda(
            [&Commits](const FText&, ETextCommit::Type) { ++Commits; }));
    TestTrue(TEXT("Slate stages a new value in the edit buffer"),
        Field->SetEditableText(FText::FromString(TEXT("[00:01:00:00]"))));
    // UE 5.8.3 SEditableText::GetText exposes the BOUND attribute,
    // NOT the uncommitted edited buffer. Previously this assertion was
    // inverted, hiding the stale-source repeated-key hazard.
    TestEqual(TEXT("Staging must not replace the bound source attribute"),
        Field->GetText().ToString(), Initial);
    Field->SelectText(FTextLocation(0, 6), FTextLocation(0, 7));
    TestEqual(TEXT("Selection reads the actual staged text buffer"),
        Field->GetSelectedText().ToString(), FString(TEXT("0")));
    TestEqual(TEXT("Staging must not synthesize a native property commit"),
        Commits, 0);
    return true;
}

// The native numeric formatter must preserve an in-flight decimal position:
// carrying 1.234569 -> 1.234570 must not delete the rightmost zero before
// Digit restores the host's ordinary minimum on mouse release.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDigitRotatorDragDecimalWidthTest,
    "Digit.Numeric.RotatorDragDecimalWidth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDigitRotatorDragDecimalWidthTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    TDefaultNumericTypeInterface<double> Formatter;
    Formatter.SetMaxFractionalDigits(TAttribute<TOptional<int32>>(TOptional<int32>(6)));
    Formatter.SetMinFractionalDigits(TAttribute<TOptional<int32>>(TOptional<int32>(1)));
    const FString Ordinary = Formatter.ToString(1.234570);

    Formatter.SetMinFractionalDigits(TAttribute<TOptional<int32>>(TOptional<int32>(6)));
    const FString PinnedCarry = Formatter.ToString(1.234570);
    TestTrue(TEXT("Held fractional width keeps the carried zero"),
        PinnedCarry.EndsWith(TEXT("0")) && PinnedCarry.Len() == Ordinary.Len() + 1);
    TestFalse(TEXT("Pinned rotation display does not force scientific text"),
        PinnedCarry.Contains(TEXT("e+"), ESearchCase::IgnoreCase)
        || PinnedCarry.Contains(TEXT("...")));

    Formatter.SetMinFractionalDigits(TAttribute<TOptional<int32>>(TOptional<int32>(1)));
    TestEqual(TEXT("Native display returns after release"), Formatter.ToString(1.234570), Ordinary);
    return true;
}


// Verify generic floating-point spin box formatters independently of Rotators:
// decimal carry must retain the selected rightmost zero until release, and
// each formatter's native minimum must be restored afterward.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDigitFloatingDragDecimalWidthTest,
    "Digit.Numeric.FloatingDragDecimalWidth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDigitFloatingDragDecimalWidthTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    TDefaultNumericTypeInterface<double> DoubleFormatter;
    DoubleFormatter.SetMaxFractionalDigits(TAttribute<TOptional<int32>>(TOptional<int32>(6)));
    DoubleFormatter.SetMinFractionalDigits(TAttribute<TOptional<int32>>(TOptional<int32>(1)));
    const FString DoubleOrdinary = DoubleFormatter.ToString(125.340);
    DoubleFormatter.SetMinFractionalDigits(TAttribute<TOptional<int32>>(TOptional<int32>(3)));
    const FString DoubleHeld = DoubleFormatter.ToString(125.340);
    TestTrue(TEXT("Double Location retains the carried trailing zero during capture"),
        DoubleHeld.EndsWith(TEXT("340")) && DoubleHeld.Len() == DoubleOrdinary.Len() + 1);
    DoubleFormatter.SetMinFractionalDigits(TAttribute<TOptional<int32>>(TOptional<int32>(1)));
    TestEqual(TEXT("Double Location returns to native formatting"),
        DoubleFormatter.ToString(125.340), DoubleOrdinary);

    TDefaultNumericTypeInterface<float> FloatFormatter;
    FloatFormatter.SetMaxFractionalDigits(TAttribute<TOptional<int32>>(TOptional<int32>(6)));
    FloatFormatter.SetMinFractionalDigits(TAttribute<TOptional<int32>>(TOptional<int32>(1)));
    const FString FloatOrdinary = FloatFormatter.ToString(1.230f);
    FloatFormatter.SetMinFractionalDigits(TAttribute<TOptional<int32>>(TOptional<int32>(3)));
    const FString FloatHeld = FloatFormatter.ToString(1.230f);
    TestTrue(TEXT("Float Scale retains the carried trailing zero during capture"),
        FloatHeld.EndsWith(TEXT("230")) && FloatHeld.Len() == FloatOrdinary.Len() + 1);
    FloatFormatter.SetMinFractionalDigits(TAttribute<TOptional<int32>>(TOptional<int32>(1)));
    TestEqual(TEXT("Float Scale returns to native formatting"),
        FloatFormatter.ToString(1.230f), FloatOrdinary);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS


