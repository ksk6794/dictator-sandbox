#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SandboxBodyTuning.h"

namespace
{
	const EAutomationTestFlags BodyTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBodyTuningDefaultsTest, "DictatorSandbox.Body.Tuning.DefaultsAreValid", BodyTestFlags)
bool FBodyTuningDefaultsTest::RunTest(const FString& Parameters)
{
	const FSandboxBodyTuning Tuning;
	FString Error;
	if (!TestTrue(TEXT("default tuning validates"), Tuning.Validate(Error)))
	{
		AddError(Error);
	}

	// Standing needs muscles in every group and an assist on every body the rig names.
	TestTrue(TEXT("every muscle group is on"),
		Tuning.Spine.StiffnessNmPerRad > 0.0 && Tuning.Neck.StiffnessNmPerRad > 0.0 && Tuning.Arms.StiffnessNmPerRad > 0.0 && Tuning.Legs.StiffnessNmPerRad > 0.0);
	TestEqual(TEXT("the pelvis assist leaves the weight to the legs"), Tuning.Pelvis.VerticalNPerM, 0.0);

	TArray<TPair<FString, double>> Values;
	Tuning.ListValues(Values);
	TSet<FString> Names;
	for (const TPair<FString, double>& Value : Values)
	{
		Names.Add(Value.Key);
	}
	TestEqual(TEXT("value names are unique"), Names.Num(), Values.Num());
	TestTrue(TEXT("muscle values are listed"), Names.Contains(TEXT("muscles.legs.stiffness_nm_per_rad")));
	TestTrue(TEXT("assist values are listed"), Names.Contains(TEXT("assist.pelvis.horizontal_n_per_m")));
	TestTrue(TEXT("solver values are listed"), Names.Contains(TEXT("solver.position_iterations")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBodyTuningOverridesTest, "DictatorSandbox.Body.Tuning.OverridesByName", BodyTestFlags)
bool FBodyTuningOverridesTest::RunTest(const FString& Parameters)
{
	FSandboxBodyTuning Tuning;
	FString Error;

	TestTrue(TEXT("a list of overrides applies"), Tuning.ApplyOverrides(TEXT("muscles.legs.stiffness_nm_per_rad=450, assist.feet.damping_s=0.5,solver.position_iterations=32"), Error));
	TestEqual(TEXT("muscle value"), Tuning.Legs.StiffnessNmPerRad, 450.0);
	TestEqual(TEXT("assist value"), Tuning.Feet.DampingS, 0.5);
	TestEqual(TEXT("solver value"), Tuning.Solver.PositionIterations, 32.0);
	TestTrue(TEXT("still valid"), Tuning.Validate(Error));

	TestFalse(TEXT("an unknown name is rejected"), Tuning.ApplyOverrides(TEXT("muscles.tail.stiffness_nm_per_rad=1"), Error));
	TestFalse(TEXT("a rejection carries a reason"), Error.IsEmpty());
	TestFalse(TEXT("a missing value is rejected"), Tuning.ApplyOverrides(TEXT("muscles.legs.stiffness_nm_per_rad"), Error));
	TestFalse(TEXT("a non-numeric value is rejected"), Tuning.ApplyOverrides(TEXT("muscles.legs.stiffness_nm_per_rad=strong"), Error));

	// Out-of-range values are accepted by name and caught by validation.
	TestTrue(TEXT("a negative value can be set"), Tuning.SetValue(TEXT("muscles.arms.stiffness_nm_per_rad"), -1.0, Error));
	TestFalse(TEXT("a negative value does not validate"), Tuning.Validate(Error));
	Tuning = FSandboxBodyTuning();
	TestTrue(TEXT("a fractional count can be set"), Tuning.SetValue(TEXT("solver.position_iterations"), 8.5, Error));
	TestFalse(TEXT("a fractional iteration count does not validate"), Tuning.Validate(Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBodyTuningGroupsTest, "DictatorSandbox.Body.Tuning.JointGroups", BodyTestFlags)
bool FBodyTuningGroupsTest::RunTest(const FString& Parameters)
{
	const TPair<const TCHAR*, ESandboxMuscleGroup> Expected[] = {
		{ TEXT("spine_02"), ESandboxMuscleGroup::Spine }, { TEXT("spine_04"), ESandboxMuscleGroup::Spine },
		{ TEXT("neck_01"), ESandboxMuscleGroup::Neck }, { TEXT("head"), ESandboxMuscleGroup::Neck },
		{ TEXT("clavicle_l"), ESandboxMuscleGroup::Arms }, { TEXT("upperarm_r"), ESandboxMuscleGroup::Arms },
		{ TEXT("lowerarm_l"), ESandboxMuscleGroup::Arms }, { TEXT("hand_r"), ESandboxMuscleGroup::Arms },
		{ TEXT("thigh_l"), ESandboxMuscleGroup::Legs }, { TEXT("calf_r"), ESandboxMuscleGroup::Legs },
		{ TEXT("foot_l"), ESandboxMuscleGroup::Legs }, { TEXT("ball_r"), ESandboxMuscleGroup::Legs } };
	for (const TPair<const TCHAR*, ESandboxMuscleGroup>& Entry : Expected)
	{
		TestEqual(FString::Printf(TEXT("group of %s"), Entry.Key),
			FString(FSandboxBodyTuning::GroupName(FSandboxBodyTuning::GroupOfBone(FName(Entry.Key)))), FString(FSandboxBodyTuning::GroupName(Entry.Value)));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
