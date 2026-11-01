// Copyright Mippithedork 2026, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include <limits>
#include "Misc/FrameRate.h"
#include "FrameNumberNumericInterface.h"
#include "DigitTemporalDisplay.h"
#include "DigitTemporalTimecode.h"
#include "DigitTemporalQuantize.h"
#include "DigitTemporalEditable.h"
#include "DigitTemporalDropFrame.h"
#include "DigitTemporalSubframe.h"
#include "DigitCurveEditableValue.h"
#include "DigitCurveTextMouse.h"
#include "DigitScrubDisplayPolicy.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/SSpinBox.h"
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


// Curve Editor text-only Time can present ordinary frame or seconds labels
// instead of bracketed timecode. Check place selection and the REAL engine
// numeric interfaces; host ownership and commit remain editor QA concerns.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDigitTemporalEditablePlainNumberTest,
    "Digit.Temporal.EditableFrameSecondsPlaces",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDigitTemporalEditablePlainNumberTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    double Step = 0.0;
    TestTrue(TEXT("Frame tens place"),
        DigitTemporalEditable::TryGetPlainNumericDigitStep(TEXT("0024"), 2, Step));
    TestEqual(TEXT("Frame tens increment"), Step, 10.0);
    TestTrue(TEXT("Seconds thousandths"),
        DigitTemporalEditable::TryGetPlainNumericDigitStep(TEXT("12.375s"), 5, Step));
    TestTrue(TEXT("Seconds thousandth increment"), FMath::IsNearlyEqual(Step, 0.001));
    TestTrue(TEXT("Negative seconds tenths"),
        DigitTemporalEditable::TryGetPlainNumericDigitStep(TEXT("-12.375"), 4, Step));
    TestTrue(TEXT("Negative seconds tenths increment"), FMath::IsNearlyEqual(Step, 0.1));
    TestFalse(TEXT("Do not parse non-drop timecode as a decimal number"),
        DigitTemporalEditable::TryGetPlainNumericDigitStep(TEXT("[00:00:01:00]"), 11, Step));
    TestFalse(TEXT("Do not parse drop-frame timecode"),
        DigitTemporalEditable::TryGetPlainNumericDigitStep(TEXT("[00:00:01;00]"), 11, Step));
    TestFalse(TEXT("Do not parse subframes"),
        DigitTemporalEditable::TryGetPlainNumericDigitStep(TEXT("0024*0.5"), 2, Step));
    TestFalse(TEXT("Do not parse thousands separators"),
        DigitTemporalEditable::TryGetPlainNumericDigitStep(TEXT("1,000"), 2, Step));
    TestFalse(TEXT("Do not parse exponential notation"),
        DigitTemporalEditable::TryGetPlainNumericDigitStep(TEXT("1e+05"), 0, Step));
    TestFalse(TEXT("Do not select decimal point"),
        DigitTemporalEditable::TryGetPlainNumericDigitStep(TEXT("12.375"), 2, Step));
    TestFalse(TEXT("Reject excessive precision"),
        DigitTemporalEditable::TryGetPlainNumericDigitStep(TEXT("1.00000000001"), 12, Step));

    const TAttribute<uint8> Padding(static_cast<uint8>(4));
    const TAttribute<FFrameRate> TickRate(FFrameRate(24000, 1));
    const TAttribute<FFrameRate> DisplayRate(FFrameRate(24, 1));
    FFrameNumberInterface Frames(
        TAttribute<EFrameNumberDisplayFormats>(EFrameNumberDisplayFormats::Frames),
        Padding, TickRate, DisplayRate);
    const FString FrameText = Frames.ToString(24000.0);
    const int32 UnitsIndex = FrameText.Len() - 1;
    TestTrue(TEXT("Native frame display exposes a decimal digit"),
        DigitTemporalEditable::TryGetPlainNumericDigitStep(FrameText, UnitsIndex, Step));
    if (Step > 0.0)
    {
        const double TargetTicks = 24000.0 + Step * 1000.0;
        const FString NextFrames = Frames.ToString(TargetTicks);
        const TOptional<double> Parsed = Frames.FromString(NextFrames, 24000.0);
        TestTrue(TEXT("Frame step round-trips through native parser"), Parsed.IsSet()
            && FMath::IsNearlyEqual(Parsed.GetValue(), TargetTicks, 1e-7));
    }

    FFrameNumberInterface Seconds(
        TAttribute<EFrameNumberDisplayFormats>(EFrameNumberDisplayFormats::Seconds),
        Padding, TickRate, DisplayRate);
    const FString SecondsText = Seconds.ToString(24000.0);
    const int32 SecondsUnitsIndex = SecondsText.Find(TEXT("1"));
    TestTrue(TEXT("Native seconds display has selectable unit digit"),
        DigitTemporalEditable::TryGetPlainNumericDigitStep(
            SecondsText, SecondsUnitsIndex, Step));
    if (Step > 0.0)
    {
        const double TargetTicks = 24000.0 + Step * 24000.0;
        const FString NextSeconds = Seconds.ToString(TargetTicks);
        const TOptional<double> Parsed = Seconds.FromString(NextSeconds, 24000.0);
        TestTrue(TEXT("Seconds step round-trips through native parser"), Parsed.IsSet()
            && FMath::IsNearlyEqual(Parsed.GetValue(), TargetTicks, 1e-7));
    }
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
    // Minute units changed from '0' in the bound attribute to '1' in the
    // staged buffer. Index 6 is a colon, not a digit; use index 5 to prove
    // we are observing the live edited value rather than stale bound text.
    Field->SelectText(FTextLocation(0, 5), FTextLocation(0, 6));
    TestEqual(TEXT("Selection reads the actual staged text buffer"),
        Field->GetSelectedText().ToString(), FString(TEXT("1")));
    const FTextSelection SavedSelection = Field->GetSelection();
    Field->SelectAllText();
    TestEqual(TEXT("Select-all exposes the entire LIVE staged buffer"),
        Field->GetSelectedText().ToString(), FString(TEXT("[00:01:00:00]")));
    Field->SelectText(SavedSelection.GetBeginning(), SavedSelection.GetEnd());
    TestEqual(TEXT("Single-digit selection survives a live-buffer read"),
        Field->GetSelectedText().ToString(), FString(TEXT("1")));
    TestEqual(TEXT("Staging must not synthesize a native property commit, including live-buffer reads"),
        Commits, 0);
    TestTrue(TEXT("A second keyboard step can stage without a host commit"),
        Field->SetEditableText(FText::FromString(TEXT("[00:01:00:01]"))));
    Field->SelectAllText();
    TestEqual(TEXT("A second staged edit reads the latest live text"),
        Field->GetSelectedText().ToString(), FString(TEXT("[00:01:00:01]")));
    TestEqual(TEXT("Neither staged step commits a property"), Commits, 0);
    return true;
}

