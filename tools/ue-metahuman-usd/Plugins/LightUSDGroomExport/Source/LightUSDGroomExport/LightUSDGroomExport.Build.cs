using UnrealBuildTool;

public class LightUSDGroomExport : ModuleRules
{
    public LightUSDGroomExport(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "HairStrandsCore"
        });
    }
}
