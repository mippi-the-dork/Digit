// Copyright Mippithedork 2026, Inc. All Rights Reserved.

#include "DigitInputProcessor.h"
#include "DigitSettings.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "DigitTemporalDisplay.h"
#include "DigitTemporalTimecode.h"
#include "DigitTemporalQuantize.h"
#include "DigitTemporalEditable.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Layout/Children.h"
#include "Widgets/SWidget.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "Internationalization/FastDecimalFormat.h"
#include "Math/UnitConversion.h"
#include "Widgets/Input/NumericTypeInterface.h"
#include "Layout/Visibility.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SEditableText.h"
#include "Framework/Text/TextLayout.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#include <limits>
#include <cmath>
#include <type_traits>

DEFINE_LOG_CATEGORY_STATIC(LogDigit, Log, All);

namespace DigitPrivate
{
    // Match Origin's default Parent / Deparent hierarchy feedback colors so
    // direction communication stays consistent across the Facet tools.
    static const FLinearColor IncreaseOutlineColor(0.0f, 0.162029f, 0.745404f, 0.700000f);
    static const FLinearColor DecreaseOutlineColor(0.745404f, 0.228181f, 0.0f, 0.700000f);

    // Follow the EXACT rules used by UE 5.8's numeric entry interface.
    // TDefaultNumericTypeInterface formats and parses through ExpressionParser,
    // whose localized rules can differ from the FInternationalization locale
    // selected for non-numeric text. This matters for Region & Language options.
    static const FDecimalNumberFormattingRules& GetNumberRules()
    {
        return ExpressionParser::GetLocalizedNumberFormattingRules();
    }

    static bool IsDigitCharacter(TCHAR Character)
    {
        if (Character >= TEXT('0') && Character <= TEXT('9'))
        {
            return true;
        }
        const FDecimalNumberFormattingRules& Rules = GetNumberRules();
        for (int32 Index = 0; Index < 10; ++Index)
        {
            if (Character == Rules.DigitCharacters[Index])
            {
                return true;
            }
        }
        return false;
    }

    static bool IsNumberPunctuation(TCHAR Character)
    {
        const FDecimalNumberFormattingRules& Rules = GetNumberRules();
        return Character == TEXT('.') || Character == TEXT(',')
            || Character == Rules.DecimalSeparatorCharacter
            || Character == Rules.GroupingSeparatorCharacter
            || Character == TEXT('+') || Character == TEXT('-')
            || Character == static_cast<TCHAR>(0x2212); // U+2212 minus
    }

    static bool IsNumericTokenCharacter(const FString& Text, int32 Index)
    {
        if (!Text.IsValidIndex(Index))
        {
            return false;
        }
        const TCHAR C = Text[Index];
        if (IsDigitCharacter(C) || IsNumberPunctuation(C))
        {
            return true;
        }
        // Some locales group with a regular, no-break or narrow no-break space.
        // Admit whitespace ONLY between digits so unit labels do not become
        // part of the numeric token ("1 234,50 cm").
        const bool bGroupingSpace = C == TEXT(' ')
            || C == static_cast<TCHAR>(0x00A0)
            || C == static_cast<TCHAR>(0x202F);
        return bGroupingSpace
            && Text.IsValidIndex(Index - 1)
            && Text.IsValidIndex(Index + 1)
            && IsDigitCharacter(Text[Index - 1])
            && IsDigitCharacter(Text[Index + 1]);
    }

    struct FNumericToken
    {
        int32 Start = INDEX_NONE;
        int32 End = INDEX_NONE;
        int32 Radix = INDEX_NONE;
    };

    static bool LocateNumericToken(const FString& Text, int32 DigitIndex, FNumericToken& OutToken)
    {
        if (!Text.IsValidIndex(DigitIndex) || !IsDigitCharacter(Text[DigitIndex]))
        {
            return false;
        }

        int32 Start = DigitIndex;
        while (Start > 0 && IsNumericTokenCharacter(Text, Start - 1))
        {
            --Start;
        }
        int32 End = DigitIndex;
        while (End + 1 < Text.Len() && IsNumericTokenCharacter(Text, End + 1))
        {
            ++End;
        }

        // Avoid assigning nonsensical place values to scientific notation.
        // e.g. the 04 in "1.25e-04" is an exponent, not a hundredths field.
        if ((Start >= 1 && (Text[Start - 1] == TEXT('e') || Text[Start - 1] == TEXT('E')))
            || (End + 1 < Text.Len() && (Text[End + 1] == TEXT('e') || Text[End + 1] == TEXT('E'))))
        {
            return false;
        }

        const FDecimalNumberFormattingRules& Rules = GetNumberRules();
        int32 LastDot = INDEX_NONE;
        int32 LastComma = INDEX_NONE;
        int32 LastLocaleRadix = INDEX_NONE;
        int32 OccurrencesOfLocaleGrouping = 0;
        for (int32 Index = Start; Index <= End; ++Index)
        {
            const TCHAR C = Text[Index];
            if (C == TEXT('.')) { LastDot = Index; }
            if (C == TEXT(',')) { LastComma = Index; }
            if (C == Rules.DecimalSeparatorCharacter) { LastLocaleRadix = Index; }
            if (C == Rules.GroupingSeparatorCharacter) { ++OccurrencesOfLocaleGrouping; }
        }

        int32 Radix = INDEX_NONE;
        if (LastDot != INDEX_NONE && LastComma != INDEX_NONE)
        {
            // Both styles appear: the final punctuation is conventionally the
            // decimal separator, independent of the editor's active locale.
            Radix = FMath::Max(LastDot, LastComma);
        }
        else if (LastLocaleRadix != INDEX_NONE)
        {
            Radix = LastLocaleRadix;
        }
        else
        {
            const int32 Candidate = FMath::Max(LastDot, LastComma);
            if (Candidate != INDEX_NONE)
            {
                const TCHAR Separator = Text[Candidate];
                if (Separator != Rules.GroupingSeparatorCharacter)
                {
                    // A custom numeric type interface may format with an
                    // invariant decimal point even in a comma-decimal locale.
                    Radix = Candidate;
                }
                else if (OccurrencesOfLocaleGrouping == 1)
                {
                    int32 FractionCount = 0;
                    for (int32 Index = Candidate + 1; Index <= End; ++Index)
                    {
                        if (IsDigitCharacter(Text[Index])) { ++FractionCount; }
                    }
                    // Ambiguous "1.234" follows the locale's grouping convention.
                    // "1.5" does not and is most likely a custom decimal value.
                    if (FractionCount != 3 && FractionCount > 0)
                    {
                        Radix = Candidate;
                    }
                }
            }
        }

        OutToken.Start = Start;
        OutToken.End = End;
        OutToken.Radix = Radix;
        return true;
    }

    // FFrameNumberInterface can render a single numeric SSpinBox as frames,
    // seconds, or timecode. A timecode's fields are NOT decimal place values;
    // e.g. the final 12 in [01:02:03:12] represents frames, not hundredths.
    // The subframe marker (*) is also not an ordinary numeric suffix. Never
    // arm Digit on structured time text until the host's frame-rate/format
    // conversion is available through a verified public interface.
    static bool HasStructuredTimeDisplay(const FString& Text)
    {
        return DigitTemporalDisplay::IsStructuredTimeDisplay(Text);
    }

    // Some editor panels replace an ordinary numeric label with a relative
    // edit annotation (Control Rig: "Multiple Values: + 1.000"), an empty
    // placeholder, or a non-decimal representation after the drag begins.
    // These do not name an absolute decimal digit of the source property.
    // Check both at hover/capture and on every active movement. In particular,
    // merely comparing the unit suffix is NOT enough: both "1.00" and
    // "Multiple Values: + 1.00" have an empty unit suffix.
    static bool HasCanonicalNumericPrefix(const FString& Text)
    {
        int32 FirstDigit = INDEX_NONE;
        for (int32 Index = 0; Index < Text.Len(); ++Index)
        {
            if (IsDigitCharacter(Text[Index]))
            {
                FirstDigit = Index;
                break;
            }
        }

        FNumericToken Token;
        if (FirstDigit == INDEX_NONE || !LocateNumericToken(Text, FirstDigit, Token))
        {
            return false;
        }

        // Ordinary numeric labels can start with optional Unicode direction
        // marks/whitespace and then a sign. LocateNumericToken absorbs the sign.
        // Arbitrary text preceding the token is an annotation, not a value.
        for (int32 Index = 0; Index < Token.Start; ++Index)
        {
            const TCHAR Prefix = Text[Index];
            const bool bHarmlessPrefix = Prefix == TEXT(' ') || Prefix == TEXT('\t')
                || Prefix == static_cast<TCHAR>(0x00A0) // no-break space
                || Prefix == static_cast<TCHAR>(0x202F) // narrow no-break space
                || Prefix == static_cast<TCHAR>(0x200E) // left-to-right mark
                || Prefix == static_cast<TCHAR>(0x200F) // right-to-left mark
                || Prefix == static_cast<TCHAR>(0x061C); // Arabic letter mark
            if (!bHarmlessPrefix)
            {
                return false;
            }
        }
        return true;
    }

    static int32 GetDisplayedFractionalDigitCount(const FString& Text)
    {
        for (int32 Index = 0; Index < Text.Len(); ++Index)
        {
            if (IsDigitCharacter(Text[Index]))
            {
                FNumericToken Token;
                if (!LocateNumericToken(Text, Index, Token))
                {
                    return INDEX_NONE;
                }
                int32 Count = 0;
                if (Token.Radix != INDEX_NONE)
                {
                    for (int32 Fraction = Token.Radix + 1; Fraction <= Token.End; ++Fraction)
                    {
                        if (IsDigitCharacter(Text[Fraction]))
                        {
                            ++Count;
                        }
                    }
                }
                return Count;
            }
        }
        return INDEX_NONE;
    }

    // SSpinBox may display a unit-converted number, e.g. 1.25 m for an
    // underlying 125 cm. The selected place is in DISPLAY units, not in the
    // widget's underlying NumericType. Only read the first numeric token.
    static bool ParseDisplayedNumber(const FString& Text, double& OutValue)
    {
        int32 FirstDigit = INDEX_NONE;
        for (int32 Index = 0; Index < Text.Len(); ++Index)
        {
            if (IsDigitCharacter(Text[Index]))
            {
                FirstDigit = Index;
                break;
            }
        }
        FNumericToken Token;
        if (FirstDigit == INDEX_NONE || !LocateNumericToken(Text, FirstDigit, Token))
        {
            return false;
        }
        const FDecimalNumberFormattingRules& Rules = GetNumberRules();
        FString Normalized;
        bool bHasDigit = false;
        for (int32 Index = Token.Start; Index <= Token.End; ++Index)
        {
            const TCHAR C = Text[Index];
            if (C >= TEXT('0') && C <= TEXT('9'))
            {
                Normalized.AppendChar(C);
                bHasDigit = true;
                continue;
            }
            bool bConverted = false;
            for (int32 Digit = 0; Digit < 10; ++Digit)
            {
                if (C == Rules.DigitCharacters[Digit])
                {
                    Normalized.AppendChar(static_cast<TCHAR>(TEXT('0') + Digit));
                    bHasDigit = true;
                    bConverted = true;
                    break;
                }
            }
            if (bConverted)
            {
                continue;
            }
            if (Index == Token.Radix)
            {
                Normalized.AppendChar(TEXT('.'));
            }
            else if (C == TEXT('-') || C == static_cast<TCHAR>(0x2212))
            {
                if (Normalized.IsEmpty()) { Normalized.AppendChar(TEXT('-')); }
            }
            else if (C == TEXT('+'))
            {
                if (Normalized.IsEmpty()) { Normalized.AppendChar(TEXT('+')); }
            }
            // All remaining separators are grouping, including Unicode spaces.
        }
        if (!bHasDigit)
        {
            return false;
        }
        OutValue = FCString::Atod(*Normalized);
        return FMath::IsFinite(OutValue);
    }

    static bool IsDisplayedZero(const FString& Text)
    {
        double Value = 0.0;
        return ParseDisplayedNumber(Text, Value)
            && Value == 0.0
            && !HasStructuredTimeDisplay(Text);
    }

    // SPropertyEditorNumeric can change its fixed display unit when the
    // property crosses an automatic unit threshold. Isolate the suffix so
    // the digit selected at mouse-down is never silently reinterpreted in
    // another unit mid-drag (e.g. cm changing to m).
    static FString GetNumericUnitSuffix(const FString& Text)
    {
        for (int32 Index = 0; Index < Text.Len(); ++Index)
        {
            if (IsDigitCharacter(Text[Index]))
            {
                FNumericToken Token;
                if (LocateNumericToken(Text, Index, Token))
                {
                    FString Suffix = Text.Mid(Token.End + 1);
                    Suffix.TrimStartAndEndInline();
                    return Suffix;
                }
                break;
            }
        }
        return FString();
    }

    // Sequencer's generic SValueStepper accepts a custom numeric type
    // interface. If there is no owning Sequencer, we can only fall back to
    // Digit's normal scalar path when the visible value is demonstrably the
    // native value, NOT merely its rounded representation. Otherwise an
    // unverified time conversion could produce a wrong step at nonzero values.
    static bool IsUnconvertedNativeDisplay(const FString& Text, double NativeValue)
    {
        double DisplayValue = 0.0;
        if (!FMath::IsFinite(NativeValue)
            || !HasCanonicalNumericPrefix(Text)
            || !GetNumericUnitSuffix(Text).IsEmpty()
            || !ParseDisplayedNumber(Text, DisplayValue))
        {
            return false;
        }
        // A unit suffix belongs to a different converter: do not treat it as
        // proof that the generic Sequencer stepper uses the native value.
        // Ordinary digits must be a native-value identity even before taking
        // rounding into account. Zero remains ambiguous and is rejected by
        // the caller unless the host interface has been verified.
        const double Tolerance = std::numeric_limits<double>::epsilon()
            * FMath::Max(1.0, FMath::Abs(NativeValue)) * 4.0;
        return FMath::Abs(NativeValue - DisplayValue) <= Tolerance;
    }

    // SSpinBox does not expose INumericTypeInterface or the source property's
    // Units/ForceUnits metadata. Resolve a visible unit only if the displayed
    // suffix is recognized by Unreal and the actual engine conversion produces
    // the displayed rounded number from exactly ONE numeric scale.
    //
    // A native/display value ratio is not a unit conversion. It fails for
    // temperatures with offsets, for zero, and for rounded fields. Instead,
    // test the complete set of UE EUnit source candidates that are compatible
    // with the displayed unit. A candidate's native-units-per-display-unit
    // slope is calculated from Convert(0) and Convert(1); that works for
    // affine conversions such as Fahrenheit to Celsius too.
    //
    // The underlying unit remains private to the owning editor. If several
    // candidate sources fit the display but require different step sizes,
    // we must NOT invent a mapping. Native editing remains available.
    static TOptional<double> ResolveDisplayToNativeScale(
        double NativeValue, const FString& Text,
        bool bKnownNativeDegrees = false, bool bKnownNiagaraNativeUnits = false)
    {
        double DisplayValue = 0.0;
        if (HasStructuredTimeDisplay(Text)
            || !FMath::IsFinite(NativeValue)
            || !ParseDisplayedNumber(Text, DisplayValue))
        {
            return TOptional<double>();
        }

        // The display must represent a scalar absolute value, never an
        // editor-specific relative-value annotation or a placeholder.
        if (!HasCanonicalNumericPrefix(Text))
        {
            return TOptional<double>();
        }

        int32 FirstDigit = INDEX_NONE;
        for (int32 Index = 0; Index < Text.Len(); ++Index)
        {
            if (IsDigitCharacter(Text[Index]))
            {
                FirstDigit = Index;
                break;
            }
        }
        FNumericToken Token;
        if (FirstDigit == INDEX_NONE || !LocateNumericToken(Text, FirstDigit, Token))
        {
            return TOptional<double>();
        }

        const FString UnitSuffix = GetNumericUnitSuffix(Text);
        int32 FractionalDigits = 0;
        if (Token.Radix != INDEX_NONE)
        {
            for (int32 Index = Token.Radix + 1; Index <= Token.End; ++Index)
            {
                if (IsDigitCharacter(Text[Index]))
                {
                    ++FractionalDigits;
                }
            }
        }
        // The visible precision determines how far its rounded value can
        // legitimately differ from the numeric value held by the spin box.
        const double DisplayTolerance = 0.500001 * FMath::Pow(
            10.0, -static_cast<double>(FMath::Min(FractionalDigits, 12)));

        if (UnitSuffix.IsEmpty())
        {
            // The ordinary numeric interface displays the native value in
            // the same units. However, some specialized editors use a custom
            // OnGetDisplayValue to show a transformed number WITHOUT a suffix
            // (frames, percentages, etc.). Those must not be scrubbed using
            // an assumed 1:1 mapping. Ordinary display rounding is fine.
            return FMath::Abs(NativeValue - DisplayValue) <= DisplayTolerance
                ? TOptional<double>(1.0) : TOptional<double>();
        }

        const TOptional<EUnit> DisplayUnit = FUnitConversion::UnitFromString(*UnitSuffix);
        // A rotator component's stored numeric value is already in degrees.
        // For an explicit degree symbol/name, that native unit is known from
        // the SNumericRotatorInputBox ancestry. Do not try to infer it from
        // the displayed number: both 0 radians and 0 degrees display as 0,
        // which otherwise makes every zero-valued rotator fail eligibility.
        // This exception is NEVER applied to unrelated angle-valued fields.
        const bool bExplicitDegrees =
            (DisplayUnit.IsSet() && DisplayUnit.GetValue() == EUnit::Degrees)
            || UnitSuffix == FUnitConversion::GetUnitDisplayString(EUnit::Degrees);
        if ((!DisplayUnit.IsSet() || DisplayUnit.GetValue() == EUnit::Unspecified)
            && !(bKnownNativeDegrees && bExplicitDegrees))
        {
            // Includes custom/unrecognized unit abbreviations. No guessing.
            return TOptional<double>();
        }

        // UE 5.8.3 Niagara's *standard* float, integer, and vector parameter
        // editors create TNumericUnitTypeInterface with DisplayUnit as the
        // underlying unit. They do not select another FixedDisplayUnits or
        // UserDisplayUnits. Thus a displayed "0.0 cm" is safely native cm,
        // despite the generic unit inference being ambiguous at zero.
        // This rule is only enabled for confirmed Niagara parameter widgets.
        if (bKnownNiagaraNativeUnits)
        {
            return FMath::Abs(NativeValue - DisplayValue) <= DisplayTolerance
                ? TOptional<double>(1.0) : TOptional<double>();
        }

        if (bKnownNativeDegrees)
        {
            // The number must actually display the stored degree value,
            // allowing only the rounding implied by the visible decimals.
            // A custom rotator displaying radians is not a 1:1 degree field.
            return bExplicitDegrees
                && FMath::Abs(NativeValue - DisplayValue) <= DisplayTolerance
                ? TOptional<double>(1.0) : TOptional<double>();
        }

        TOptional<double> UniqueScale;
        // EUnit is documented as a contiguous, zero-indexed enum. Do not
        // include EUnit::Unspecified in candidate conversions.
        for (int32 Index = 0; Index < static_cast<int32>(EUnit::Unspecified); ++Index)
        {
            const EUnit SourceUnit = static_cast<EUnit>(Index);
            if (!FUnitConversion::AreUnitsCompatible(SourceUnit, DisplayUnit.GetValue()))
            {
                continue;
            }

            const double ConvertedDisplay = FUnitConversion::Convert(
                NativeValue, SourceUnit, DisplayUnit.GetValue());
            // Coarsely rounded displays may accidentally match an unrelated
            // source unit within half a displayed digit. Require close relative
            // agreement too; displayed zero requires near-exact agreement.
            const double AbsoluteError = FMath::Abs(ConvertedDisplay - DisplayValue);
            const double RelativeConfidence = FMath::Max(
                1.e-8, FMath::Abs(DisplayValue) * 0.075);
            if (!FMath::IsFinite(ConvertedDisplay)
                || AbsoluteError > DisplayTolerance
                || AbsoluteError > RelativeConfidence)
            {
                continue;
            }

            const double AtZero = FUnitConversion::Convert(
                0.0, SourceUnit, DisplayUnit.GetValue());
            const double AtOne = FUnitConversion::Convert(
                1.0, SourceUnit, DisplayUnit.GetValue());
            const double DisplayUnitsPerNative = AtOne - AtZero;
            if (!FMath::IsFinite(DisplayUnitsPerNative) || DisplayUnitsPerNative <= 0.0)
            {
                continue;
            }
            const double Scale = 1.0 / DisplayUnitsPerNative;
            if (!FMath::IsFinite(Scale) || Scale <= 0.0)
            {
                continue;
            }

            if (UniqueScale.IsSet())
            {
                // Equivalent aliases can have the same scale. Different
                // native units fitting a heavily rounded display are unsafe.
                const double RelativeDifference = FMath::Abs(
                    Scale / UniqueScale.GetValue() - 1.0);
                if (RelativeDifference > 1.e-7)
                {
                    return TOptional<double>();
                }
            }
            else
            {
                UniqueScale = Scale;
            }
        }
        return UniqueScale;
    }