// The Curve Editor VALUE slot can be a plain editable text field rather than
// an SSpinBox. Verify exact decimal place stepping and selection preservation
// across carries/sign changes without invoking native property commits.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDigitCurveEditableValuePlacesTest,
    "Digit.CurveEditor.EditableValuePlaces",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDigitCurveEditableValuePlacesTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    const auto Check = [this](const TCHAR* Initial, int32 Index, int32 Direction,
        const TCHAR* Expected, int32 ExpectedIndex)
    {
        FString Next;
        int32 NextIndex = INDEX_NONE;
        TestTrue(TEXT("Plain Value supports selected digit stepping"),
            DigitCurveEditableValue::TryStepSelectedPlace(
                Initial, Index, Direction, Next, NextIndex));
        TestEqual(TEXT("Exact Value step retains decimal formatting"), Next,
            FString(Expected));
        TestEqual(TEXT("Selection tracks place across sign and carry"),
            NextIndex, ExpectedIndex);
    };
    Check(TEXT("12.340"), 4, +1, TEXT("12.350"), 4);
    Check(TEXT("12.340"), 4, -1, TEXT("12.330"), 4);
    Check(TEXT("9.99"), 0, +1, TEXT("10.99"), 1);
    Check(TEXT("-0.10"), 3, +1, TEXT("0.00"), 2);
    Check(TEXT("0.00"), 2, -1, TEXT("-0.10"), 3);
    Check(TEXT("0012.30"), 1, +1, TEXT("0112.30"), 1);
    Check(TEXT("1"), 0, -1, TEXT("0"), 0);

    FString Next;
    int32 NextIndex = INDEX_NONE;
    const auto Reject = [&Next, &NextIndex](const TCHAR* Text, int32 Index)
    {
        return DigitCurveEditableValue::TryStepSelectedPlace(
            Text, Index, 1, Next, NextIndex);
    };
    TestFalse(TEXT("Timecode is not Curve Value"), Reject(TEXT("[00:01:00:00]"), 2));
    TestFalse(TEXT("Subframe indicator is not a numeric Value"), Reject(TEXT("0001*"), 2));
    TestFalse(TEXT("Scientific notation is left to host"), Reject(TEXT("1.25e-3"), 2));
    TestFalse(TEXT("Unit annotations are left to host"), Reject(TEXT("12.5 cm"), 1));
    TestFalse(TEXT("Expressions are left to host"), Reject(TEXT("2+3"), 0));
    TestFalse(TEXT("No digit selection on punctuation"), Reject(TEXT("1.25"), 1));
    TestFalse(TEXT("Never lose precision at 15-digit range"),
        Reject(TEXT("999999999999999"), 0));
    TestFalse(TEXT("Reject unsupported unbounded fractions"),
        Reject(TEXT("0.1234567890"), 2));
    TestFalse(TEXT("Never adjust empty text"), Reject(TEXT(""), 0));

    int32 Commits = 0;
    const TSharedRef<SEditableText> Field = SNew(SEditableText)
        .Text(FText::FromString(TEXT("12.340")))
        .OnTextCommitted(FOnTextCommitted::CreateLambda(
            [&Commits](const FText&, ETextCommit::Type) { ++Commits; }));
    Field->SelectText(FTextLocation(0, 4), FTextLocation(0, 5));
    FString LiveNext;
    int32 LiveIndex = INDEX_NONE;
    TestTrue(TEXT("Step value in original Slate editable buffer"),
        DigitCurveEditableValue::TryStepSelectedPlace(
            Field->GetText().ToString(), 4, 1, LiveNext, LiveIndex));
    TestTrue(TEXT("Stage instead of committing native value"),
        Field->SetEditableText(FText::FromString(LiveNext)));
    Field->SelectText(FTextLocation(0, LiveIndex), FTextLocation(0, LiveIndex + 1));
    TestEqual(TEXT("Staged numeric Value reads updated digit"),
        Field->GetSelectedText().ToString(), FString(TEXT("5")));
    TestEqual(TEXT("Staging preserves original bound Value"),
        Field->GetText().ToString(), FString(TEXT("12.340")));
    TestEqual(TEXT("No native commit before Enter/focus loss"), Commits, 0);
    const FTextSelection Saved = Field->GetSelection();
    Field->SelectAllText();
    const FString LiveBuffer = Field->GetSelectedText().ToString();
    Field->SelectText(Saved.GetBeginning(), Saved.GetEnd());
    TestTrue(TEXT("Second Value adjustment reads live staging, not stale bound data"),
        DigitCurveEditableValue::TryStepSelectedPlace(
            LiveBuffer, LiveIndex, 1, LiveNext, LiveIndex));
    TestEqual(TEXT("Repeated Value steps accumulate before commit"),
        LiveNext, FString(TEXT("12.360")));
    TestTrue(TEXT("Native text widget stages repeated Value"),
        Field->SetEditableText(FText::FromString(LiveNext)));
    TestEqual(TEXT("No fabricated per-key transaction"), Commits, 0);
    return true;
}

