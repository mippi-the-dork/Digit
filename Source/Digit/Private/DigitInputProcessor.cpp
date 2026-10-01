#include "DigitInputProcessor.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#include <limits>

DEFINE_LOG_CATEGORY_STATIC(LogDigit, Log, All);

namespace DigitPrivate
{
    static bool IsDigitCharacter(TCHAR Character)
    {
        return Character >= TEXT('0') && Character <= TEXT('9');
    }

    static bool IsNumericTokenCharacter(TCHAR Character)
    {
        return IsDigitCharacter(Character)
            || Character == TEXT('.')
            || Character == TEXT(',')
            || Character == TEXT('+')
            || Character == TEXT('-');
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
        // Do not compare against SSpinBox<NumericType>::StaticWidgetClass() directly here.
        // That accessor contains a function-local static and Digit lives in a different DLL
        // from Slate, so the address of the class-data object is not a reliable cross-module
        // identity test. A real SSpinBox probe dispatches GetWidgetClass() through the same
        // engine-side virtual implementation used by editor-created spin boxes.
        static const TSharedRef<SSpinBox<NumericType>> ProbeSpinBox =
            SNew(SSpinBox<NumericType>);

        return ProbeSpinBox->GetWidgetClass();
    }

    template<typename NumericType>
    bool IsWidgetClass(const TSharedRef<SWidget>& Widget)
    {
        if (Widget->GetType() != FName(TEXT("SSpinBox")))
        {
            return false;
        }

        return &Widget->GetWidgetClass() == &GetRuntimeSpinBoxClass<NumericType>();
    }
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

        // Input preprocessors see mouse-down before the native SSpinBox handles it.
        // Do not interpret the brief pre-capture window as lost capture. Wait until
        // the SSpinBox has captured the mouse at least once for this interaction.
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
    }

    return false;
}

bool FDigitInputProcessor::HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
    if (ActiveSpinBox.IsValid() && !bRestorePending)
    {
        // Reapply immediately before native Slate routing. This guarantees the
        // existing SSpinBox reads Digit's selected place value for this move,
        // while Unreal still owns direction, sensitivity, clamping and commits.
        if (ApplySelectedDeltaFunction)
        {
            ApplySelectedDeltaFunction();
        }

        if (const TSharedPtr<SWidget> PinnedSpinBox = ActiveSpinBox.Pin())
        {
            if (PinnedSpinBox->HasMouseCapture())
            {
                bAwaitingNativeCapture = false;
            }
        }
    }
    else if (!bRestorePending)
    {
        UpdateHoverHighlight(SlateApp, MouseEvent);
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
        FWidgetPath DebugPath = SlateApp.LocateWindowUnderMouse(
            MouseEvent.GetScreenSpacePosition(),
            SlateApp.GetInteractiveTopLevelWindows(),
            false,
            MouseEvent.GetUserIndex());

        FString PathTypes;
        for (int32 WidgetIndex = 0; WidgetIndex < DebugPath.Widgets.Num(); ++WidgetIndex)
        {
            const FArrangedWidget& ArrangedWidget = DebugPath.Widgets[WidgetIndex];
            if (!PathTypes.IsEmpty())
            {
                PathTypes += TEXT(" > ");
            }
            PathTypes += ArrangedWidget.Widget->GetTypeAsString();
        }

        UE_LOG(LogDigit, Warning, TEXT("Mouse-down did not resolve a numeric digit. Widget path: %s"), *PathTypes);
        ClearHighlight();
        return false;
    }

    if (ArmSpinBox(Hit, MouseEvent.GetPointerIndex()))
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
    }

    return false;
}

bool FDigitInputProcessor::HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
{
    if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton
        && ActiveSpinBox.IsValid()
        && MouseEvent.GetPointerIndex() == ActivePointerIndex)
    {
        // Mouse-up is also preprocessed before the native SSpinBox receives it.
        // Reapply once more so the native final GridSnap/commit sees Digit's Delta,
        // then restore on Slate post-tick after routing has completed.
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
        RestoreActiveDelta();
        ClearHighlight();
    }
}

