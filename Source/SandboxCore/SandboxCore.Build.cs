using UnrealBuildTool;

// Shared services: logging, JSON helpers. Later: parameter store, metric types.
// Every other Sandbox module may depend on this one; it depends on none of them.
public class SandboxCore : ModuleRules
{
	public SandboxCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Json" });
	}
}