    template<typename NumericType>
    static bool CanSafelyScrubInteger(NumericType Value)
    {
        if constexpr (TIsIntegral<NumericType>::Value)
        {
            // Digit accumulates movement as double. Both signs of a wide
            // integer must be exactly representable or the first drag could
            // silently overwrite low bits. Check the original integer before
            // casting it to double, including UINT64_MAX and INT64_MIN.
            constexpr long double MaxExactInteger = 9007199254740991.0L;
            const long double ExactValue = static_cast<long double>(Value);
            return ExactValue >= -MaxExactInteger && ExactValue <= MaxExactInteger;
        }
        else
        {
            return true;
        }
    }

    // The spacing near 0.001f is ~1e-10, not epsilon(float) * 1.0f.
    // Using the real adjacent representable values avoids incorrectly
    // disabling precision for tiny material and animation parameters.
    template<typename NumericType>
    static double GetNumericResolution(double NativeValue)
    {
        if constexpr (std::is_floating_point_v<NumericType>)
        {
            if (!FMath::IsFinite(NativeValue))
            {
                return std::numeric_limits<double>::infinity();
            }
            const NumericType Native = static_cast<NumericType>(NativeValue);
            const NumericType Above = std::nextafter(Native, std::numeric_limits<NumericType>::infinity());
            const NumericType Below = std::nextafter(Native, -std::numeric_limits<NumericType>::infinity());
            const double UpStep = static_cast<double>(Above) - static_cast<double>(Native);
            const double DownStep = static_cast<double>(Native) - static_cast<double>(Below);
            return FMath::Min(UpStep, DownStep);
        }
        else
        {
            return 1.0;
        }
    }

    // Unreal's nearly-integer numeric indicator displays a visible ellipsis
    // whenever a rotator stores floating-point dust close to a whole degree.
    // A decimal scrub starting on the visible grid must not manufacture such
    // hidden fractions through cumulative floating-point addition.
    static double StabilizeRotatorWholeDegree(double WrappedDegrees, bool bCleanInitialDisplay)
    {
        if (!bCleanInitialDisplay || !FMath::IsFinite(WrappedDegrees))
        {
            return WrappedDegrees;
        }

        const double NearestDegree = std::round(WrappedDegrees);
        // This is an arithmetic-error tolerance, NOT a rounding of the
        // selected digit or the original source's meaningful subdegrees.
        // Use double precision here even for float-backed angle properties.
        const double DustTolerance = 8.0 * std::numeric_limits<double>::epsilon()
            * FMath::Max(1.0, FMath::Abs(WrappedDegrees));
        return FMath::Abs(WrappedDegrees - NearestDegree) <= DustTolerance
            ? NearestDegree : WrappedDegrees;
    }

    // IEEE-754 arithmetic can create tiny off-grid fractions between degrees,
    // not just at whole degrees. Only remove double-rounding noise if the
    // starting rotation was exactly on the visible decimal grid. Never erase
    // meaningful subdegree precision that predates the drag.
    static double StabilizeRotatorDecimalGrid(double WrappedDegrees,
        bool bCleanInitialDisplay, double DisplayGrid)
    {
        if (!bCleanInitialDisplay || !FMath::IsFinite(WrappedDegrees)
            || !FMath::IsFinite(DisplayGrid) || DisplayGrid <= 0.0)
        {
            return WrappedDegrees;
        }
        const double NearestGridValue = std::round(WrappedDegrees / DisplayGrid) * DisplayGrid;
        const double DustTolerance = 16.0 * std::numeric_limits<double>::epsilon()
            * FMath::Max(1.0, FMath::Abs(WrappedDegrees));
        return FMath::Abs(WrappedDegrees - NearestGridValue) <= DustTolerance
            ? NearestGridValue : WrappedDegrees;
    }

    static FString LocalizeStepText(FString Text)
    {
        const FDecimalNumberFormattingRules& Rules = GetNumberRules();
        for (int32 Index = 0; Index < Text.Len(); ++Index)
        {
            const TCHAR C = Text[Index];
            if (C == TEXT('.'))
            {
                Text[Index] = Rules.DecimalSeparatorCharacter;
            }
            else if (C >= TEXT('0') && C <= TEXT('9'))
            {
                Text[Index] = Rules.DigitCharacters[C - TEXT('0')];
            }
        }
        return Text;
    }

    static double GetDefaultInputEventMultiplier(const FInputEvent& InputEvent)
    {
        if (InputEvent.IsShiftDown())
        {
            return InputEvent.IsAltDown() ? 100.0 : 10.0;
        }

        if (InputEvent.IsControlDown())
        {
            return InputEvent.IsAltDown() ? 0.01 : 0.1;
        }

        return 1.0;
    }

    static double ApplySliderExponent(double Fraction, double SliderExponent)
    {
        Fraction = FMath::Clamp(Fraction, 0.0, 1.0);
        if (FMath::IsNearlyEqual(SliderExponent, 1.0))
        {
            return Fraction;
        }

        return 1.0 - FMath::Pow(1.0 - Fraction, SliderExponent);
    }

    static bool FindCharacterVisualBounds(
        const TSharedRef<FSlateFontMeasure>& FontMeasure,
        const FString& DisplayString,
        const FSlateFontInfo& Font,
        int32 CharacterIndex,
        float& OutStartX,
        float& OutWidth)
    {
        if (!DisplayString.IsValidIndex(CharacterIndex))
        {
            return false;
        }

        // Use explicit prefix lengths rather than the ranged Measure overload.
        // The ranged API's character-index semantics do not line up with the insertion-style
        // index returned by FindCharacterIndexAtOffset for this use case, which produced a
        // consistent one-character mismatch between the highlighted glyph and DigitPlace.
        // Prefix measurement gives us unambiguous visual boundaries: [0, Index) and
        // [0, Index + 1).
        const FString PrefixBefore = DisplayString.Left(CharacterIndex);
        const FString PrefixThrough = DisplayString.Left(CharacterIndex + 1);

        const float StartX = CharacterIndex > 0
            ? FontMeasure->Measure(FStringView(PrefixBefore), Font).X
            : 0.0f;
        const float EndX = FontMeasure->Measure(FStringView(PrefixThrough), Font).X;

        if (!FMath::IsFinite(StartX) || !FMath::IsFinite(EndX) || EndX <= StartX)
        {
            return false;
        }

        OutStartX = StartX;
        OutWidth = FMath::Max(1.0f, EndX - StartX);
        return true;
    }

    static int32 FindCharacterAtVisualOffset(
        const TSharedRef<FSlateFontMeasure>& FontMeasure,
        const FString& DisplayString,
        const FSlateFontInfo& Font,
        float LocalX)
    {
        if (DisplayString.IsEmpty() || LocalX < 0.0f)
        {
            return INDEX_NONE;
        }

        for (int32 CharacterIndex = 0; CharacterIndex < DisplayString.Len(); ++CharacterIndex)
        {
            float CharacterStartX = 0.0f;
            float CharacterWidth = 0.0f;
            if (!FindCharacterVisualBounds(FontMeasure, DisplayString, Font, CharacterIndex, CharacterStartX, CharacterWidth))
            {
                continue;
            }

            const float CharacterEndX = CharacterStartX + CharacterWidth;
            const bool bIsLastCharacter = CharacterIndex == DisplayString.Len() - 1;
            if (LocalX >= CharacterStartX && (LocalX < CharacterEndX || (bIsLastCharacter && LocalX <= CharacterEndX)))
            {
                return CharacterIndex;
            }
        }

        return INDEX_NONE;
    }

    static FString FormatIncrementForTooltip(int32 DigitPlace)
    {
        FString Result;
        if (DigitPlace >= 0)
        {
            Result = TEXT("1");
            for (int32 Index = 0; Index < DigitPlace; ++Index)
            {
                Result += TEXT("0");
            }
            Result += TEXT(".0");
            return Result;
        }

        Result = TEXT("0.");
        for (int32 Index = 1; Index < -DigitPlace; ++Index)
        {
            Result += TEXT("0");
        }
        Result += TEXT("1");
        return Result;
    }

    static FText MakeDigitToolTip(const FString& CurrentValue, int32 DigitPlace)
    {
        const FString BaseIncrement = LocalizeStepText(FormatIncrementForTooltip(DigitPlace));
        const FString ShiftIncrement = LocalizeStepText(FormatIncrementForTooltip(DigitPlace + 1));
        const FString CtrlIncrement = LocalizeStepText(FormatIncrementForTooltip(DigitPlace - 1));

        return FText::FromString(FString::Printf(
            TEXT("%s\nIncrement by %s\nShift: %s    Ctrl: %s\nAlt: Value Ladder"),
            *CurrentValue,
            *BaseIncrement,
            *ShiftIncrement,
            *CtrlIncrement));
    }


    static FString FormatMagnitudeForLadder(int32 DigitPlace)
    {
        if (DigitPlace > 6 || DigitPlace < -6)
        {
            return LocalizeStepText(FString::Printf(TEXT("1e%+d"), DigitPlace));
        }

        if (DigitPlace >= 0)
        {
            FString Result(TEXT("1"));
            for (int32 Index = 0; Index < DigitPlace; ++Index)
            {
                Result += TEXT("0");
            }
            return LocalizeStepText(Result);
        }

        FString Result(TEXT("0."));
        for (int32 Index = 1; Index < -DigitPlace; ++Index)
        {
            Result += TEXT("0");
        }
        Result += TEXT("1");
        return LocalizeStepText(Result);
    }

    template<typename NumericType>
    bool MakeStepForPlace(int32 DigitPlace, NumericType& OutStep)
    {
        if constexpr (TIsIntegral<NumericType>::Value)
        {
            if (DigitPlace < 0)
            {
                return false;
            }
        }

        const double StepAsDouble = FMath::Pow(10.0, static_cast<double>(DigitPlace));
        if (!FMath::IsFinite(StepAsDouble) || StepAsDouble <= 0.0)
        {
            return false;
        }

        if constexpr (TIsIntegral<NumericType>::Value)
        {
            const long double StepAsLongDouble = static_cast<long double>(StepAsDouble);
            const long double MaxValue = static_cast<long double>(std::numeric_limits<NumericType>::max());
            if (StepAsLongDouble > MaxValue)
            {
                return false;
            }
        }

        OutStep = static_cast<NumericType>(StepAsDouble);
        return OutStep > NumericType(0);
    }

    template<typename NumericType>
    const FSlateWidgetClassData& GetRuntimeSpinBoxClass()
    {
        static const TSharedRef<SSpinBox<NumericType>> ProbeSpinBox =
            SNew(SSpinBox<NumericType>);

        return ProbeSpinBox->GetWidgetClass();
    }

    template<typename NumericType>
    bool IsWidgetClass(const TSharedRef<SWidget>& Widget)
    {
        // Sequencer's SNonThrottledSpinBox<T> inherits SSpinBox<T>
        // without declaring a new Slate widget class. Its inherited class
        // identity matches this probe, so callbacks stay on the native widget.
        return &Widget->GetWidgetClass() == &GetRuntimeSpinBoxClass<NumericType>();
    }

    template<typename NumericType>
    bool IsScrubbableSpinBox(const TSharedRef<SWidget>& Widget)
    {
        if (!IsWidgetClass<NumericType>(Widget))
        {
            return false;
        }
        const TSharedRef<SSpinBox<NumericType>> SpinBox = StaticCastSharedRef<SSpinBox<NumericType>>(Widget);
        return SpinBox->IsEnabled() && SpinBox->GetEnableSlider();
    }

    template<typename NumericType>
    bool CanRepresentDisplayedMagnitude(
        const TSharedRef<SWidget>& Widget, const FString& DisplayString,
        bool bKnownNativeDegrees, bool bKnownNiagaraNativeUnits)
    {
        if (!IsWidgetClass<NumericType>(Widget))
        {
            return false;
        }
        const TSharedRef<SSpinBox<NumericType>> SpinBox = StaticCastSharedRef<SSpinBox<NumericType>>(Widget);
        return CanSafelyScrubInteger(SpinBox->GetValue())
            && ResolveDisplayToNativeScale(
                static_cast<double>(SpinBox->GetValue()), DisplayString,
                bKnownNativeDegrees, bKnownNiagaraNativeUnits).IsSet();
    }

    static bool CanRepresentDisplayedMagnitude(
        const TSharedRef<SWidget>& Widget, const FString& DisplayString,
        bool bKnownNativeDegrees, bool bKnownNiagaraNativeUnits)
    {
        return CanRepresentDisplayedMagnitude<double>(Widget, DisplayString, bKnownNativeDegrees, bKnownNiagaraNativeUnits)
            || CanRepresentDisplayedMagnitude<float>(Widget, DisplayString, bKnownNativeDegrees, bKnownNiagaraNativeUnits)
            || CanRepresentDisplayedMagnitude<uint64>(Widget, DisplayString, bKnownNativeDegrees, bKnownNiagaraNativeUnits)
            || CanRepresentDisplayedMagnitude<uint32>(Widget, DisplayString, bKnownNativeDegrees, bKnownNiagaraNativeUnits)
            || CanRepresentDisplayedMagnitude<uint16>(Widget, DisplayString, bKnownNativeDegrees, bKnownNiagaraNativeUnits)
            || CanRepresentDisplayedMagnitude<uint8>(Widget, DisplayString, bKnownNativeDegrees, bKnownNiagaraNativeUnits)
            || CanRepresentDisplayedMagnitude<int64>(Widget, DisplayString, bKnownNativeDegrees, bKnownNiagaraNativeUnits)
            || CanRepresentDisplayedMagnitude<int32>(Widget, DisplayString, bKnownNativeDegrees, bKnownNiagaraNativeUnits)
            || CanRepresentDisplayedMagnitude<int16>(Widget, DisplayString, bKnownNativeDegrees, bKnownNiagaraNativeUnits)
            || CanRepresentDisplayedMagnitude<int8>(Widget, DisplayString, bKnownNativeDegrees, bKnownNiagaraNativeUnits);
    }

    template<typename NumericType>
    bool CanScrubDisplayedPlace(const TSharedRef<SWidget>& Widget, int32 DigitPlace)
    {
        if (!IsWidgetClass<NumericType>(Widget))
        {
            return false;
        }
        NumericType Step = NumericType(0);
        if (!MakeStepForPlace<NumericType>(DigitPlace, Step))
        {
            return false;
        }
        return CanSafelyScrubInteger(Step);
    }

    static bool CanScrubDisplayedPlace(const TSharedRef<SWidget>& Widget, int32 DigitPlace)
    {
        return CanScrubDisplayedPlace<double>(Widget, DigitPlace)
            || CanScrubDisplayedPlace<float>(Widget, DigitPlace)
            || CanScrubDisplayedPlace<uint64>(Widget, DigitPlace)
            || CanScrubDisplayedPlace<uint32>(Widget, DigitPlace)
            || CanScrubDisplayedPlace<uint16>(Widget, DigitPlace)
            || CanScrubDisplayedPlace<uint8>(Widget, DigitPlace)
            || CanScrubDisplayedPlace<int64>(Widget, DigitPlace)
            || CanScrubDisplayedPlace<int32>(Widget, DigitPlace)
            || CanScrubDisplayedPlace<int16>(Widget, DigitPlace)
            || CanScrubDisplayedPlace<int8>(Widget, DigitPlace);
    }

    // UE 5.8's SSpinBox<T> owns the displayed value as the first slot in its
    // SHorizontalBox content. GetDisplayValue() is PROTECTED in UE 5.8, so a
    // plugin must not call it. Instead, inspect the native value-label widget
    // through Slate's public child hierarchy. This also distinguishes the
    // editable value from unrelated informational text nearby.
    static TSharedPtr<STextBlock> GetNativeSpinValueLabel(const TSharedRef<SWidget>& SpinWidget)
    {
        FChildren* RootChildren = SpinWidget->GetChildren();
        if (!RootChildren || RootChildren->Num() != 1)
        {
            return nullptr;
        }

        const TSharedRef<SWidget> LayoutWidget = RootChildren->GetChildAt(0);
        if (LayoutWidget->GetType() != FName(TEXT("SHorizontalBox")))
        {
            return nullptr;
        }

        FChildren* LayoutChildren = LayoutWidget->GetChildren();
        if (!LayoutChildren || LayoutChildren->Num() < 2)
        {
            return nullptr;
        }

        const TSharedRef<SWidget> ValueWidget = LayoutChildren->GetChildAt(0);
        if (ValueWidget->GetType() != FName(TEXT("STextBlock")))
        {
            return nullptr;
        }

        const TSharedRef<STextBlock> ValueText = StaticCastSharedRef<STextBlock>(ValueWidget);
        const EVisibility Visibility = ValueText->GetVisibility();
        if (Visibility == EVisibility::Hidden || Visibility == EVisibility::Collapsed)
        {
            // SSpinBox collapses the display text during native text entry.
            return nullptr;
        }
        return ValueText;
    }

    static bool IsVisibleValueLabel(const TSharedRef<STextBlock>& Widget, const FString& ExpectedDisplay)
    {
        const EVisibility Visibility = Widget->GetVisibility();
        return Visibility != EVisibility::Hidden
            && Visibility != EVisibility::Collapsed
            && Widget->GetText().ToString() == ExpectedDisplay;
    }

    static TSharedPtr<STextBlock> FindTextUnderSpinBox(
        const TSharedRef<SWidget>& Widget,
        const FString& ExpectedDisplay,
        const FVector2D& ScreenPosition,
        int32 RemainingDepth = 20)
    {
        if (RemainingDepth <= 0)
        {
            return nullptr;
        }
        if (Widget->GetType() == FName(TEXT("STextBlock")))
        {
            const TSharedRef<STextBlock> TextWidget = StaticCastSharedRef<STextBlock>(Widget);
            if (IsVisibleValueLabel(TextWidget, ExpectedDisplay))
            {
                const FGeometry& Geometry = TextWidget->GetTickSpaceGeometry();
                const FVector2D LocalPosition = Geometry.AbsoluteToLocal(ScreenPosition);
                const FVector2D Size = Geometry.GetLocalSize();
                if (LocalPosition.X >= 0.0 && LocalPosition.Y >= 0.0
                    && LocalPosition.X <= Size.X && LocalPosition.Y <= Size.Y)
                {
                    return TextWidget;
                }
            }
        }
        // Some SSpinBox skins use an overlay over their text or place the text
        // in a hit-test-invisible subtree. Such text isn't on the pointer's
        // FWidgetPath, but still has a valid layout geometry.
        FChildren* Children = Widget->GetChildren();
        if (!Children)
        {
            return nullptr;
        }
        for (int32 Index = 0; Index < Children->Num(); ++Index)
        {
            if (TSharedPtr<STextBlock> Candidate = FindTextUnderSpinBox(
                Children->GetChildAt(Index), ExpectedDisplay, ScreenPosition, RemainingDepth - 1))
            {
                return Candidate;
            }
        }
        return nullptr;
    }
}

void FDigitInputProcessor::SetDigitCursorOverride(
    const TSharedPtr<STextBlock>& TextWidget,
    EMouseCursor::Type Cursor)
{
    if (!TextWidget.IsValid())
    {
        return;
    }

    const TSharedPtr<STextBlock> PreviousTextWidget = CursorOverrideTextWidget.Pin();
    if (PreviousTextWidget.IsValid() && PreviousTextWidget != TextWidget)
    {
        PreviousTextWidget->SetCursor(TOptional<EMouseCursor::Type>());
    }

    CursorOverrideTextWidget = TextWidget;
    TextWidget->SetCursor(Cursor);
}

void FDigitInputProcessor::ClearDigitCursorOverride()
{
    if (const TSharedPtr<STextBlock> TextWidget = CursorOverrideTextWidget.Pin())
    {
        TextWidget->SetCursor(TOptional<EMouseCursor::Type>());
    }

    CursorOverrideTextWidget.Reset();
}

void FDigitInputProcessor::ApplyDigitCursor()
{
    // Keep Digit's cursor state on the text widget itself instead of pushing a
    // ProcessCursorReply every frame. SSpinBox performs its own cursor queries while
    // scrubbing, so repeatedly forcing a reply here caused the native widget and Digit
    // to alternate cursor ownership and visibly flicker.
    //
    // The override is persistent for the active interaction: normal pointer while the
    // digit is being targeted, then ResizeLeftRight once the native drag threshold has
    // been crossed. Highlight rebuilds preserve this same state.
    const TSharedPtr<STextBlock> TextWidget = ActiveSpinBox.IsValid()
        ? ActiveTextWidget.Pin()
        : HighlightTextWidget.Pin();

    SetDigitCursorOverride(
        TextWidget,
        ActiveSpinBox.IsValid() && bDigitDragStarted
            ? EMouseCursor::ResizeLeftRight
            : EMouseCursor::Default);
}

void FDigitInputProcessor::Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
    if (LastStagedEditable.IsValid()
        && SlateApp.GetKeyboardFocusedWidget().Get() != LastStagedEditable.Pin().Get())
    {
        LastStagedEditable.Reset();
        LastStagedSource.Reset();
    }
    if (LadderWidget.IsValid() && (!LadderSourceSpinBox.IsValid() || !LadderSourceText.IsValid()))
    {
        ClearLadder();
    }
    if (!HasActiveSession() || bRestorePending)
    {
        return;
    }

    const TSharedPtr<SWidget> PinnedSpinBox = ActiveSpinBox.Pin();
    if (!PinnedSpinBox.IsValid())
    {
        // A Details row or floating editor tab can be destroyed during a drag.
        // A weak widget does not keep the owning editor (or its callbacks) alive.
        QueueRestore();
        return;
    }

    if (bAwaitingNativeCapture)
    {
        if (PinnedSpinBox->HasMouseCapture())
        {
            bAwaitingNativeCapture = false;
            PendingCapturePostTicks = 0;
        }
        return;
    }

    if (!PinnedSpinBox->HasMouseCapture())
    {
        // Native mouse capture was lost without passing through our button-up
        // preprocessor. Never continue scaling subsequent pointer movements.
        QueueRestore();
    }
}