// Digit 2.0 text editing needs stable place selection across carries and
// predictable movement between digits. These tests use the real Slate
// selection/edit buffer. They do not pretend to test Curve Editor Undo/Redo.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDigitCurveEditorTextNavigationTest,
    "Digit.CurveEditor.TextDigitNavigation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDigitCurveEditorTextNavigationTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    const auto CheckNavigation = [this](const TCHAR* Text, int32 Index,
        int32 Direction, int32 Expected)
    {
        int32 Target = INDEX_NONE;
        TestTrue(TEXT("Navigates past punctuation to next digit"),
            DigitTemporalEditable::TryNavigateSelectedDigit(
                Text, Index, Direction, Target));
        TestEqual(TEXT("Adjacent digit index"), Target, Expected);
    };
    CheckNavigation(TEXT("12.340"), 1, +1, 3);
    CheckNavigation(TEXT("12.340"), 3, -1, 1);
    CheckNavigation(TEXT("[00:01:00;02]"), 2, +1, 4);
    CheckNavigation(TEXT("-0.250s"), 1, +1, 3);
    CheckNavigation(TEXT("0024*0.125"), 3, +1, 5);
    int32 OutIndex = INDEX_NONE;
    TestFalse(TEXT("Navigation never wraps past the last digit"),
        DigitTemporalEditable::TryNavigateSelectedDigit(TEXT("19.0"), 3, 1, OutIndex));
    TestFalse(TEXT("Navigation rejects punctuation as its origin"),
        DigitTemporalEditable::TryNavigateSelectedDigit(TEXT("19.0"), 2, 1, OutIndex));
    TestFalse(TEXT("Navigation rejects invalid direction"),
        DigitTemporalEditable::TryNavigateSelectedDigit(TEXT("19.0"), 1, 0, OutIndex));
    TestFalse(TEXT("Navigation rejects a marker without a selected digit"),
        DigitTemporalEditable::TryNavigateSelectedDigit(TEXT("0000*"), 4, -1, OutIndex));

    const auto CheckRemap = [this](const TCHAR* Source, int32 SourceIndex,
        const TCHAR* Target, int32 Expected)
    {
        int32 Remapped = INDEX_NONE;
        TestTrue(TEXT("Native text carry preserves selected place"),
            DigitTemporalEditable::TryRemapSelectedDigit(
                Source, SourceIndex, Target, Remapped));
        TestEqual(TEXT("Selected magnitude is retained"), Remapped, Expected);
    };
    CheckRemap(TEXT("9"), 0, TEXT("10"), 1);
    CheckRemap(TEXT("-9"), 1, TEXT("-10"), 2);
    CheckRemap(TEXT("-1"), 1, TEXT("0"), 0);
    CheckRemap(TEXT("0009"), 3, TEXT("0010"), 3);
    CheckRemap(TEXT("12.390s"), 4, TEXT("12.400s"), 4);
    CheckRemap(TEXT("[00:00:59;29]"), 11, TEXT("[00:01:00;02]"), 11);
    TestFalse(TEXT("Changed decimal precision cannot silently change magnitude"),
        DigitTemporalEditable::TryRemapSelectedDigit(
            TEXT("12.30"), 3, TEXT("12.3"), OutIndex));
    TestFalse(TEXT("Subframe marker cannot silently appear"),
        DigitTemporalEditable::TryRemapSelectedDigit(
            TEXT("0024"), 2, TEXT("0024*"), OutIndex));
    TestFalse(TEXT("A decimal cannot become a timecode"),
        DigitTemporalEditable::TryRemapSelectedDigit(
            TEXT("12.30"), 3, TEXT("[00:00:12:03]"), OutIndex));

    int32 Commits = 0;
    const TSharedRef<SEditableText> Field = SNew(SEditableText)
        .Text(FText::FromString(TEXT("9.99")))
        .OnTextCommitted(FOnTextCommitted::CreateLambda(
            [&Commits](const FText&, ETextCommit::Type) { ++Commits; }));
    Field->SelectText(FTextLocation(0, 0), FTextLocation(0, 1));
    FString Current = TEXT("9.99");
    int32 Index = 0;
    // Ctrl+Alt+Shift+Up applies ten verified single steps. A native
    // transaction must still be deferred until the host commits.
    for (int32 Step = 0; Step < 10; ++Step)
    {
        FString Next;
        int32 NextIndex = INDEX_NONE;
        TestTrue(TEXT("Every accelerated decimal step is representable"),
            DigitCurveEditableValue::TryStepSelectedPlace(
                Current, Index, 1, Next, NextIndex));
        Current = Next;
        Index = NextIndex;
    }
    TestEqual(TEXT("Accelerated Value step"), Current, FString(TEXT("19.99")));
    TestEqual(TEXT("Units place remains selected after carry"), Index, 1);
    TestTrue(TEXT("Stage one finished edit, not ten native commits"),
        Field->SetEditableText(FText::FromString(Current)));
    Field->SelectText(FTextLocation(0, Index), FTextLocation(0, Index + 1));
    TestEqual(TEXT("Selected units value after batch"),
        Field->GetSelectedText().ToString(), FString(TEXT("9")));
    TestEqual(TEXT("Native property commit remains deferred"), Commits, 0);
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

