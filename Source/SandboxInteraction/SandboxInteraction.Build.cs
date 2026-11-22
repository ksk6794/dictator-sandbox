using UnrealBuildTool;

// The player side: game mode now; effectors, grab, target sources (desktop, OpenXR) and the pawn from M2 on.
// Must not depend on SandboxBody: the two sides meet only as rigid bodies in the physics world.
public class SandboxInteraction : ModuleRules
{
	public SandboxInteraction(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "SandboxCore" });
	}
}
