using UnrealBuildTool;

// Headless scenario runner, metrics and tests. Depends on the other Sandbox modules; nothing depends on it.
public class SandboxScenario : ModuleRules
{
	public SandboxScenario(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Json", "PhysicsCore", "SandboxCore", "SandboxBody", "SandboxInteraction" });
		PrivateDependencyModuleNames.AddRange(new string[] { "RHI", "RenderCore", "HeadMountedDisplay", "Chaos" });
	}
}
