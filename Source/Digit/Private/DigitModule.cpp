#include "DigitModule.h"
#include "DigitInputProcessor.h"

#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "FDigitModule"

void FDigitModule::StartupModule()
{
    if (!FSlateApplication::IsInitialized())
    {
        return;
    }

    FSlateApplication& SlateApp = FSlateApplication::Get();

    InputProcessor = MakeShared<FDigitInputProcessor>();
    SlateApp.RegisterInputPreProcessor(InputProcessor, EInputPreProcessorType::Editor);
    PostTickHandle = SlateApp.OnPostTick().AddRaw(this, &FDigitModule::HandleSlatePostTick);
}

void FDigitModule::ShutdownModule()
{
    if (InputProcessor.IsValid())
    {
        InputProcessor->Shutdown();
    }

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

    InputProcessor.Reset();
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