bool FDigitInputProcessor::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
    // An explicit keyboard-only path for Curve Editor Time's text editor.
    // Keep this separate from SSpinBox capture: SEditableTextBox has no
    // public numeric slider transaction or external OnTextCommitted call.
    // Stage a valid formatted edit, then let Enter / focus loss invoke the
    // original FFrameNumberDetailsCustomization property-handle callback.
    if (!HasActiveSession() && !bRestorePending
        && InKeyEvent.IsControlDown() && InKeyEvent.IsAltDown()
        && !InKeyEvent.IsShiftDown()
        && (InKeyEvent.GetKey() == EKeys::Up || InKeyEvent.GetKey() == EKeys::Down))
    {
        const TSharedPtr<SWidget> FocusedWidget = SlateApp.GetKeyboardFocusedWidget();
        if (FocusedWidget.IsValid()
            && FocusedWidget->GetType() == FName(TEXT("SEditableText")))
        {
            const FWidgetPath Path = SlateApp.LocateWindowUnderMouse(
                SlateApp.GetCursorPos(),
                SlateApp.GetInteractiveTopLevelWindows(),
                false, InKeyEvent.GetUserIndex());
            int32 CurvePanelIndex = INDEX_NONE;
            int32 TextBoxIndex = INDEX_NONE;
            int32 EditableIndex = INDEX_NONE;
            for (int32 Index = 0; Index < Path.Widgets.Num(); ++Index)
            {
                const TSharedRef<SWidget>& Item = Path.Widgets[Index].Widget;
                if (Item->GetType() == FName(TEXT("SCurveKeyDetailPanel")))
                {
                    CurvePanelIndex = Index;
                }
                else if (Item->GetType() == FName(TEXT("SEditableTextBox")))
                {
                    TextBoxIndex = Index;
                }
                else if (&Item.Get() == FocusedWidget.Get()
                    && Item->GetType() == FName(TEXT("SEditableText")))
                {
                    EditableIndex = Index;
                }
            }
            if (CurvePanelIndex != INDEX_NONE
                && TextBoxIndex > CurvePanelIndex
                && EditableIndex > TextBoxIndex)
            {
                // UE 5.8.3 SCurveKeyDetailPanel uses an SHorizontalBox:
                // child 0 is Time, child 1 is Value. The temporary loading
                // placeholder must not qualify as a frame editor.
                bool bInsideTimeBranch = false;
                for (int32 Index = CurvePanelIndex + 1; Index < TextBoxIndex; ++Index)
                {
                    const TSharedRef<SWidget>& Item = Path.Widgets[Index].Widget;
                    if (Item->GetType() == FName(TEXT("SHorizontalBox")))
                    {
                        FChildren* Children = Item->GetChildren();
                        if (Children && Children->Num() == 2
                            && Index + 1 < Path.Widgets.Num())
                        {
                            bInsideTimeBranch = &Children->GetChildAt(0).Get()
                                == &Path.Widgets[Index + 1].Widget.Get();
                        }
                        break;
                    }
                }
                if (bInsideTimeBranch)
                {
                    const TSharedRef<SEditableTextBox> TextBox =
                        StaticCastSharedRef<SEditableTextBox>(Path.Widgets[TextBoxIndex].Widget);
                    const TSharedRef<SEditableText> Editable =
                        StaticCastSharedRef<SEditableText>(Path.Widgets[EditableIndex].Widget);
                    bool bHostEnabled = !TextBox->IsReadOnly() && !TextBox->IsPassword()
                        && !Editable->IsTextReadOnly();
                    for (int32 Index = CurvePanelIndex; Index <= EditableIndex; ++Index)
                    {
                        const TSharedRef<SWidget>& Item = Path.Widgets[Index].Widget;
                        const EVisibility Visibility = Item->GetVisibility();
                        bHostEnabled &= Item->IsEnabled()
                            && Visibility != EVisibility::Collapsed
                            && Visibility != EVisibility::Hidden;
                    }
                    if (bHostEnabled)
                    {
                        // Require exactly one explicitly selected digit.
                        // Never reinterpret a caret or multi-digit selection.
                        const FString DisplayText = TextBox->GetText().ToString();
                        int32 CharacterIndex = INDEX_NONE;
                        if (DigitTemporalEditable::TryGetSingleSelectedDigit(
                            Editable->GetSelection(), DisplayText, CharacterIndex))
                        {
                            // SEditableText::GetText reports its BOUND source,
                            // not necessarily the uncommitted edit buffer.
                            // Do not calculate another step from stale source
                            // text after SetEditableText has staged a change.
                            // Clear the guard when focus exits or the host
                            // genuinely updates its bound property.
                            if (LastStagedEditable.Pin().Get() == &Editable.Get()
                                && DisplayText == LastStagedSource)
                            {
                                return false;
                            }
                            FString NextDisplay;
                            const int32 Direction = InKeyEvent.GetKey() == EKeys::Up ? 1 : -1;
                            if (FDigitSequencerContext::TryStepEditableNonDropTimecode(
                                    Path, DisplayText, CharacterIndex, Direction, NextDisplay)
                                && Editable->SetEditableText(FText::FromString(NextDisplay)))
                            {
                                LastStagedEditable = Editable;
                                LastStagedSource = DisplayText;
                                Editable->SelectText(
                                    FTextLocation(0, CharacterIndex),
                                    FTextLocation(0, CharacterIndex + 1));
                                return true;
                            }
                        }
                    }
                }
            }
        }
    }

    if (InKeyEvent.GetKey() == EKeys::Escape
        && HasActiveSession()
        && !bRestorePending
        && InKeyEvent.GetUserIndex() == ActiveUserIndex)
    {
        // DO NOT consume Escape. SSpinBox::OnKeyDown is responsible for
        // CancelMouseCapture/NotifyValueCommitted. We only defer restoring our
        // temporary attributes until Slate has delivered that native event.
        if (const TSharedPtr<SWidget> PinnedSpinBox = ActiveSpinBox.Pin())
        {
            if (PinnedSpinBox->HasMouseCapture())
            {
                QueueRestore();
            }
        }
    }

    if ((InKeyEvent.GetKey() == EKeys::LeftAlt || InKeyEvent.GetKey() == EKeys::RightAlt)
        && !InKeyEvent.IsRepeat())
    {
        // A new Alt press is the ONLY opportunity to choose a new origin.
        // Moving across digits with Alt held must never re-anchor the ladder.
        if (IsLadderToggleEnabled())
        {
            if (LadderWidget.IsValid())
            {
                ClearLadder();
                bLadderModeActive = false;
            }
            else if (!HasActiveSession() && !bRestorePending)
            {
                ShowLadderFromCurrentHighlight(SlateApp);
                bLadderToggledOn = LadderWidget.IsValid();
            }
        }
        else if (!HasActiveSession() && !bRestorePending)
        {
            ShowLadderFromCurrentHighlight(SlateApp);
        }
    }

    return false;
}

bool FDigitInputProcessor::HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
    if ((InKeyEvent.GetKey() == EKeys::LeftAlt || InKeyEvent.GetKey() == EKeys::RightAlt)
        && !IsLadderToggleEnabled())
    {
        ClearLadder();
        // The numerical drag itself may still be captured by its native spin box.
        // Closing its ladder disables magnitude switching, not native release.
        bLadderModeActive = false;
    }

    return false;
}

bool FDigitInputProcessor::HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
    if (HasActiveSession() && !bRestorePending)
    {
        const TSharedPtr<SWidget> PinnedSpinBox = ActiveSpinBox.Pin();
        if (!PinnedSpinBox.IsValid())
        {
            QueueRestore();
            return false;
        }

        if (!PinnedSpinBox->HasMouseCapture())
        {
            // A native mouse-down can be intercepted. Before the initial capture,
            // let the original event route normally; after capture, abort the Digit
            // session instead of ever forwarding an unscaled extra movement.
            if (!bAwaitingNativeCapture)
            {
                QueueRestore();
            }
            return false;
        }

        bAwaitingNativeCapture = false;
        PendingCapturePostTicks = 0;
        // Another pointer/user must not modify this pointer's active scrub.
        if (MouseEvent.GetPointerIndex() != ActivePointerIndex
            || MouseEvent.GetUserIndex() != ActiveUserIndex)
        {
            return false;
        }

        if (ApplySelectedDeltaFunction)
        {
            ApplySelectedDeltaFunction();
        }

        if (!RouteActiveMoveFunction)
        {
            return false;
        }

        // Ladder selection is deliberately independent from horizontal value movement.
        // Crossing into another rung changes the magnitude, and that same mouse event does
        // not also change the value. The next horizontal motion starts from the current value.
        const bool bMagnitudeChangedThisMove = bLadderModeActive
            && !IsLadderRowSelectionEnabled()
            ? UpdateLadderSelection(MouseEvent)
            : false;

        // Preserve Unreal's native physical horizontal drag threshold. Until that threshold
        // is crossed, pass the real cursor delta into SSpinBox. The crossing move only
        // transitions the native widget into drag mode; actual value changes begin after it.
        const bool bScaleMovement = bDigitDragStarted;
        if (!bDigitDragStarted && !bMagnitudeChangedThisMove)
        {
            PhysicalDragDistance += FMath::Abs(static_cast<float>(MouseEvent.GetCursorDelta().X));
        }

        const bool bRouted = RouteActiveMoveFunction(
            MouseEvent,
            bScaleMovement,
            ActiveDigitPlace,
            bMagnitudeChangedThisMove);

        if (bActiveDigitScrubSuspended)
        {
            ClearActiveDragOutline();
        }

        if (bScaleMovement
            && bRouted
            && !bActiveDigitScrubSuspended
            && !bMagnitudeChangedThisMove
            && !FMath::IsNearlyZero(static_cast<float>(MouseEvent.GetCursorDelta().X)))
        {
            UpdateActiveDragOutline(
                SlateApp,
                MouseEvent.GetCursorDelta().X > 0.0f
                    ? EActiveDragDirection::Increasing
                    : EActiveDragDirection::Decreasing);
        }

        if (!bDigitDragStarted
            && PhysicalDragDistance > SlateApp.GetDragTriggerDistance())
        {
            bDigitDragStarted = true;
            ApplyDigitCursor();
        }

        // We directly routed the move to the captured SSpinBox. Returning true prevents
        // the original, unscaled mouse event from reaching it a second time.
        return bRouted;
    }

    if (!bRestorePending)
    {
        if (LadderWidget.IsValid())
        {
            // Do not retarget the source field when moving from the digit into
            // the ladder. A disappearing source window closes its own overlay.
            if (!LadderSourceSpinBox.IsValid() || !LadderSourceText.IsValid())
            {
                ClearLadder();
            }
            else
            {
                UpdateLadderHover(MouseEvent.GetScreenSpacePosition());
            }
        }
        else
        {
            UpdateHoverHighlight(SlateApp, MouseEvent);
        }
    }

    return false;
}

bool FDigitInputProcessor::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
    if (HasActiveSession() || bRestorePending)
    {
        return false;
    }

    if (LadderWidget.IsValid())
    {
        int32 RowPlace = INDEX_NONE;
        const bool bInsideLadder = IsOverLadder(MouseEvent.GetScreenSpacePosition(), &RowPlace);
        if (bInsideLadder)
        {
            // Interactive row widgets now receive the REAL mouse-down through
            // Slate's normal dispatch. Do not swallow it in the preprocessor or
            // replay it during post-tick. A row returns the native SSpinBox's
            // FReply, including its native capture request.
            return MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton
                || RowPlace == INDEX_NONE
                || !(IsLadderToggleEnabled() || IsLadderRowSelectionEnabled());
        }
        if (IsLadderToggleEnabled() && !IsOverLadderSourceField(SlateApp, MouseEvent.GetScreenSpacePosition()))
        {
            // Source field + ladder form one interaction island. Anything else
            // dismisses toggle mode, but the clicked editor widget gets its
            // original input. In row-selection mode, source clicks are ignored
            // rather than closing the ladder or accidentally editing a key.
            ClearLadder();
            ClearHighlight();
            return false;
        }
    }

    if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
    {
        return false;
    }

    FNumericHit Hit;
    // Once opened, the source FIELD is the target. Digit selection happened
    // when Alt was pressed, and must not be recalculated on mouse-down.
    const bool bHit = LadderWidget.IsValid()
        && IsOverLadderSourceField(SlateApp, MouseEvent.GetScreenSpacePosition())
        ? BuildLadderSourceHit(SlateApp, LadderSelectedDigitPlace, Hit)
        : FindNumericHit(SlateApp, MouseEvent, Hit);
    if (!bHit)
    {
        // Numeric failures are common for intentional non-digit clicks. Keep
        // diagnostics completely off the input hot path unless explicitly enabled.
        const UDigitSettings* Settings = UDigitSettings::Get();
        if (Settings && Settings->bLogWidgetDiagnostics)
        {
            FWidgetPath DebugPath = SlateApp.LocateWindowUnderMouse(
                MouseEvent.GetScreenSpacePosition(),
                SlateApp.GetInteractiveTopLevelWindows(),
                false,
                MouseEvent.GetUserIndex());

            FString PathTypes;
            bool bContainsSpinBox = false;
            for (int32 WidgetIndex = 0; WidgetIndex < DebugPath.Widgets.Num(); ++WidgetIndex)
            {
                const FArrangedWidget& ArrangedWidget = DebugPath.Widgets[WidgetIndex];
                const FString TypeString = ArrangedWidget.Widget->GetTypeAsString();
                if (!PathTypes.IsEmpty())
                {
                    PathTypes += TEXT(" > ");
                }
                PathTypes += TypeString;
                bContainsSpinBox |= TypeString.StartsWith(TEXT("SSpinBox"));
            }
            if (bContainsSpinBox)
            {
                const FDecimalNumberFormattingRules& Rules = DigitPrivate::GetNumberRules();
                UE_LOG(LogDigit, Warning,
                    TEXT("Unresolved numeric click (culture=%s, active numeric decimal U+%04X, group U+%04X). Widget path: %s"),
                    *FInternationalization::Get().GetCurrentLocale()->GetName(),
                    static_cast<uint32>(Rules.DecimalSeparatorCharacter),
                    static_cast<uint32>(Rules.GroupingSeparatorCharacter),
                    *PathTypes);
            }
        }

        ClearHighlight();
        ClearLadder();
        return false;
    }

    const bool bOverOriginalField = !LadderWidget.IsValid()
        || LadderSpinBoxIdentity == Hit.SpinBoxWidget.Get();
    if (LadderWidget.IsValid() && !bOverOriginalField)
    {
        // Alt belongs to the source that originally opened the ladder.
        // An unrelated spin box must not silently inherit its magnitude.
        return false;
    }
    if (LadderWidget.IsValid() && IsLadderRowSelectionEnabled())
    {
        // In row-selection mode only a ladder-row mouse-down starts a scrub.
        return true;
    }
    const bool bUseLadder = LadderWidget.IsValid()
        && (MouseEvent.IsAltDown() || (IsLadderToggleEnabled() && bLadderToggledOn));
    FNumericHit EffectiveHit = Hit;
    if (bUseLadder)
    {
        // Clicking a different digit in the SAME field must not change which
        // magnitude is being scrubbed. Pass the locked ladder selection into
        // the native arm path, including its representable-step checks.
        EffectiveHit.DigitPlace = LadderSelectedDigitPlace;
    }
    if (ArmSpinBox(EffectiveHit, MouseEvent.GetPointerIndex(), bUseLadder))
    {
        ActiveUserIndex = MouseEvent.GetUserIndex();
        if (!bUseLadder)
        {
            FWidgetPath Path = SlateApp.LocateWindowUnderMouse(
                MouseEvent.GetScreenSpacePosition(),
                SlateApp.GetInteractiveTopLevelWindows(),
                false,
                MouseEvent.GetUserIndex());
            if (Path.IsValid())
            {
                ShowHighlight(SlateApp, Path, Hit);
            }
            ClearLadder();
        }
        // In ladder mode, retain the initial digit marker and origin.
        // The active drag refresh will update that same origin's highlight.
    }

    return false;
}

bool FDigitInputProcessor::HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
    if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton
        && HasActiveSession()
        && MouseEvent.GetPointerIndex() == ActivePointerIndex
        && MouseEvent.GetUserIndex() == ActiveUserIndex)
    {
        // The input preprocessor sees mouse-up BEFORE SSpinBox does. A host
        // value can change after the last mouse move (animation evaluation,
        // parameter refresh, multi-control selection). Reconcile it now so
        // native OnValueCommitted cannot write our stale temporary literal.
        if (VerifyActiveDisplayFunction)
        {
            VerifyActiveDisplayFunction();
        }
        if (VerifyActiveSourceFunction)
        {
            VerifyActiveSourceFunction();
        }
        // Preserve Delta=0 during the native final commit and deferred Tick.
        // Restoring the original Delta early would re-snap material values.
        if (ApplySelectedDeltaFunction)
        {
            ApplySelectedDeltaFunction();
        }
        QueueRestore(true);
    }

    return false;
}

void FDigitInputProcessor::HandlePostSlateTick()
{
    if (bLadderDismissQueued)
    {
        // A row callback must not remove its own overlay during Slate's
        // mouse-down dispatch. Tear it down only after dispatch completes.
        bLadderDismissQueued = false;
        ClearLadder();
    }
    if (HasActiveSession() && !bRestorePending)
    {
        const TSharedPtr<SWidget> PinnedSpinBox = ActiveSpinBox.Pin();
        if (!PinnedSpinBox.IsValid())
        {
            // The row can expire without another input event (selection changes,
            // panel teardown, asset-editor replacement). Do not orphan the
            // per-widget temporary Delta or block all future Digit sessions.
            QueueRestore();
        }
        else if (bAwaitingNativeCapture)
        {
            if (PinnedSpinBox->HasMouseCapture())
            {
                bAwaitingNativeCapture = false;
                PendingCapturePostTicks = 0;
            }
            else if (++PendingCapturePostTicks >= 2)
            {
                // The preprocessor arms before native OnMouseButtonDown. If
                // another widget intercepts mouse-down, SSpinBox never captures
                // and there will be no matching native slider release. Disarm.
                const UDigitSettings* Settings = UDigitSettings::Get();
                if (Settings && Settings->bLogWidgetDiagnostics)
                {
                    UE_LOG(LogDigit, Warning,
                        TEXT("Native spin box did not capture within two Slate post-ticks; cancelling Digit arm"));
                }
                QueueRestore();
            }
        }
        else if (!PinnedSpinBox->HasMouseCapture())
        {
            // Lost capture can skip the native slider-end delegate; Digit does
            // not synthesize property transactions or call engine-private APIs.
            const UDigitSettings* Settings = UDigitSettings::Get();
            if (Settings && Settings->bLogWidgetDiagnostics && bDigitDragStarted)
            {
                UE_LOG(LogDigit, Warning,
                    TEXT("Native spin box lost capture during a Digit drag; restoring widget state (verify editor transaction behavior)"));
            }
            QueueRestore();
        }
        else
        {
            // Native numeric displays and specialized source bindings can
            // refresh while the cursor is stationary, before mouse-up.
            if (VerifyActiveDisplayFunction)
            {
                VerifyActiveDisplayFunction();
            }
            if (VerifyActiveSourceFunction)
            {
                VerifyActiveSourceFunction();
            }
        }
    }

    if (bRestorePending)
    {
        // Mouse-up and Escape arrive at the input preprocessor BEFORE the
        // captured SSpinBox. Never restore during the native release/commit.
        if (const TSharedPtr<SWidget> CapturedSpinBox = ActiveSpinBox.Pin())
        {
            if (CapturedSpinBox->HasMouseCapture())
            {
                PendingRestorePostTicks = 0;
                return;
            }
        }

        // A native mouse-move value may be committed in SSpinBox::Tick. Allow
        // another complete widget tick before reinstating its original Delta
        // and, for direct-value routes, its original bound value attribute.
        ClearHighlight();
        if (++PendingRestorePostTicks >= 2)
        {
            RestoreActiveDelta();
        }
        return;
    }

    if (HasActiveSession() && ActiveTextWidget.IsValid() && FSlateApplication::IsInitialized())
    {
        RefreshActiveHighlight(FSlateApplication::Get());
    }
}

void FDigitInputProcessor::Shutdown()
{
    ClearLadder();
    RestoreActiveDelta();
    ClearHighlight();
    ClearActiveDragOutline();
    ClearDigitCursorOverride();
}

