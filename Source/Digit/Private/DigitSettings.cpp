// Copyright Mippithedork 2026, Inc. All Rights Reserved.

#include "DigitSettings.h"

UDigitSettings::UDigitSettings()
    : ScrubSensitivity(1.0f)
    , bCurveEditorTextDrag(true)
    , bCurveEditorTextDragCommitOnRelease(false)
    , bCurveEditorTextDragUseNativeCapture(false)
    , LadderMagnitudeSensitivity(1.0f)
    , bToggleLadderWithAlt(false)
    , bSelectMagnitudeFromLadder(false)
    , bLockLadderMagnitudeWithCtrl(true)
    , DigitHighlightColor(FLinearColor(0.0f, 0.162029f, 0.745404f, 0.38f))
    , HighlightedTextColor(FLinearColor::White)
    , bLogWidgetDiagnostics(false)
{
}

const UDigitSettings* UDigitSettings::Get()
{
    return GetDefault<UDigitSettings>();
}
