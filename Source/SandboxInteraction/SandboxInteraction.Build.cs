using UnrealBuildTool;

// The player side: effectors (physical hands), the target-source interface with its desktop and scripted
// sources, the drag handle, the desktop pawn with its tools, the game mode. The OpenXR source comes in M4.
// Must not depend on SandboxBody: the two sides meet only as rigid bodies in the physics world.
public class SandboxInteraction : ModuleRules
{
	public SandboxInteraction(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "PhysicsCore", "SandboxCore" });
		PrivateDependencyModuleNames.AddRange(new string[] { "InputCore", "EnhancedInput" });
	}
}
