// Copyright Mippithedork 2026, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"
#include "Layout/Geometry.h"
#include "InputCoreTypes.h"
#include "Input/Events.h"
#include "Input/Reply.h"
#include "DigitSequencerContext.h"
#include "ScopedTransaction.h"

class STextBlock;
class SBorder;
class SEditableText;
class SWidget;
class SWindow;
class FWidgetPath;
class IToolTip;
class IPropertyHandle;
class SOverlay;

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
        bool bCurveKeyValueField = false;
        ENumericOwner Owner = ENumericOwner::General;
        TOptional<FDigitTemporalSpinContext> TemporalContext;
        FString DisplayString;
        // Weak path through the native owner, including the target spin box.
        // Needed when multi-key/tangent tool rows rebuild during capture.
        TArray<TWeakPtr<SWidget>> HostHierarchy;
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
    void RefreshLadderRowVisuals();
    void ClearLadder();
    bool IsOverLadder(const FVector2D& ScreenPosition, int32* OutDigitPlace = nullptr) const;
    void UpdateLadderHover(const FVector2D& ScreenPosition);
    FReply HandleLadderRowMouseDown(const FGeometry& RowGeometry, const FPointerEvent& MouseEvent, int32 DigitPlace);
    bool IsOverLadderSourceField(FSlateApplication& SlateApp, const FVector2D& ScreenPosition) const;
    bool BuildLadderSourceHit(FSlateApplication& SlateApp, int32 DigitPlace, FNumericHit& OutHit) const;
    bool IsLadderToggleEnabled() const;
    bool IsLadderRowSelectionEnabled() const;
    bool ShouldKeepLadderPreview() const;
    bool UpdateLadderSelection(const FPointerEvent& MouseEvent);
    bool IsIntegralSpinBox(const TSharedPtr<SWidget>& Widget) const;

    // An explicitly selected Curve Editor Time/Value digit may be dragged in
    // the original editable field. No synthetic native mouse-down, external
    // OnTextCommitted, or property transactions are generated.
    // Shared Curve Editor Value uses SPropertyEditorNumeric<T> >
    // SNumericEntryBox<T> > SEditableText, not the SEditableTextBox above.
    // Digit tracks its proposed value without modifying the bound native text.
    // Equal values support delayed direct digit-hover drags; Alt opens the
    // common Value Ladder. Commit uses native Enter or the live property handle.
    bool ResolveCurvePropertyValueTarget(const FWidgetPath& Path,
        const SWidget* EditableWidget) const;
    bool TryBeginCurvePropertyScrub(FSlateApplication& SlateApp, const FPointerEvent& Event,
        bool bFromPendingDirect = false,
        const TSharedPtr<SEditableText>& ForcedSource = nullptr,
        int32 ForcedPlace = INDEX_NONE, bool bFromLadderRow = false);
    bool ResolveCurvePropertyPointerTarget(FSlateApplication& SlateApp,
        const FVector2D& Position, int32 UserIndex,
        TSharedPtr<SEditableText>& OutEdit, FWidgetPath& OutPath) const;
    bool GetCurvePropertyHoveredDigit(FSlateApplication& SlateApp,
        const TSharedRef<SEditableText>& Edit, const FString& Source,
        const FVector2D& Position, int32& OutIndex, int32& OutPlace) const;
    void ClearPendingCurvePropertyDirect();
    bool UpdateCurvePropertyScrub(FSlateApplication& SlateApp, const FPointerEvent& Event);
    void EndCurvePropertyScrub(bool bKeepStagedText = false);
    void FlushCurvePropertyCommit(FSlateApplication& SlateApp);
    void ClearCurvePropertyCommit();
    void ShowCurvePropertyLadder(FSlateApplication& SlateApp,
        const TSharedRef<SEditableText>& Edit, int32 OriginPlace = 0);

    bool ResolveCurveTextTarget(const FWidgetPath& Path,
        const SWidget* FocusedWidget, bool& bOutTime) const;
    bool TryBeginCurveTextDrag(FSlateApplication& SlateApp, const FPointerEvent& Event);
    bool UpdateCurveTextDrag(FSlateApplication& SlateApp, const FPointerEvent& Event);
    // Always release only Digit's captured pointer before discarding the weak
    // source. Native text commits are a separate, optional mouse-up action.
    void EndCurveTextDrag();
    bool CanFinishCurveTextDrag(FSlateApplication& SlateApp) const;
    void FlushDeferredCurveTextCommit(FSlateApplication& SlateApp);
    void ClearDeferredCurveTextCommit();

    bool IsActiveHostHierarchyInteractive() const;
    bool HasActiveSession() const;
    void QueueRestore(bool bPreserveLadderPreview = false);
    void RestoreActiveDelta();

    bool IsSupportedSpinBox(const TSharedRef<SWidget>& Widget) const;

