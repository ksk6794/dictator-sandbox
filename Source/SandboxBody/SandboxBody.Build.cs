using UnrealBuildTool;

// The simulated body: ragdoll actor and probes that measure it.
// Later: Physics Control muscles, balance assist, balance controller, state machine.
// Must not depend on SandboxInteraction or SandboxScenario.
public class SandboxBody : ModuleRules
{
	public SandboxBody(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "PhysicsCore", "SandboxCore" });
	}
}
