using UnrealBuildTool;

public class TinyUSDZGroomExport : ModuleRules
{
    public TinyUSDZGroomExport(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "HairStrandsCore"
        });
    }
}
