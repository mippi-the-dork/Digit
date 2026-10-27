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

    UPROPERTY(EditAnywhere, Config, Category = "Scrubbing", meta = (DisplayName = "Ladder Magnitude Sensitivity", ClampMin = "0.1", ClampMax = "10.0", UIMin = "0.25", UIMax = "4.0", ToolTip = "Scales vertical Value Ladder magnitude selection. 1.0 is the default and is 50% less sensitive than Digit's original ladder behavior. Lower values require more vertical movement to change magnitude; higher values require less. 2.0 matches the original ladder sensitivity. Does not affect horizontal scrubbing."))
    float LadderMagnitudeSensitivity;

    UPROPERTY(EditAnywhere, Config, Category = "Value Ladder", meta = (DisplayName = "Toggle Ladder with Alt", ToolTip = "Press Alt once over a digit to show the Value Ladder, and press Alt again anywhere to dismiss it. Clicking outside the ladder also dismisses it. Drags must begin inside the ladder."))
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