bool FDigitInputProcessor::FindNumericHit(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent, FNumericHit& OutHit) const
{
    FWidgetPath Path = SlateApp.LocateWindowUnderMouse(
        MouseEvent.GetScreenSpacePosition(),
        SlateApp.GetInteractiveTopLevelWindows(),
        false,
        MouseEvent.GetUserIndex());

    if (!Path.IsValid())
    {
        return false;
    }

    int32 SpinBoxPathIndex = INDEX_NONE;
    int32 TextPathIndex = INDEX_NONE;

    for (int32 Index = Path.Widgets.Num() - 1; Index >= 0; --Index)
    {
        const TSharedRef<SWidget>& Widget = Path.Widgets[Index].Widget;

        if (TextPathIndex == INDEX_NONE
            && Widget->GetType() == FName(TEXT("STextBlock")))
        {
            TextPathIndex = Index;
        }

        if (SpinBoxPathIndex == INDEX_NONE && IsSupportedSpinBox(Widget))
        {
            SpinBoxPathIndex = Index;
        }
    }

    if (SpinBoxPathIndex == INDEX_NONE)
    {
        return false;
    }

    // Some Slate windows (notably virtual/drag-drop windows) do not construct
    // an overlay. Digit draws its digit marker, drag outline, and value ladder
    // in SWindow overlay slots. Do not arm custom numeric input in a window
    // where those visuals cannot be hosted; let the native widget handle it.
    const TSharedPtr<SWindow> NumericWindow = SlateApp.FindWidgetWindow(Path.Widgets[SpinBoxPathIndex].Widget);
    if (!NumericWindow.IsValid() || !NumericWindow->HasOverlay())
    {
        return false;
    }

    // Identify rotators *before* unit eligibility, including at displayed
    // zero. SNumericRotatorInputBox supplies native degrees; other scalar
    // fields with a degree suffix have no such guarantee.
    bool bIsRotatorComponent = false;
    bool bCurveKeyTimeField = false;
    ENumericOwner NumericOwner = ENumericOwner::General;
    int32 CurveKeyDetailPathIndex = INDEX_NONE;
    for (int32 Index = 0; Index < SpinBoxPathIndex; ++Index)
    {
        const FString WidgetType = Path.Widgets[Index].Widget->GetTypeAsString();
        if (WidgetType.Contains(TEXT("RotatorInputBox")))
        {
            bIsRotatorComponent = true;
        }

        // UE 5.8.3 Niagara's ordinary numeric parameter editors own their
        // begin/change/end callbacks, and their unit interface displays the
        // same unit used for the stored value. Recognize those classes only,
        // not arbitrary widgets merely living in a Niagara asset editor.
        if (WidgetType.Contains(TEXT("SNiagaraFloatParameterEditor"))
            || WidgetType.Contains(TEXT("SNiagaraIntegerParameterEditor"))
            || (WidgetType.Contains(TEXT("SNiagaraVector"))
                && WidgetType.Contains(TEXT("ParameterEditor"))))
        {
            NumericOwner = ENumericOwner::NiagaraParameter;
        }
        else if (WidgetType.Contains(TEXT("SNumericKeyEditorWidget")))
        {
            NumericOwner = ENumericOwner::SequencerKey;
        }
        else if (WidgetType.Contains(TEXT("SCurveKeyDetailPanel")))
        {
            // UE's Curve Key Detail panel has distinct Time and Value slots.
            // Frame-number Time uses a customized text editor, not a native
            // spinbox; some other time property rows can expose spinboxes.
            NumericOwner = ENumericOwner::CurveEditorKeyDetail;
            CurveKeyDetailPathIndex = Index;
        }
        else if (WidgetType.Contains(TEXT("SCurveEditorToolProperties")))
        {
            // Curve Editor extension-tool options are structure-backed Details
            // widgets that can be rebuilt when the active tool changes.
            NumericOwner = ENumericOwner::CurveEditorToolOptions;
        }
        else if (WidgetType.Contains(TEXT("SValueStepper")))
        {
            NumericOwner = ENumericOwner::SequencerValueStepper;
        }
        else if (WidgetType.Contains(TEXT("SAnimDetailsValueNumeric")))
        {
            NumericOwner = ENumericOwner::ControlRigAnimationDetails;
        }
    }

    // UE 5.8.3 SCurveKeyDetailPanel::ConstructChildLayout uses an SBox
    // containing an SHorizontalBox with TimeWidget in slot 0 and ValueWidget
    // in slot 1. Identify which *actual Slate branch* was hit, rather than
    // relying on localized labels or guessing from the displayed digits.
    // Only the immediate layout horizontal box is relevant: nested widgets
    // may have unrelated horizontal boxes of their own.
    if (CurveKeyDetailPathIndex != INDEX_NONE)
    {
        for (int32 Index = CurveKeyDetailPathIndex + 1; Index < SpinBoxPathIndex; ++Index)
        {
            const TSharedRef<SWidget>& Candidate = Path.Widgets[Index].Widget;
            if (Candidate->GetType() == FName(TEXT("SHorizontalBox")))
            {
                FChildren* Children = Candidate->GetChildren();
                if (Children && Children->Num() == 2 && Index + 1 < Path.Widgets.Num())
                {
                    // TSharedRef::Get() returns SWidget&, which cannot be
                    // compared using operator==. Compare widget identity by
                    // address instead of comparing the SWidget objects.
                    bCurveKeyTimeField = &Children->GetChildAt(0).Get()
                        == &Path.Widgets[Index + 1].Widget.Get();
                }
                break;
            }
        }
    }

    // A child SSpinBox can report itself enabled while an enclosing Details
    // row or editor is disabled. Slate's effective enabled state also depends
    // on its ancestors, so do not advertise Digit on a read-only hierarchy.
    // An undetermined SNumericEntryBox uses its text-only overlay with the
    // underlying spinbox collapsed. Never arm a hidden spinbox.
    for (int32 Index = 0; Index <= SpinBoxPathIndex; ++Index)
    {
        const TSharedRef<SWidget>& Candidate = Path.Widgets[Index].Widget;
        if (!Candidate->IsEnabled())
        {
            return false;
        }
        const EVisibility Visibility = Candidate->GetVisibility();
        if (Visibility == EVisibility::Collapsed || Visibility == EVisibility::Hidden)
        {
            return false;
        }
    }

    const TSharedRef<SWidget>& SpinWidget = Path.Widgets[SpinBoxPathIndex].Widget;
    const TSharedPtr<STextBlock> NativeValueText = DigitPrivate::GetNativeSpinValueLabel(SpinWidget);
    if (!NativeValueText.IsValid())
    {
        return false;
    }
    const FString NativeDisplayString = NativeValueText->GetText().ToString();
    if (NativeDisplayString.IsEmpty())
    {
        return false;
    }

    TSharedPtr<STextBlock> TextWidget;
    FGeometry TextGeometry;
    if (TextPathIndex > SpinBoxPathIndex)
    {
        TextWidget = StaticCastSharedRef<STextBlock>(Path.Widgets[TextPathIndex].Widget);
        TextGeometry = Path.Widgets[TextPathIndex].Geometry;
    }
    else
    {
        TextWidget = DigitPrivate::FindTextUnderSpinBox(
            SpinWidget, NativeDisplayString, MouseEvent.GetScreenSpacePosition());
        if (TextWidget.IsValid())
        {
            TextGeometry = TextWidget->GetTickSpaceGeometry();
        }
    }
    if (!TextWidget.IsValid())
    {
        return false;
    }

    // The hit-test path can also contain informational numeric labels or
    // an SSpinBox's collapsed display label while it is in text-edit mode.
    // Only the *visible, bound value label* constructed by SSpinBox qualifies.
    if (TextWidget != NativeValueText
        || !DigitPrivate::IsVisibleValueLabel(TextWidget.ToSharedRef(), NativeDisplayString))
    {
        return false;
    }

    const FText DisplayText = TextWidget->GetText();
    const FString DisplayString = NativeDisplayString;

    // UE's numeric type interface may append "..." when the displayed
    // decimal is rounded away from a nonzero (often extremely small) value.
    // Selecting a digit in the rounded string would suggest an inaccurate
    // step and can mislead users into overwriting values they cannot see.
    const bool bStructuredDisplay = DigitPrivate::HasStructuredTimeDisplay(DisplayString);
    const bool bTimecodeCandidate = bStructuredDisplay
        && DisplayString.StartsWith(TEXT("["))
        && DisplayString.EndsWith(TEXT("]"))
        && (NumericOwner == ENumericOwner::SequencerValueStepper || bCurveKeyTimeField)
        && DigitPrivate::IsWidgetClass<double>(SpinWidget);
    if (DisplayString.Contains(TEXT("..."))
        || (bStructuredDisplay && !bTimecodeCandidate)
        || (!bTimecodeCandidate && !DigitPrivate::HasCanonicalNumericPrefix(DisplayString)))
    {
        return false;
    }

    // A Sequencer stepper may display frames or seconds while storing ticks.
    // Ask the actual owning Sequencer and verify its numeric interface's
    // ToString/FromString mapping before using any frame-aware magnitude.
    // In particular, 0 frames and 0 ticks cannot be resolved by a ratio.
    TOptional<FDigitTemporalSpinContext> TemporalContext;
    const bool bPotentialTemporalField = NumericOwner == ENumericOwner::SequencerValueStepper
        || bCurveKeyTimeField;
    if (bPotentialTemporalField && !bTimecodeCandidate
        && DigitPrivate::IsWidgetClass<double>(SpinWidget))
    {
        const double NativeValue = StaticCastSharedRef<SSpinBox<double>>(SpinWidget)->GetValue();
        FDigitTemporalSpinContext VerifiedContext;
        if (FDigitSequencerContext::Resolve(Path, DisplayString, NativeValue, VerifiedContext))
        {
            TemporalContext = VerifiedContext;
        }
    }
    if (bPotentialTemporalField && !bTimecodeCandidate && !TemporalContext.IsSet())
    {
        // Curve key Time belongs to a timeline, even when its displayed
        // number is nonzero. Never guess a tick/frame ratio from that number.
        // SValueStepper is a generic widget: allow its *nonzero* ordinary
        // scalar values only when the visible and stored values match at
        // double precision, with no inferred numeric conversion.
        const double NativeValue = DigitPrivate::IsWidgetClass<double>(SpinWidget)
            ? StaticCastSharedRef<SSpinBox<double>>(SpinWidget)->GetValue()
            : 0.0;
        if (bCurveKeyTimeField || DigitPrivate::IsDisplayedZero(DisplayString)
            || !DigitPrivate::IsWidgetClass<double>(SpinWidget)
            || !DigitPrivate::IsUnconvertedNativeDisplay(DisplayString, NativeValue))
        {
            return false;
        }
    }

    // A converted unit with an unknown scale is not digit-safe. Reject it
    // during hover as well as mouse-down so the highlight never promises a
    // scrubbing magnitude that the plugin cannot honor. A temporal field
    // with a verified host conversion is the one exception at displayed zero.
    if (!TemporalContext.IsSet() && !bTimecodeCandidate
        && !DigitPrivate::CanRepresentDisplayedMagnitude(
            SpinWidget, DisplayString, bIsRotatorComponent,
            NumericOwner == ENumericOwner::NiagaraParameter))
    {
        return false;
    }

    const FVector2D LocalMouse = TextGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
    if (LocalMouse.X < 0.0f || LocalMouse.Y < 0.0f
        || LocalMouse.X > TextGeometry.GetLocalSize().X
        || LocalMouse.Y > TextGeometry.GetLocalSize().Y)
    {
        return false;
    }

    const TSharedRef<FSlateFontMeasure> FontMeasure = SlateApp.GetRenderer()->GetFontMeasureService();
    const FSlateFontInfo Font = TextWidget->GetFont();
    const FVector2D FullTextSize = FontMeasure->Measure(DisplayText, Font);

    const float TextStartX = 0.0f;
    const float TextEndX = TextStartX + FullTextSize.X;
    if (LocalMouse.X < TextStartX || LocalMouse.X > TextEndX)
    {
        return false;
    }

    // Resolve the hovered glyph from the exact same visual bounds used to paint
    // the highlight. This keeps the displayed digit, DigitPlace and scrub step
    // locked to one shared character index. Punctuation such as the decimal point
    // is therefore never reinterpreted as the following digit.
    const int32 CharacterIndex = DigitPrivate::FindCharacterAtVisualOffset(
        FontMeasure,
        DisplayString,
        Font,
        LocalMouse.X - TextStartX);

    if (!DisplayString.IsValidIndex(CharacterIndex)
        || !DigitPrivate::IsDigitCharacter(DisplayString[CharacterIndex]))
    {
        return false;
    }

    int32 DigitPlace = 0;
    if (bTimecodeCandidate)
    {
        // Timecode magnitude is component-aware, not a decimal place.
        // Store the exact character index and resolve its native tick size
        // from the verified owning Sequencer numeric interface.
        FDigitTemporalSpinContext VerifiedTimecode;
        const double NativeValue =
            StaticCastSharedRef<SSpinBox<double>>(SpinWidget)->GetValue();
        if (!FDigitSequencerContext::ResolveNonDropTimecode(
            Path, DisplayString, NativeValue, CharacterIndex, VerifiedTimecode))
        {
            return false;
        }
        TemporalContext = VerifiedTimecode;
        DigitPlace = CharacterIndex;
    }
    else if (!ResolveDigitPlace(DisplayString, CharacterIndex, DigitPlace))
    {
        return false;
    }
    // The text may contain a digit that the backing numeric type cannot
    // actually adjust (fractional places on integral spin boxes or enormous
    // places beyond safe 64-bit integer precision). Do not highlight it.
    if (!bTimecodeCandidate
        && !DigitPrivate::CanScrubDisplayedPlace(SpinWidget, DigitPlace))
    {
        return false;
    }

    OutHit.SpinBoxWidget = SpinWidget;
    OutHit.bRotatorComponent = bIsRotatorComponent;
    OutHit.bCurveKeyTimeField = bCurveKeyTimeField;
    OutHit.Owner = NumericOwner;
    OutHit.TemporalContext = TemporalContext;
    OutHit.TextWidget = TextWidget;
    OutHit.TextGeometry = TextGeometry;
    OutHit.CharacterIndex = CharacterIndex;
    OutHit.DigitPlace = DigitPlace;
    OutHit.DisplayString = DisplayString;
    return true;
}

bool FDigitInputProcessor::ResolveDigitPlace(const FString& DisplayString, int32 CharacterIndex, int32& OutDigitPlace) const
{
    DigitPrivate::FNumericToken Token;
    if (!DigitPrivate::LocateNumericToken(DisplayString, CharacterIndex, Token))
    {
        return false;
    }

    if (Token.Radix == INDEX_NONE || CharacterIndex < Token.Radix)
    {
        const int32 RightBoundary = Token.Radix == INDEX_NONE ? Token.End : Token.Radix - 1;
        int32 DigitsToRight = 0;
        for (int32 Index = CharacterIndex + 1; Index <= RightBoundary; ++Index)
        {
            if (DigitPrivate::IsDigitCharacter(DisplayString[Index]))
            {
                ++DigitsToRight;
            }
        }
        OutDigitPlace = DigitsToRight;
        return true;
    }

    int32 FractionalOrdinal = 0;
    for (int32 Index = Token.Radix + 1; Index <= CharacterIndex; ++Index)
    {
        if (DigitPrivate::IsDigitCharacter(DisplayString[Index]))
        {
            ++FractionalOrdinal;
        }
    }
    if (FractionalOrdinal == 0)
    {
        return false;
    }
    OutDigitPlace = -FractionalOrdinal;
    return true;
}

bool FDigitInputProcessor::FindCharacterIndexForDigitPlace(const FString& DisplayString, int32 DigitPlace, int32& OutCharacterIndex) const
{
    OutCharacterIndex = INDEX_NONE;
    DigitPrivate::FNumericToken Token;
    bool bFoundToken = false;
    // The editable numeric value is the first number in SSpinBox's display.
    // Ignoring trailing unit labels matters for e.g. "125.00 cm2".
    for (int32 Index = 0; Index < DisplayString.Len(); ++Index)
    {
        if (DigitPrivate::IsDigitCharacter(DisplayString[Index]))
        {
            bFoundToken = DigitPrivate::LocateNumericToken(DisplayString, Index, Token);
            break;
        }
    }
    if (!bFoundToken)
    {
        return false;
    }

    if (DigitPlace >= 0)
    {
        const int32 RightBoundary = Token.Radix == INDEX_NONE ? Token.End : Token.Radix - 1;
        int32 Place = 0;
        for (int32 Index = RightBoundary; Index >= Token.Start; --Index)
        {
            if (!DigitPrivate::IsDigitCharacter(DisplayString[Index]))
            {
                continue;
            }
            if (Place++ == DigitPlace)
            {
                OutCharacterIndex = Index;
                return true;
            }
        }
        return false;
    }

    if (Token.Radix == INDEX_NONE)
    {
        return false;
    }
    int32 Ordinal = 0;
    for (int32 Index = Token.Radix + 1; Index <= Token.End; ++Index)
    {
        if (DigitPrivate::IsDigitCharacter(DisplayString[Index]) && ++Ordinal == -DigitPlace)
        {
            OutCharacterIndex = Index;
            return true;
        }
    }
    return false;
}

int32 FDigitInputProcessor::CountFractionalDigits(const FString& DisplayString, int32 CharacterIndex) const
{
    DigitPrivate::FNumericToken Token;
    if (!DigitPrivate::LocateNumericToken(DisplayString, CharacterIndex, Token)
        || Token.Radix == INDEX_NONE)
    {
        return 0;
    }

    int32 Count = 0;
    for (int32 Index = Token.Radix + 1; Index <= Token.End; ++Index)
    {
        if (DigitPrivate::IsDigitCharacter(DisplayString[Index]))
        {
            ++Count;
        }
    }
    return Count;
}

bool FDigitInputProcessor::ArmSpinBox(const FNumericHit& Hit, int32 PointerIndex, bool bUseLadder)
{
    if (!Hit.SpinBoxWidget.IsValid())
    {
        return false;
    }

    bool bArmed = false;
    bArmed = bArmed || TryArmTypedSpinBox<double>(Hit.SpinBoxWidget, Hit, bUseLadder);
    bArmed = bArmed || TryArmTypedSpinBox<float>(Hit.SpinBoxWidget, Hit, bUseLadder);
    bArmed = bArmed || TryArmTypedSpinBox<uint64>(Hit.SpinBoxWidget, Hit, bUseLadder);
    bArmed = bArmed || TryArmTypedSpinBox<uint32>(Hit.SpinBoxWidget, Hit, bUseLadder);
    bArmed = bArmed || TryArmTypedSpinBox<uint16>(Hit.SpinBoxWidget, Hit, bUseLadder);
    bArmed = bArmed || TryArmTypedSpinBox<uint8>(Hit.SpinBoxWidget, Hit, bUseLadder);
    bArmed = bArmed || TryArmTypedSpinBox<int64>(Hit.SpinBoxWidget, Hit, bUseLadder);
    bArmed = bArmed || TryArmTypedSpinBox<int32>(Hit.SpinBoxWidget, Hit, bUseLadder);
    bArmed = bArmed || TryArmTypedSpinBox<int16>(Hit.SpinBoxWidget, Hit, bUseLadder);
    bArmed = bArmed || TryArmTypedSpinBox<int8>(Hit.SpinBoxWidget, Hit, bUseLadder);

    if (bArmed)
    {
        ActiveSpinBox = Hit.SpinBoxWidget;
        ActiveTextWidget = Hit.TextWidget;
        ActivePointerIndex = PointerIndex;
        ActiveDigitPlace = Hit.DigitPlace;
        bActiveNonDropTimecode = Hit.TemporalContext.IsSet()
            && Hit.TemporalContext.GetValue().bNonDropTimecode;
        PhysicalDragDistance = 0.0f;
        bDigitDragStarted = false;
        bActiveDigitScrubSuspended = false;
        bRestorePending = false;
        PendingRestorePostTicks = 0;
        PendingCapturePostTicks = 0;
        bAwaitingNativeCapture = true;
        bLadderModeActive = bUseLadder && !bActiveNonDropTimecode;
        if (!LadderWidget.IsValid())
        {
            LadderOriginDigitPlace = Hit.DigitPlace;
            LadderSelectedDigitPlace = Hit.DigitPlace;
        }
        ActiveDigitPlace = bLadderModeActive ? LadderSelectedDigitPlace : Hit.DigitPlace;
        LadderVerticalTravel = 0.0f;
        bCtrlMagnitudeWasLocked = false;

        const UDigitSettings* DigitSettings = UDigitSettings::Get();
        const float ConfiguredLadderSensitivity =
            DigitSettings && FMath::IsFinite(DigitSettings->LadderMagnitudeSensitivity)
                ? FMath::Clamp(DigitSettings->LadderMagnitudeSensitivity, 0.1f, 10.0f)
                : 1.0f;

        // The public setting uses 1.0 as the new default, but that default intentionally
        // represents half of Digit's original vertical ladder sensitivity. A setting of
        // 2.0 therefore reproduces the pre-setting ladder behavior.
        ActiveLadderMagnitudeSensitivity = ConfiguredLadderSensitivity * 0.5f;
    }

    if (!bArmed)
    {
        const UDigitSettings* Settings = UDigitSettings::Get();
        if (Settings && Settings->bLogWidgetDiagnostics)
        {
            UE_LOG(LogDigit, Warning,
                TEXT("Could not arm numeric widget: type=%s, value='%s', place=%d (possible unsupported template, native slider disabled, or unrepresentable step)"),
                *Hit.SpinBoxWidget->GetTypeAsString(),
                *Hit.DisplayString, Hit.DigitPlace);
        }
    }
    return bArmed;
}

