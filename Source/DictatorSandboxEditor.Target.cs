using UnrealBuildTool;

public class DictatorSandboxEditorTarget : TargetRules
{
	public DictatorSandboxEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.AddRange(new string[] { "SandboxCore", "SandboxBody", "SandboxInteraction", "SandboxScenario", "SandboxEditor" });
	}
}
