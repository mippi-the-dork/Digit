// Copyright Mippithedork 2026, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "DigitCurveTextMouse.h"

// UE 5.8.3's SPropertyEditorNumeric<T> constructs an SNumericEntryBox<T>
// with AllowSpin=false when its property row represents multiple objects.
// The editable text is still bound to the original property editor and its
// native OnValueCommitted / OnUndeterminedValueCommitted delegates. This
// policy stages valid text; it never sets a keyframe or owns a transaction.
namespace DigitCurveMultiKeyTextPolicy
{
    enum class EMode : uint8 { None, EqualValues, MixedValues };

    // UE 5.8.3 may report Ctrl/Alt key-down events after the preprocessor has
    // already armed an Alt or Ctrl+Alt mouse gesture. These are not a new edit request;
    // cancelling on them drops the session before mouse release. Shift is an
    // ordinary scrubbing sensitivity modifier and may change mid-drag.
    // The caller must still cancel Escape and unrelated key presses.
    inline bool IsGestureModifierKey(const FKey& Key)
    {
        return Key == EKeys::LeftControl || Key == EKeys::RightControl
            || Key == EKeys::LeftAlt || Key == EKeys::RightAlt
            || Key == EKeys::LeftShift || Key == EKeys::RightShift;
    }

    // Text-only key Value field path observed in UE 5.8.3 Sequencer. This
    // checks widget ordering only; the caller must ALSO verify that the first
    // SHorizontalBox beneath SCurveKeyDetailPanel owns the SECOND (Value)
    // slot, and validate enable/visibility and actual live ancestry.
    inline bool HasValueTextWidgetTypes(const TArray<FString>& Types)
    {
        int32 Panel = INDEX_NONE;
        int32 Row = INDEX_NONE;
        int32 Property = INDEX_NONE;
        int32 Entry = INDEX_NONE;
        for (int32 Index = 0; Index < Types.Num(); ++Index)
        {
            const FString& Type = Types[Index];
            if (Type == TEXT("SCurveKeyDetailPanel")) Panel = Index;
            else if (Panel != INDEX_NONE && Row == INDEX_NONE
                && Type == TEXT("SHorizontalBox")) Row = Index;
            else if (Row != INDEX_NONE && Type.StartsWith(TEXT("SPropertyEditorNumeric")))
                Property = Index;
            else if (Property != INDEX_NONE && Type.StartsWith(TEXT("SNumericEntryBox")))
                Entry = Index;
        }
        return Panel != INDEX_NONE && Row > Panel && Property > Row
            && Entry > Property && Types.Num() > Entry + 1
            && Types.Last() == TEXT("SEditableText");
    }

    // Digit consumes the original mouse-down and owns the text widget's
    // actual Slate capture. Never treat capture by some other widget as ours.
    // Keep the original key-detail ancestry and focused host intact.
    inline bool CanContinueOwnedCaptureDrag(bool bMouseDown, bool bOwnCapture,
        bool bFocused, bool bAttached)
    {
        return bMouseDown && bOwnCapture && bFocused && bAttached;
    }


    // The Property Editor supplies its localized undetermined text. For the
    // initial preview, accept only the verified English UE 5.8 display. A
    // guessed placeholder could accidentally commit a different property.
    inline bool IsVerifiedMixedMarker(const FString& Text)
    {
        return Text == TEXT("Multiple Values");
    }

    // Native property text must still equal the value present when mouse-down
    // was claimed. A changed selection or a stale buffer never receives Enter.
    inline bool CanCommitDeferredValue(const FString& Initial,
        const FString& NativeLive, const FString& Proposed)
    {
        return !Initial.IsEmpty() && !Proposed.IsEmpty()
            && NativeLive == Initial && Proposed != Initial;
    }

    inline bool CanSynchronizeLiveEqualText(const FString& Initial,
        const FString& NativeLive, const FString& Proposed)
    {
        if (Initial.IsEmpty() || Proposed.IsEmpty() || Proposed == Initial)
            return false;
        double ProposedValue = 0.0;
        if (!LexTryParseString(ProposedValue, *Proposed)
            || !FMath::IsFinite(ProposedValue)) return false;
        if (NativeLive == Initial || NativeLive == Proposed) return true;
        // When the property handle updates live, the native widget may have
        // reformatted the same numeric result (12.40 -> 12.4). Only accept
        // that equivalent value; property values are checked separately.
        double NativeValue = 0.0;
        return LexTryParseString(NativeValue, *NativeLive)
            && FMath::IsFinite(NativeValue)
            && FMath::IsNearlyEqual(NativeValue, ProposedValue, 1.e-6);
    }

    inline bool CanBeginEqualValues(const FString& Initial, int32 DigitIndex)
    {
        FString Next;
        int32 NextIndex = INDEX_NONE;
        return DigitCurveTextMouse::TryStageValueSteps(
            Initial, DigitIndex, 1, Next, NextIndex)
            || DigitCurveTextMouse::TryStageValueSteps(
                Initial, DigitIndex, -1, Next, NextIndex);
    }

    inline bool TryStageMixedRelative(const FString& Original, int32 SignedSteps,
        FString& OutExpression)
    {
        OutExpression.Reset();
        if (!IsVerifiedMixedMarker(Original) || SignedSteps == 0
            || SignedSteps < -100000 || SignedSteps > 100000)
        {
            return false;
        }
        // PropertyEditorNumeric::OnUndeterminedValueCommitted retrieves each
        // selected object's original value and substitutes it for this exact
        // marker, then evaluates the resulting numeric expression separately.
        OutExpression = FString::Printf(TEXT("%s %s %d"), *Original,
            SignedSteps > 0 ? TEXT("+") : TEXT("-"), FMath::Abs(SignedSteps));
        return true;
    }
}