// Drop-frame timecode has no fixed per-component tick magnitude across
// skipped-minute boundaries. Validate the pure label stepping and the ACTUAL
// engine formatter/parser at both supported NTSC display rates.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDigitTemporalDropFrameLabelTest,
    "Digit.Temporal.DropFrameLabels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDigitTemporalDropFrameLabelTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    const auto CheckBoundary = [this](const FFrameRate& Rate,
        int32 Nominal, int32 Skipped, const FString& Label,
        const FString& ExpectedNext)
    {
        DigitTemporalDropFrame::FLabelStep Step;
        TestTrue(TEXT("DF label advances across skipped minute"),
            DigitTemporalDropFrame::TryStepLabel(Label, 11, 1, Rate, Step));
        TestEqual(TEXT("DF step skips missing labels"), Step.TargetLabel, ExpectedNext);
        TestEqual(TEXT("DF boundary advances one actual frame"),
            Step.TargetFrame - Step.SourceFrame, static_cast<int64>(1));
        DigitTemporalDropFrame::FLabelStep Reverse;
        TestTrue(TEXT("DF boundary reverses to previous visible label"),
            DigitTemporalDropFrame::TryStepLabel(ExpectedNext, 11, -1, Rate, Reverse));
        TestEqual(TEXT("DF reverse returns to source"), Reverse.TargetLabel, Label);
        TestEqual(TEXT("DF reverse moves one physical frame"),
            Reverse.TargetFrame - Reverse.SourceFrame, static_cast<int64>(-1));

        const TAttribute<uint8> Padding(static_cast<uint8>(4));
        const FFrameRate TickRate(Nominal * 1000, 1);
        const double TicksPerFrame = TickRate.AsDecimal() / Rate.AsDecimal();
        FFrameNumberInterface Native(
            TAttribute<EFrameNumberDisplayFormats>(EFrameNumberDisplayFormats::DropFrameTimecode),
            Padding, TAttribute<FFrameRate>(TickRate), TAttribute<FFrameRate>(Rate));
        const FString Before = Native.ToString(Step.SourceFrame * TicksPerFrame);
        const FString After = Native.ToString(Step.TargetFrame * TicksPerFrame);
        TestEqual(TEXT("Native UE DF source matches label mapping"), Before, Label);
        TestEqual(TEXT("Native UE DF target matches label mapping"), After, ExpectedNext);
        const TOptional<double> Parsed = Native.FromString(ExpectedNext, 0.0);
        TestTrue(TEXT("Native UE DF target parses"), Parsed.IsSet());
        if (Parsed.IsSet())
        {
            TestTrue(TEXT("Native UE DF parsed target ticks match"),
                FMath::IsNearlyEqual(Parsed.GetValue(), Step.TargetFrame * TicksPerFrame, 1e-7));
        }
        const FString Invalid = FString::Printf(TEXT("[00:01:00;%02d]"), Skipped - 1);
        TestFalse(TEXT("Cannot select skipped timecode labels"),
            DigitTemporalDropFrame::TryStepLabel(Invalid, 11, 1, Rate, Step));
    };
    CheckBoundary(FFrameRate(30000, 1001), 30, 2,
        TEXT("[00:00:59;29]"), TEXT("[00:01:00;02]"));
    CheckBoundary(FFrameRate(60000, 1001), 60, 4,
        TEXT("[00:00:59;59]"), TEXT("[00:01:00;04]"));
    DigitTemporalDropFrame::FLabelStep Step;
    TestTrue(TEXT("Ten-minute boundary does not drop labels"),
        DigitTemporalDropFrame::TryStepLabel(
            TEXT("[00:09:59;29]"), 11, 1, FFrameRate(30000, 1001), Step));
    TestEqual(TEXT("Ten-minute boundary target"), Step.TargetLabel,
        FString(TEXT("[00:10:00;00]")));
    TestFalse(TEXT("Reject drop-frame on non-NTSC rate"),
        DigitTemporalDropFrame::TryStepLabel(
            TEXT("[00:00:59;29]"), 11, 1, FFrameRate(24, 1), Step));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDigitTemporalFrameSubframeTest,
    "Digit.Temporal.SubframePlaces",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDigitTemporalFrameSubframeTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    const TAttribute<uint8> Padding(static_cast<uint8>(4));
    FFrameNumberInterface Frames(
        TAttribute<EFrameNumberDisplayFormats>(EFrameNumberDisplayFormats::Frames),
        Padding, TAttribute<FFrameRate>(FFrameRate(24000, 1)),
        TAttribute<FFrameRate>(FFrameRate(24, 1)));
    const FString NativeText = Frames.ToString(1.0);
    const FString Indicator = Frames.GetSubframeIndicator();
    const int32 Marker = NativeText.Find(Indicator);
    TestTrue(TEXT("Native fractional frame carries a subframe indicator"),
        !Indicator.IsEmpty() && Marker > 0);
    AddInfo(FString::Printf(TEXT("Native ToString(1 tick) = '%s', indicator = '%s'"),
        *NativeText, *Indicator));
    if (Marker > 0 && !Indicator.IsEmpty())
    {
        double Step = 0.0;
        const int32 SuffixStart = Marker + Indicator.Len();
        const bool bHasExplicitFraction = NativeText.Mid(SuffixStart).StartsWith(TEXT("0."));
        const TOptional<double> Parsed = Frames.FromString(NativeText, 0.0);
        const FString ParsedText = Parsed.IsSet()
            ? FString::SanitizeFloat(Parsed.GetValue()) : TEXT("unset");
        AddInfo(FString::Printf(TEXT("Native FromString(ToString(1 tick)) = %s"),
            *ParsedText));
        // UE 5.8's default frame formatter may display only an asterisk as
        // a *warning* that the value is between frames, without the actual
        // fractional digits. Such a label cannot identify any fraction
        // digit, and its FromString result need not recreate the lost tick.
        // We must never invent a 0.x place based solely on this marker.
        if (!bHasExplicitFraction)
        {
            TestFalse(TEXT("A marker-only native label exposes no editable fractional digit"),
                DigitTemporalSubframe::TryGetFrameDigitStep(NativeText, SuffixStart, Step));
        }
        else
        {
            const int32 SelectedFractionDigit = SuffixStart + 2;
            TestTrue(TEXT("An explicit native 0.x digit may be selected"),
                DigitTemporalSubframe::TryGetFrameDigitStep(
                    NativeText, SelectedFractionDigit, Step));
        }
        TestFalse(TEXT("Cannot select native subframe indicator"),
            DigitTemporalSubframe::TryGetFrameDigitStep(NativeText, Marker, Step));
        // The real edit path additionally requires exact native tick and
        // ToString/FromString round trips before it stages a change. Merely
        // parsing the marker-only display must not be treated as such proof.
        if (!Parsed.IsSet()
            || !FMath::IsNearlyEqual(Parsed.GetValue(), 1.0, 1e-7)
            || Frames.ToString(Parsed.GetValue()) != NativeText)
        {
            AddInfo(TEXT("Native subframe text does not exactly encode the source tick; "
                "Digit's owning-Sequencer round-trip gate must reject it."));
        }
    }
    double Step = 0.0;
    // The helper supports an explicit numeric fractional suffix if a host
    // actually provides one. This does not imply the default UE formatter
    // produces, parses or preserves that suffix.
    TestTrue(TEXT("Explicit 0.x fractional tenth is selectable"),
        DigitTemporalSubframe::TryGetFrameDigitStep(TEXT("0024*0.125"), 7, Step));
    TestTrue(TEXT("Explicit fractional tenth is 0.1 display frames"),
        FMath::IsNearlyEqual(Step, 0.1, 1e-9));
    TestTrue(TEXT("Explicit fractional hundredth is selectable"),
        DigitTemporalSubframe::TryGetFrameDigitStep(TEXT("0024*0.125"), 8, Step));
    TestTrue(TEXT("Explicit fractional hundredth is 0.01 display frames"),
        FMath::IsNearlyEqual(Step, 0.01, 1e-9));
    TestFalse(TEXT("Reject suffix indicator with no fractional digits"),
        DigitTemporalSubframe::TryGetFrameDigitStep(TEXT("0024*"), 5, Step));
    TestFalse(TEXT("Reject subframe glyph with ambiguous fractional suffix"),
        DigitTemporalSubframe::TryGetFrameDigitStep(TEXT("0024*3.5"), 6, Step));
    TestFalse(TEXT("Reject a plain decimal without subframe marker"),
        DigitTemporalSubframe::TryGetFrameDigitStep(TEXT("12.375"), 3, Step));
    return true;
}


