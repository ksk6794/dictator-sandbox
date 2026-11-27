#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "SandboxBodyTuning.h"
#include "SandboxParamStore.h"

namespace
{
	const EAutomationTestFlags BodyTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	/**
	 * A store of its own on the repo's tuning files, so a test never touches the process-wide one. The
	 * directory holds the values of every module (effectors, tools), so the store takes the process-wide
	 * store's schema, which every module has declared its names to.
	 */
	bool LoadRepoTuning(FSandboxParamStore& Store, FString& OutError)
	{
		Store = FSandboxParamStore::Get().CloneSchema();
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
	TestTrue(TEXT("surface values are listed"), Names.Contains(TEXT("surface.friction")) && Names.Contains(TEXT("surface.feet_friction")) && Names.Contains(TEXT("surface.restitution")));
	TestTrue(TEXT("thresholds are listed"), Names.Contains(TEXT("thresholds.fall_pelvis_drop_cm")));
	TestTrue(TEXT("balance decisions are listed"), Names.Contains(TEXT("balance.step_reach_cm")));
	TestTrue(TEXT("reactions are listed"), Names.Contains(TEXT("reactions.flinch_weakening")));

	// What the states are for: a flinch weakens, a falling body is let go and goes limp, a lying one more so.
	TestTrue(TEXT("a flinch weakens the hit muscles"), Tuning.Reactions.FlinchWeakening < 1.0 && Tuning.Reactions.FlinchRecoveryS > 0.0);
	FSandboxBodyTuning Standing, Falling, Lying;
	if (TestTrue(TEXT("the standing state reads"), FSandboxBodyTuning::FromParams(Store, FSandboxBodyTuning::StateName(ESandboxBodyState::Standing), Standing, Error)) &&
		TestTrue(TEXT("the falling state reads"), FSandboxBodyTuning::FromParams(Store, FSandboxBodyTuning::StateName(ESandboxBodyState::Falling), Falling, Error)) &&
		TestTrue(TEXT("the lying state reads"), FSandboxBodyTuning::FromParams(Store, FSandboxBodyTuning::StateName(ESandboxBodyState::Lying), Lying, Error)))
	{
		TestTrue(TEXT("standing uses the base values"), Standing.HasSameValues(Tuning));
		const FSandboxBodyTuning* Down[] = { &Falling, &Lying };
		for (const FSandboxBodyTuning* State : Down)
		{
			const FSandboxAssistTuning* Assists[] = { &State->Pelvis, &State->Chest, &State->Head, &State->Feet };
			for (const FSandboxAssistTuning* Assist : Assists)
			{
				TestTrue(TEXT("a body that is going down or lying has no balance assist"),
					Assist->HorizontalNPerM == 0.0 && Assist->VerticalNPerM == 0.0 && Assist->AngularNmPerRad == 0.0);
			}
		}
		TestTrue(TEXT("falling muscles are weaker than standing ones"), Falling.Legs.StiffnessNmPerRad < Standing.Legs.StiffnessNmPerRad && Falling.Spine.StiffnessNmPerRad < Standing.Spine.StiffnessNmPerRad);
		TestTrue(TEXT("lying muscles are weaker still"), Lying.Legs.StiffnessNmPerRad < Falling.Legs.StiffnessNmPerRad && Lying.Spine.StiffnessNmPerRad < Falling.Spine.StiffnessNmPerRad);
	}
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
	TestFalse(TEXT("a combine mode that is not one of the four is rejected"), Store.SetOverrides(TEXT("surface.friction_combine=1.5"), Error));
	TestFalse(TEXT("a restitution above 1 is rejected"), Store.SetOverrides(TEXT("surface.restitution=1.2"), Error));
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

	// A state the files give no values of their own, so the test does not depend on the tuning in force.
	TestTrue(TEXT("a state can carry its own values"),
		Store.SetOverrides(TEXT("states.stagger.muscles.legs.stiffness_nm_per_rad=50,states.stagger.assist.pelvis.horizontal_n_per_m=0"), Error));

	FSandboxBodyTuning Falling, Standing, Again;
	TestTrue(TEXT("the state reads"), FSandboxBodyTuning::FromParams(Store, TEXT("stagger"), Falling, Error));
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
		{ TEXT("clavicle_l"), ESandboxMuscleGroup::Spine }, { TEXT("upperarm_r"), ESandboxMuscleGroup::Arms },
		{ TEXT("lowerarm_l"), ESandboxMuscleGroup::Arms }, { TEXT("hand_r"), ESandboxMuscleGroup::Arms },
		{ TEXT("thigh_l"), ESandboxMuscleGroup::Legs }, { TEXT("calf_r"), ESandboxMuscleGroup::Legs },
		{ TEXT("foot_l"), ESandboxMuscleGroup::Legs }, { TEXT("ball_r"), ESandboxMuscleGroup::Toes } };
	for (const TPair<const TCHAR*, ESandboxMuscleGroup>& Entry : Expected)
	{
		TestEqual(FString::Printf(TEXT("group of %s"), Entry.Key),
			FString(FSandboxBodyTuning::GroupName(FSandboxBodyTuning::GroupOfBone(FName(Entry.Key)))), FString(FSandboxBodyTuning::GroupName(Entry.Value)));
	}
	return true;
}