template<typename NumericType>
bool FDigitInputProcessor::TryArmTypedSpinBox(const TSharedPtr<SWidget>& Widget, const FNumericHit& Hit, bool bUseLadder)
{
    if (!Widget.IsValid())
    {
        return false;
    }

    const TSharedRef<SWidget> WidgetRef = Widget.ToSharedRef();
    if (!DigitPrivate::IsWidgetClass<NumericType>(WidgetRef))
    {
        return false;
    }

    const bool bNonDropTimecode = Hit.TemporalContext.IsSet()
        && Hit.TemporalContext.GetValue().bNonDropTimecode;
    if (bNonDropTimecode && !std::is_same<NumericType, double>::value)
    {
        return false;
    }
    NumericType InitialDesiredStep = NumericType(0);
    if (bNonDropTimecode)
    {
        InitialDesiredStep = static_cast<NumericType>(
            Hit.TemporalContext.GetValue().NativeTimecodeTickStep);
    }
    else if (!DigitPrivate::MakeStepForPlace<NumericType>(Hit.DigitPlace, InitialDesiredStep))
    {
        return false;
    }

    const TSharedRef<SSpinBox<NumericType>> SpinBox = StaticCastSharedRef<SSpinBox<NumericType>>(WidgetRef);
    bActiveSpinBoxIsIntegral = TIsIntegral<NumericType>::Value;
    bLadderIntegral = bActiveSpinBoxIsIntegral;

    if (!SpinBox->IsEnabled() || !SpinBox->GetEnableSlider())
    {
        return false;
    }

    const NumericType OriginalDelta = SpinBox->GetDelta();

    // UE 5.8 uses Delta for *two different purposes*: unbounded motion with
    // private LinearDeltaSensitivity and final value grid snapping. Keep Delta
    // zero strictly while OnMouseMove calculates movement, then restore a
    // decimal-grid Delta BEFORE the deferred native Tick commits the result.
    // This prevents a layout-dependent sensitivity path from engaging in e.g.
    // Transform rows while still avoiding small floating-point display noise.
    // Preserve the original field's Delta after the interaction.

    const UDigitSettings* DigitSettings = UDigitSettings::Get();
    const double ScrubSensitivity = DigitSettings && FMath::IsFinite(DigitSettings->ScrubSensitivity)
        ? FMath::Clamp(static_cast<double>(DigitSettings->ScrubSensitivity), 0.1, 10.0)
        : 1.0;

    const double SliderExponent = static_cast<double>(SpinBox->GetSliderExponent());
    if (!FMath::IsFinite(SliderExponent) || SliderExponent <= 0.0)
    {
        return false;
    }

    // SSpinBox has two fundamentally different scrub paths.
    // Unlimited ranges use Delta as part of the numeric movement calculation. Bounded ranges
    // instead map horizontal pixels across the min/max slider range. The route function below
    // supports both paths and recalculates the requested magnitude whenever the ladder rung changes.
    const NumericType MinSliderValue = SpinBox->GetMinSliderValue();
    const NumericType MaxSliderValue = SpinBox->GetMaxSliderValue();
    const NumericType NumericLowest = std::numeric_limits<NumericType>::lowest();
    const NumericType NumericMax = std::numeric_limits<NumericType>::max();

    const bool bBoundedSliderRange =
        MinSliderValue != NumericLowest
        && MaxSliderValue != NumericMax
        && MaxSliderValue > MinSliderValue;

    const double MinSliderAsDouble = static_cast<double>(MinSliderValue);
    const double MaxSliderAsDouble = static_cast<double>(MaxSliderValue);
    const double MinValueAsDouble = static_cast<double>(SpinBox->GetMinValue());
    const double MaxValueAsDouble = static_cast<double>(SpinBox->GetMaxValue());
    const double SliderRange = bBoundedSliderRange
        ? MaxSliderAsDouble - MinSliderAsDouble
        : 0.0;

    if (bBoundedSliderRange && (!FMath::IsFinite(SliderRange) || SliderRange <= 0.0))
    {
        return false;
    }

    // SSpinBox::GetDefaultStepSize in UE 5.8.3 uses this exact test,
    // including for integer spin boxes. The routed event carries no modifiers:
    // Digit itself applies its advertised Shift/Ctrl/Alt magnitude, avoiding
    // hidden host-specific multiplier settings and Alt dynamic-bound changes.
    const double NativeBaseStep = bBoundedSliderRange && SliderRange <= 10.0 ? 0.1 : 1.0;
    // A nonlinear bounded slider can have far less precision than its numeric
    // property. Ground Radius is float, slider 1..7000 and exponent 5: near
    // 6360, one representable float fill fraction is roughly 1.2 km. Passing
    // tiny virtual mouse deltas through that curve makes the value jump even
    // though the float property can represent hundredths and thousandths.
    // Bypass ONLY that lossy movement conversion, while keeping SSpinBox's
    // native drag begin/end, capture, callbacks, and property transaction.
    const bool bRouteNonlinearValuesDirectly = bBoundedSliderRange
        && !TIsIntegral<NumericType>::Value
        && !FMath::IsNearlyEqual(SliderExponent, 1.0);
    // Bounded *linear* sliders also quantize to float-filled fractions.
    // Take the precision-preserving route only when the finest selected
    // magnitude (including Ctrl and the optional Value Ladder) approaches
    // that float fraction's precision, leaving ordinary sliders native.
    const int32 FinestReachablePlace = bNonDropTimecode
        ? 0 : Hit.DigitPlace - (bUseLadder ? 4 : 1);
    const double FinestReachableStep = FMath::Pow(10.0, static_cast<double>(FinestReachablePlace));
    const bool bRoutePrecisionLimitedLinearValuesDirectly = bBoundedSliderRange
        && !TIsIntegral<NumericType>::Value
        && FMath::IsNearlyEqual(SliderExponent, 1.0)
        && FMath::IsFinite(FinestReachableStep)
        && FinestReachableStep > 0.0
        && FinestReachableStep <= SliderRange * static_cast<double>(std::numeric_limits<float>::epsilon()) * 2.0;

    // Integer SSpinBoxes lose large selected-place increments when their
    // fractional slider position is converted back to an integer. A rotator
    // is also not a bounded scalar: it should retain an unwrapped accumulator
    // while the displayed property cycles through +/-180 degrees.
    // Constrain rotator detection to the actual rotator widget hierarchy.
    const bool bRouteIntegersDirectly = TIsIntegral<NumericType>::Value;
    const bool bRouteRotatorsDirectly = Hit.bRotatorComponent && !TIsIntegral<NumericType>::Value;
    const bool bRouteValueDirectly = bNonDropTimecode
        || bRouteNonlinearValuesDirectly
        || bRoutePrecisionLimitedLinearValuesDirectly
        || bRouteIntegersDirectly || bRouteRotatorsDirectly;

    // Integer precision beyond 2^53 cannot be safely represented in the
    // double accumulator. Let Unreal's native input take over rather than
    // corrupt an int64/uint64 field through a lossy conversion.
    if constexpr (TIsIntegral<NumericType>::Value)
    {
        if (!DigitPrivate::CanSafelyScrubInteger(SpinBox->GetValue())
            || !DigitPrivate::CanSafelyScrubInteger(InitialDesiredStep))
        {
            return false;
        }
    }
    const TAttribute<NumericType> OriginalValueAttribute = SpinBox->GetValueAttribute();
    // The Sequencer/Control Rig/Niagara editors can update numeric values
    // outside this drag (key evaluation, rig evaluation, parameter refresh).
    // Observe their original source binding, not Digit's temporary literal.
    // Do not require immediate echo: Niagara may intentionally defer updates
    // until a commit, and a stale-but-unchanged source is not a conflict.
    const bool bMonitorSpecializedSource = Hit.Owner != ENumericOwner::General
        && OriginalValueAttribute.IsBound();
    const TSharedRef<bool> bDirectValueAssigned = MakeShared<bool>(false);
    const TWeakPtr<SSpinBox<NumericType>> WeakSpinBox = SpinBox;
    const TSharedRef<NumericType> WorkingDelta = MakeShared<NumericType>(NumericType(0));
    SpinBox->SetDelta(NumericType(0));

    ApplySelectedDeltaFunction = [WeakSpinBox, WorkingDelta]()
    {
        if (const TSharedPtr<SSpinBox<NumericType>> PinnedSpinBox = WeakSpinBox.Pin())
        {
            PinnedSpinBox->SetDelta(*WorkingDelta);
        }
    };

    // Shared between the arm path and cleanup, including capture loss.
    const TSharedRef<bool> bPinnedFractionalDisplayWidth = MakeShared<bool>(false);
    const int32 SavedMinFractionalDigits = SpinBox->GetMinFractionalDigits();
    RestoreDeltaFunction = [WeakSpinBox, OriginalDelta, OriginalValueAttribute, bDirectValueAssigned,
        bPinnedFractionalDisplayWidth, SavedMinFractionalDigits]()
    {
        if (const TSharedPtr<SSpinBox<NumericType>> PinnedSpinBox = WeakSpinBox.Pin())
        {
            // SetValue invokes UE 5.8.3 SSpinBox::CommitValue synchronously.
            // When AlwaysUsesDeltaSnap is enabled, CommitValue can snap even
            // CommittedViaCode values against the CURRENT Delta. Rebind while
            // Delta is zero so restoring a source attribute cannot re-quantize
            // the value that the editor has already committed on mouse-up.
            // Reinstate the host's original Delta only after the binding is
            // back. Never restore an unbound initial constant, which would
            // discard the edit instead of preserving the new native value.
            if (*bDirectValueAssigned && OriginalValueAttribute.IsBound())
            {
                PinnedSpinBox->SetDelta(NumericType(0));
                PinnedSpinBox->SetValue(OriginalValueAttribute);
            }
            PinnedSpinBox->SetDelta(OriginalDelta);
            if (*bPinnedFractionalDisplayWidth)
            {
                // Restoration runs after the host's native mouse-up commit,
                // not during it. Restore the exact original formatter minimum
                // (including when this numeric interface is shared by other
                // fields), so native trailing-zero trimming resumes.
                PinnedSpinBox->SetMinFractionalDigits(
                    TAttribute<TOptional<int32>>(TOptional<int32>(SavedMinFractionalDigits)));
            }
        }
    };

    // Repeat the temporal preflight at capture time. A Sequencer stepper may
    // change formatting between hover and mouse-down without changing its
    // widget identity.
    const bool bTemporalCandidate = Hit.Owner == ENumericOwner::SequencerValueStepper
        || Hit.bCurveKeyTimeField;
    const bool bStaleHostTemporalConversion = Hit.TemporalContext.IsSet()
        && !FDigitSequencerContext::IsStillCurrent(Hit.TemporalContext.GetValue());
    const bool bUnverifiedTemporalField = bTemporalCandidate
        && !Hit.TemporalContext.IsSet()
        && (Hit.bCurveKeyTimeField
            || DigitPrivate::IsDisplayedZero(Hit.DisplayString)
            || !DigitPrivate::IsUnconvertedNativeDisplay(
                Hit.DisplayString, static_cast<double>(SpinBox->GetValue())));
    if (bStaleHostTemporalConversion || bUnverifiedTemporalField)
    {
        // No temporary value binding has been installed yet. Leave the
        // native spin box and its host transaction in control.
        SpinBox->SetDelta(OriginalDelta);
        ApplySelectedDeltaFunction = nullptr;
        RestoreDeltaFunction = nullptr;
        return false;
    }

    const double InitialDigitValue = static_cast<double>(SpinBox->GetValue());
    const TOptional<double> ResolvedDisplayScale = bNonDropTimecode
        ? TOptional<double>(1.0)
        : Hit.TemporalContext.IsSet()
        ? TOptional<double>(Hit.TemporalContext.GetValue().NativePerDisplayedUnit)
        : DigitPrivate::ResolveDisplayToNativeScale(
            InitialDigitValue, Hit.DisplayString, Hit.bRotatorComponent,
            Hit.Owner == ENumericOwner::NiagaraParameter);
    if (!ResolvedDisplayScale.IsSet())
    {
        // A visible unit conversion is not always a simple ratio (and the
        // numeric TypeInterface cannot be retrieved from SSpinBox). Do not
        // pretend the hovered display digit is in native units.
        const UDigitSettings* Settings = UDigitSettings::Get();
        if (Settings && Settings->bLogWidgetDiagnostics)
        {
            UE_LOG(LogDigit, Warning,
                TEXT("Numeric scrub skipped: unavailable or ambiguous displayed-unit conversion for '%s' (native %.9g)"),
                *Hit.DisplayString, InitialDigitValue);
        }
        SpinBox->SetDelta(OriginalDelta);
        ApplySelectedDeltaFunction = nullptr;
        RestoreDeltaFunction = nullptr;
        return false;
    }
    const double DisplayScale = ResolvedDisplayScale.GetValue();
    const FString InitialUnitSuffix = DigitPrivate::GetNumericUnitSuffix(Hit.DisplayString);
    const TWeakPtr<STextBlock> WeakDisplayText = Hit.TextWidget;
    const int32 DisplayFractionDigits = bNonDropTimecode
        ? 0 : CountFractionalDigits(Hit.DisplayString, Hit.CharacterIndex);
    // Any floating-point spin box can trim insignificant trailing zeros (e.g.
    // 1.234570 becomes 1.23457). Doing that mid-drag deletes the selected
    // rightmost digit after a carry. Hold the original displayed fractional
    // width for this mouse-captured session, including values with native unit
    // suffixes, then restore the host formatter after native mouse-up. This
    // changes only presentation, never the stored numeric value.
    const int32 OriginalMinFractionalDigits = SpinBox->GetMinFractionalDigits();
    const int32 OriginalMaxFractionalDigits = SpinBox->GetMaxFractionalDigits();
    // This is a FLOATING-POINT display rule, not a Rotation rule. Location,
    // Scale, ordinary float/double properties and editor-specific numeric
    // spin boxes all lose the selected digit if a carry creates a trailing
    // zero and their numeric interface trims it while mouse capture is held.
    // Keep the starting fractional width for any eligible scalar spin box.
    // Never change integer or structured timecode presentation, and do not
    // exceed the host numeric formatter's existing maximum precision.
    const bool bPinFractionalDisplayWidth = !TIsIntegral<NumericType>::Value
        && !bNonDropTimecode
        && DisplayFractionDigits > OriginalMinFractionalDigits
        && DisplayFractionDigits <= OriginalMaxFractionalDigits;
    // Only clean up numerical dust when the starting rotator value was on
    // its visible decimal grid. Preserve off-grid source precision, including
    // values that were already extremely close to a whole degree.
    const int32 SafeRotatorDecimalPlaces = FMath::Clamp(DisplayFractionDigits, 0, 15);
    const double RotatorDisplayGrid = FMath::Pow(10.0, -static_cast<double>(SafeRotatorDecimalPlaces));
    const bool bCleanInitialRotatorDisplay = bRouteRotatorsDirectly
        && FMath::IsFinite(InitialDigitValue)
        && FMath::IsFinite(RotatorDisplayGrid)
        && RotatorDisplayGrid > 0.0
        && std::round(InitialDigitValue / RotatorDisplayGrid) * RotatorDisplayGrid == InitialDigitValue;
    // Native Delta snapping can keep numeric fields on their visible decimal grid.
    // Only do so when the current numeric value is already aligned to the grid;
    // otherwise do not discard lower digits the user had before dragging.

    // A display change can also happen while the mouse is stationary. A
    // changed unit, precision, or structured annotation invalidates the
    // initial place-value meaning. Check every captured post-tick and once
    // immediately before native mouse-up, not only on horizontal movement.
    VerifyActiveDisplayFunction = [
        this, WeakSpinBox, WeakDisplayText, InitialUnitSuffix,
        DisplayFractionDigits, WorkingDelta, bNonDropTimecode,
        TimecodeCharacterIndex = Hit.CharacterIndex,
        TemporalContext = Hit.TemporalContext]()
    {
        if (bActiveDigitScrubSuspended)
        {
            return;
        }
        const TSharedPtr<SSpinBox<NumericType>> Pinned = WeakSpinBox.Pin();
        const TSharedPtr<STextBlock> DisplayText = WeakDisplayText.Pin();
        if (!Pinned.IsValid() || !Pinned->HasMouseCapture() || !DisplayText.IsValid())
        {
            return;
        }
        const FString CurrentDisplay = DisplayText->GetText().ToString();
        const FString CurrentSuffix = DigitPrivate::GetNumericUnitSuffix(CurrentDisplay);
        const int32 CurrentFractionDigits =
            DigitPrivate::GetDisplayedFractionalDigitCount(CurrentDisplay);
        bool bUnsupportedDisplay = CurrentDisplay.IsEmpty()
            || CurrentDisplay.Contains(TEXT("..."))
            || (TemporalContext.IsSet()
                && !FDigitSequencerContext::IsStillCurrent(TemporalContext.GetValue()));
        if (bNonDropTimecode)
        {
            DigitTemporalTimecode::FDigitStep CurrentStep;
            bUnsupportedDisplay = bUnsupportedDisplay
                || !TemporalContext.IsSet()
                || !DigitTemporalTimecode::TryGetNonDropFrameDigitStep(
                    CurrentDisplay, TimecodeCharacterIndex,
                    TemporalContext.GetValue().TickResolution,
                    TemporalContext.GetValue().DisplayRate, CurrentStep)
                || CurrentStep.NativeTickStep
                    != TemporalContext.GetValue().NativeTimecodeTickStep;
        }
        else
        {
            bUnsupportedDisplay = bUnsupportedDisplay
                || DigitPrivate::HasStructuredTimeDisplay(CurrentDisplay)
                || !DigitPrivate::HasCanonicalNumericPrefix(CurrentDisplay)
                || CurrentFractionDigits != DisplayFractionDigits;
        }
        // A timecode's "suffix" would include its remaining components,
        // which naturally change as frames roll over. Only compare decimal
        // unit suffixes for ordinary numeric displays.
        if ((!bNonDropTimecode && CurrentSuffix != InitialUnitSuffix)
            || bUnsupportedDisplay)
        {
            bActiveDigitScrubSuspended = true;
            *WorkingDelta = NumericType(0);
            Pinned->SetDelta(NumericType(0));
            const UDigitSettings* Settings = UDigitSettings::Get();
            if (Settings && Settings->bLogWidgetDiagnostics)
            {
                UE_LOG(LogDigit, Warning,
                    TEXT("Numeric display changed during Digit scrub (unit '%s', current '%s'); holding value until release"),
                    *InitialUnitSuffix, *CurrentDisplay);
            }
        }
    };

    // The original host value must also be observed when NO mouse move
    // arrives. Keep the source/write history shared between the drag route,
    // captured post-tick and the pre-native mouse-up check. In UE 5.8,
    // SetValue(OriginalValueAttribute) dispatches OnValueChanged, so an
    // independent edit must be restored BEFORE SSpinBox's release commit,
    // never after its transaction has ended.
    const TSharedRef<NumericType> LastWrittenNative = MakeShared<NumericType>(SpinBox->GetValue());
    const TSharedRef<NumericType> LastSourceValue = MakeShared<NumericType>(SpinBox->GetValue());
    const TSharedRef<TArray<NumericType>> RecentDirectWrites = MakeShared<TArray<NumericType>>();
    if (bRouteValueDirectly && bMonitorSpecializedSource)
    {
        VerifyActiveSourceFunction = [
            this, WeakSpinBox, OriginalValueAttribute, bDirectValueAssigned,
            WorkingDelta, LastWrittenNative, LastSourceValue, RecentDirectWrites]()
        {
            if (!*bDirectValueAssigned)
            {
                return;
            }
            const TSharedPtr<SSpinBox<NumericType>> Pinned = WeakSpinBox.Pin();
            if (!Pinned.IsValid() || !Pinned->HasMouseCapture())
            {
                return;
            }
            const NumericType SourceValue = OriginalValueAttribute.Get();
            if (SourceValue == *LastSourceValue)
            {
                return;
            }
            *LastSourceValue = SourceValue;
            // Some editors report one of our previous values after a tick.
            // That is a delayed echo, not an independent source edit.
            if (SourceValue == *LastWrittenNative || RecentDirectWrites->Contains(SourceValue))
            {
                return;
            }

            bActiveDigitScrubSuspended = true;
            *WorkingDelta = NumericType(0);
            Pinned->SetDelta(NumericType(0));
            // Clear this before calling SetValue: UE's implementation invokes
            // the native OnValueChanged delegate synchronously and can rebuild
            // the owning widget hierarchy.
            *bDirectValueAssigned = false;
            Pinned->SetValue(OriginalValueAttribute);
            const UDigitSettings* Settings = UDigitSettings::Get();
            if (Settings && Settings->bLogWidgetDiagnostics)
            {
                UE_LOG(LogDigit, Warning,
                    TEXT("Specialized numeric source changed independently; cancelled pending Digit value before native commit"));
            }
        };
    }
    else
    {
        VerifyActiveSourceFunction = nullptr;
    }

    RouteActiveMoveFunction = [
        this,
        WeakSpinBox,
        WeakDisplayText,
        InitialUnitSuffix,
        WorkingDelta,
        bRouteIntegersDirectly,
        bRouteRotatorsDirectly,
        bRouteValueDirectly,
        bDirectValueAssigned,
        OriginalValueAttribute,
        bMonitorSpecializedSource,
        DisplayScale,
        DisplayFractionDigits,
        bNonDropTimecode,
        TimecodeNativeTickStep = bNonDropTimecode
            ? static_cast<double>(Hit.TemporalContext.GetValue().NativeTimecodeTickStep) : 0.0,
        bBoundedSliderRange,
        SliderRange,
        NativeBaseStep,
        SliderExponent,
        ScrubSensitivity,
        MinSliderAsDouble,
        MaxSliderAsDouble,
        MinValueAsDouble,
        MaxValueAsDouble,
        DigitValue = InitialDigitValue,
        LastObservedValue = InitialDigitValue,
        LastWrittenNative,
        RecentDirectWrites,
        InitialDigitValue,
        bCleanInitialRotatorDisplay,
        RotatorDisplayGrid,
        RotationAccumulatedChange = 0.0,
        RotationChangeCorrection = 0.0,
        TimecodeAccumulatedTickChange = 0.0,
        bLoggedPrecisionLimit = false](
            const FPointerEvent& MouseEvent,
            bool bScaleMovement,
            int32 DigitPlace,
            bool bSuppressHorizontalMovement) mutable -> bool
    {
        const TSharedPtr<SSpinBox<NumericType>> PinnedSpinBox = WeakSpinBox.Pin();
        if (!PinnedSpinBox.IsValid() || !PinnedSpinBox->HasMouseCapture())
        {
            return false;
        }

        NumericType DesiredStep = NumericType(0);
        if (bNonDropTimecode)
        {
            DesiredStep = static_cast<NumericType>(TimecodeNativeTickStep);
        }
        else if (!DigitPrivate::MakeStepForPlace<NumericType>(DigitPlace, DesiredStep))
        {
            return false;
        }

        // SPropertyEditorNumeric or a specialized editor may change the
        // displayed unit/format. Use the same guard as captured idle ticks
        // and pre-release, so stationary refreshes cannot bypass it.
        if (bScaleMovement && VerifyActiveDisplayFunction)
        {
            VerifyActiveDisplayFunction();
        }
        // Also checked on every post-tick and immediately before native
        // mouse-up, to catch source changes without a subsequent mouse move.
        if (bScaleMovement && VerifyActiveSourceFunction)
        {
            VerifyActiveSourceFunction();
        }
        if (bActiveDigitScrubSuspended)
        {
            // A stale native decimal grid must not snap the last real value
            // on mouse-up after the display unit changed.
            *WorkingDelta = NumericType(0);
            PinnedSpinBox->SetDelta(NumericType(0));
            // Consume the mouse movement so the unscaled event never reaches
            // SSpinBox a second time. Leave native capture/release untouched.
            return true;
        }

        FVector2D RoutedDelta = MouseEvent.GetCursorDelta();
        if (bSuppressHorizontalMovement)
        {
            RoutedDelta.X = 0.0f;
        }
        else if (bScaleMovement)
        {
            const double PhysicalDeltaX = static_cast<double>(MouseEvent.GetCursorDelta().X);
            const double InputMultiplier = DigitPrivate::GetDefaultInputEventMultiplier(MouseEvent);
            const double NativeStep = static_cast<double>(DesiredStep) * DisplayScale;
            const double RequestedChange = PhysicalDeltaX
                * NativeStep * InputMultiplier * ScrubSensitivity;

            // UE 5.8 may defer a mouse move's native commit to a widget Tick.
            // When the committed value becomes observable, rebase on it instead
            // of carrying forward a mathematically idealized target indefinitely.
            const double ObservedValue = static_cast<double>(PinnedSpinBox->GetValue());
            if (ObservedValue != LastObservedValue)
            {
                // An angle may normalize from +180 to -180 many times in a
                // single drag. Never rebase its continuous accumulator on the
                // normalized display value; this is what makes reversal and
                // multiple turns possible. Likewise, retain sub-integer
                // movement remainder rather than truncating it each tick.
                if (!bRouteValueDirectly && FMath::IsFinite(ObservedValue))
                {
                    DigitValue = ObservedValue;
                }
                LastObservedValue = ObservedValue;
            }

            // A native float/double cannot change by a step below its ULP.
            // Avoid injecting huge virtual mouse deltas for such changes.
            // Rotator's backing float lives in its *normalized* range, not in
            // the cumulative drag angle (which can grow through many turns).
            const double RepresentedValue = bRouteRotatorsDirectly ? ObservedValue : DigitValue;
            const double ApproxUlp = DigitPrivate::GetNumericResolution<NumericType>(RepresentedValue);
            // In a float-backed Rotator a one-ULP change is technically
            // representable but often cannot preserve the SELECTED decimal
            // place. For example at 75 degrees a float ULP is ~0.0000076:
            // attempting to edit 0.00001 produces 75.000007629..., which
            // can shorten the visible digits or invoke scientific/ellipsis
            // formatting. Require at least eight storage ULPs per selected
            // unmodified place, and leave finer digits native/unchanged.
            const bool bRotatorDigitBelowStoragePrecision = bRouteRotatorsDirectly
                && FMath::Abs(NativeStep) < ApproxUlp * 8.0;
            const bool bUnrepresentableStep = (!TIsIntegral<NumericType>::Value
                && FMath::Abs(NativeStep * InputMultiplier) < ApproxUlp * 0.50)
                || bRotatorDigitBelowStoragePrecision;
            if (bUnrepresentableStep)
            {
                RoutedDelta.X = 0.0;
                *WorkingDelta = NumericType(0);
                if (!bLoggedPrecisionLimit)
                {
                    bLoggedPrecisionLimit = true;
                    const UDigitSettings* Settings = UDigitSettings::Get();
                    if (Settings && Settings->bLogWidgetDiagnostics)
                    {
                        UE_LOG(LogDigit, Warning,
                            TEXT("Selected digit step %.9g cannot be represented near numeric value %.9g (native ULP approx %.9g); suppressing sub-resolution scrub"),
                            NativeStep, DigitValue, ApproxUlp);
                    }
                }
            }
            else
            {
                // Native CommitValue grid-snaps to Delta, but a fixed selected
                // place need not delete digits that were already present to its
                // right. Prefer the original display's fractional resolution.
                // Never snap converted values based on a rounded guessed scale.
                // Match the numeric type's actual decimal capacity. The old
                // hard-coded nine-digit cap needlessly snapped double fields
                // displaying 10-17 fractional digits to a coarser grid.
                // Values below the type's ULP still disable snapping below.
                constexpr int32 MaxPreciseDecimalDigits =
                    std::numeric_limits<NumericType>::max_digits10;
                const double GridStep = FMath::Min(
                    FMath::Abs(NativeStep * InputMultiplier),
                    FMath::Pow(10.0, -static_cast<double>(
                        FMath::Clamp(DisplayFractionDigits, 0, MaxPreciseDecimalDigits))));
                const double SnappedInitial = GridStep > 0.0
                    ? std::round(InitialDigitValue / GridStep) * GridStep
                    : InitialDigitValue;
                const bool bGridAligned = DisplayScale == 1.0 && GridStep > 0.0
                    && FMath::IsFinite(GridStep)
                    && FMath::Abs(SnappedInitial - InitialDigitValue)
                        <= FMath::Max(ApproxUlp * 2.0, GridStep * 0.00001);
                *WorkingDelta = bGridAligned && GridStep >= ApproxUlp * 0.50
                    ? static_cast<NumericType>(GridStep)
                    : NumericType(0);
            }
            const double SliderWidth = FMath::Max(
                static_cast<double>(PinnedSpinBox->GetTickSpaceGeometry().GetDrawSize().X), 100.0);

            if (bUnrepresentableStep || !FMath::IsFinite(RequestedChange)
                || !FMath::IsFinite(SliderWidth) || SliderWidth <= 0.0)
            {
                RoutedDelta.X = 0.0f;
            }
            else if (bNonDropTimecode)
            {
                // Keep non-drop timecode on the integral selected-digit tick
                // grid. Shift/Ctrl alter cursor sensitivity, never create a
                // fractional frame or bypass Sequencer's native mouse-up
                // commit. Sum movement from the original value so reversal
                // does not accumulate floating-point rounding error.
                TimecodeAccumulatedTickChange += RequestedChange;
                std::int64_t NativeTickOffset = 0;
                const bool bExactStep = DigitTemporalQuantize::TryWholeSelectedSteps(
                    TimecodeAccumulatedTickChange,
                    static_cast<std::int64_t>(TimecodeNativeTickStep),
                    NativeTickOffset);
                const double TargetNativeValue = InitialDigitValue
                    + static_cast<double>(NativeTickOffset);
                if (bExactStep && FMath::IsFinite(TargetNativeValue)
                    && TargetNativeValue >= static_cast<double>(MIN_int32)
                    && TargetNativeValue <= static_cast<double>(MAX_int32)
                    && TargetNativeValue >= MinValueAsDouble
                    && TargetNativeValue <= MaxValueAsDouble)
                {
                    const NumericType TargetNative = static_cast<NumericType>(TargetNativeValue);
                    if (TargetNative != *LastWrittenNative)
                    {
                        PinnedSpinBox->SetDelta(NumericType(0));
                        PinnedSpinBox->SetValue(TAttribute<NumericType>(TargetNative));
                        *LastWrittenNative = TargetNative;
                        *bDirectValueAssigned = true;
                        if (bMonitorSpecializedSource)
                        {
                            RecentDirectWrites->Add(TargetNative);
                            if (RecentDirectWrites->Num() > 8)
                            {
                                RecentDirectWrites->RemoveAt(0);
                            }
                        }
                    }
                }
                RoutedDelta.X = 0.0f;
                *WorkingDelta = NumericType(0);
                return true;
            }
            else if (bRouteValueDirectly)
            {
                // Avoid the float-valued slider fraction for integers and
                // nonlinear fields. For rotator components, maintain an
                // UNWRAPPED value and normalize only what is sent to Unreal.
                // Never rebase on the normalized readback during a drag.
                // Decimal increments such as 0.1 cannot be represented
                // exactly in binary. Repeatedly adding them to the full
                // rotation angle introduces enough error to turn 76.0 into
                // 75.99999999999994 (shown by UE as "76.0..."). Accumulate
                // just the cursor-origin displacement with compensation,
                // then add the original angle once. Keep this entirely
                // separate from the integer and bounded-slider paths.
                double AccumulatedValue = DigitValue + RequestedChange;
                if (bRouteRotatorsDirectly)
                {
                    const double CorrectedChange = RequestedChange - RotationChangeCorrection;
                    const double NextSum = RotationAccumulatedChange + CorrectedChange;
                    RotationChangeCorrection = (NextSum - RotationAccumulatedChange) - CorrectedChange;
                    RotationAccumulatedChange = NextSum;
                    AccumulatedValue = InitialDigitValue + RotationAccumulatedChange;
                }
                double TargetValue = AccumulatedValue;
                if (bRouteRotatorsDirectly)
                {
                    // [-180, 180), repeatedly, in BOTH directions. Fmod is
                    // signed, so compensate for negative values explicitly.
                    TargetValue = FMath::Fmod(AccumulatedValue + 180.0, 360.0);
                    if (TargetValue < 0.0)
                    {
                        TargetValue += 360.0;
                    }
                    TargetValue -= 180.0;
                    TargetValue = DigitPrivate::StabilizeRotatorWholeDegree(
                        TargetValue, bCleanInitialRotatorDisplay);
                    TargetValue = DigitPrivate::StabilizeRotatorDecimalGrid(
                        TargetValue, bCleanInitialRotatorDisplay, RotatorDisplayGrid);
                    TargetValue = FMath::Clamp(TargetValue, MinValueAsDouble, MaxValueAsDouble);
                }
                else
                {
                    // Integer controls with a 0 minimum can use the type's
                    // numeric 'lowest()' as a real slider boundary. Detect
                    // those even when the ordinary bounded-range inference
                    // (used by float controls) cannot distinguish them.
                    const bool bIntegerHasSliderLimits = bRouteIntegersDirectly
                        && (MinSliderAsDouble != static_cast<double>(std::numeric_limits<NumericType>::lowest())
                            || MaxSliderAsDouble != static_cast<double>(std::numeric_limits<NumericType>::max()));
                    if (bBoundedSliderRange || bIntegerHasSliderLimits)
                    {
                        TargetValue = FMath::Clamp(TargetValue, MinSliderAsDouble, MaxSliderAsDouble);
                    }
                    TargetValue = FMath::Clamp(TargetValue, MinValueAsDouble, MaxValueAsDouble);
                }

                // Round explicitly instead of letting a fractional slider
                // fraction quantize a hundreds or tens-place integer change.
                // Retain the pre-rounding accumulator so low sensitivity does
                // not lose movement fractions on every physical mouse event.
                const double UnroundedTarget = TargetValue;
                if constexpr (TIsIntegral<NumericType>::Value)
                {
                    TargetValue = std::round(TargetValue);
                }

                // No lossy conversion to int64/uint64 outside the exactly
                // representable double integer range. Do not overwrite the
                // backing property with a rounded 64-bit value.
                const bool bUnsafeWideInteger = bRouteIntegersDirectly
                    && FMath::Abs(TargetValue) > 9007199254740991.0;
                if (FMath::IsFinite(TargetValue) && !bUnsafeWideInteger)
                {
                    // For rotators retain the continuous position even as the
                    // property displays -180..180. For integers retain any
                    // sub-integer cursor remainder while within hard limits.
                    if (bRouteRotatorsDirectly)
                    {
                        DigitValue = AccumulatedValue;
                    }
                    else if (bRouteIntegersDirectly)
                    {
                        DigitValue = UnroundedTarget;
                    }
                    else if (TargetValue == AccumulatedValue)
                    {
                        DigitValue = AccumulatedValue;
                    }
                    else
                    {
                        DigitValue = TargetValue;
                    }

                    const NumericType TargetNative = static_cast<NumericType>(TargetValue);
                    if (bRouteRotatorsDirectly)
                    {
                        // A float conversion can remove more of the selected
                        // digit than the UI can convey, despite a large enough
                        // nominal ULP step. Reject that write and preserve the
                        // native source until a coarser place is chosen.
                        const double StorageError = FMath::Abs(
                            static_cast<double>(TargetNative) - TargetValue);
                        const double SelectedPlaceTolerance = FMath::Abs(NativeStep) * 0.10;
                        const FNumberFormattingOptions FormattingOptions = FNumberFormattingOptions()
                            .SetUseGrouping(false)
                            .SetMinimumFractionalDigits(PinnedSpinBox->GetMinFractionalDigits())
                            .SetMaximumFractionalDigits(PinnedSpinBox->GetMaxFractionalDigits())
                            .SetIndicateNearlyInteger(true);
                        const FString Preview = FastDecimalFormat::NumberToString(
                            TargetNative, ExpressionParser::GetLocalizedNumberFormattingRules(),
                            FormattingOptions);
                        if (StorageError > SelectedPlaceTolerance
                            || Preview.Contains(TEXT("..."))
                            || Preview.Contains(TEXT("e+"), ESearchCase::IgnoreCase)
                            || Preview.Contains(TEXT("e-"), ESearchCase::IgnoreCase))
                        {
                            RoutedDelta.X = 0.0f;
                            *WorkingDelta = NumericType(0);
                            return true;
                        }
                    }
                    if (TargetNative != *LastWrittenNative)
                    {
                        PinnedSpinBox->SetDelta(NumericType(0));
                        PinnedSpinBox->SetValue(TAttribute<NumericType>(TargetNative));
                        *LastWrittenNative = TargetNative;
                        *bDirectValueAssigned = true;
                        if (bMonitorSpecializedSource)
                        {
                            RecentDirectWrites->Add(TargetNative);
                            if (RecentDirectWrites->Num() > 8)
                            {
                                RecentDirectWrites->RemoveAt(0);
                            }
                        }
                    }
                }
                RoutedDelta.X = 0.0f;
                *WorkingDelta = NumericType(0);
                // Bypassing synthetic mouse movement also avoids a deferred
                // slider-fraction commit overwriting the exact value.
                return true;
            }
            else if (bBoundedSliderRange)
            {
                // This is the inverse of UE 5.8 SSpinBox's bounded path:
                // Fraction += CursorDelta * NativeStep, then divide by width,
                // apply the inverse slider exponent, and interpolate bounds.
                // The native float fraction is inherently precision-limited.
                const double TargetValue = FMath::Clamp(
                    FMath::Clamp(DigitValue + RequestedChange, MinSliderAsDouble, MaxSliderAsDouble),
                    MinValueAsDouble, MaxValueAsDouble);
                const double CurrentFraction = FMath::Clamp(
                    (DigitValue - MinSliderAsDouble) / SliderRange, 0.0, 1.0);
                const double TargetFraction = FMath::Clamp(
                    (TargetValue - MinSliderAsDouble) / SliderRange, 0.0, 1.0);
                const double CurrentFilled = DigitPrivate::ApplySliderExponent(CurrentFraction, SliderExponent);
                const double TargetFilled = DigitPrivate::ApplySliderExponent(TargetFraction, SliderExponent);
                const double VirtualDelta = (TargetFilled - CurrentFilled) * SliderWidth / NativeBaseStep;
                if (FMath::IsFinite(VirtualDelta)
                    && FMath::Abs(VirtualDelta) <= static_cast<double>(std::numeric_limits<float>::max()))
                {
                    RoutedDelta.X = VirtualDelta;
                    DigitValue = TargetValue;
                }
                else
                {
                    RoutedDelta.X = 0.0f;
                }
            }
            else
            {
                // UE 5.8 SSpinBox's *unbounded* fallback ignores Delta
                // unless its private LinearDeltaSensitivity attribute is set.
                // The per-move zero Delta forces the public-value-based branch:
                // Internal += CursorDelta / Width * pow(max(abs(V),1), Exp).
                // Invert that exact scale instead of dividing by an imagined
                // pow(WorkingDelta, Exp), which previously caused huge jumps.
                const double CurrentMagnitude = FMath::Clamp(
                    FMath::Abs(DigitValue), 1.0,
                    static_cast<double>(std::numeric_limits<NumericType>::max()));
                const double NativeInfluence = FMath::Pow(CurrentMagnitude, SliderExponent);
                const double TargetValue = FMath::Clamp(
                    DigitValue + RequestedChange, MinValueAsDouble, MaxValueAsDouble);
                const double VirtualDelta = NativeInfluence > 0.0
                    ? (TargetValue - DigitValue) * SliderWidth / NativeInfluence
                    : 0.0;
                if (FMath::IsFinite(VirtualDelta)
                    && FMath::Abs(VirtualDelta) <= static_cast<double>(std::numeric_limits<float>::max()))
                {
                    RoutedDelta.X = VirtualDelta;
                    DigitValue = TargetValue;
                }
                else
                {
                    RoutedDelta.X = 0.0f;
                }
            }
        }

        // The native mouse-move modifier values are configurable per widget.
        // Strip them here: Digit has already applied its own advertised
        // Shift/Ctrl multipliers, while Alt must not resize slider limits.
        // This also lets native CommittedViaSpin respect its normal clamps.
        const FPointerEvent RoutedEvent(
            MouseEvent.GetUserIndex(),
            MouseEvent.GetPointerIndex(),
            MouseEvent.GetScreenSpacePosition(),
            MouseEvent.GetLastScreenSpacePosition(),
            RoutedDelta,
            MouseEvent.GetPressedButtons(),
            FModifierKeysState());

        // Force the native motion calculation onto the same unbounded branch
        // we inverted, regardless of this widget's private sensitivity setting.
        // The widget may commit later during its Tick, so install our working
        // grid AFTER computing the new InternalValue, before the deferred commit.
        PinnedSpinBox->SetDelta(NumericType(0));
        PinnedSpinBox->OnMouseMove(PinnedSpinBox->GetTickSpaceGeometry(), RoutedEvent);
        PinnedSpinBox->SetDelta(*WorkingDelta);
        return true;
    };

    if (bPinFractionalDisplayWidth)
    {
        // Do this only after the whole arm path succeeds. The native
        // type interface can be shared by the owner. Never overwrite its
        // maximum or leave the temporary minimum installed after capture.
        SpinBox->SetMinFractionalDigits(
            TAttribute<TOptional<int32>>(TOptional<int32>(DisplayFractionDigits)));
        *bPinnedFractionalDisplayWidth = true;
    }

    UE_LOG(LogDigit, Log,
        TEXT("Armed digit place %d%s: display step %.9g, native/display scale %.9g, original Delta %.9g, sensitivity %.3g, bounded %s, slider [%.9g, %.9g], exponent %.9g, direct route %s, rotator %s"),
        Hit.DigitPlace,
        bUseLadder ? TEXT(" with ladder") : TEXT(""),
        static_cast<double>(InitialDesiredStep),
        DisplayScale,
        static_cast<double>(OriginalDelta),
        ScrubSensitivity,
        bBoundedSliderRange ? TEXT("true") : TEXT("false"),
        MinSliderAsDouble,
        MaxSliderAsDouble,
        SliderExponent,
        bRouteValueDirectly ? TEXT("true") : TEXT("false"),
        bRouteRotatorsDirectly ? TEXT("true") : TEXT("false"));

    return true;
}

