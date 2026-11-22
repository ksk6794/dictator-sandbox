#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "SandboxBodyTuning.h"
#include "SandboxParamStore.h"

namespace
{
	const EAutomationTestFlags BodyTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/** A store of its own on the repo's tuning files, so a test never touches the process-wide one. */
	bool LoadRepoTuning(FSandboxParamStore& Store, FString& OutError)
	{
		FSandboxBodyTuning::DeclareParams(Store);
		Store.SetDirectory(FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("Tuning")));
		return Store.Load(OutError);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBodyTuningRepoFilesTest, "DictatorSandbox.Body.Tuning.RepoFilesAreValid", BodyTestFlags)
bool FBodyTuningRepoFilesTest::RunTest(const FString& Parameters)
{
	// The code holds no values: an empty tuning must not pass for a usable one.
	FString Error;
	TestFalse(TEXT("a default-constructed tuning does not validate"), FSandboxBodyTuning().Validate(Error));

	FSandboxParamStore Store;
	FSandboxBodyTuning Tuning;
	if (!TestTrue(TEXT("Config/Tuning loads"), LoadRepoTuning(Store, Error)) ||
		!TestTrue(TEXT("and gives a valid tuning"), FSandboxBodyTuning::FromParams(Store, FString(), Tuning, Error)))
	{
		AddError(Error);
		return false;
	}

	// Standing needs muscles in every group and an assist on every body the rig names.
	TestTrue(TEXT("every muscle group is on"),
		Tuning.Spine.StiffnessNmPerRad > 0.0 && Tuning.Neck.StiffnessNmPerRad > 0.0 && Tuning.Arms.StiffnessNmPerRad > 0.0 && Tuning.Legs.StiffnessNmPerRad > 0.0);
	TestEqual(TEXT("the pelvis assist leaves the weight to the legs"), Tuning.Pelvis.VerticalNPerM, 0.0);
	TestFalse(TEXT("the rig names its bones"), Tuning.Rig.Pelvis.IsNone() || Tuning.Rig.Head.IsNone() || Tuning.Rig.FootLeft.IsNone());

	// Every listed value comes from the files, under the name it is listed with.
	TArray<TPair<FString, double>> Values;
	Tuning.ListValues(Values);
	TSet<FString> Names;
	for (const TPair<FString, double>& Value : Values)
	{
		Names.Add(Value.Key);
		double InStore = 0.0;
		TestTrue(FString::Printf(TEXT("%s is in the files"), *Value.Key), Store.FindNumber(Value.Key, FString(), InStore));
		TestEqual(FString::Printf(TEXT("%s is read as written"), *Value.Key), Value.Value, InStore);
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
	FSandboxParamStore Store;
	FSandboxBodyTuning Base;
	FString Error;
	if (!TestTrue(TEXT("Config/Tuning loads"), LoadRepoTuning(Store, Error)) ||
		!TestTrue(TEXT("and gives a valid tuning"), FSandboxBodyTuning::FromParams(Store, FString(), Base, Error)))
	{
		AddError(Error);
		return false;
	}

	FSandboxBodyTuning Tuning;
	TestTrue(TEXT("a list of overrides applies"), Store.SetOverrides(TEXT("muscles.legs.stiffness_nm_per_rad=450, assist.feet.damping_s=0.5,solver.position_iterations=32"), Error));
	TestTrue(TEXT("the overridden tuning reads"), FSandboxBodyTuning::FromParams(Store, FString(), Tuning, Error));
	TestEqual(TEXT("muscle value"), Tuning.Legs.StiffnessNmPerRad, 450.0);
	TestEqual(TEXT("assist value"), Tuning.Feet.DampingS, 0.5);
	TestEqual(TEXT("solver value"), Tuning.Solver.PositionIterations, 32.0);
	TestEqual(TEXT("the rest is as in the files"), Tuning.Arms.StiffnessNmPerRad, Base.Arms.StiffnessNmPerRad);
	TestFalse(TEXT("the tuning differs from the files"), Tuning.HasSameValues(Base));

	// The body's own checks guard the store: nothing it could not use gets in.
	TestFalse(TEXT("an unknown name is rejected"), Store.SetOverrides(TEXT("muscles.tail.stiffness_nm_per_rad=1"), Error));
	TestFalse(TEXT("a rejection carries a reason"), Error.IsEmpty());
	TestFalse(TEXT("a negative stiffness is rejected"), Store.SetOverrides(TEXT("muscles.arms.stiffness_nm_per_rad=-1"), Error));
	TestFalse(TEXT("a fractional iteration count is rejected"), Store.SetOverrides(TEXT("solver.position_iterations=8.5"), Error));
	TestFalse(TEXT("a rig bone is not a number"), Store.SetOverrides(TEXT("rig.pelvis=1"), Error));
	TestTrue(TEXT("the tuning still reads"), FSandboxBodyTuning::FromParams(Store, FString(), Tuning, Error));
	TestEqual(TEXT("and is unchanged by the rejections"), Tuning.Legs.StiffnessNmPerRad, 450.0);

	TestTrue(TEXT("overrides can be cleared"), Store.ClearOverrides(FString(), Error));
	TestTrue(TEXT("the tuning reads"), FSandboxBodyTuning::FromParams(Store, FString(), Tuning, Error));
	TestTrue(TEXT("and equals the files again"), Tuning.HasSameValues(Base));

	// By-name access on the struct itself, as the result files use it.
	TestTrue(TEXT("a value can be set by name"), Tuning.SetValue(TEXT("muscles.arms.stiffness_nm_per_rad"), -1.0, Error));
	TestFalse(TEXT("a negative value does not validate"), Tuning.Validate(Error));
	TestFalse(TEXT("an unknown name cannot be set"), Tuning.SetValue(TEXT("muscles.tail.stiffness_nm_per_rad"), 1.0, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBodyTuningStatesTest, "DictatorSandbox.Body.Tuning.StatesReplaceBaseValues", BodyTestFlags)
bool FBodyTuningStatesTest::RunTest(const FString& Parameters)
{
	FSandboxParamStore Store;
	FSandboxBodyTuning Base;
	FString Error;
	if (!TestTrue(TEXT("Config/Tuning loads"), LoadRepoTuning(Store, Error)) ||
		!TestTrue(TEXT("and gives a valid tuning"), FSandboxBodyTuning::FromParams(Store, FString(), Base, Error)))
	{
		AddError(Error);
		return false;
	}

	// What part 3 will write into the "states" section of the file, here as overrides.
	TestTrue(TEXT("a state can carry its own values"),
		Store.SetOverrides(TEXT("states.falling.muscles.legs.stiffness_nm_per_rad=50,states.falling.assist.pelvis.horizontal_n_per_m=0"), Error));

	FSandboxBodyTuning Falling, Standing, Again;
	TestTrue(TEXT("the state reads"), FSandboxBodyTuning::FromParams(Store, TEXT("falling"), Falling, Error));
	TestEqual(TEXT("its own muscle value"), Falling.Legs.StiffnessNmPerRad, 50.0);
	TestEqual(TEXT("its own assist value"), Falling.Pelvis.HorizontalNPerM, 0.0);
	TestEqual(TEXT("the rest comes from the base"), Falling.Arms.StiffnessNmPerRad, Base.Arms.StiffnessNmPerRad);

	TestTrue(TEXT("a state without values of its own reads"), FSandboxBodyTuning::FromParams(Store, TEXT("standing"), Standing, Error));
	TestTrue(TEXT("and equals the base"), Standing.HasSameValues(Base));
	TestTrue(TEXT("the base reads"), FSandboxBodyTuning::FromParams(Store, FString(), Again, Error));
	TestTrue(TEXT("and is untouched by the state"), Again.HasSameValues(Base));

	TestFalse(TEXT("an unknown state is rejected"), Store.SetOverrides(TEXT("states.dancing.muscles.legs.stiffness_nm_per_rad=50"), Error));
	TestFalse(TEXT("a state value out of range is rejected"), Store.SetOverrides(TEXT("states.lying.muscles.legs.damping_s=-1"), Error));
	TestTrue(TEXT("the reason names the state"), Error.Contains(TEXT("lying")));
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
