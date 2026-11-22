using UnrealBuildTool;

// Editor-only: turns text specs into binary assets (physics asset, room map, imports) from the command line.
public class SandboxEditor : ModuleRules
{
	public SandboxEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Json", "SandboxCore" });
		PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "PhysicsCore", "PhysicsUtilities", "AssetRegistry" });
	}
}
