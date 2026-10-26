// Copyright Mippithedork 2026, Inc. All Rights Reserved.

#include "DigitModule.h"
#include "DigitInputProcessor.h"
#include "DigitSequencerContext.h"

#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "FDigitModule"

void FDigitModule::StartupModule()
{
    if (!FSlateApplication::IsInitialized())
    {
        return;
    }

    FSlateApplication& SlateApp = FSlateApplication::Get();

    FDigitSequencerContext::Initialize();

    InputProcessor = MakeShared<FDigitInputProcessor>();
    SlateApp.RegisterInputPreProcessor(InputProcessor, EInputPreProcessorType::Editor);
    PostTickHandle = SlateApp.OnPostTick().AddRaw(this, &FDigitModule::HandleSlatePostTick);
}

void FDigitModule::ShutdownModule()
{
    // Disconnect Slate callbacks BEFORE touching overlay/widget state, so
    // shutdown cleanup cannot receive another input or post-tick callback.
    if (FSlateApplication::IsInitialized())
    {
        FSlateApplication& SlateApp = FSlateApplication::Get();

        if (PostTickHandle.IsValid())
        {
            SlateApp.OnPostTick().Remove(PostTickHandle);
            PostTickHandle.Reset();
        }

        if (InputProcessor.IsValid())
        {
            SlateApp.UnregisterInputPreProcessor(InputProcessor);
        }
    }

    if (InputProcessor.IsValid())
    {
        InputProcessor->Shutdown();
    }
    InputProcessor.Reset();
    FDigitSequencerContext::Shutdown();
}

void FDigitModule::HandleSlatePostTick(float DeltaTime)
{
    if (InputProcessor.IsValid())
    {
        InputProcessor->HandlePostSlateTick();
    }
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDigitModule, Digit)
