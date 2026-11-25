using UnrealBuildTool;

// The player side: effectors (physical hands), the target-source interface with its desktop, scripted, recorded
// and tracked sources, the drag handle, the pawn with its tools, the game mode.
// Must not depend on SandboxBody: the two sides meet only as rigid bodies in the physics world.
public class SandboxInteraction : ModuleRules
{
	public SandboxInteraction(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "PhysicsCore", "SandboxCore" });
		// Json: a recording of the hands is a text file (FSandboxTargetRecording).
		// HeadMountedDisplay: where the headset's controllers are (FSandboxXRControllers, the only file that asks).
		// It is the engine's own interface to any XR system; the OpenXR plugin is not linked against.
		PrivateDependencyModuleNames.AddRange(new string[] { "InputCore", "EnhancedInput", "Json", "HeadMountedDisplay" });
	}
}