/**
 * A pose's mirror image (FSandboxPoseTuning::Mirrored; the side fall and, since R2's step 9, the roll of a body on its right side):
 * made-up angles whose left and right differ. The limbs change sides with the same numbers, the trunk keeps its bending forward and
 * back and turns its sideways bend and twist round, and twice mirrored is the pose itself. (Checked to fail with the swap of sides
 * taken out of Mirrored, 27 November 2026.)
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBodyTuningPoseMirrorTest, "DictatorSandbox.Body.Tuning.PoseMirror", BodyTestFlags)
bool FBodyTuningPoseMirrorTest::RunTest(const FString& Parameters)
{
	FSandboxPose Pose;
	for (int32 Index = 0; Index < SandboxPoseJointCount; ++Index)
	{
		Pose.Joints[Index].Swing1Deg = 1.0 + Index;
		Pose.Joints[Index].Swing2Deg = 20.0 + 2.0 * Index;
		Pose.Joints[Index].TwistDeg = -40.0 - 3.0 * Index;
	}
	const auto IndexOf = [](const TCHAR* Name) -> int32
	{
		for (int32 Index = 0; Index < SandboxPoseJointCount; ++Index)
		{
			if (FCString::Strcmp(SandboxPoseJoints[Index], Name) == 0)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	};
	const FSandboxPose Mirror = FSandboxPoseTuning::Mirrored(Pose);
	for (const TCHAR* const Limb : { TEXT("upperarm"), TEXT("lowerarm"), TEXT("thigh"), TEXT("calf"), TEXT("clavicle") })
	{
		const int32 Left = IndexOf(*FString::Printf(TEXT("%s_l"), Limb));
		const int32 Right = IndexOf(*FString::Printf(TEXT("%s_r"), Limb));
		if (!TestTrue(FString::Printf(TEXT("%s has both sides"), Limb), Left != INDEX_NONE && Right != INDEX_NONE))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s: the left takes the right's swing1"), Limb), Mirror.Joints[Left].Swing1Deg, Pose.Joints[Right].Swing1Deg);
		TestEqual(FString::Printf(TEXT("%s: the right takes the left's swing2"), Limb), Mirror.Joints[Right].Swing2Deg, Pose.Joints[Left].Swing2Deg);
		TestEqual(FString::Printf(TEXT("%s: and its twist"), Limb), Mirror.Joints[Right].TwistDeg, Pose.Joints[Left].TwistDeg);
	}
	const int32 Spine = IndexOf(TEXT("spine_02"));
	TestEqual(TEXT("the trunk keeps its bend forward"), Mirror.Joints[Spine].Swing1Deg, Pose.Joints[Spine].Swing1Deg);
	TestEqual(TEXT("the trunk's sideways bend turns round"), Mirror.Joints[Spine].Swing2Deg, -Pose.Joints[Spine].Swing2Deg);
	TestEqual(TEXT("the trunk's twist turns round"), Mirror.Joints[Spine].TwistDeg, -Pose.Joints[Spine].TwistDeg);
	const FSandboxPose Twice = FSandboxPoseTuning::Mirrored(Mirror);
	for (int32 Index = 0; Index < SandboxPoseJointCount; ++Index)
	{
		TestTrue(FString::Printf(TEXT("twice mirrored, %s is itself"), SandboxPoseJoints[Index]),
			Twice.Joints[Index].Swing1Deg == Pose.Joints[Index].Swing1Deg && Twice.Joints[Index].Swing2Deg == Pose.Joints[Index].Swing2Deg &&
			Twice.Joints[Index].TwistDeg == Pose.Joints[Index].TwistDeg);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
