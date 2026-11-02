// Copyright Mippithedork 2026, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"

// Native Slate spin boxes belong to their editor. A Curve Editor key-detail
// row, weighted-tangent tool, or multi-key details row can become disabled,
// hidden, or be rebuilt while that spin box still has mouse capture.
// An old details row can remain alive, so also validate its parent chain.
// Keep only weak identity references, and never infer that the native host's
// selected keys or transactions are unchanged merely because its value label
// remains numeric.
namespace DigitNativeHostGuard
{
    inline bool IsInteractive(const TArray<TWeakPtr<SWidget>>& HostHierarchy)
    {
        if (HostHierarchy.IsEmpty())
        {
            return false; // No verified native host ancestry.
        }
        TSharedPtr<SWidget> OriginalParent;
        for (int32 Index = 0; Index < HostHierarchy.Num(); ++Index)
        {
            const TSharedPtr<SWidget> Widget = HostHierarchy[Index].Pin();
            if (!Widget.IsValid() || !Widget->IsEnabled())
            {
                return false;
            }
            const EVisibility Visibility = Widget->GetVisibility();
            if (Visibility == EVisibility::Hidden || Visibility == EVisibility::Collapsed)
            {
                return false;
            }
            // A selection change may detach/reparent the details row while
            // its captured SSpinBox and old owner are still alive. Lifetime
            // and visibility alone do not prove the numeric field still
            // belongs to the original editor row. Verify each actual parent
            // link against the root-to-leaf FWidgetPath snapshot. This is
            // O(path depth), with no tree traversal or Slate allocation.
            if (Index > 0 && Widget->GetParentWidget().Get() != OriginalParent.Get())
            {
                return false;
            }
            OriginalParent = Widget;
        }
        return true;
    }
}
