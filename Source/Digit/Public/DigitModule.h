// Copyright Mippithedork 2026, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class FDigitInputProcessor;

class FDigitModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    void HandleSlatePostTick(float DeltaTime);

    TSharedPtr<FDigitInputProcessor> InputProcessor;
    FDelegateHandle PostTickHandle;
};