// Mouse integration stages original Curve Editor text only. It must never
// fabricate an OnTextCommitted callback, and a rejected step is atomic.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDigitCurveEditorTextMouseStageTest,
    "Digit.CurveEditor.TextMouseStage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDigitCurveEditorTextMouseStageTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    double Pixels = 7.0;
    TestEqual(TEXT("Sub-step pixel motion is held"),
        DigitCurveTextMouse::ConsumeSteps(Pixels, 8.0), 0);
    TestTrue(TEXT("Sub-step remainder retained"), FMath::IsNearlyEqual(Pixels, 7.0));
    Pixels += 18.0;
    TestEqual(TEXT("Forward motion yields three steps"),
        DigitCurveTextMouse::ConsumeSteps(Pixels, 8.0), 3);
    TestTrue(TEXT("Overshoot is retained"), FMath::IsNearlyEqual(Pixels, 1.0));
    Pixels -= 17.0;
    TestEqual(TEXT("Reverse motion yields two steps"),
        DigitCurveTextMouse::ConsumeSteps(Pixels, 8.0), -2);
    TestTrue(TEXT("Negative remainder exhausted"), FMath::IsNearlyZero(Pixels));
    Pixels = 9999.0;
    TestEqual(TEXT("Large frame delta is bounded"),
        DigitCurveTextMouse::ConsumeSteps(Pixels, 8.0), 12);
    TestTrue(TEXT("Unconsumed backlog remains"), Pixels > 0.0);
    Pixels = std::numeric_limits<double>::quiet_NaN();
    TestEqual(TEXT("Nonfinite motion is rejected"),
        DigitCurveTextMouse::ConsumeSteps(Pixels, 8.0), 0);
    TestTrue(TEXT("Invalid cursor deltas are cleared"), FMath::IsNearlyZero(Pixels));

    FString Next;
    int32 NewIndex = INDEX_NONE;
    TestTrue(TEXT("Native Curve Value stage applies exact magnitude"),
        DigitCurveTextMouse::TryStageValueSteps(TEXT("12.340"), 4, 2,
            Next, NewIndex));
    TestEqual(TEXT("Two hundredth steps"), Next, FString(TEXT("12.360")));
    TestEqual(TEXT("Selected hundredth stays selected"), NewIndex, 4);
    TestTrue(TEXT("Magnitude survives carry"),
        DigitCurveTextMouse::TryStageValueSteps(TEXT("9.99"), 0, 2,
            Next, NewIndex));
    TestEqual(TEXT("Carry results"), Next, FString(TEXT("11.99")));
    TestEqual(TEXT("Carry selection"), NewIndex, 1);
    TestFalse(TEXT("Atomic rejection at upper value limit"),
        DigitCurveTextMouse::TryStageValueSteps(TEXT("999999999999998"), 14, 3,
            Next, NewIndex));
    TestEqual(TEXT("Rejected batch has no candidate text"), Next, FString());
    TestEqual(TEXT("Rejected batch has no candidate selection"), NewIndex, INDEX_NONE);
    TestFalse(TEXT("Timecode cannot be misinterpreted as Value"),
        DigitCurveTextMouse::TryStageValueSteps(TEXT("[00:01:00;00]"), 2, 1,
            Next, NewIndex));
    TestFalse(TEXT("Subframe marker cannot be misinterpreted as Value"),
        DigitCurveTextMouse::TryStageValueSteps(TEXT("0000*"), 2, 1,
            Next, NewIndex));

    int32 Commits = 0;
    const TSharedRef<SEditableText> Field = SNew(SEditableText)
        .Text(FText::FromString(TEXT("12.340")))
        .OnTextCommitted(FOnTextCommitted::CreateLambda(
            [&Commits](const FText&, ETextCommit::Type) { ++Commits; }));
    Field->SelectText(FTextLocation(0, 4), FTextLocation(0, 5));
    TestTrue(TEXT("Drag stages through native Slate buffer"),
        Field->SetEditableText(FText::FromString(TEXT("12.360"))));
    Field->SelectText(FTextLocation(0, 4), FTextLocation(0, 5));
    TestEqual(TEXT("Selected staged digit"), Field->GetSelectedText().ToString(),
        FString(TEXT("6")));
    TestEqual(TEXT("Native bound source remains unchanged before commit"),
        Field->GetText().ToString(), FString(TEXT("12.340")));
    TestEqual(TEXT("No per-pixel native commit"), Commits, 0);
    return true;
}