void FDigitInputProcessor::Shutdown()
{
    RestoreActiveDelta();
    ClearHighlight();
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

        // Do not compare Slate class-data addresses across module boundaries.
        // GetType() is derived from the widget's engine-side runtime class data
        // and is stable for non-templated widgets like STextBlock.
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

    if (SpinBoxPathIndex == INDEX_NONE || TextPathIndex == INDEX_NONE)
    {
        return false;
    }

    // The text block must be inside the spin box, not an axis label or another nearby label.
    if (TextPathIndex <= SpinBoxPathIndex)
    {
        return false;
    }

    const TSharedRef<SWidget>& SpinWidget = Path.Widgets[SpinBoxPathIndex].Widget;
    const TSharedRef<SWidget>& TextWidgetBase = Path.Widgets[TextPathIndex].Widget;
    const TSharedRef<STextBlock> TextWidget = StaticCastSharedRef<STextBlock>(TextWidgetBase);
    const FGeometry& TextGeometry = Path.Widgets[TextPathIndex].Geometry;

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

    // Standard SNumericEntryBox / SSpinBox values are left-justified. Keep the first
    // prototype deliberately scoped to that native path instead of guessing custom layout.
    const float TextStartX = 0.0f;
    const float TextEndX = TextStartX + FullTextSize.X;
    if (LocalMouse.X < TextStartX || LocalMouse.X > TextEndX)
    {
        return false;
    }

    const int32 CharacterIndex = FontMeasure->FindCharacterIndexAtOffset(
        DisplayText,
        Font,
        FMath::RoundToInt(LocalMouse.X - TextStartX));

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

    OutHit.SpinBoxWidget = SpinWidget;
    OutHit.TextWidget = TextWidget;
    OutHit.TextGeometry = TextGeometry;
    OutHit.CharacterIndex = CharacterIndex;
    OutHit.DigitPlace = DigitPlace;
    OutHit.DisplayString = DisplayString;
    return true;
}

