// Copyright Mippithedork 2026, Inc. All Rights Reserved.

#include "DigitSettings.h"

UDigitSettings::UDigitSettings()
    : ScrubSensitivity(1.0f)
    , DigitHighlightColor(FLinearColor(0.0f, 0.162029f, 0.745404f, 0.38f))
    , HighlightedTextColor(FLinearColor::White)
{
}

const UDigitSettings* UDigitSettings::Get()
{
    return GetDefault<UDigitSettings>();
}