// Exercise reply construction and native-commit policy without opening a
// second editor transaction or synthesizing a user click in an automation run.
// Real multi-window capture and keyframe Undo/Redo require manual QA.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDigitCurveEditorMouseCapturePolicyTest,
    "Digit.CurveEditor.MouseCapturePolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDigitCurveEditorMouseCapturePolicyTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    const TSharedRef<SEditableText> Edit = SNew(SEditableText)
        .Text(FText::FromString(TEXT("12.340")));
    const FReply Capture = FReply::Handled().CaptureMouse(Edit);
    const FReply Release = FReply::Handled().ReleaseMouseCapture();
    TestTrue(TEXT("Native Slate capture reply is constructible"),
        Capture.IsEventHandled());
    TestTrue(TEXT("Native Slate release reply is constructible"),
        Release.IsEventHandled());
    TestTrue(TEXT("Capture reply targets original editable widget"),
        Capture.GetMouseCaptor().Get() == &Edit.Get());
    TestTrue(TEXT("Release reply requests pointer release"),
        Release.ShouldReleaseMouse());

    TestFalse(TEXT("Default/manual workflow never auto-commits"),
        DigitCurveTextMouse::ShouldRequestNativeCommit(false, true, true, true));
    TestFalse(TEXT("Click without staged movement cannot commit"),
        DigitCurveTextMouse::ShouldRequestNativeCommit(true, false, true, true));
    TestFalse(TEXT("Lost capture prevents auto-commit"),
        DigitCurveTextMouse::ShouldRequestNativeCommit(true, true, false, true));
    TestFalse(TEXT("Replaced source prevents auto-commit"),
        DigitCurveTextMouse::ShouldRequestNativeCommit(true, true, true, false));
    TestTrue(TEXT("Verified opt-in staged mouse-up requests native commit"),
        DigitCurveTextMouse::ShouldRequestNativeCommit(true, true, true, true));
    return true;
}

// Regression: a captured drag must not reinterpret another editor's newly
// staged text or a changed digit selection as Digit's own continuation.
// This is an engine-native test for the pure state gate, not a simulated OS
// pointer drag, and it does not create an editor transaction.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDigitCurveEditorDragBufferIsolationTest,
    "Digit.CurveEditor.DragBufferIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDigitCurveEditorDragBufferIsolationTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace DigitCurveTextMouse;
    TestTrue(TEXT("Initial source and selected index are stable"),
        IsUninterruptedEdit(TEXT("12.340"), TEXT("12.340"), TEXT(""), 4, 4));
    TestFalse(TEXT("Different selection cannot silently change scrub magnitude"),
        IsUninterruptedEdit(TEXT("12.340"), TEXT("12.340"), TEXT(""), 3, 4));
    TestTrue(TEXT("Next event reads latest Digit-staged buffer"),
        IsUninterruptedEdit(TEXT("12.360"), TEXT("12.340"), TEXT("12.360"), 4, 4));
    TestFalse(TEXT("External text mutation invalidates the gesture"),
        IsUninterruptedEdit(TEXT("88.000"), TEXT("12.340"), TEXT("12.360"), 4, 4));
    TestFalse(TEXT("Original bound value cannot replace a staged buffer"),
        IsUninterruptedEdit(TEXT("12.340"), TEXT("12.340"), TEXT("12.360"), 4, 4));
    TestTrue(TEXT("Carried number remains stable at remapped digit"),
        IsUninterruptedEdit(TEXT("10.99"), TEXT("9.99"), TEXT("10.99"), 1, 1));
    TestFalse(TEXT("Stale pre-carry index must not be accepted"),
        IsUninterruptedEdit(TEXT("10.99"), TEXT("9.99"), TEXT("10.99"), 0, 1));

    FString Next;
    int32 Index = INDEX_NONE;
    TestFalse(TEXT("Upper bound prevents positive step"),
        DigitCurveEditableValue::TryStepSelectedPlace(
            TEXT("999999999999999"), 14, 1, Next, Index));
    TestTrue(TEXT("Upper bound still permits negative step"),
        DigitCurveEditableValue::TryStepSelectedPlace(
            TEXT("999999999999999"), 14, -1, Next, Index));
    TestEqual(TEXT("Backwards preflight preserves safe value"),
        Next, FString(TEXT("999999999999998")));
    TestTrue(TEXT("Shift makes Curve Editor text drag four times faster"),
        FMath::IsNearlyEqual(GetPixelsPerStep(1.0, true), 2.0));
    TestTrue(TEXT("Standard drag remains eight pixels per step"),
        FMath::IsNearlyEqual(GetPixelsPerStep(1.0, false), 8.0));
    TestTrue(TEXT("Pixel spacing has a lower bound under high sensitivity"),
        GetPixelsPerStep(10.0, true) >= 1.0);
    return true;
}