bool FDigitInputProcessor::ResolveDigitPlace(const FString& DisplayString, int32 CharacterIndex, int32& OutDigitPlace) const
{
    if (!DisplayString.IsValidIndex(CharacterIndex)
        || !DigitPrivate::IsDigitCharacter(DisplayString[CharacterIndex]))
    {
        return false;
    }

    int32 TokenStart = CharacterIndex;
    while (TokenStart > 0 && DigitPrivate::IsNumericTokenCharacter(DisplayString[TokenStart - 1]))
    {
        --TokenStart;
    }

    int32 TokenEnd = CharacterIndex;
    while (TokenEnd + 1 < DisplayString.Len() && DigitPrivate::IsNumericTokenCharacter(DisplayString[TokenEnd + 1]))
    {
        ++TokenEnd;
    }

    // Scientific notation is intentionally deferred for the first prototype.
    for (int32 Index = TokenStart; Index <= TokenEnd; ++Index)
    {
        if (DisplayString[Index] == TEXT('e') || DisplayString[Index] == TEXT('E'))
        {
            return false;
        }
    }

    // v0.1.0 uses Unreal's common en-US style: '.' is decimal and ',' is grouping.
    // This is deliberately isolated here so locale-aware decimal detection can replace it later.
    int32 DecimalIndex = INDEX_NONE;
    for (int32 Index = TokenStart; Index <= TokenEnd; ++Index)
    {
        if (DisplayString[Index] == TEXT('.'))
        {
            DecimalIndex = Index;
            break;
        }
    }

    if (DecimalIndex == INDEX_NONE || CharacterIndex < DecimalIndex)
    {
        const int32 RightBoundary = (DecimalIndex == INDEX_NONE) ? TokenEnd : DecimalIndex - 1;
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

    if (CharacterIndex > DecimalIndex)
    {
        int32 FractionalDigitOrdinal = 0;
        for (int32 Index = DecimalIndex + 1; Index <= CharacterIndex; ++Index)
        {
            if (DigitPrivate::IsDigitCharacter(DisplayString[Index]))
            {
                ++FractionalDigitOrdinal;
            }
        }

        if (FractionalDigitOrdinal <= 0)
        {
            return false;
        }

        OutDigitPlace = -FractionalDigitOrdinal;
        return true;
    }

    return false;
}

bool FDigitInputProcessor::ArmSpinBox(const FNumericHit& Hit, int32 PointerIndex)
{
    if (!Hit.SpinBoxWidget.IsValid())
    {
        return false;
    }

    const TSharedRef<SWidget> Widget = Hit.SpinBoxWidget.ToSharedRef();

    bool bArmed = false;
    bArmed = bArmed || TryArmTypedSpinBox<double>(Hit.SpinBoxWidget, Hit.DigitPlace);
    bArmed = bArmed || TryArmTypedSpinBox<float>(Hit.SpinBoxWidget, Hit.DigitPlace);
    bArmed = bArmed || TryArmTypedSpinBox<uint64>(Hit.SpinBoxWidget, Hit.DigitPlace);
    bArmed = bArmed || TryArmTypedSpinBox<uint32>(Hit.SpinBoxWidget, Hit.DigitPlace);
    bArmed = bArmed || TryArmTypedSpinBox<uint16>(Hit.SpinBoxWidget, Hit.DigitPlace);
    bArmed = bArmed || TryArmTypedSpinBox<uint8>(Hit.SpinBoxWidget, Hit.DigitPlace);
    bArmed = bArmed || TryArmTypedSpinBox<int64>(Hit.SpinBoxWidget, Hit.DigitPlace);
    bArmed = bArmed || TryArmTypedSpinBox<int32>(Hit.SpinBoxWidget, Hit.DigitPlace);
    bArmed = bArmed || TryArmTypedSpinBox<int16>(Hit.SpinBoxWidget, Hit.DigitPlace);
    bArmed = bArmed || TryArmTypedSpinBox<int8>(Hit.SpinBoxWidget, Hit.DigitPlace);

    if (bArmed)
    {
        ActiveSpinBox = Hit.SpinBoxWidget;
        ActivePointerIndex = PointerIndex;
        bRestorePending = false;
        bAwaitingNativeCapture = true;
    }

    return bArmed;
}

template<typename NumericType>
bool FDigitInputProcessor::TryArmTypedSpinBox(const TSharedPtr<SWidget>& Widget, int32 DigitPlace)
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

    NumericType NewDelta = NumericType(0);
    if (!DigitPrivate::MakeStepForPlace<NumericType>(DigitPlace, NewDelta))
    {
        return false;
    }

    const TSharedRef<SSpinBox<NumericType>> SpinBox = StaticCastSharedRef<SSpinBox<NumericType>>(WidgetRef);
    if (!SpinBox->GetEnableSlider())
    {
        return false;
    }

    const NumericType OriginalDelta = SpinBox->GetDelta();
    const TWeakPtr<SSpinBox<NumericType>> WeakSpinBox = SpinBox;

    SpinBox->SetDelta(NewDelta);

    ApplySelectedDeltaFunction = [WeakSpinBox, NewDelta]()
    {
        if (const TSharedPtr<SSpinBox<NumericType>> PinnedSpinBox = WeakSpinBox.Pin())
        {
            PinnedSpinBox->SetDelta(NewDelta);
        }
    };

    RestoreDeltaFunction = [WeakSpinBox, OriginalDelta]()
    {
        if (const TSharedPtr<SSpinBox<NumericType>> PinnedSpinBox = WeakSpinBox.Pin())
        {
            PinnedSpinBox->SetDelta(OriginalDelta);
        }
    };

    UE_LOG(LogDigit, Log, TEXT("Armed digit place %d: Delta %.9g -> %.9g"),
        DigitPlace, static_cast<double>(OriginalDelta), static_cast<double>(NewDelta));

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

    const FString Prefix = Hit.DisplayString.Left(Hit.CharacterIndex);
    const FString PrefixWithCharacter = Hit.DisplayString.Left(Hit.CharacterIndex + 1);

    const float PrefixWidth = FontMeasure->Measure(FText::FromString(Prefix), Font).X;
    const float PrefixWithCharacterWidth = FontMeasure->Measure(FText::FromString(PrefixWithCharacter), Font).X;
    const float CharacterWidth = FMath::Max(1.0f, PrefixWithCharacterWidth - PrefixWidth);
    const float TextHeight = FMath::Max(1.0f, FontMeasure->Measure(FText::FromString(Hit.DisplayString), Font).Y);

    const float CharacterLocalY = FMath::Max(0.0f, (Hit.TextGeometry.GetLocalSize().Y - TextHeight) * 0.5f);
    const FVector2D CharacterAbsolutePosition = Hit.TextGeometry.LocalToAbsolute(FVector2D(PrefixWidth, CharacterLocalY));

    // The first arranged widget in a normal FWidgetPath is the top-level window.
    // Convert through its geometry instead of manually compensating for DPI or window borders.
    const FGeometry& WindowGeometry = Path.Widgets[0].Geometry;
    const FVector2D CharacterWindowLocal = WindowGeometry.AbsoluteToLocal(CharacterAbsolutePosition);

    ClearHighlight();

    TSharedRef<SBox> Marker =
        SNew(SBox)
        .Visibility(EVisibility::HitTestInvisible)
        .WidthOverride(CharacterWidth)
        .HeightOverride(TextHeight)
        [
            SNew(SBorder)
            .Visibility(EVisibility::HitTestInvisible)
            .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
            .BorderBackgroundColor(FLinearColor(0.18f, 0.52f, 1.0f, 0.28f))
            .Padding(0.0f)
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
    HighlightTextWidget = Hit.TextWidget;
    HighlightCharacterIndex = Hit.CharacterIndex;
    HighlightDisplayString = Hit.DisplayString;
}

void FDigitInputProcessor::ClearHighlight()
{
    if (HighlightWidget.IsValid())
    {
        if (const TSharedPtr<SWindow> Window = HighlightWindow.Pin())
        {
            Window->RemoveOverlaySlot(HighlightWidget.ToSharedRef());
        }
    }

    HighlightWindow.Reset();
    HighlightWidget.Reset();
    HighlightTextWidget.Reset();
    HighlightCharacterIndex = INDEX_NONE;
    HighlightDisplayString.Reset();
}

void FDigitInputProcessor::QueueRestore()
{
    if (ActiveSpinBox.IsValid() && RestoreDeltaFunction)
    {
        bRestorePending = true;
    }
}

void FDigitInputProcessor::RestoreActiveDelta()
{
    if (RestoreDeltaFunction)
    {
        RestoreDeltaFunction();
    }

    ApplySelectedDeltaFunction = nullptr;
    RestoreDeltaFunction = nullptr;
    ActiveSpinBox.Reset();
    ActivePointerIndex = INDEX_NONE;
    bRestorePending = false;
    bAwaitingNativeCapture = false;
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
