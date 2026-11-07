// Copyright Mippithedork 2026, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DigitSettings.generated.h"

UCLASS(Config = Digit, DefaultConfig, meta = (DisplayName = "Digit"))
class DIGIT_API UDigitSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UDigitSettings();

    virtual FName GetSectionName() const override { return TEXT("Digit"); }
    virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

    UPROPERTY(EditAnywhere, Config, Category = "Scrubbing", meta = (DisplayName = "Scrub Sensitivity", ClampMin = "0.1", ClampMax = "10.0", UIMin = "0.25", UIMax = "4.0", ToolTip = "Scales horizontal Digit scrubbing. 1.0 is the default speed. Lower values require more mouse movement; higher values require less. Does not affect the drag threshold or Value Ladder rung selection."))
    float ScrubSensitivity;

    UPROPERTY(EditAnywhere, Config, Category = "Scrubbing|Curve Editor Shared Values (Experimental)", meta = (DisplayName = "Legacy Shared Value Slider (Ineffective in UE 5.8)", ToolTip = "Deprecated preview. UE 5.8.3 creates a text-only numeric field for multiple selected keys, not an SSpinBox. Leave this OFF. For experimental support use Preview Ctrl+Alt Multi-Key Text Scrub instead."))
    bool bCurveEditorSharedValueSliderPreview;

    UPROPERTY(EditAnywhere, Config, Category = "Scrubbing|Curve Editor Multi-Key (Experimental)", meta = (DisplayName = "Preview Ctrl+Alt Multi-Key Text Scrub", ToolTip = "OFF by default. Ctrl+Alt+left-drag in the Curve Editor key Value field when Unreal uses its native multi-object text widget. The Value Ladder appears at 1.0; vertical movement chooses magnitude (10, 1, 0.1, 0.01, etc.), and horizontal movement applies whole steps at that magnitude. Equal values stage a new shared value; differing values stage a signed relative expression preserving differences. Digit captures the Slate pointer until release, then requests the ORIGINAL property editor's Enter commit after its own capture is released. Unreal owns Undo/Redo. Enable Live Multi-Key Curve Updates for immediate curve movement; otherwise the original native field commits on release. The mixed-value marker is verified for English UI only."))
    bool bCurveEditorMultiKeyTextScrubPreview;

    UPROPERTY(EditAnywhere, Config, Category = "Scrubbing|Curve Editor Multi-Key (Experimental)", meta = (DisplayName = "Show Multi-Key Scrub Preview in Value Field", ToolTip = "Show the proposed result directly over the Curve Editor Value field during Ctrl+Alt drag, without editing Unreal's bound text. Equal keys show their proposed shared numeric value. Mixed keys show a signed relative change such as +5, -0.25 or -12. The original field and native commit remain intact."))
    bool bCurveEditorMultiKeyFieldFeedback;

    UPROPERTY(EditAnywhere, Config, Category = "Scrubbing|Curve Editor Multi-Key (Experimental)", meta = (DisplayName = "Live Multi-Key Curve Updates (Experimental)", ToolTip = "OFF by default. Attempts to update selected Curve Editor key values during Ctrl+Alt scrubbing through the same PropertyEditor Value handle as manual editing. A single transaction spans the drag and Escape attempts to restore the original values. The verified release-only native text commit remains the fallback when this option is OFF or the property handle cannot be validated. Test with disposable sequences and verify Undo/Redo before use on production content."))
    bool bCurveEditorMultiKeyLivePreview;

    UPROPERTY(EditAnywhere, Config, Category = "Scrubbing|Curve Editor Text (Experimental)", meta = (DisplayName = "Enable Text-Only Ctrl+Alt Drag", ToolTip = "Enables Ctrl+Alt+left-drag on an already selected single digit in the Curve Editor Time/Value text boxes. Horizontal movement stages digit steps in Unreal's original text field. Press Enter to commit or Escape to cancel. No second property transaction or synthetic mouse capture is created."))
    bool bCurveEditorTextDrag;

    UPROPERTY(EditAnywhere, Config, Category = "Scrubbing|Curve Editor Text (Experimental)", meta = (EditCondition = "bCurveEditorTextDrag && bCurveEditorTextDragUseNativeCapture", DisplayName = "Commit Text Drag on Mouse Release (Preview)", ToolTip = "OPTIONAL and off by default. Requires Curve Editor Text Native Mouse Capture enabled. When a verified Ctrl+Alt text drag has staged at least one change, release mouse capture and request Enter on the NEXT Slate post-tick, after validating the original focused Curve Editor Time/Value field, live text, and selection again. Uses the editor's own text commit and transaction handlers, never calls OnTextCommitted directly. Remains experimental until mouse-up, Escape, Undo/Redo and multi-key workflows are validated in UE 5.8.3."))
    bool bCurveEditorTextDragCommitOnRelease;

    UPROPERTY(EditAnywhere, Config, Category = "Scrubbing|Curve Editor Text (Experimental)", meta = (EditCondition = "bCurveEditorTextDrag", DisplayName = "Capture Mouse for Text Drag (Preview)", ToolTip = "OPTIONAL and off by default. Requests true Slate pointer capture for Ctrl+Alt text drags, allowing editing beyond the original text field. Release, focus loss, or widget teardown ends the session and releases Digit's capture. Turn on to validate native capture in UE 5.8.3. Required for the optional mouse-release commit setting."))
    bool bCurveEditorTextDragUseNativeCapture;

    UPROPERTY(EditAnywhere, Config, Category = "Scrubbing", meta = (DisplayName = "Ladder Magnitude Sensitivity", ClampMin = "0.1", ClampMax = "10.0", UIMin = "0.25", UIMax = "4.0", ToolTip = "Scales vertical Value Ladder magnitude selection. 1.0 is the default and is 50% less sensitive than Digit's original ladder behavior. Lower values require more vertical movement to change magnitude; higher values require less. 2.0 matches the original ladder sensitivity. Does not affect horizontal scrubbing."))
    float LadderMagnitudeSensitivity;

    UPROPERTY(EditAnywhere, Config, Category = "Value Ladder", meta = (DisplayName = "Toggle Ladder with Alt", ToolTip = "Press Alt once over a digit to show the Value Ladder, and press Alt again anywhere to dismiss it. Clicking outside the ladder and its source numeric field dismisses it. By default, a scrub can start in either the original field or a ladder row. When row selection is enabled, drags must start on a ladder row."))
    bool bToggleLadderWithAlt;

    UPROPERTY(EditAnywhere, Config, Category = "Value Ladder", meta = (DisplayName = "Select Magnitude from Ladder", ToolTip = "Hover a ladder row to highlight it, then start a horizontal scrub by clicking that row. Vertical movement does not change magnitude during this drag."))
    bool bSelectMagnitudeFromLadder;

    UPROPERTY(EditAnywhere, Config, Category = "Value Ladder", meta = (DisplayName = "Lock Magnitude with Ctrl", ToolTip = "Hold Ctrl while scrubbing with the Value Ladder to freeze the current magnitude. Release Ctrl to resume vertical magnitude selection. Ctrl still applies its normal fine-scrub multiplier."))
    bool bLockLadderMagnitudeWithCtrl;

    UPROPERTY(EditAnywhere, Config, Category = "Appearance|Digit Highlight", meta = (DisplayName = "Highlight Color", ToolTip = "Background color drawn behind the numeric digit currently targeted by Digit scrubbing."))
    FLinearColor DigitHighlightColor;

    UPROPERTY(EditAnywhere, Config, Category = "Appearance|Digit Highlight", meta = (DisplayName = "Highlighted Text Color", ToolTip = "Text color drawn over the numeric digit currently targeted by Digit scrubbing."))
    FLinearColor HighlightedTextColor;

    UPROPERTY(EditAnywhere, Config, Category = "Diagnostics", meta = (DisplayName = "Log Numeric Widget Diagnostics", ToolTip = "When enabled, unresolved clicks on numeric spin boxes write widget paths and locale settings to the Output Log under LogDigit. Intended for reporting unsupported fields; leave off during normal editing."))
    bool bLogWidgetDiagnostics;

    static const UDigitSettings* Get();
};
