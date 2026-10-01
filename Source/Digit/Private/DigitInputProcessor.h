#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"
#include "Layout/Geometry.h"

class STextBlock;
class SWidget;
class SWindow;
class FWidgetPath;

class FDigitInputProcessor final : public IInputProcessor
{
public:
    virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override;
    virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
    virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
    virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
    virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
    virtual const TCHAR* GetDebugName() const override { return TEXT("DigitInputProcessor"); }

    void HandlePostSlateTick();
    void Shutdown();

private:
    struct FNumericHit
    {
        TSharedPtr<SWidget> SpinBoxWidget;
        TSharedPtr<STextBlock> TextWidget;
        FGeometry TextGeometry;
        int32 CharacterIndex = INDEX_NONE;
        int32 DigitPlace = 0;
        FString DisplayString;
    };

    bool FindNumericHit(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent, FNumericHit& OutHit) const;
    bool ResolveDigitPlace(const FString& DisplayString, int32 CharacterIndex, int32& OutDigitPlace) const;
    bool FindCharacterIndexForDigitPlace(const FString& DisplayString, int32 DigitPlace, int32& OutCharacterIndex) const;
    int32 CountFractionalDigits(const FString& DisplayString, int32 CharacterIndex) const;

    bool ArmSpinBox(const FNumericHit& Hit, int32 PointerIndex);

    template<typename NumericType>
    bool TryArmTypedSpinBox(const TSharedPtr<SWidget>& Widget, const FNumericHit& Hit);

    void UpdateHoverHighlight(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent);
    void RefreshActiveHighlight(FSlateApplication& SlateApp);
    void ShowHighlight(FSlateApplication& SlateApp, const FWidgetPath& Path, const FNumericHit& Hit);
    void ClearHighlight();

    void QueueRestore();
    void RestoreActiveDelta();

    bool IsSupportedSpinBox(const TSharedRef<SWidget>& Widget) const;

private:
    TWeakPtr<SWidget> ActiveSpinBox;
    TWeakPtr<STextBlock> ActiveTextWidget;
    TFunction<void()> ApplySelectedDeltaFunction;
    TFunction<void()> RestoreDeltaFunction;
    TFunction<bool(const FPointerEvent&, bool)> RouteActiveMoveFunction;
    int32 ActivePointerIndex = INDEX_NONE;
    int32 ActiveDigitPlace = 0;
    float PhysicalDragDistance = 0.0f;
    bool bDigitDragStarted = false;
    bool bRestorePending = false;
    bool bAwaitingNativeCapture = false;

    TWeakPtr<SWindow> HighlightWindow;
    TSharedPtr<SWidget> HighlightWidget;
    TWeakPtr<STextBlock> HighlightTextWidget;
    int32 HighlightCharacterIndex = INDEX_NONE;
    FString HighlightDisplayString;
};