void FDigitInputProcessor::UpdateHoverHighlight(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
    FNumericHit Hit;
    if (!FindNumericHit(SlateApp, MouseEvent, Hit))
    {
        ClearHighlight();
        return;
    }

    if (HighlightTextWidget.Pin() == Hit.TextWidget
        && HighlightCharacterIndex == Hit.CharacterIndex
        && HighlightDisplayString == Hit.DisplayString)
    {
        return;
    }

    FWidgetPath Path = SlateApp.LocateWindowUnderMouse(
        MouseEvent.GetScreenSpacePosition(),
        SlateApp.GetInteractiveTopLevelWindows(),
        false,
        MouseEvent.GetUserIndex());

    if (!Path.IsValid())
    {
        ClearHighlight();
        return;
    }

    ShowHighlight(SlateApp, Path, Hit);
}

void FDigitInputProcessor::RefreshActiveHighlight(FSlateApplication& SlateApp)
{
    const TSharedPtr<STextBlock> TextWidget = ActiveTextWidget.Pin();
    const TSharedPtr<SWidget> SpinBoxWidget = ActiveSpinBox.Pin();
    if (!TextWidget.IsValid() || !SpinBoxWidget.IsValid())
    {
        ClearHighlight();
        return;
    }

    const FString DisplayString = TextWidget->GetText().ToString();
    const int32 HighlightPlace = bLadderModeActive ? LadderOriginDigitPlace : ActiveDigitPlace;
    int32 CharacterIndex = INDEX_NONE;
    if (bActiveNonDropTimecode)
    {
        CharacterIndex = HighlightPlace;
        if (!DisplayString.IsValidIndex(CharacterIndex)
            || !DigitPrivate::IsDigitCharacter(DisplayString[CharacterIndex])
            || !DisplayString.StartsWith(TEXT("[")))
        {
            ClearHighlight();
            return;
        }
    }
    else if (!FindCharacterIndexForDigitPlace(DisplayString, HighlightPlace, CharacterIndex))
    {
        // The selected place can temporarily disappear, for example the hundreds place
        // when scrubbing 123 down below 100. Keep the place locked and hide only the marker.
        ClearHighlight();
        return;
    }

    if (HighlightTextWidget.Pin() == TextWidget
        && HighlightCharacterIndex == CharacterIndex
        && HighlightDisplayString == DisplayString)
    {
        return;
    }

    FWidgetPath Path;
    if (!SlateApp.GeneratePathToWidgetUnchecked(TextWidget.ToSharedRef(), Path))
    {
        ClearHighlight();
        return;
    }

    FNumericHit Hit;
    Hit.SpinBoxWidget = SpinBoxWidget;
    Hit.TextWidget = TextWidget;
    Hit.TextGeometry = TextWidget->GetTickSpaceGeometry();
    Hit.CharacterIndex = CharacterIndex;
    Hit.DigitPlace = HighlightPlace;
    Hit.DisplayString = DisplayString;

    ShowHighlight(SlateApp, Path, Hit);
}

