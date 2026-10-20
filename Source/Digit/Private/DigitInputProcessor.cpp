// Copyright Mippithedork 2026, Inc. All Rights Reserved.

#include "DigitInputProcessor.h"
#include "DigitSettings.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Layout/Children.h"
#include "Widgets/SWidget.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "Internationalization/FastDecimalFormat.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#include <limits>
#include <cmath>

DEFINE_LOG_CATEGORY_STATIC(LogDigit, Log, All);

namespace DigitPrivate
{
    // Match Origin's default Parent / Deparent hierarchy feedback colors so
    // direction communication stays consistent across the Facet tools.
    static const FLinearColor IncreaseOutlineColor(0.0f, 0.162029f, 0.745404f, 0.700000f);
    static const FLinearColor DecreaseOutlineColor(0.745404f, 0.228181f, 0.0f, 0.700000f);

    // Always read the number locale, not the editor language. Region & Language
    // can configure those independently (for example English UI and German numbers).
    static const FDecimalNumberFormattingRules& GetNumberRules()
    {
        return FInternationalization::Get().GetCurrentLocale()->GetDecimalNumberFormattingRules();
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

    static double ResolveDisplayToNativeScale(double NativeValue, const FString& Text)
    {
        double DisplayValue = 0.0;
        if (!ParseDisplayedNumber(Text, DisplayValue)
            || FMath::Abs(DisplayValue) < 1.e-8 || !FMath::IsFinite(NativeValue))
        {
            return 1.0;
        }
        const double Scale = NativeValue / DisplayValue;
        if (!FMath::IsFinite(Scale) || Scale <= 0.0)
        {
            return 1.0;
        }
        // Display roundoff should not masquerade as an exotic unit conversion.
        if (Scale > 0.9 && Scale < 1.1)
        {
            return 1.0;
        }
        // Avoid huge conversions derived from near-zero rounded displays.
        if (Scale < 1.e-9 || Scale > 1.e9)
        {
            return 1.0;
        }
        return Scale;
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
        return &Widget->GetWidgetClass() == &GetRuntimeSpinBoxClass<NumericType>();
    }

    static TSharedPtr<STextBlock> FindTextUnderSpinBox(
        const TSharedRef<SWidget>& Widget,
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
            const FString Text = TextWidget->GetText().ToString();
            bool bHasDigit = false;
            for (TCHAR Character : Text)
            {
                if (IsDigitCharacter(Character))
                {
                    bHasDigit = true;
                    break;
                }
            }
            if (bHasDigit)
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
                Children->GetChildAt(Index), ScreenPosition, RemainingDepth - 1))
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
    if (ActiveSpinBox.IsValid() && !bRestorePending)
    {
        const TSharedPtr<SWidget> PinnedSpinBox = ActiveSpinBox.Pin();
        if (!PinnedSpinBox.IsValid())
        {
            QueueRestore();
            return;
        }

        if (bAwaitingNativeCapture)
        {
            if (PinnedSpinBox->HasMouseCapture())
            {
                bAwaitingNativeCapture = false;
            }
            return;
        }

        if (!PinnedSpinBox->HasMouseCapture())
        {
            QueueRestore();
        }
    }
}

bool FDigitInputProcessor::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
    if (InKeyEvent.GetKey() == EKeys::Escape && ActiveSpinBox.IsValid())
    {
        QueueRestore();
        return false;
    }

    if ((InKeyEvent.GetKey() == EKeys::LeftAlt || InKeyEvent.GetKey() == EKeys::RightAlt)
        && !ActiveSpinBox.IsValid()
        && !bRestorePending)
    {
        ShowLadderFromCurrentHighlight(SlateApp);
    }

    return false;
}

bool FDigitInputProcessor::HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
    if ((InKeyEvent.GetKey() == EKeys::LeftAlt || InKeyEvent.GetKey() == EKeys::RightAlt)
        && !bLadderModeActive)
    {
        ClearLadder();
    }

    return false;
}

