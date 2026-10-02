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

    UPROPERTY(EditAnywhere, Config, Category = "Appearance|Digit Highlight", meta = (DisplayName = "Highlight Color", ToolTip = "Background color drawn behind the numeric digit currently targeted by Digit scrubbing."))
    FLinearColor DigitHighlightColor;

    UPROPERTY(EditAnywhere, Config, Category = "Appearance|Digit Highlight", meta = (DisplayName = "Highlighted Text Color", ToolTip = "Text color drawn over the numeric digit currently targeted by Digit scrubbing."))
    FLinearColor HighlightedTextColor;

    static const UDigitSettings* Get();
};