private:
    // The public SEditableText API does not expose its uncommitted buffer or
    // edit transactions. Never re-step stale bound text within the same edit.

    struct FCurvePropertyCommit
    {
        TWeakPtr<SEditableText> Editable;
        TArray<TWeakPtr<SWidget>> OriginalHierarchy;
        FString ExpectedText;
        FString OriginalText;
        int32 UserIndex = INDEX_NONE;
    };
    TOptional<FCurvePropertyCommit> PendingCurvePropertyCommit;
    TWeakPtr<SEditableText> CurvePropertyEditable;
    TArray<TWeakPtr<SWidget>> CurvePropertyHierarchy;
    FString CurvePropertyInitialText;
    FString CurvePropertyLastStaged;
    int32 CurvePropertyDigitIndex = INDEX_NONE;
    int32 CurvePropertyPointer = INDEX_NONE;
    int32 CurvePropertyUser = INDEX_NONE;
    int32 CurvePropertyAccumulatedSteps = 0;
    double CurvePropertyCurrentOffset = 0.0;
    int32 CurvePropertyFinestPlace = 0;
    bool bCurvePropertyLadderActive = false;
    TWeakPtr<SEditableText> CurvePropertyLadderSource;
    // A plain click must reach native SEditableText. Only a later actual
    // drag over the same equal-value digit takes over mouse capture.
    TWeakPtr<SEditableText> PendingCurvePropertyDirect;
    FString PendingCurvePropertyText;
    FVector2D PendingCurvePropertyPosition = FVector2D::ZeroVector;
    int32 PendingCurvePropertyDigitIndex = INDEX_NONE;
    int32 PendingCurvePropertyPlace = 0;
    int32 PendingCurvePropertyUser = INDEX_NONE;
    int32 PendingCurvePropertyPointer = INDEX_NONE;
    bool bCurvePropertyCtrlUsedForActivation = false;
    bool bCurvePropertyRowLocked = false;
    bool bCurvePropertyCtrlWasLocked = false;
    double CurvePropertyPendingPixels = 0.0;
    bool bCurvePropertyScrubActive = false;
    bool bCurvePropertyOwnsCapture = false;
    bool bCurvePropertyReceivedMove = false;
    bool bCurvePropertyMixed = false;
    bool bCurvePropertyChanged = false;
    // Non-interactive preview only modifies a hit-test-invisible overlay. It
    // never writes to Unreal's original property-bound SEditableText.
    TWeakPtr<SOverlay> CurvePropertyFeedbackOverlay;
    TSharedPtr<SWidget> CurvePropertyFeedbackWidget;
    TSharedPtr<STextBlock> CurvePropertyFeedbackLabel;
    // Optional interactive key updates use the original PropertyEditor handle.
    // The known-good deferred text commit remains the fallback when disabled.
    TSharedPtr<IPropertyHandle> CurvePropertyLiveHandle;
    TArray<FString> CurvePropertyOriginalObjectValues;
    TArray<double> CurvePropertyOriginalNumericValues;
    TArray<FString> CurvePropertyCurrentObjectValues;
    TUniquePtr<FScopedTransaction> CurvePropertyLiveTransaction;
    bool bCurvePropertyLiveEnabled = false;
    bool bCurvePropertyLiveApplied = false;

    TWeakPtr<SEditableText> CurveTextDragEditable;
    int32 CurveTextDragPointer = INDEX_NONE;
    int32 CurveTextDragUser = INDEX_NONE;
    double CurveTextDragPendingPixels = 0.0;
    bool bCurveTextDragActive = false;
    bool bCurveTextDragTime = false;
    bool bCurveTextDragChanged = false;
    bool bCurveTextDragOwnsCapture = false;
    // Selected place is an invariant even if formatting carries add digits.
    int32 CurveTextDragExpectedIndex = INDEX_NONE;
    FString CurveTextDragInitialText;
    FString CurveTextDragLastStaged;

    // The mouse-up input preprocessor must not dispatch native Enter while
    // Slate is still processing the pointer event. One-shot, weak source
    // identity, and text snapshot are revalidated on the next post-tick.
    struct FDeferredCurveTextCommit
    {
        TWeakPtr<SEditableText> Editable;
        FString ExpectedText;
        int32 ExpectedIndex = INDEX_NONE;
        int32 UserIndex = INDEX_NONE;
        bool bTime = false;
    };
    TOptional<FDeferredCurveTextCommit> DeferredCurveTextCommit;

    TWeakPtr<SWidget> ActiveSpinBox;
    TWeakPtr<STextBlock> ActiveTextWidget;
    TArray<TWeakPtr<SWidget>> ActiveHostHierarchy;
    TFunction<void()> SuspendActiveScrubFunction;
    TFunction<void()> ApplySelectedDeltaFunction;
    TFunction<void()> RestoreDeltaFunction;
    TFunction<bool(const FPointerEvent&, bool, int32, bool)> RouteActiveMoveFunction;
    TFunction<void()> VerifyActiveSourceFunction;
    TFunction<void()> VerifyActiveBindingFunction;
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
    bool bActiveNonDropTimecode = false;

    TWeakPtr<SWindow> HighlightWindow;
    TSharedPtr<SWidget> HighlightWidget;
    TWeakPtr<SWidget> HighlightSpinBoxWidget;
    TWeakPtr<STextBlock> HighlightTextWidget;
    int32 HighlightCharacterIndex = INDEX_NONE;
    int32 HighlightDigitPlace = 0;
    FString HighlightDisplayString;
    // Preserve native owner/temporal conversion when Alt opens a ladder from
    // hover. Ordinary mouse-down recomputes this, but the ladder begins from
    // the cached highlight, including frame/tick and rotator semantics.
    bool HighlightSourceRotator = false;
    bool HighlightSourceCurveTime = false;
    ENumericOwner HighlightSourceOwner = ENumericOwner::General;
    TOptional<FDigitTemporalSpinContext> HighlightSourceTemporalContext;

    TWeakPtr<SWidget> DigitToolTipWidget;
    TSharedPtr<IToolTip> OriginalToolTip;
    TWeakPtr<STextBlock> CursorOverrideTextWidget;

    TWeakPtr<SWindow> ActiveDragOutlineWindow;
    TSharedPtr<SWidget> ActiveDragOutlineWidget;
    EActiveDragDirection ActiveDragDirection = EActiveDragDirection::None;

    TWeakPtr<SWindow> LadderWindow;
    TSharedPtr<SWidget> LadderWidget;
    // Keep overlay geometry stable across hover and rung selection. Changing
    // a row must not detach a Slate widget during pointer dispatch.
    TWeakPtr<SBorder> LadderRowBackgrounds[9];
    TWeakPtr<STextBlock> LadderRowLabels[9];
    // Ladder rows are native mouse targets in optional click modes. They
    // forward the host SSpinBox's FReply through normal Slate dispatch.
    // Identity only. Never dereference: the host may destroy its SSpinBox
    // before Digit receives capture-loss / window-teardown notification.
    // Avoid holding a second Slate weak-reference control block solely for
    // comparing the currently highlighted field.
    const SWidget* LadderSpinBoxIdentity = nullptr;
    FVector2D LadderWindowPosition = FVector2D::ZeroVector;
    int32 LadderOriginDigitPlace = 0;
    int32 LadderSelectedDigitPlace = 0;
    float LadderVerticalTravel = 0.0f;
    float ActiveLadderMagnitudeSensitivity = 0.5f;
    bool bLadderModeActive = false;
    bool bLadderIntegral = false;
    bool bLadderToggledOn = false;
    bool bLadderDismissQueued = false;
    bool bCtrlMagnitudeWasLocked = false;
    int32 LadderHoveredDigitPlace = INDEX_NONE;
    TWeakPtr<SWidget> LadderSourceSpinBox;
    TWeakPtr<STextBlock> LadderSourceText;
    bool bLadderSourceRotator = false;
    bool bLadderSourceCurveTime = false;
    ENumericOwner LadderSourceOwner = ENumericOwner::General;
    TOptional<FDigitTemporalSpinContext> LadderSourceTemporalContext;
};