bool FDigitInputProcessor::HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
    if (ActiveSpinBox.IsValid() && !bRestorePending)
    {
        const TSharedPtr<SWidget> PinnedSpinBox = ActiveSpinBox.Pin();
        if (!PinnedSpinBox.IsValid())
        {
            QueueRestore();
            return false;
        }

        if (!PinnedSpinBox->HasMouseCapture())
        {
            // Mouse-down is preprocessed before SSpinBox receives it. If capture has not
            // happened yet, allow normal Slate routing for this event instead of consuming it.
            return false;
        }

        bAwaitingNativeCapture = false;

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

        if (bScaleMovement
            && bRouted
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
        UpdateHoverHighlight(SlateApp, MouseEvent);

        if (MouseEvent.IsAltDown())
        {
            ShowLadderFromCurrentHighlight(SlateApp);
        }
        else
        {
            ClearLadder();
        }
    }

    return false;
}

bool FDigitInputProcessor::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
    if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || ActiveSpinBox.IsValid())
    {
        return false;
    }

    FNumericHit Hit;
    if (!FindNumericHit(SlateApp, MouseEvent, Hit))
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
                    TEXT("Unresolved numeric click (locale=%s, decimal U+%04X, group U+%04X). Widget path: %s"),
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

    const bool bUseLadder = MouseEvent.IsAltDown();
    if (ArmSpinBox(Hit, MouseEvent.GetPointerIndex(), bUseLadder))
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

        if (bUseLadder)
        {
            ShowLadder(SlateApp, Hit);
        }
        else
        {
            ClearLadder();
        }
    }

    return false;
}

bool FDigitInputProcessor::HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
    if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton
        && ActiveSpinBox.IsValid()
        && MouseEvent.GetPointerIndex() == ActivePointerIndex)
    {
        // Preserve Delta=0 during the native final commit and deferred Tick.
        // Restoring the original Delta early would re-snap material values.
        if (ApplySelectedDeltaFunction)
        {
            ApplySelectedDeltaFunction();
        }
        QueueRestore();
    }

    return false;
}

