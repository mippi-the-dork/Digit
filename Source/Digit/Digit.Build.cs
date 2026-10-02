using UnrealBuildTool;

public class Digit : ModuleRules
{
    public Digit(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "ApplicationCore",
                "CoreUObject",
                "DeveloperSettings",
                "Engine",
                "InputCore",
                "Slate",
                "SlateCore"
            }
        );
    }
}