void FDigitInputProcessor::ShowHighlight(FSlateApplication& SlateApp, const FWidgetPath& Path, const FNumericHit& Hit)
{
    if (!Hit.SpinBoxWidget.IsValid() || !Hit.TextWidget.IsValid() || !Hit.DisplayString.IsValidIndex(Hit.CharacterIndex))
    {
        ClearHighlight();
        return;
    }

    const TSharedPtr<SWindow> Window = SlateApp.FindWidgetWindow(Hit.SpinBoxWidget.ToSharedRef());
    if (!Window.IsValid() || !Window->HasOverlay() || Path.Widgets.Num() == 0)
    {
        // SWindow::AddOverlaySlot ensures on windows without an overlay.
        // A missing overlay is normal for some temporary Slate windows.
        ClearHighlight();
        return;
    }

    const FSlateFontInfo Font = Hit.TextWidget->GetFont();
    const TSharedRef<FSlateFontMeasure> FontMeasure = SlateApp.GetRenderer()->GetFontMeasureService();

    const FText DisplayText = FText::FromString(Hit.DisplayString);
    float CharacterStartX = 0.0f;
    float CharacterWidth = 0.0f;
    if (!DigitPrivate::FindCharacterVisualBounds(FontMeasure, Hit.DisplayString, Font, Hit.CharacterIndex, CharacterStartX, CharacterWidth))
    {
        ClearHighlight();
        return;
    }

    const float TextHeight = FMath::Max(1.0f, FontMeasure->Measure(DisplayText, Font).Y);

    const float CharacterLocalY = FMath::Max(0.0f, (Hit.TextGeometry.GetLocalSize().Y - TextHeight) * 0.5f);
    const FVector2D CharacterAbsolutePosition = Hit.TextGeometry.LocalToAbsolute(FVector2D(CharacterStartX, CharacterLocalY));

    const FGeometry& WindowGeometry = Path.Widgets[0].Geometry;
    const FVector2D CharacterWindowLocal = WindowGeometry.AbsoluteToLocal(CharacterAbsolutePosition);

    ClearHighlight();

    // Put Digit's increment help on the exact text widget only while a digit is
    // highlighted. Preserve and restore any tooltip the widget already owned.
    OriginalToolTip = Hit.TextWidget->GetToolTip();
    DigitToolTipWidget = Hit.TextWidget;
    if (DigitPrivate::HasStructuredTimeDisplay(Hit.DisplayString))
    {
        // The decimal-tooltip magnitudes and Value Ladder do not describe
        // hours/minutes/seconds/frames. Preserve the native tooltip instead.
    }
    else
    {
        Hit.TextWidget->SetToolTipText(
            DigitPrivate::MakeDigitToolTip(Hit.DisplayString, Hit.DigitPlace));
    }

    // The nested text widget gets first chance to answer the cursor query. Keep its
    // persistent override synchronized with the current interaction state so rebuilding
    // the highlight as the value changes cannot flip the cursor back to Default mid-drag.
    SetDigitCursorOverride(
        Hit.TextWidget,
        ActiveSpinBox.IsValid() && bDigitDragStarted
            ? EMouseCursor::ResizeLeftRight
            : EMouseCursor::Default);

    const UDigitSettings* Settings = UDigitSettings::Get();
    const FLinearColor HighlightColor = Settings
        ? Settings->DigitHighlightColor
        : FLinearColor(0.0f, 0.162029f, 0.745404f, 0.38f);
    const FLinearColor HighlightTextColor = Settings
        ? Settings->HighlightedTextColor
        : FLinearColor::White;

    FString HighlightedDigit;
    HighlightedDigit.AppendChar(Hit.DisplayString[Hit.CharacterIndex]);

    TSharedRef<SBox> Marker =
        SNew(SBox)
        .Visibility(EVisibility::HitTestInvisible)
        .WidthOverride(CharacterWidth)
        .HeightOverride(TextHeight)
        [
            SNew(SOverlay)

            + SOverlay::Slot()
            [
                SNew(SBorder)
                .Visibility(EVisibility::HitTestInvisible)
                .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                .BorderBackgroundColor(HighlightColor)
                .Padding(0.0f)
            ]

            + SOverlay::Slot()
            .HAlign(HAlign_Fill)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Visibility(EVisibility::HitTestInvisible)
                .Text(FText::FromString(HighlightedDigit))
                .Font(Font)
                .ColorAndOpacity(HighlightTextColor)
                .Justification(ETextJustify::Center)
            ]
        ];

    Window->AddOverlaySlot(10000)
        .HAlign(HAlign_Left)
        .VAlign(VAlign_Top)
        .Padding(FMargin(CharacterWindowLocal.X, CharacterWindowLocal.Y, 0.0f, 0.0f))
        [
            Marker
        ];

    HighlightWindow = Window;
    HighlightWidget = Marker;
    HighlightSpinBoxWidget = Hit.SpinBoxWidget;
    HighlightTextWidget = Hit.TextWidget;
    HighlightCharacterIndex = Hit.CharacterIndex;
    HighlightDigitPlace = Hit.DigitPlace;
    HighlightDisplayString = Hit.DisplayString;
    HighlightSourceRotator = Hit.bRotatorComponent;
    HighlightSourceCurveTime = Hit.bCurveKeyTimeField;
    HighlightSourceOwner = Hit.Owner;
    HighlightSourceTemporalContext = Hit.TemporalContext;
}

void FDigitInputProcessor::UpdateActiveDragOutline(FSlateApplication& SlateApp, EActiveDragDirection Direction)
{
    if (Direction == EActiveDragDirection::None)
    {
        ClearActiveDragOutline();
        return;
    }

    const TSharedPtr<SWidget> SpinBoxWidget = ActiveSpinBox.Pin();
    if (!SpinBoxWidget.IsValid())
    {
        ClearActiveDragOutline();
        return;
    }

    // If only the value is changing, the field geometry remains stable. Keep the existing
    // overlay until the drag direction flips so we do not churn Slate widgets every mouse move.
    if (ActiveDragOutlineWidget.IsValid() && ActiveDragDirection == Direction)
    {
        return;
    }

    const TSharedPtr<SWindow> Window = SlateApp.FindWidgetWindow(SpinBoxWidget.ToSharedRef());
    if (!Window.IsValid() || !Window->HasOverlay())
    {
        ClearActiveDragOutline();
        return;
    }

    FWidgetPath Path;
    if (!SlateApp.GeneratePathToWidgetUnchecked(SpinBoxWidget.ToSharedRef(), Path) || Path.Widgets.Num() == 0)
    {
        ClearActiveDragOutline();
        return;
    }

    const FGeometry& FieldGeometry = SpinBoxWidget->GetTickSpaceGeometry();
    const FVector2D FieldSize = FieldGeometry.GetLocalSize();
    if (FieldSize.X <= 0.0f || FieldSize.Y <= 0.0f)
    {
        ClearActiveDragOutline();
        return;
    }

    const FVector2D FieldAbsolutePosition = FieldGeometry.LocalToAbsolute(FVector2D::ZeroVector);
    const FGeometry& WindowGeometry = Path.Widgets[0].Geometry;
    const FVector2D FieldWindowLocal = WindowGeometry.AbsoluteToLocal(FieldAbsolutePosition);

    const FLinearColor OutlineColor = Direction == EActiveDragDirection::Increasing
        ? DigitPrivate::IncreaseOutlineColor
        : DigitPrivate::DecreaseOutlineColor;

    ClearActiveDragOutline();

    constexpr float OutlineThickness = 1.0f;
    const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));

    TSharedRef<SBox> OutlineRoot =
        SNew(SBox)
        .Visibility(EVisibility::HitTestInvisible)
        .WidthOverride(FieldSize.X)
        .HeightOverride(FieldSize.Y)
        [
            SNew(SOverlay)

            + SOverlay::Slot()
            .HAlign(HAlign_Fill)
            .VAlign(VAlign_Top)
            [
                SNew(SBox)
                .HeightOverride(OutlineThickness)
                [
                    SNew(SBorder)
                    .BorderImage(WhiteBrush)
                    .BorderBackgroundColor(OutlineColor)
                    .Padding(0.0f)
                ]
            ]

            + SOverlay::Slot()
            .HAlign(HAlign_Fill)
            .VAlign(VAlign_Bottom)
            [
                SNew(SBox)
                .HeightOverride(OutlineThickness)
                [
                    SNew(SBorder)
                    .BorderImage(WhiteBrush)
                    .BorderBackgroundColor(OutlineColor)
                    .Padding(0.0f)
                ]
            ]

            + SOverlay::Slot()
            .HAlign(HAlign_Left)
            .VAlign(VAlign_Fill)
            [
                SNew(SBox)
                .WidthOverride(OutlineThickness)
                [
                    SNew(SBorder)
                    .BorderImage(WhiteBrush)
                    .BorderBackgroundColor(OutlineColor)
                    .Padding(0.0f)
                ]
            ]

            + SOverlay::Slot()
            .HAlign(HAlign_Right)
            .VAlign(VAlign_Fill)
            [
                SNew(SBox)
                .WidthOverride(OutlineThickness)
                [
                    SNew(SBorder)
                    .BorderImage(WhiteBrush)
                    .BorderBackgroundColor(OutlineColor)
                    .Padding(0.0f)
                ]
            ]
        ];

    Window->AddOverlaySlot(10001)
        .HAlign(HAlign_Left)
        .VAlign(VAlign_Top)
        .Padding(FMargin(FieldWindowLocal.X, FieldWindowLocal.Y, 0.0f, 0.0f))
        [
            OutlineRoot
        ];

    ActiveDragOutlineWindow = Window;
    ActiveDragOutlineWidget = OutlineRoot;
    ActiveDragDirection = Direction;
}

void FDigitInputProcessor::ClearActiveDragOutline()
{
    if (ActiveDragOutlineWidget.IsValid())
    {
        if (const TSharedPtr<SWindow> Window = ActiveDragOutlineWindow.Pin())
        {
            if (Window->HasOverlay())
            {
                Window->RemoveOverlaySlot(ActiveDragOutlineWidget.ToSharedRef());
            }
        }
    }

    ActiveDragOutlineWindow.Reset();
    ActiveDragOutlineWidget.Reset();
    ActiveDragDirection = EActiveDragDirection::None;
}

void FDigitInputProcessor::ClearHighlight()
{
    if (const TSharedPtr<SWidget> ToolTipWidget = DigitToolTipWidget.Pin())
    {
        ToolTipWidget->SetToolTip(TAttribute<TSharedPtr<IToolTip>>(OriginalToolTip));
    }
    DigitToolTipWidget.Reset();
    OriginalToolTip.Reset();

    if (HighlightWidget.IsValid())
    {
        if (const TSharedPtr<SWindow> Window = HighlightWindow.Pin())
        {
            if (Window->HasOverlay())
            {
                Window->RemoveOverlaySlot(HighlightWidget.ToSharedRef());
            }
        }
    }

    HighlightWindow.Reset();
    HighlightWidget.Reset();
    HighlightSpinBoxWidget.Reset();
    HighlightTextWidget.Reset();
    HighlightCharacterIndex = INDEX_NONE;
    HighlightDigitPlace = 0;
    HighlightDisplayString.Reset();
    HighlightSourceRotator = false;
    HighlightSourceCurveTime = false;
    HighlightSourceOwner = ENumericOwner::General;
    HighlightSourceTemporalContext.Reset();

    if (!ActiveSpinBox.IsValid())
    {
        ClearDigitCursorOverride();
    }
}

void FDigitInputProcessor::ShowLadderFromCurrentHighlight(FSlateApplication& SlateApp)
{
    if (bLadderModeActive || DigitPrivate::HasStructuredTimeDisplay(HighlightDisplayString))
    {
        return;
    }

    const TSharedPtr<SWidget> SpinBoxWidget = HighlightSpinBoxWidget.Pin();
    const TSharedPtr<STextBlock> TextWidget = HighlightTextWidget.Pin();
    if (!SpinBoxWidget.IsValid()
        || !TextWidget.IsValid()
        || !HighlightDisplayString.IsValidIndex(HighlightCharacterIndex))
    {
        ClearLadder();
        return;
    }

    FNumericHit Hit;
    Hit.SpinBoxWidget = SpinBoxWidget;
    Hit.TextWidget = TextWidget;
    Hit.TextGeometry = TextWidget->GetTickSpaceGeometry();
    Hit.CharacterIndex = HighlightCharacterIndex;
    Hit.DigitPlace = HighlightDigitPlace;
    Hit.DisplayString = HighlightDisplayString;
    Hit.bRotatorComponent = HighlightSourceRotator;
    Hit.bCurveKeyTimeField = HighlightSourceCurveTime;
    Hit.Owner = HighlightSourceOwner;
    Hit.TemporalContext = HighlightSourceTemporalContext;
    ShowLadder(SlateApp, Hit);
}

void FDigitInputProcessor::ShowLadder(FSlateApplication& SlateApp, const FNumericHit& Hit)
{
    if (!Hit.SpinBoxWidget.IsValid() || !Hit.TextWidget.IsValid()
        || DigitPrivate::HasStructuredTimeDisplay(Hit.DisplayString))
    {
        ClearLadder();
        return;
    }

    const TSharedPtr<SWindow> Window = SlateApp.FindWidgetWindow(Hit.SpinBoxWidget.ToSharedRef());
    if (!Window.IsValid() || !Window->HasOverlay())
    {
        ClearLadder();
        return;
    }

    if (LadderWidget.IsValid())
    {
        // During a single Alt hold / toggle, the origin must be immutable.
        // A later hit on another digit must not move the preview or re-center it.
        return;
    }

    FWidgetPath Path;
    if (!SlateApp.GeneratePathToWidgetUnchecked(Hit.SpinBoxWidget.ToSharedRef(), Path)
        || Path.Widgets.Num() == 0)
    {
        ClearLadder();
        return;
    }

    const FGeometry& FieldGeometry = Hit.SpinBoxWidget->GetTickSpaceGeometry();
    const FVector2D FieldSize = FieldGeometry.GetLocalSize();
    if (FieldSize.X <= 0.0f || FieldSize.Y <= 0.0f)
    {
        ClearLadder();
        return;
    }

    constexpr float LadderWidth = 72.0f;
    constexpr float LadderRowHeight = 17.0f;
    constexpr float LadderOuterPadding = 6.0f;
    constexpr float LadderGap = 6.0f;
    constexpr float WindowMargin = 4.0f;
    constexpr float LadderHeight = (LadderRowHeight * 9.0f) + LadderOuterPadding;

    const FGeometry& WindowGeometry = Path.Widgets[0].Geometry;
    const FVector2D WindowSize = WindowGeometry.GetLocalSize();
    const FVector2D FieldAbsolutePosition = FieldGeometry.LocalToAbsolute(FVector2D::ZeroVector);
    const FVector2D FieldWindowLocal = WindowGeometry.AbsoluteToLocal(FieldAbsolutePosition);

    // Prefer the left side of the field so the ladder stays clear of Slate's standard
    // tooltip placement, which typically occupies the cursor's lower-right quadrant.
    // Only fall back to the right when the left side does not have enough room.
    const float LeftLadderX = FieldWindowLocal.X - LadderWidth - LadderGap;
    const float RightLadderX = FieldWindowLocal.X + FieldSize.X + LadderGap;

    float LadderX = LeftLadderX;
    if (LeftLadderX < WindowMargin)
    {
        LadderX = RightLadderX;
    }
    LadderX = FMath::Clamp(LadderX, WindowMargin, FMath::Max(WindowMargin, WindowSize.X - LadderWidth - WindowMargin));

    const float FieldCenterY = FieldWindowLocal.Y + (FieldSize.Y * 0.5f);
    float LadderY = FieldCenterY - (LadderHeight * 0.5f);
    LadderY = FMath::Clamp(LadderY, WindowMargin, FMath::Max(WindowMargin, WindowSize.Y - LadderHeight - WindowMargin));

    if (!bLadderModeActive)
    {
        LadderOriginDigitPlace = Hit.DigitPlace;
        LadderSelectedDigitPlace = Hit.DigitPlace;
        LadderVerticalTravel = 0.0f;
        bLadderIntegral = IsIntegralSpinBox(Hit.SpinBoxWidget);
    }

    // A preview stores only weak widget references. Unlike owning an SSpinBox,
    // this cannot prevent Details/Sequencer tabs from being destroyed.
    LadderSourceSpinBox = Hit.SpinBoxWidget;
    LadderSourceText = Hit.TextWidget;
    bLadderSourceRotator = Hit.bRotatorComponent;
    bLadderSourceCurveTime = Hit.bCurveKeyTimeField;
    LadderSourceOwner = Hit.Owner;
    LadderSourceTemporalContext = Hit.TemporalContext;
    LadderHoveredDigitPlace = INDEX_NONE;
    LadderWindow = Window;
    LadderSpinBoxIdentity = Hit.SpinBoxWidget.Get();
    LadderWindowPosition = FVector2D(LadderX, LadderY);
    RebuildLadderVisual();
}

