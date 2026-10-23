// Copyright Mippithedork 2026, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"
#include "Layout/Geometry.h"
#include "InputCoreTypes.h"

class STextBlock;
class SWidget;
class SWindow;
class FWidgetPath;
class IToolTip;

class FDigitInputProcessor final : public IInputProcessor
{
private:
    enum class EActiveDragDirection : uint8
    {
        None,
        Increasing,
        Decreasing
    };

public:
    virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override;
    virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
    virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
    virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
    virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
    virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
    virtual const TCHAR* GetDebugName() const override { return TEXT("DigitInputProcessor"); }

    void HandlePostSlateTick();
    void Shutdown();

private:
    // The owning editor determines what the spin box's value callbacks mean.
    // This classification is based on the actual Slate ancestry of the field,
    // not the active tab/window name. Native editors still own transactions.
    enum class ENumericOwner : uint8
    {
        General,
        NiagaraParameter,
        SequencerKey,
        SequencerValueStepper,
        CurveEditorKeyDetail,
        CurveEditorToolOptions,
        ControlRigAnimationDetails
    };

    struct FNumericHit
    {
        TSharedPtr<SWidget> SpinBoxWidget;
        TSharedPtr<STextBlock> TextWidget;
        FGeometry TextGeometry;
        int32 CharacterIndex = INDEX_NONE;
        int32 DigitPlace = 0;
        bool bRotatorComponent = false;
        bool bCurveKeyTimeField = false;
        ENumericOwner Owner = ENumericOwner::General;
        FString DisplayString;
    };

    bool FindNumericHit(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent, FNumericHit& OutHit) const;
    bool ResolveDigitPlace(const FString& DisplayString, int32 CharacterIndex, int32& OutDigitPlace) const;
    bool FindCharacterIndexForDigitPlace(const FString& DisplayString, int32 DigitPlace, int32& OutCharacterIndex) const;
    int32 CountFractionalDigits(const FString& DisplayString, int32 CharacterIndex) const;

    bool ArmSpinBox(const FNumericHit& Hit, int32 PointerIndex, bool bUseLadder);

    template<typename NumericType>
    bool TryArmTypedSpinBox(const TSharedPtr<SWidget>& Widget, const FNumericHit& Hit, bool bUseLadder);

    void UpdateHoverHighlight(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent);
    void RefreshActiveHighlight(FSlateApplication& SlateApp);
    void ShowHighlight(FSlateApplication& SlateApp, const FWidgetPath& Path, const FNumericHit& Hit);
    void ClearHighlight();
    void UpdateActiveDragOutline(FSlateApplication& SlateApp, EActiveDragDirection Direction);
    void ClearActiveDragOutline();
    void SetDigitCursorOverride(const TSharedPtr<STextBlock>& TextWidget, EMouseCursor::Type Cursor);
    void ClearDigitCursorOverride();
    void ApplyDigitCursor();

    void ShowLadder(FSlateApplication& SlateApp, const FNumericHit& Hit);
    void ShowLadderFromCurrentHighlight(FSlateApplication& SlateApp);
    void RebuildLadderVisual();
    void ClearLadder();
    bool UpdateLadderSelection(const FPointerEvent& MouseEvent);
    bool IsIntegralSpinBox(const TSharedPtr<SWidget>& Widget) const;

    bool HasActiveSession() const;
    void QueueRestore();
    void RestoreActiveDelta();

    bool IsSupportedSpinBox(const TSharedRef<SWidget>& Widget) const;

private:
    TWeakPtr<SWidget> ActiveSpinBox;
    TWeakPtr<STextBlock> ActiveTextWidget;
    TFunction<void()> ApplySelectedDeltaFunction;
    TFunction<void()> RestoreDeltaFunction;
    TFunction<bool(const FPointerEvent&, bool, int32, bool)> RouteActiveMoveFunction;
    TFunction<void()> VerifyActiveSourceFunction;
    TFunction<void()> VerifyActiveDisplayFunction;
    int32 ActivePointerIndex = INDEX_NONE;
    int32 ActiveUserIndex = INDEX_NONE;
    int32 ActiveDigitPlace = 0;
    float PhysicalDragDistance = 0.0f;
    bool bDigitDragStarted = false;
    bool bActiveDigitScrubSuspended = false;
    bool bRestorePending = false;
    int32 PendingRestorePostTicks = 0;
    int32 PendingCapturePostTicks = 0;
    bool bAwaitingNativeCapture = false;
    bool bActiveSpinBoxIsIntegral = false;

    TWeakPtr<SWindow> HighlightWindow;
    TSharedPtr<SWidget> HighlightWidget;
    TWeakPtr<SWidget> HighlightSpinBoxWidget;
    TWeakPtr<STextBlock> HighlightTextWidget;
    int32 HighlightCharacterIndex = INDEX_NONE;
    int32 HighlightDigitPlace = 0;
    FString HighlightDisplayString;

    TWeakPtr<SWidget> DigitToolTipWidget;
    TSharedPtr<IToolTip> OriginalToolTip;
    TWeakPtr<STextBlock> CursorOverrideTextWidget;

    TWeakPtr<SWindow> ActiveDragOutlineWindow;
    TSharedPtr<SWidget> ActiveDragOutlineWidget;
    EActiveDragDirection ActiveDragDirection = EActiveDragDirection::None;

    TWeakPtr<SWindow> LadderWindow;
    TSharedPtr<SWidget> LadderWidget;
    TWeakPtr<SWidget> LadderSpinBoxWidget;
    FVector2D LadderWindowPosition = FVector2D::ZeroVector;
    int32 LadderOriginDigitPlace = 0;
    int32 LadderSelectedDigitPlace = 0;
    float LadderVerticalTravel = 0.0f;
    float ActiveLadderMagnitudeSensitivity = 0.5f;
    bool bLadderModeActive = false;
    bool bLadderIntegral = false;
};
