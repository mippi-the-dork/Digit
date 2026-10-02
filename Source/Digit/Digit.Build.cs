// Copyright Mippithedork 2026, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Digit : ModuleRules
{
    public Digit(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "DeveloperSettings"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "ApplicationCore",
                "Engine",
                "InputCore",
                "Slate",
                "SlateCore"
            }
        );
    }
}
