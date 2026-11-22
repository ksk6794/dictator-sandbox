using UnrealBuildTool;

public class DictatorSandboxTarget : TargetRules
{
	public DictatorSandboxTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.AddRange(new string[] { "SandboxCore", "SandboxBody", "SandboxInteraction", "SandboxScenario" });
	}
}