// Probe native SSpinBox's numeric delta lifecycle without swapping property
// callbacks. Curve Editor tool options and weighted-tangent controls are only
// eligible when they use the native spinbox. This test proves temporary delta
// adjustments cannot themselves commit to a host; it does not prove every
// tangent/multikey widget's semantics.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDigitCurveEditorNativeNumericHostPolicyTest,
    "Digit.CurveEditor.NativeNumericHostPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDigitCurveEditorNativeNumericHostPolicyTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    int32 Changes = 0;
    int32 Commits = 0;
    const TSharedRef<SSpinBox<double>> Native = SNew(SSpinBox<double>)
        .Value(12.34)
        .Delta(0.25)
        .OnValueChanged_Lambda([&Changes](double) { ++Changes; })
        .OnValueCommitted_Lambda([&Commits](double, ETextCommit::Type) { ++Commits; });
    const double Original = Native->GetDelta();
    TestTrue(TEXT("Native spinbox starts with host delta"),
        FMath::IsNearlyEqual(Original, 0.25));
    Native->SetDelta(0.01);
    TestTrue(TEXT("Digit may temporarily change native decimal magnitude"),
        FMath::IsNearlyEqual(Native->GetDelta(), 0.01));
    TestEqual(TEXT("Changing delta does not invoke host change callback"), Changes, 0);
    TestEqual(TEXT("Changing delta does not invoke host commit callback"), Commits, 0);
    Native->SetDelta(Original);
    TestTrue(TEXT("Original numeric delta restored"),
        FMath::IsNearlyEqual(Native->GetDelta(), Original));
    TestTrue(TEXT("Native value remains host-owned"),
        FMath::IsNearlyEqual(Native->GetValue(), 12.34));
    return true;
}

// A native commit request must never be dispatched while Slate is still
// processing mouse-up. Verify the one-shot post-tick policy rejects changed
// focus, different source, changed selection/text, or renewed mouse capture.
// This deliberately does not claim to simulate a real Sequencer transaction.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDigitCurveEditorDeferredCommitPolicyTest,
    "Digit.CurveEditor.DeferredCommitPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDigitCurveEditorDeferredCommitPolicyTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace DigitCurveTextMouse;
    TestTrue(TEXT("Stable post-release host may receive an Enter request"),
        CanDispatchDeferredCommit(true, true, true, true, true, true));
    TestFalse(TEXT("Default manual mode never dispatches Enter"),
        CanDispatchDeferredCommit(false, true, true, true, true, true));
    TestFalse(TEXT("Focus loss invalidates pending commit"),
        CanDispatchDeferredCommit(true, false, true, true, true, true));
    TestFalse(TEXT("Widget teardown invalidates pending commit"),
        CanDispatchDeferredCommit(true, true, false, true, true, true));
    TestFalse(TEXT("Selection change invalidates pending commit"),
        CanDispatchDeferredCommit(true, true, true, false, true, true));
    TestFalse(TEXT("Host buffer replacement invalidates pending commit"),
        CanDispatchDeferredCommit(true, true, true, true, false, true));
    TestFalse(TEXT("Unreleased or foreign pointer capture invalidates pending commit"),
        CanDispatchDeferredCommit(true, true, true, true, true, false));

    // The live editable-text API is used rather than reading a stale Text
    // binding. This verifies that a staged buffer can be observed directly.
    const TSharedRef<SEditableText> Edit = SNew(SEditableText)
        .Text(FText::FromString(TEXT("12.340")));
    TestTrue(TEXT("Original host text accepts staged value"),
        Edit->SetEditableText(FText::FromString(TEXT("12.350"))));
    Edit->SelectAllText();
    TestEqual(TEXT("Native editable buffer holds staged value"),
        Edit->GetSelectedText().ToString(), FString(TEXT("12.350")));
    return true;
}

