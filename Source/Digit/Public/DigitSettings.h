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

    UPROPERTY(EditAnywhere, Config, Category = "Appearance|Digit Highlight", meta = (DisplayName = "Highlight Color", ToolTip = "Background color drawn behind the numeric digit currently targeted by Digit scrubbing."))
    FLinearColor DigitHighlightColor;

    UPROPERTY(EditAnywhere, Config, Category = "Appearance|Digit Highlight", meta = (DisplayName = "Highlighted Text Color", ToolTip = "Text color drawn over the numeric digit currently targeted by Digit scrubbing."))
    FLinearColor HighlightedTextColor;

    static const UDigitSettings* Get();
};
