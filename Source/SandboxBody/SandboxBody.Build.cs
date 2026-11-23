using UnrealBuildTool;

// The simulated body: ragdoll actor, the controller that makes it an active ragdoll (muscles and balance
// assist through Physics Control) and probes that measure it.
// Later: balance controller, state machine, reactions.
// Must not depend on SandboxInteraction or SandboxScenario.
public class SandboxBody : ModuleRules
{
	public SandboxBody(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "PhysicsCore", "SandboxCore" });
		PrivateDependencyModuleNames.AddRange(new string[] { "PhysicsControl", "Chaos" });
	}
}