// Model a bound tangent/weight control and a neighboring control, as a
// native SSpinBox exposes them to Digit. Temporary delta adjustment must not
// replace the host's binding, overwrite another key, or trigger a callback.
// Actual Curve Editor multi-key transactions still require manual QA.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDigitCurveEditorBoundNumericIsolationTest,
    "Digit.CurveEditor.BoundNumericIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDigitCurveEditorBoundNumericIsolationTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    double HostTangentValue = 0.5;
    double AnotherKeyValue = 3.0;
    int32 Changes = 0;
    int32 Commits = 0;
    int32 HostReads = 0;
    const TSharedRef<SSpinBox<double>> BoundTangent = SNew(SSpinBox<double>)
        .Value_Lambda([&HostTangentValue, &HostReads]()
        {
            ++HostReads;
            return HostTangentValue;
        })
        .Delta(0.25)
        .OnValueChanged_Lambda([&Changes](double) { ++Changes; })
        .OnValueCommitted_Lambda([&Commits](double, ETextCommit::Type) { ++Commits; });

    // SSpinBox::GetValue() may expose its most recently synchronized value
    // until Slate updates the widget. It is not an immediate re-evaluation
    // contract for a live .Value_Lambda binding. Query the value attribute
    // itself to distinguish an intact host binding from a stale widget cache.
    const TAttribute<double> LiveValue = BoundTangent->GetValueAttribute();
    TestTrue(TEXT("Native numeric value attribute remains bound"), LiveValue.IsBound());
    const int32 ReadsBeforeFirstQuery = HostReads;
    TestTrue(TEXT("Host bound value unchanged before delta edit"),
        FMath::IsNearlyEqual(LiveValue.Get(), 0.5));
    TestTrue(TEXT("Value attribute actually evaluated host binding"),
        HostReads > ReadsBeforeFirstQuery);

    const double OriginalDelta = BoundTangent->GetDelta();
    BoundTangent->SetDelta(0.01);
    TestTrue(TEXT("Magnitude change affects only native delta"),
        FMath::IsNearlyEqual(BoundTangent->GetDelta(), 0.01));
    TestTrue(TEXT("Host binding intact after changing delta"),
        BoundTangent->GetValueAttribute().IsBound()
        && FMath::IsNearlyEqual(BoundTangent->GetValueAttribute().Get(), 0.5));
    TestTrue(TEXT("Neighboring selected key is untouched"),
        FMath::IsNearlyEqual(AnotherKeyValue, 3.0));

    HostTangentValue = 0.75; // External host update before another Slate Tick.
    const int32 ReadsBeforeHostChangeQuery = HostReads;
    TestTrue(TEXT("Live native binding reads updated host value"),
        FMath::IsNearlyEqual(BoundTangent->GetValueAttribute().Get(), 0.75));
    TestTrue(TEXT("Updated host binding was evaluated"),
        HostReads > ReadsBeforeHostChangeQuery);
    TestEqual(TEXT("No synthetic value-change callback"), Changes, 0);
    TestEqual(TEXT("No synthetic commit callback"), Commits, 0);

    BoundTangent->SetDelta(OriginalDelta);
    TestTrue(TEXT("Native delta restored"),
        FMath::IsNearlyEqual(BoundTangent->GetDelta(), OriginalDelta));
    TestTrue(TEXT("Native host binding preserved after restoration"),
        BoundTangent->GetValueAttribute().IsBound()
        && FMath::IsNearlyEqual(BoundTangent->GetValueAttribute().Get(), 0.75));
    TestEqual(TEXT("Restoring delta does not trigger value changes"), Changes, 0);
    TestEqual(TEXT("Restoring delta does not trigger commits"), Commits, 0);
    AddInfo(FString::Printf(
        TEXT("Live bound value=%.3f, unsynchronized widget GetValue=%.3f, host binding evaluations=%d"),
        BoundTangent->GetValueAttribute().Get(), BoundTangent->GetValue(), HostReads));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS



// Native Curve Editor SSpinBox path, distinct from text-only Ctrl+Alt staging.
// A finer intended step can expand the displayed fractional width without
// interrupting the original captured native scrub transaction.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDigitNativeScrubPrecisionContinuityTest,
    "Digit.CurveEditor.NativeScrubPrecisionContinuity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDigitNativeScrubPrecisionContinuityTest::RunTest(const FString& Parameters)
{
    (void)Parameters;
    using namespace DigitScrubDisplayPolicy;
    TestEqual(TEXT("Native Ctrl+Alt has no hidden 0.01x slowdown"),
        NativeModifierMultiplier(false, true, true), 1.0);
    TestEqual(TEXT("Shift+Ctrl+Alt retains Shift's coarse multiplier"),
        NativeModifierMultiplier(true, true, true), 10.0);
    TestEqual(TEXT("Ctrl alone retains its 0.1x precision modifier"),
        NativeModifierMultiplier(false, true, false), 0.1);
    TestEqual(TEXT("Shift+Alt retains its coarse modifier"),
        NativeModifierMultiplier(true, false, true), 100.0);
    TestEqual(TEXT("Unmodified native scrub retains normal magnitude"),
        NativeModifierMultiplier(false, false, false), 1.0);

    const int32 Base = 2;
    const int32 Max = 9;
    TestEqual(TEXT("Two-decimal movement does not authorize extra decimals"),
        MaximumSelfGeneratedFractionDigits(Base, Max, 0.01), 2);
    const int32 Fine = MaximumSelfGeneratedFractionDigits(Base, Max, 0.0001);
    TestEqual(TEXT("A 0.0001 step authorizes four decimals"), Fine, 4);
    TestTrue(TEXT("12.34 becoming 12.3401 does not suspend after fine scrub"),
        IsExpectedFractionalWidth(Base, 4, Fine, true));
    TestFalse(TEXT("Unsolicited precision increase suspends before movement"),
        IsExpectedFractionalWidth(Base, 4, Fine, false));
    TestFalse(TEXT("Unexpected further precision expansion suspends"),
        IsExpectedFractionalWidth(Base, 5, Fine, true));
    TestFalse(TEXT("Loss of original decimals still suspends"),
        IsExpectedFractionalWidth(Base, 1, Fine, true));
    TestTrue(TEXT("Initial precision remains valid without movement"),
        IsExpectedFractionalWidth(Base, 2, Base, false));
    TestEqual(TEXT("Finer ladder rungs remain bounded by formatter precision"),
        MaximumSelfGeneratedFractionDigits(Base, 5, 1.e-9), 5);
    return true;
}