void FDigitInputProcessor::RebuildLadderVisual()
{
    const TSharedPtr<SWindow> Window = LadderWindow.Pin();
    if (!Window.IsValid() || !Window->HasOverlay())
    {
        ClearLadder();
        return;
    }

    // Release processor ownership before calling back into Slate. Widget
    // teardown may synchronously process more input or invalidate descendants.
    const TSharedPtr<SWidget> PreviousWidget = MoveTemp(LadderWidget);
    for (int32 Index = 0; Index < 9; ++Index)
    {
        LadderRowBackgrounds[Index].Reset();
        LadderRowLabels[Index].Reset();
    }
    if (PreviousWidget.IsValid())
    {
        Window->RemoveOverlaySlot(PreviousWidget.ToSharedRef());
    }

    const UDigitSettings* Settings = UDigitSettings::Get();
    FLinearColor AccentColor = Settings
        ? Settings->DigitHighlightColor
        : FLinearColor(0.0f, 0.162029f, 0.745404f, 0.38f);
    AccentColor.A = FMath::Max(AccentColor.A, 0.62f);

    FLinearColor OriginColor = AccentColor;
    OriginColor.A = 1.0f;

    const FLinearColor PanelEdgeColor(0.13f, 0.13f, 0.13f, 1.0f);
    const FLinearColor PanelColor(0.025f, 0.025f, 0.025f, 0.98f);
    const FLinearColor NormalTextColor(0.78f, 0.78f, 0.78f, 1.0f);
    const FLinearColor DisabledTextColor(0.34f, 0.34f, 0.34f, 1.0f);
    const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));

    const TSharedPtr<STextBlock> SourceText = ActiveTextWidget.IsValid()
        ? ActiveTextWidget.Pin()
        : HighlightTextWidget.Pin();
    const FSlateFontInfo LadderFont = SourceText.IsValid()
        ? SourceText->GetFont()
        : FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9);

    TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);

    for (int32 RowIndex = 0; RowIndex < 9; ++RowIndex)
    {
        const int32 DigitPlace = LadderOriginDigitPlace + 4 - RowIndex;
        const bool bValidPlace = !bLadderIntegral || DigitPlace >= 0;
        const bool bSelected = DigitPlace == LadderSelectedDigitPlace;
        const bool bOrigin = DigitPlace == LadderOriginDigitPlace;

        const bool bHovered = IsLadderRowSelectionEnabled()
            && DigitPlace == LadderHoveredDigitPlace && bValidPlace;
        FLinearColor HoverColor = AccentColor;
        HoverColor.A *= 0.48f;
        const FLinearColor RowBackground = bHovered
            ? (bSelected ? OriginColor : HoverColor)
            : (bSelected && bValidPlace ? AccentColor : FLinearColor::Transparent);
        const FLinearColor TextColor = bValidPlace
            ? ((bSelected || bHovered) ? FLinearColor::White : NormalTextColor)
            : DisabledTextColor;
        const FLinearColor MarkerColor = bOrigin ? OriginColor : FLinearColor::Transparent;

        TSharedPtr<SBorder> Background;
        TSharedPtr<STextBlock> Label;
        Rows->AddSlot()
        .AutoHeight()
        [
            SNew(SBox)
            .HeightOverride(17.0f)
            [
                SAssignNew(Background, SBorder)
                .BorderImage(WhiteBrush)
                .BorderBackgroundColor(RowBackground)
                .OnMouseButtonDown_Lambda([this, DigitPlace](const FGeometry& RowGeometry, const FPointerEvent& Event)
                {
                    return HandleLadderRowMouseDown(RowGeometry, Event, DigitPlace);
                })
                .Padding(FMargin(4.0f, 0.0f, 5.0f, 0.0f))
                [
                    SNew(SHorizontalBox)

                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    [
                        SNew(SBox)
                        .WidthOverride(2.0f)
                        .HeightOverride(9.0f)
                        [
                            SNew(SBorder)
                            .BorderImage(WhiteBrush)
                            .BorderBackgroundColor(MarkerColor)
                            .Padding(0.0f)
                        ]
                    ]

                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .HAlign(HAlign_Right)
                    .VAlign(VAlign_Center)
                    .Padding(FMargin(5.0f, 0.0f, 0.0f, 0.0f))
                    [
                        SAssignNew(Label, STextBlock)
                        .Text(FText::FromString(DigitPrivate::FormatMagnitudeForLadder(DigitPlace)))
                        .Font(LadderFont)
                        .ColorAndOpacity(TextColor)
                        .Justification(ETextJustify::Right)
                    ]
                ]
            ]
        ];
        LadderRowBackgrounds[RowIndex] = Background;
        LadderRowLabels[RowIndex] = Label;
    }

    TSharedRef<SBox> LadderRoot =
        SNew(SBox)
        .Visibility(IsLadderToggleEnabled() || IsLadderRowSelectionEnabled()
            ? EVisibility::Visible : EVisibility::HitTestInvisible)
        .WidthOverride(72.0f)
        [
            SNew(SBorder)
            .BorderImage(WhiteBrush)
            .BorderBackgroundColor(PanelEdgeColor)
            .Padding(1.0f)
            [
                SNew(SBorder)
                .BorderImage(WhiteBrush)
                .BorderBackgroundColor(PanelColor)
                .Padding(2.0f)
                [
                    Rows
                ]
            ]
        ];

    Window->AddOverlaySlot(10002)
        .HAlign(HAlign_Left)
        .VAlign(VAlign_Top)
        .Padding(FMargin(LadderWindowPosition.X, LadderWindowPosition.Y, 0.0f, 0.0f))
        [
            LadderRoot
        ];

    LadderWidget = LadderRoot;
}

void FDigitInputProcessor::RefreshLadderRowVisuals()
{
    // Do not remove and re-add the overlay in response to pointer movement.
    // The previous implementation could destroy a widget while Slate was
    // dispatching input through that same overlay, and invalidate hit tests.
    const UDigitSettings* Settings = UDigitSettings::Get();
    FLinearColor AccentColor = Settings
        ? Settings->DigitHighlightColor
        : FLinearColor(0.0f, 0.162029f, 0.745404f, 0.38f);
    AccentColor.A = FMath::Max(AccentColor.A, 0.62f);
    FLinearColor HoverColor = AccentColor;
    HoverColor.A *= 0.48f;
    const FLinearColor NormalTextColor(0.78f, 0.78f, 0.78f, 1.0f);
    const FLinearColor DisabledTextColor(0.34f, 0.34f, 0.34f, 1.0f);

    for (int32 Index = 0; Index < 9; ++Index)
    {
        const int32 Place = LadderOriginDigitPlace + 4 - Index;
        const bool bValid = !bLadderIntegral || Place >= 0;
        const bool bSelected = Place == LadderSelectedDigitPlace;
        const bool bHovered = IsLadderRowSelectionEnabled()
            && Place == LadderHoveredDigitPlace && bValid;
        const FLinearColor RowColor = bHovered
            ? (bSelected ? FLinearColor(AccentColor.R, AccentColor.G, AccentColor.B, 1.0f) : HoverColor)
            : (bSelected && bValid ? AccentColor : FLinearColor::Transparent);
        if (const TSharedPtr<SBorder> Border = LadderRowBackgrounds[Index].Pin())
        {
            Border->SetBorderBackgroundColor(FSlateColor(RowColor));
        }
        if (const TSharedPtr<STextBlock> Label = LadderRowLabels[Index].Pin())
        {
            Label->SetColorAndOpacity(FSlateColor(bValid
                ? ((bSelected || bHovered) ? FLinearColor::White : NormalTextColor)
                : DisabledTextColor));
        }
    }
}

void FDigitInputProcessor::ClearLadder()
{
    // Capture the actual owner and hold the overlay alive locally. Reset our
    // state BEFORE RemoveOverlaySlot: removing an overlay can synchronously
    // tear down Slate children or re-enter Digit's input processor.
    const TSharedPtr<SWindow> Window = LadderWindow.Pin();
    const TSharedPtr<SWidget> PreviousWidget = MoveTemp(LadderWidget);
    bLadderDismissQueued = false;
    for (int32 Index = 0; Index < 9; ++Index)
    {
        LadderRowBackgrounds[Index].Reset();
        LadderRowLabels[Index].Reset();
    }
    LadderWindow.Reset();
    LadderSpinBoxIdentity = nullptr;
    LadderWindowPosition = FVector2D::ZeroVector;

    bLadderToggledOn = false;
    bCtrlMagnitudeWasLocked = false;
    LadderHoveredDigitPlace = INDEX_NONE;
    LadderSourceSpinBox.Reset();
    LadderSourceText.Reset();
    bLadderSourceRotator = false;
    bLadderSourceCurveTime = false;
    LadderSourceOwner = ENumericOwner::General;
    LadderSourceTemporalContext.Reset();
    if (!bLadderModeActive)
    {
        LadderOriginDigitPlace = 0;
        LadderSelectedDigitPlace = 0;
        LadderVerticalTravel = 0.0f;
        bLadderIntegral = false;
    }

    // Closing a window can invalidate its overlay before we receive capture
    // loss. Never attempt removal from a different or overlay-less window.
    if (PreviousWidget.IsValid() && Window.IsValid() && Window->HasOverlay())
    {
        Window->RemoveOverlaySlot(PreviousWidget.ToSharedRef());
    }
}

bool FDigitInputProcessor::UpdateLadderSelection(const FPointerEvent& MouseEvent)
{
    if (!bLadderModeActive)
    {
        return false;
    }

    const UDigitSettings* Settings = UDigitSettings::Get();
    const bool bShouldLock = Settings && Settings->bLockLadderMagnitudeWithCtrl
        && MouseEvent.IsControlDown();
    if (bShouldLock)
    {
        bCtrlMagnitudeWasLocked = true;
        LadderVerticalTravel = 0.0f;
        return false;
    }
    if (bCtrlMagnitudeWasLocked)
    {
        // The first motion after Ctrl release becomes the new vertical origin.
        // No jump from movement accumulated during a locked drag.
        bCtrlMagnitudeWasLocked = false;
        LadderVerticalTravel = 0.0f;
        return false;
    }

    constexpr float LadderRowHeight = 17.0f;
    constexpr float RungThreshold = LadderRowHeight * 0.5f;

    LadderVerticalTravel +=
        static_cast<float>(MouseEvent.GetCursorDelta().Y) * ActiveLadderMagnitudeSensitivity;

    const int32 MaxPlace = LadderOriginDigitPlace + 4;
    const int32 MinPlace = bLadderIntegral
        ? FMath::Max(0, LadderOriginDigitPlace - 4)
        : LadderOriginDigitPlace - 4;

    bool bChanged = false;

    while (LadderVerticalTravel <= -RungThreshold && LadderSelectedDigitPlace < MaxPlace)
    {
        ++LadderSelectedDigitPlace;
        LadderVerticalTravel += LadderRowHeight;
        bChanged = true;
    }

    while (LadderVerticalTravel >= RungThreshold && LadderSelectedDigitPlace > MinPlace)
    {
        --LadderSelectedDigitPlace;
        LadderVerticalTravel -= LadderRowHeight;
        bChanged = true;
    }

    if (LadderSelectedDigitPlace >= MaxPlace)
    {
        LadderVerticalTravel = FMath::Max(LadderVerticalTravel, -RungThreshold);
    }
    if (LadderSelectedDigitPlace <= MinPlace)
    {
        LadderVerticalTravel = FMath::Min(LadderVerticalTravel, RungThreshold);
    }

    if (bChanged)
    {
        ActiveDigitPlace = LadderSelectedDigitPlace;
        ClearActiveDragOutline();
        RefreshLadderRowVisuals();
    }

    return bChanged;
}

bool FDigitInputProcessor::IsLadderToggleEnabled() const
{
    const UDigitSettings* Settings = UDigitSettings::Get();
    return Settings && Settings->bToggleLadderWithAlt;
}

bool FDigitInputProcessor::IsLadderRowSelectionEnabled() const
{
    const UDigitSettings* Settings = UDigitSettings::Get();
    return Settings && Settings->bSelectMagnitudeFromLadder;
}

bool FDigitInputProcessor::ShouldKeepLadderPreview() const
{
    return LadderWidget.IsValid()
        && LadderSourceSpinBox.IsValid()
        && LadderSourceText.IsValid()
        && ((IsLadderToggleEnabled() && bLadderToggledOn)
            || (!IsLadderToggleEnabled() && FSlateApplication::IsInitialized()
                && FSlateApplication::Get().GetModifierKeys().IsAltDown()));
}

bool FDigitInputProcessor::IsOverLadder(const FVector2D& ScreenPosition, int32* OutDigitPlace) const
{
    if (OutDigitPlace)
    {
        *OutDigitPlace = INDEX_NONE;
    }
    const TSharedPtr<SWindow> Window = LadderWindow.Pin();
    if (!Window.IsValid() || !Window->HasOverlay() || !LadderWidget.IsValid())
    {
        return false;
    }
    // The overlay's actual arranged geometry is authoritative. Reconstructing
    // it from SWindow geometry and slot padding produces incorrect hit tests
    // on DPI-scaled or inset editor windows. The overlay is no longer rebuilt
    // every time hover changes, so its geometry remains stable during clicks.
    const FGeometry& Geometry = LadderWidget->GetTickSpaceGeometry();
    const FVector2D Size = Geometry.GetLocalSize();
    if (Size.X <= 0.0f || Size.Y <= 0.0f)
    {
        return false;
    }
    const FVector2D Local = Geometry.AbsoluteToLocal(ScreenPosition);
    const float Width = Size.X;
    const float Height = Size.Y;
    if (Local.X < 0.0f || Local.X >= Width || Local.Y < 0.0f || Local.Y >= Height)
    {
        return false;
    }
    if (OutDigitPlace)
    {
        constexpr float ContentTop = 3.0f;
        constexpr float RowHeight = 17.0f;
        const int32 RowIndex = FMath::FloorToInt((Local.Y - ContentTop) / RowHeight);
        if (RowIndex >= 0 && RowIndex < 9)
        {
            const int32 Place = LadderOriginDigitPlace + 4 - RowIndex;
            if (!bLadderIntegral || Place >= 0)
            {
                *OutDigitPlace = Place;
            }
        }
    }
    return true;
}

void FDigitInputProcessor::UpdateLadderHover(const FVector2D& ScreenPosition)
{
    int32 Hovered = INDEX_NONE;
    if (IsLadderRowSelectionEnabled())
    {
        IsOverLadder(ScreenPosition, &Hovered);
    }
    if (Hovered != LadderHoveredDigitPlace)
    {
        LadderHoveredDigitPlace = Hovered;
        RefreshLadderRowVisuals();
    }
}

bool FDigitInputProcessor::IsOverLadderSourceField(
    FSlateApplication& SlateApp, const FVector2D& ScreenPosition) const
{
    const TSharedPtr<SWidget> Source = LadderSourceSpinBox.Pin();
    if (!LadderWidget.IsValid() || !Source.IsValid()
        || SlateApp.FindWidgetWindow(Source.ToSharedRef()) != LadderWindow.Pin())
    {
        return false;
    }
    const FGeometry& FieldGeometry = Source->GetTickSpaceGeometry();
    return FieldGeometry.IsUnderLocation(ScreenPosition);
}

bool FDigitInputProcessor::BuildLadderSourceHit(
    FSlateApplication& SlateApp, int32 DigitPlace, FNumericHit& OutHit) const
{
    const TSharedPtr<SWidget> Source = LadderSourceSpinBox.Pin();
    const TSharedPtr<STextBlock> TextWidget = LadderSourceText.Pin();
    if (!Source.IsValid() || !TextWidget.IsValid()
        || !Source->IsEnabled() || !TextWidget->IsEnabled()
        || SlateApp.FindWidgetWindow(Source.ToSharedRef()) != LadderWindow.Pin()
        || (bLadderIntegral && DigitPlace < 0)
        || (LadderSourceTemporalContext.IsSet()
            && !FDigitSequencerContext::IsStillCurrent(LadderSourceTemporalContext.GetValue())))
    {
        return false;
    }
    FWidgetPath FieldPath;
    if (!SlateApp.GeneratePathToWidgetUnchecked(Source.ToSharedRef(), FieldPath)
        || FieldPath.Widgets.Num() == 0)
    {
        return false;
    }
    const FString Display = TextWidget->GetText().ToString();
    if (DigitPrivate::HasStructuredTimeDisplay(Display)
        || Display.Contains(TEXT("..."))
        || !DigitPrivate::HasCanonicalNumericPrefix(Display))
    {
        return false;
    }

    int32 CharacterIndex = INDEX_NONE;
    if (!FindCharacterIndexForDigitPlace(Display, LadderOriginDigitPlace, CharacterIndex))
    {
        for (int32 Index = 0; Index < Display.Len(); ++Index)
        {
            if (DigitPrivate::IsDigitCharacter(Display[Index]))
            {
                CharacterIndex = Index;
                break;
            }
        }
    }
    if (CharacterIndex == INDEX_NONE)
    {
        return false;
    }
    OutHit.SpinBoxWidget = Source;
    OutHit.TextWidget = TextWidget;
    OutHit.TextGeometry = TextWidget->GetTickSpaceGeometry();
    OutHit.CharacterIndex = CharacterIndex;
    OutHit.DigitPlace = DigitPlace;
    OutHit.DisplayString = Display;
    OutHit.bRotatorComponent = bLadderSourceRotator;
    OutHit.bCurveKeyTimeField = bLadderSourceCurveTime;
    OutHit.Owner = LadderSourceOwner;
    OutHit.TemporalContext = LadderSourceTemporalContext;
    return true;
}

FReply FDigitInputProcessor::HandleLadderRowMouseDown(
    const FGeometry& RowGeometry, const FPointerEvent& MouseEvent, int32 DigitPlace)
{
    if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton
        || HasActiveSession() || bRestorePending)
    {
        return FReply::Handled();
    }
    FSlateApplication& SlateApp = FSlateApplication::Get();
    FNumericHit Hit;
    if (!BuildLadderSourceHit(SlateApp, DigitPlace, Hit))
    {
        // The source may have been destroyed or its temporal context changed.
        // Never route a click to a stale SSpinBox. Defer removing the live
        // overlay until Slate has finished dispatching this row callback.
        bLadderDismissQueued = true;
        return FReply::Handled();
    }
    const TSharedPtr<SWidget> Source = Hit.SpinBoxWidget;

    // This callback runs as part of the REAL ladder-row Slate mouse-down,
    // NOT from IInputProcessor and NOT from OnPostTick. Passing the native
    // SSpinBox's reply back to Slate lets the ordinary input dispatch apply
    // capture and route subsequent move/up events to the original field.
    // No nested RoutePointerDownEvent/ProcessReply or deferred fake click.
    if (!ArmSpinBox(Hit, MouseEvent.GetPointerIndex(), true))
    {
        return FReply::Handled();
    }
    ActiveUserIndex = MouseEvent.GetUserIndex();
    ActiveDigitPlace = DigitPlace;
    LadderSelectedDigitPlace = DigitPlace;
    LadderVerticalTravel = 0.0f;
    RefreshLadderRowVisuals();

    const FReply NativeReply = Source->OnMouseButtonDown(
        Source->GetTickSpaceGeometry(), MouseEvent);
    const UDigitSettings* Settings = UDigitSettings::Get();
    if (Settings && Settings->bLogWidgetDiagnostics)
    {
        UE_LOG(LogDigit, Log,
            TEXT("Ladder row mouse-down at digit place %d: native spin box %s (capture reply pending)"),
            DigitPlace, NativeReply.IsEventHandled() ? TEXT("handled") : TEXT("unhandled"));
    }
    if (!NativeReply.IsEventHandled())
    {
        bLadderDismissQueued = true;
        QueueRestore(true);
        return FReply::Handled();
    }
    return NativeReply;
}

bool FDigitInputProcessor::IsIntegralSpinBox(const TSharedPtr<SWidget>& Widget) const
{
    if (!Widget.IsValid())
    {
        return false;
    }

    const TSharedRef<SWidget> WidgetRef = Widget.ToSharedRef();
    return DigitPrivate::IsWidgetClass<uint64>(WidgetRef)
        || DigitPrivate::IsWidgetClass<uint32>(WidgetRef)
        || DigitPrivate::IsWidgetClass<uint16>(WidgetRef)
        || DigitPrivate::IsWidgetClass<uint8>(WidgetRef)
        || DigitPrivate::IsWidgetClass<int64>(WidgetRef)
        || DigitPrivate::IsWidgetClass<int32>(WidgetRef)
        || DigitPrivate::IsWidgetClass<int16>(WidgetRef)
        || DigitPrivate::IsWidgetClass<int8>(WidgetRef);
}

bool FDigitInputProcessor::HasActiveSession() const
{
    // The weak widget may already be destroyed. Its validity alone must not
    // determine whether there is still a temporary Delta/binding to restore.
    return ActivePointerIndex != INDEX_NONE || static_cast<bool>(RestoreDeltaFunction);
}

void FDigitInputProcessor::QueueRestore(bool bPreserveLadderPreview)
{
    // A normal mouse-up should leave the preview anchored while Alt remains
    // held (or a toggle is still open). Capture loss / teardown never does.
    if (!bPreserveLadderPreview || !ShouldKeepLadderPreview())
    {
        ClearLadder();
    }
    // Any path that ends an active interaction, including lost mouse capture, must
    // transition the persistent cursor override out of the drag state before cleanup.
    bDigitDragStarted = false;
    ApplyDigitCursor();
    ClearActiveDragOutline();

    if (HasActiveSession())
    {
        if (!bRestorePending)
        {
            PendingRestorePostTicks = 0;
        }
        bRestorePending = true;
    }
}

void FDigitInputProcessor::RestoreActiveDelta()
{
    ClearActiveDragOutline();

    // Restoring a bound value attribute can synchronously notify its owning
    // editor. Clear ALL session state before invoking the callback so an input
    // re-entry cannot run an old restore or be cleared as a newly armed scrub.
    TFunction<void()> RestoreFunction = MoveTemp(RestoreDeltaFunction);
    ApplySelectedDeltaFunction = nullptr;
    RestoreDeltaFunction = nullptr;
    RouteActiveMoveFunction = nullptr;
    VerifyActiveSourceFunction = nullptr;
    VerifyActiveDisplayFunction = nullptr;
    ActiveSpinBox.Reset();
    ActiveTextWidget.Reset();
    ActivePointerIndex = INDEX_NONE;
    ActiveUserIndex = INDEX_NONE;
    ActiveDigitPlace = 0;
    PhysicalDragDistance = 0.0f;
    bDigitDragStarted = false;
    bActiveDigitScrubSuspended = false;
    bRestorePending = false;
    PendingRestorePostTicks = 0;
    PendingCapturePostTicks = 0;
    bAwaitingNativeCapture = false;
    bActiveSpinBoxIsIntegral = false;
    bActiveNonDropTimecode = false;
    bLadderModeActive = false;
    bCtrlMagnitudeWasLocked = false;
    if (!LadderWidget.IsValid())
    {
        bLadderIntegral = false;
        LadderOriginDigitPlace = 0;
        LadderSelectedDigitPlace = 0;
    }
    LadderVerticalTravel = 0.0f;
    ActiveLadderMagnitudeSensitivity = 0.5f;
    if (RestoreFunction)
    {
        RestoreFunction();
    }
}

bool FDigitInputProcessor::IsSupportedSpinBox(const TSharedRef<SWidget>& Widget) const
{
    return DigitPrivate::IsScrubbableSpinBox<double>(Widget)
        || DigitPrivate::IsScrubbableSpinBox<float>(Widget)
        || DigitPrivate::IsScrubbableSpinBox<uint64>(Widget)
        || DigitPrivate::IsScrubbableSpinBox<uint32>(Widget)
        || DigitPrivate::IsScrubbableSpinBox<uint16>(Widget)
        || DigitPrivate::IsScrubbableSpinBox<uint8>(Widget)
        || DigitPrivate::IsScrubbableSpinBox<int64>(Widget)
        || DigitPrivate::IsScrubbableSpinBox<int32>(Widget)
        || DigitPrivate::IsScrubbableSpinBox<int16>(Widget)
        || DigitPrivate::IsScrubbableSpinBox<int8>(Widget);
}
