// Copyright Mippithedork 2026, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"

// Digit can temporarily place a literal in an SSpinBox when Unreal's native
// bounded-slider path cannot represent the selected digit (rotators, integral
// controls, and certain editor-specialized values). A Curve Editor details
// row may retain the SAME widget yet bind it to a different key or tangent.
// Do not restore the previous key's binding over that new native host owner.
namespace DigitNativeBindingGuard
{
    template<typename NumericType>
    inline bool OwnsTemporaryLiteral(
        const TAttribute<NumericType>& Current,
        const NumericType LastDigitWrite)
    {
        // The temporary Digit SetValue is intentionally an unbound literal.
        // A host-owned binding or a different literal is never ours to undo.
        return Current.IsSet() && !Current.IsBound()
            && Current.Get() == LastDigitWrite;
    }

    template<typename NumericType>
    inline bool IsUnchangedHostBinding(
        const TAttribute<NumericType>& Current,
        const TAttribute<NumericType>& Original)
    {
        // An unbound field can legitimately acquire a new value during a
        // native drag; only bound fields have a stable getter identity.
        return !Original.IsBound() || Current.IdenticalTo(Original);
    }
}