void FDigitInputProcessor::HandlePostSlateTick()
{
    if (bRestorePending)
    {
        // SSpinBox 5.8 defers mouse-move commits to its own Tick. Keep Delta=0
        // through one subsequent widget tick so a pending value is not snapped
        // to the field's original Delta after mouse-up. This also preserves
        // native OnEndSliderMovement and undo transaction ownership.
        ClearHighlight();
        if (++PendingRestorePostTicks >= 2)
        {
            RestoreActiveDelta();
        }
        return;
    }

    if (ActiveSpinBox.IsValid() && ActiveTextWidget.IsValid() && FSlateApplication::IsInitialized())
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

    const TSharedRef<SWidget>& SpinWidget = Path.Widgets[SpinBoxPathIndex].Widget;
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
            SpinWidget, MouseEvent.GetScreenSpacePosition());
        if (TextWidget.IsValid())
        {
            TextGeometry = TextWidget->GetTickSpaceGeometry();
        }
    }
    if (!TextWidget.IsValid())
    {
        return false;
    }

    const FText DisplayText = TextWidget->GetText();
    const FString DisplayString = DisplayText.ToString();
    if (DisplayString.IsEmpty())
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
    if (!ResolveDigitPlace(DisplayString, CharacterIndex, DigitPlace))
    {
        return false;
    }

    // Identify actual rotator component widgets from the Slate ancestry.
    // A degree suffix alone is not sufficient: light cone angles and other
    // scalar angle properties can have real hard limits and must not wrap.
    bool bIsRotatorComponent = false;
    for (int32 Index = 0; Index < SpinBoxPathIndex; ++Index)
    {
        if (Path.Widgets[Index].Widget->GetTypeAsString().Contains(TEXT("RotatorInputBox")))
        {
            bIsRotatorComponent = true;
            break;
        }
    }

    OutHit.SpinBoxWidget = SpinWidget;
    OutHit.bRotatorComponent = bIsRotatorComponent;
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
        PhysicalDragDistance = 0.0f;
        bDigitDragStarted = false;
        bRestorePending = false;
        PendingRestorePostTicks = 0;
        bAwaitingNativeCapture = true;
        bLadderModeActive = bUseLadder;
        LadderOriginDigitPlace = Hit.DigitPlace;
        LadderSelectedDigitPlace = Hit.DigitPlace;
        LadderVerticalTravel = 0.0f;

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

    NumericType InitialDesiredStep = NumericType(0);
    if (!DigitPrivate::MakeStepForPlace<NumericType>(Hit.DigitPlace, InitialDesiredStep))
    {
        return false;
    }

    const TSharedRef<SSpinBox<NumericType>> SpinBox = StaticCastSharedRef<SSpinBox<NumericType>>(WidgetRef);
    bActiveSpinBoxIsIntegral = TIsIntegral<NumericType>::Value;
    bLadderIntegral = bActiveSpinBoxIsIntegral;

    if (!SpinBox->GetEnableSlider())
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

    // Integer SSpinBoxes lose large selected-place increments when their
    // fractional slider position is converted back to an integer. A rotator
    // is also not a bounded scalar: it should retain an unwrapped accumulator
    // while the displayed property cycles through +/-180 degrees.
    // Constrain rotator detection to the actual rotator widget hierarchy.
    const bool bRouteIntegersDirectly = TIsIntegral<NumericType>::Value;
    const bool bRouteRotatorsDirectly = Hit.bRotatorComponent && !TIsIntegral<NumericType>::Value;
    const bool bRouteValueDirectly = bRouteNonlinearValuesDirectly
        || bRouteIntegersDirectly || bRouteRotatorsDirectly;

    // Integer precision beyond 2^53 cannot be safely represented in the
    // double accumulator. Let Unreal's native input take over rather than
    // corrupt an int64/uint64 field through a lossy conversion.
    if constexpr (TIsIntegral<NumericType>::Value)
    {
        const double SafeIntegerLimit = 9007199254740991.0;
        if (FMath::Abs(static_cast<double>(SpinBox->GetValue())) > SafeIntegerLimit
            || static_cast<double>(InitialDesiredStep) > SafeIntegerLimit)
        {
            return false;
        }
    }
    const TAttribute<NumericType> OriginalValueAttribute = SpinBox->GetValueAttribute();
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

    RestoreDeltaFunction = [WeakSpinBox, OriginalDelta, OriginalValueAttribute, bDirectValueAssigned]()
    {
        if (const TSharedPtr<SSpinBox<NumericType>> PinnedSpinBox = WeakSpinBox.Pin())
        {
            PinnedSpinBox->SetDelta(OriginalDelta);
            // SetValue with a literal temporarily replaces the widget's bound
            // value attribute. Restore the original binding after the mouse-up
            // and deferred native ticks have completed. Never restore an
            // unbound initial constant: that would discard the edited value.
            if (*bDirectValueAssigned && OriginalValueAttribute.IsBound())
            {
                PinnedSpinBox->SetValue(OriginalValueAttribute);
            }
        }
    };

    const double InitialDigitValue = static_cast<double>(SpinBox->GetValue());
    const double DisplayScale = DigitPrivate::ResolveDisplayToNativeScale(
        InitialDigitValue, Hit.DisplayString);
    const int32 DisplayFractionDigits = CountFractionalDigits(Hit.DisplayString, Hit.CharacterIndex);
    // Native Delta snapping can keep numeric fields on their visible decimal grid.
    // Only do so when the current numeric value is already aligned to the grid;
    // otherwise do not discard lower digits the user had before dragging.

    RouteActiveMoveFunction = [
        WeakSpinBox,
        WorkingDelta,
        bRouteIntegersDirectly,
        bRouteRotatorsDirectly,
        bRouteValueDirectly,
        bDirectValueAssigned,
        DisplayScale,
        DisplayFractionDigits,
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
        LastWrittenNative = SpinBox->GetValue(),
        InitialDigitValue,
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
        if (!DigitPrivate::MakeStepForPlace<NumericType>(DigitPlace, DesiredStep))
        {
            return false;
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
            const double ApproxUlp = static_cast<double>(std::numeric_limits<NumericType>::epsilon())
                * FMath::Max(FMath::Abs(RepresentedValue), 1.0);
            const bool bUnrepresentableStep = !TIsIntegral<NumericType>::Value
                && FMath::Abs(NativeStep * InputMultiplier) < ApproxUlp * 0.50;
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
                const double GridStep = FMath::Min(
                    FMath::Abs(NativeStep * InputMultiplier),
                    FMath::Pow(10.0, -static_cast<double>(FMath::Clamp(DisplayFractionDigits, 0, 9))));
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
            else if (bRouteValueDirectly)
            {
                // Avoid the float-valued slider fraction for integers and
                // nonlinear fields. For rotator components, maintain an
                // UNWRAPPED value and normalize only what is sent to Unreal.
                // Never rebase on the normalized readback during a drag.
                const double AccumulatedValue = DigitValue + RequestedChange;
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
                    if (TargetNative != LastWrittenNative)
                    {
                        PinnedSpinBox->SetDelta(NumericType(0));
                        PinnedSpinBox->SetValue(TAttribute<NumericType>(TargetNative));
                        LastWrittenNative = TargetNative;
                        *bDirectValueAssigned = true;
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
    if (!FindCharacterIndexForDigitPlace(DisplayString, HighlightPlace, CharacterIndex))
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
    if (!Window.IsValid() || Path.Widgets.Num() == 0)
    {
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
    Hit.TextWidget->SetToolTipText(DigitPrivate::MakeDigitToolTip(Hit.DisplayString, Hit.DigitPlace));

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
    if (!Window.IsValid())
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
            Window->RemoveOverlaySlot(ActiveDragOutlineWidget.ToSharedRef());
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
            Window->RemoveOverlaySlot(HighlightWidget.ToSharedRef());
        }
    }

    HighlightWindow.Reset();
    HighlightWidget.Reset();
    HighlightSpinBoxWidget.Reset();
    HighlightTextWidget.Reset();
    HighlightCharacterIndex = INDEX_NONE;
    HighlightDigitPlace = 0;
    HighlightDisplayString.Reset();

    if (!ActiveSpinBox.IsValid())
    {
        ClearDigitCursorOverride();
    }
}

void FDigitInputProcessor::ShowLadderFromCurrentHighlight(FSlateApplication& SlateApp)
{
    if (bLadderModeActive)
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
    ShowLadder(SlateApp, Hit);
}

void FDigitInputProcessor::ShowLadder(FSlateApplication& SlateApp, const FNumericHit& Hit)
{
    if (!Hit.SpinBoxWidget.IsValid() || !Hit.TextWidget.IsValid())
    {
        ClearLadder();
        return;
    }

    const TSharedPtr<SWindow> Window = SlateApp.FindWidgetWindow(Hit.SpinBoxWidget.ToSharedRef());
    if (!Window.IsValid())
    {
        ClearLadder();
        return;
    }

    if (!bLadderModeActive
        && LadderWidget.IsValid()
        && LadderSpinBoxWidget.Pin() == Hit.SpinBoxWidget
        && LadderOriginDigitPlace == Hit.DigitPlace)
    {
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

    LadderWindow = Window;
    LadderSpinBoxWidget = Hit.SpinBoxWidget;
    LadderWindowPosition = FVector2D(LadderX, LadderY);
    RebuildLadderVisual();
}

void FDigitInputProcessor::RebuildLadderVisual()
{
    const TSharedPtr<SWindow> Window = LadderWindow.Pin();
    if (!Window.IsValid())
    {
        ClearLadder();
        return;
    }

    if (LadderWidget.IsValid())
    {
        Window->RemoveOverlaySlot(LadderWidget.ToSharedRef());
        LadderWidget.Reset();
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

        const FLinearColor RowBackground = bSelected && bValidPlace
            ? AccentColor
            : FLinearColor::Transparent;
        const FLinearColor TextColor = bValidPlace
            ? (bSelected ? FLinearColor::White : NormalTextColor)
            : DisabledTextColor;
        const FLinearColor MarkerColor = bOrigin ? OriginColor : FLinearColor::Transparent;

        Rows->AddSlot()
        .AutoHeight()
        [
            SNew(SBox)
            .HeightOverride(17.0f)
            [
                SNew(SBorder)
                .BorderImage(WhiteBrush)
                .BorderBackgroundColor(RowBackground)
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
                        SNew(STextBlock)
                        .Text(FText::FromString(DigitPrivate::FormatMagnitudeForLadder(DigitPlace)))
                        .Font(LadderFont)
                        .ColorAndOpacity(TextColor)
                        .Justification(ETextJustify::Right)
                    ]
                ]
            ]
        ];
    }

    TSharedRef<SBox> LadderRoot =
        SNew(SBox)
        .Visibility(EVisibility::HitTestInvisible)
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

void FDigitInputProcessor::ClearLadder()
{
    if (LadderWidget.IsValid())
    {
        if (const TSharedPtr<SWindow> Window = LadderWindow.Pin())
        {
            Window->RemoveOverlaySlot(LadderWidget.ToSharedRef());
        }
    }

    LadderWindow.Reset();
    LadderWidget.Reset();
    LadderSpinBoxWidget.Reset();
    LadderWindowPosition = FVector2D::ZeroVector;

    if (!bLadderModeActive)
    {
        LadderOriginDigitPlace = 0;
        LadderSelectedDigitPlace = 0;
        LadderVerticalTravel = 0.0f;
        bLadderIntegral = false;
    }
}

bool FDigitInputProcessor::UpdateLadderSelection(const FPointerEvent& MouseEvent)
{
    if (!bLadderModeActive)
    {
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
        RebuildLadderVisual();
    }

    return bChanged;
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

void FDigitInputProcessor::QueueRestore()
{
    // Any path that ends an active interaction, including lost mouse capture, must
    // transition the persistent cursor override out of the drag state before cleanup.
    bDigitDragStarted = false;
    ApplyDigitCursor();
    ClearActiveDragOutline();
    ClearLadder();

    if (ActiveSpinBox.IsValid() && RestoreDeltaFunction)
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

    if (RestoreDeltaFunction)
    {
        RestoreDeltaFunction();
    }

    ApplySelectedDeltaFunction = nullptr;
    RestoreDeltaFunction = nullptr;
    RouteActiveMoveFunction = nullptr;
    ActiveSpinBox.Reset();
    ActiveTextWidget.Reset();
    ActivePointerIndex = INDEX_NONE;
    ActiveDigitPlace = 0;
    PhysicalDragDistance = 0.0f;
    bDigitDragStarted = false;
    bRestorePending = false;
    PendingRestorePostTicks = 0;
    bAwaitingNativeCapture = false;
    bActiveSpinBoxIsIntegral = false;
    bLadderModeActive = false;
    bLadderIntegral = false;
    LadderOriginDigitPlace = 0;
    LadderSelectedDigitPlace = 0;
    LadderVerticalTravel = 0.0f;
    ActiveLadderMagnitudeSensitivity = 0.5f;
}

bool FDigitInputProcessor::IsSupportedSpinBox(const TSharedRef<SWidget>& Widget) const
{
    return DigitPrivate::IsWidgetClass<double>(Widget)
        || DigitPrivate::IsWidgetClass<float>(Widget)
        || DigitPrivate::IsWidgetClass<uint64>(Widget)
        || DigitPrivate::IsWidgetClass<uint32>(Widget)
        || DigitPrivate::IsWidgetClass<uint16>(Widget)
        || DigitPrivate::IsWidgetClass<uint8>(Widget)
        || DigitPrivate::IsWidgetClass<int64>(Widget)
        || DigitPrivate::IsWidgetClass<int32>(Widget)
        || DigitPrivate::IsWidgetClass<int16>(Widget)
        || DigitPrivate::IsWidgetClass<int8>(Widget);
}
