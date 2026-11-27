#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "ScenarioSpec.h"

namespace
{
	const EAutomationTestFlags SandboxTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	const TCHAR* ValidSpec = TEXT(R"({
		"name": "unit_test",
		"duration_s": 2.0,
		"fixed_fps": 90,
		"body": { "mesh": "/Game/Some.Mesh", "location_cm": [1, 2, 3], "rotation_deg": [10, 20, 30] },
		"events": [
			{ "t": 1.5, "type": "impulse", "body": "head", "impulse_ns": 30, "direction": [0, 2, 0] },
			{ "t": 0.5, "type": "impulse", "body": "pelvis", "impulse_ns": 80, "direction": [1, 0, 0] }
		],
		"expect": { "rest_within_s": 4.0, "max_joint_stretch_cm": 2.5 }
	})");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScenarioSpecValidTest, "DictatorSandbox.Scenario.Spec.ParsesValidSpec", SandboxTestFlags)
bool FScenarioSpecValidTest::RunTest(const FString& Parameters)
{
	FScenarioSpec Spec;
	FString Error;
	if (!TestTrue(TEXT("valid spec parses"), FScenarioSpec::FromString(ValidSpec, Spec, Error)))
	{
		AddError(Error);
		return false;
	}

	TestEqual(TEXT("name"), Spec.Name, FString(TEXT("unit_test")));
	TestEqual(TEXT("duration"), Spec.DurationS, 2.0);
	TestEqual(TEXT("total frames"), Spec.TotalFrames(), 180);
	TestEqual(TEXT("fixed dt"), Spec.FixedDt(), 1.0 / 90.0);
	TestEqual(TEXT("mesh"), Spec.MeshPath, FString(TEXT("/Game/Some.Mesh")));
	TestEqual(TEXT("location"), Spec.SpawnLocationCm, FVector(1.0, 2.0, 3.0));
	TestEqual(TEXT("rotation"), Spec.SpawnRotation, FRotator(10.0, 20.0, 30.0));

	if (TestEqual(TEXT("event count"), Spec.Events.Num(), 2))
	{
		TestEqual(TEXT("events are sorted by time"), Spec.Events[0].Body, FName(TEXT("pelvis")));
		TestEqual(TEXT("impulse"), Spec.Events[0].ImpulseNs, 80.0);
		TestEqual(TEXT("direction is normalised"), Spec.Events[1].Direction, FVector(0.0, 1.0, 0.0));
	}

	TestTrue(TEXT("rest expectation set"), Spec.Expect.RestWithinS.IsSet());
	TestEqual(TEXT("stretch expectation"), Spec.Expect.MaxJointStretchCm.Get(0.0), 2.5);
	TestFalse(TEXT("expectations that are not listed stay unset"), Spec.Expect.MaxPelvisDriftCm.IsSet());
	TestTrue(TEXT("control defaults to passive"), Spec.Control == EScenarioBodyControl::Passive);
	TestEqual(TEXT("settle time default"), Spec.SettleS, 2.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScenarioSpecActiveTest, "DictatorSandbox.Scenario.Spec.ParsesActiveControl", SandboxTestFlags)
bool FScenarioSpecActiveTest::RunTest(const FString& Parameters)
{
	const TCHAR* Text = TEXT(R"({
		"name": "stand",
		"duration_s": 10.0,
		"settle_s": 1.5,
		"body": { "mesh": "/Game/Some.Mesh", "control": "active" },
		"expect": { "max_pelvis_drift_cm": 5, "min_head_height_cm": 150, "max_settled_speed_mps": 0.05,
			"max_settled_angular_speed_rads": 0.3, "max_joint_speed_rads": 30, "max_physics_step_mean_ms": 1.5 }
	})");

	FScenarioSpec Spec;
	FString Error;
	if (!TestTrue(TEXT("active spec parses"), FScenarioSpec::FromString(Text, Spec, Error)))
	{
		AddError(Error);
		return false;
	}
	TestTrue(TEXT("control is active"), Spec.Control == EScenarioBodyControl::Active);
	TestEqual(TEXT("settle time"), Spec.SettleS, 1.5);
	TestEqual(TEXT("pelvis drift"), Spec.Expect.MaxPelvisDriftCm.Get(0.0), 5.0);
	TestEqual(TEXT("head height"), Spec.Expect.MinHeadHeightCm.Get(0.0), 150.0);
	TestEqual(TEXT("settled speed"), Spec.Expect.MaxSettledSpeedMps.Get(0.0), 0.05);
	TestEqual(TEXT("settled angular speed"), Spec.Expect.MaxSettledAngularSpeedRadS.Get(0.0), 0.3);
	TestEqual(TEXT("joint speed"), Spec.Expect.MaxJointSpeedRadS.Get(0.0), 30.0);
	TestEqual(TEXT("physics step"), Spec.Expect.MaxPhysicsStepMeanMs.Get(0.0), 1.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScenarioSpecTuningTest, "DictatorSandbox.Scenario.Spec.ParsesTuningEvents", SandboxTestFlags)
bool FScenarioSpecTuningTest::RunTest(const FString& Parameters)
{
	const TCHAR* Text = TEXT(R"({
		"name": "tune",
		"duration_s": 8.0,
		"body": { "mesh": "/Game/Some.Mesh", "control": "active" },
		"events": [
			{ "t": 5.0, "type": "edit_tuning_file", "values": { "muscles.legs.stiffness_nm_per_rad": 0, "solver.position_iterations": 32 } },
			{ "t": 4.0, "type": "console", "command": "Sandbox.Tune muscles.arms.stiffness_nm_per_rad=20" }
		],
		"expect": { "fell": true, "min_fall_time_s": 4.0, "tuning_changes": 2, "min_settled_angular_speed_rads": 0.3 }
	})");

	FScenarioSpec Spec;
	FString Error;
	if (!TestTrue(TEXT("spec with tuning events parses"), FScenarioSpec::FromString(Text, Spec, Error)))
	{
		AddError(Error);
		return false;
	}
	if (TestEqual(TEXT("event count"), Spec.Events.Num(), 2))
	{
		TestEqual(TEXT("console event"), Spec.Events[0].Type, FString(TEXT("console")));
		TestEqual(TEXT("its command"), Spec.Events[0].Command, FString(TEXT("Sandbox.Tune muscles.arms.stiffness_nm_per_rad=20")));
		TestEqual(TEXT("file event"), Spec.Events[1].Type, FString(TEXT("edit_tuning_file")));
		TestEqual(TEXT("its values"), Spec.Events[1].Values.Num(), 2);
	}
	TestTrue(TEXT("the spec knows it edits tuning files"), Spec.HasEvent(TEXT("edit_tuning_file")));
	TestFalse(TEXT("and that it pins nothing"), Spec.HasEvent(TEXT("pin")));
	TestTrue(TEXT("fell expectation"), Spec.Expect.Fell.IsSet() && Spec.Expect.Fell.GetValue());
	TestEqual(TEXT("fall time expectation"), Spec.Expect.MinFallTimeS.Get(0.0), 4.0);
	TestEqual(TEXT("tuning changes expectation"), Spec.Expect.TuningChanges.Get(0.0), 2.0);
	TestEqual(TEXT("lower bound on settled angular speed"), Spec.Expect.MinSettledAngularSpeedRadS.Get(0.0), 0.3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScenarioSpecStatesTest, "DictatorSandbox.Scenario.Spec.ParsesPushesAndStates", SandboxTestFlags)
bool FScenarioSpecStatesTest::RunTest(const FString& Parameters)
{
	const TCHAR* Text = TEXT(R"({
		"name": "push",
		"duration_s": 8.0,
		"body": { "mesh": "/Game/Some.Mesh", "control": "active" },
		"events": [
			{ "t": 2.0, "type": "impulse", "body": "thigh_l", "impulse_ns": 200, "direction": [-1, 0, 0], "duration_s": 0.1 },
			{ "t": 1.0, "type": "impulse", "body": "head", "impulse_ns": 10, "direction": [0, 1, 0] }
		],
		"expect": { "end_state": "lying", "state_sequence": "standing>flinch>falling>lying", "max_joint_limit_violation_deg": 5,
			"min_com_margin_cm": 2, "min_capture_margin_cm": 1, "max_energy_gain_j": 20, "max_rest_after_fall_s": 3,
			"max_body_penetration_cm": 2, "max_self_penetration_cm": 1.5 }
	})");

	FScenarioSpec Spec;
	FString Error;
	if (!TestTrue(TEXT("spec with a push and states parses"), FScenarioSpec::FromString(Text, Spec, Error)))
	{
		AddError(Error);
		return false;
	}
	if (TestEqual(TEXT("event count"), Spec.Events.Num(), 2))
	{
		TestEqual(TEXT("a blow has no duration"), Spec.Events[0].DurationS, 0.0);
		TestEqual(TEXT("a push has one"), Spec.Events[1].DurationS, 0.1);
	}
	TestEqual(TEXT("end state"), Spec.Expect.EndState.Get(FString()), FString(TEXT("lying")));
	TestEqual(TEXT("state sequence"), Spec.Expect.StateSequence.Get(FString()), FString(TEXT("standing>flinch>falling>lying")));
	TestEqual(TEXT("limit violation"), Spec.Expect.MaxJointLimitViolationDeg.Get(0.0), 5.0);
	TestEqual(TEXT("margin of the centre of mass"), Spec.Expect.MinComMarginCm.Get(0.0), 2.0);
	TestEqual(TEXT("margin of the capture point"), Spec.Expect.MinCaptureMarginCm.Get(0.0), 1.0);
	TestEqual(TEXT("energy gain"), Spec.Expect.MaxEnergyGainJ.Get(0.0), 20.0);
	TestEqual(TEXT("rest after the fall"), Spec.Expect.MaxRestAfterFallS.Get(0.0), 3.0);
	TestEqual(TEXT("penetration into the world"), Spec.Expect.MaxBodyPenetrationCm.Get(0.0), 2.0);
	TestEqual(TEXT("penetration into itself"), Spec.Expect.MaxSelfPenetrationCm.Get(0.0), 1.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScenarioSpecClipPoseTest, "DictatorSandbox.Scenario.Spec.ParsesClipPoses", SandboxTestFlags)
bool FScenarioSpecClipPoseTest::RunTest(const FString& Parameters)
{
	const TCHAR* Text = TEXT(R"({
		"name": "clip",
		"duration_s": 8.0,
		"body": { "mesh": "/Game/Some.Mesh", "control": "active" },
		"events": [
			{ "t": 0.0, "type": "pin", "body": "pelvis", "lock_rotation": true, "follow_clip": true, "raise_cm": 60 },
			{ "t": 1.0, "type": "clip_pose", "clip": "/Game/Some/Clip", "time_s": 1.5 },
			{ "t": 2.0, "type": "clip_pose", "clip": "/Game/Some/Clip", "time_s": 0, "rate": 1 },
			{ "t": 3.0, "type": "clip_pose", "release": true }
		]
	})");

	FScenarioSpec Spec;
	FString Error;
	if (!TestTrue(TEXT("spec with clip poses parses"), FScenarioSpec::FromString(Text, Spec, Error)))
	{
		AddError(Error);
		return false;
	}
	if (TestEqual(TEXT("event count"), Spec.Events.Num(), 4))
	{
		TestTrue(TEXT("the pin follows the clip"), Spec.Events[0].bLockRotation && Spec.Events[0].bFollowClip);
		TestEqual(TEXT("raised"), Spec.Events[0].RaiseCm, 60.0);
		TestEqual(TEXT("the clip"), Spec.Events[1].Clip, FString(TEXT("/Game/Some/Clip")));
		TestEqual(TEXT("its time"), Spec.Events[1].ClipTimeS, 1.5);
		TestEqual(TEXT("held unless a rate is given"), Spec.Events[1].ClipRate, 0.0);
		TestEqual(TEXT("a rate"), Spec.Events[2].ClipRate, 1.0);
		TestTrue(TEXT("letting go"), Spec.Events[3].bRelease && !Spec.Events[1].bRelease);
	}

	const TCHAR* Invalid[] = {
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "events": [ { "t": 0, "type": "clip_pose" } ] })"),
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "events": [ { "t": 0, "type": "clip_pose", "clip": "/Game/C" } ] })"),
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "events": [ { "t": 0, "type": "clip_pose", "clip": "/Game/C", "time_s": -1 } ] })"),
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "events": [ { "t": 0, "type": "clip_pose", "clip": "/Game/C", "time_s": 0, "rate": -1 } ] })"),
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "events": [ { "t": 0, "type": "clip_pose", "release": 1 } ] })"),
		// A clip gives the pelvis an orientation: a ball joint cannot follow it.
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "events": [ { "t": 0, "type": "pin", "body": "pelvis", "follow_clip": true } ] })"),
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "events": [ { "t": 0, "type": "pin", "body": "pelvis", "lock_rotation": true, "follow_clip": 1 } ] })"),
	};
	for (const TCHAR* Bad : Invalid)
	{
		FScenarioSpec Rejected;
		FString Reason;
		const bool bParsed = FScenarioSpec::FromString(Bad, Rejected, Reason);
		TestFalse(FString::Printf(TEXT("rejects: %s"), Bad), bParsed);
		TestFalse(TEXT("a rejection carries a reason"), !bParsed && Reason.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScenarioSpecEffectorTest, "DictatorSandbox.Scenario.Spec.ParsesEffectorPaths", SandboxTestFlags)
bool FScenarioSpecEffectorTest::RunTest(const FString& Parameters)
{
	const TCHAR* Text = TEXT(R"({
		"name": "hand",
		"duration_s": 8.0,
		"body": { "mesh": "/Game/Some.Mesh", "control": "active" },
		"events": [
			{ "t": 3.0, "type": "effector", "effector": "hand_right", "path": [
				{ "t": 0.0, "location_cm": [-70, 0, 135], "rotation_deg": [0, 90, 0] },
				{ "t": 0.2, "location_cm": [-10, 0, 135] },
				{ "t": 0.8, "location_cm": [-70, 0, 135], "rotation_deg": [0, 0, 0] } ] },
			{ "t": 1.0, "type": "effector", "effector": "hand_left", "path": [ { "t": 0, "location_cm": [50, 50, 100] } ] }
		],
		"expect": { "min_effector_impulse_ns": 5, "max_effector_impulse_ns": 60, "max_effector_force_n": 900,
			"min_effector_final_force_n": 290, "max_effector_final_force_n": 310,
				"max_effector_penetration_cm": 1, "min_effector_impact_speed_mps": 8, "min_struck_displacement_cm": 3 }
	})");

	FScenarioSpec Spec;
	FString Error;
	if (!TestTrue(TEXT("spec with effector paths parses"), FScenarioSpec::FromString(Text, Spec, Error)))
	{
		AddError(Error);
		return false;
	}
	if (TestEqual(TEXT("event count"), Spec.Events.Num(), 2))
	{
		const FScenarioEvent& Right = Spec.Events[1];
		TestEqual(TEXT("events are sorted by time"), Right.Effector, FName(TEXT("hand_right")));
		if (TestEqual(TEXT("key count"), Right.Path.Num(), 3))
		{
			TestEqual(TEXT("key time"), Right.Path[1].TimeS, 0.2);
			TestEqual(TEXT("key location"), Right.Path[1].Location, FVector(-10.0, 0.0, 135.0));
			TestTrue(TEXT("a key without a rotation keeps the one before it"), Right.Path[1].Rotation.Equals(FRotator(0.0, 90.0, 0.0).Quaternion()));
			TestTrue(TEXT("a key with a rotation has its own"), Right.Path[2].Rotation.Equals(FQuat::Identity));
		}
		TestEqual(TEXT("a path of one key is a place to stay"), Spec.Events[0].Path.Num(), 1);
	}
	TestTrue(TEXT("the spec knows it has effectors"), Spec.HasEvent(TEXT("effector")));
	TestEqual(TEXT("least impulse"), Spec.Expect.MinEffectorImpulseNs.Get(0.0), 5.0);
	TestEqual(TEXT("most impulse"), Spec.Expect.MaxEffectorImpulseNs.Get(0.0), 60.0);
	TestEqual(TEXT("peak force"), Spec.Expect.MaxEffectorForceN.Get(0.0), 900.0);
	TestEqual(TEXT("least final force"), Spec.Expect.MinEffectorFinalForceN.Get(0.0), 290.0);
	TestEqual(TEXT("most final force"), Spec.Expect.MaxEffectorFinalForceN.Get(0.0), 310.0);
	TestEqual(TEXT("deepest penetration"), Spec.Expect.MaxEffectorPenetrationCm.Get(0.0), 1.0);
	TestEqual(TEXT("slowest impact"), Spec.Expect.MinEffectorImpactSpeedMps.Get(0.0), 8.0);
	TestEqual(TEXT("least displacement of what was struck"), Spec.Expect.MinStruckDisplacementCm.Get(0.0), 3.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScenarioSpecPropMoveTest, "DictatorSandbox.Scenario.Spec.ParsesPropMove", SandboxTestFlags)
bool FScenarioSpecPropMoveTest::RunTest(const FString& Parameters)
{
	FScenarioSpec Spec;
	FString Error;
	const TCHAR* Good = TEXT(R"({ "name": "a", "duration_s": 5, "body": { "mesh": "/Game/M.M" }, "events": [ { "t": 1, "type": "prop_move", "prop": "plate", "velocity_mps": [0.3, 0, 0], "duration_s": 3, "ramp_s": 1 } ] })");
	if (TestTrue(TEXT("parses"), FScenarioSpec::FromString(Good, Spec, Error)) && TestEqual(TEXT("one event"), Spec.Events.Num(), 1))
	{
		TestEqual(TEXT("the thing"), Spec.Events[0].Prop, FName(TEXT("plate")));
		TestEqual(TEXT("its velocity"), Spec.Events[0].VelocityMps, FVector(0.3, 0.0, 0.0));
		TestEqual(TEXT("how long"), Spec.Events[0].DurationS, 3.0);
		TestEqual(TEXT("the ramp"), Spec.Events[0].RampS, 1.0);
	}
	const TCHAR* Bad[] = {
		TEXT(R"({ "t": 1, "type": "prop_move", "velocity_mps": [0.3, 0, 0], "duration_s": 3 })"),
		TEXT(R"({ "t": 1, "type": "prop_move", "prop": "plate", "velocity_mps": [0, 0, 1], "duration_s": 3 })"),
		TEXT(R"({ "t": 1, "type": "prop_move", "prop": "plate", "velocity_mps": [0.3, 0, 0] })"),
		TEXT(R"({ "t": 1, "type": "prop_move", "prop": "plate", "velocity_mps": [0.3, 0, 0], "duration_s": 3, "ramp_s": -1 })"),
	};
	for (const TCHAR* Event : Bad)
	{
		FScenarioSpec Refused;
		TestFalse(FString::Printf(TEXT("rejects: %s"), Event), FScenarioSpec::FromString(FString::Printf(TEXT(R"({ "name": "a", "duration_s": 5, "body": { "mesh": "/Game/M.M" }, "events": [ %s ] })"), Event), Refused, Error));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScenarioSpecPropBoxesTest, "DictatorSandbox.Scenario.Spec.ParsesThingsOfBoxes", SandboxTestFlags)
bool FScenarioSpecPropBoxesTest::RunTest(const FString& Parameters)
{
	// A thing made of boxes, and one box as before. A thing has the one or the other, not both and not neither.
	FScenarioSpec Spec;
	FString Error;
	if (!TestTrue(TEXT("parses"), FScenarioSpec::FromString(TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "props": [
		{ "name": "table", "mass_kg": 30, "location_cm": [10, 20, 0], "boxes": [ { "center_cm": [0, 0, 73], "size_cm": [120, 70, 4] }, { "center_cm": [-55, -30, 35.5], "size_cm": [5, 5, 71] } ] },
		{ "name": "box", "mass_kg": 5, "location_cm": [0, 0, 20], "size_cm": [40, 40, 40] } ] })"), Spec, Error)))
	{
		AddError(Error);
		return false;
	}
	if (TestEqual(TEXT("two things"), Spec.Props.Num(), 2))
	{
		TestEqual(TEXT("the table's boxes"), Spec.Props[0].Boxes.Num(), 2);
		TestEqual(TEXT("its leg's middle"), Spec.Props[0].Boxes[1].CenterCm, FVector(-55.0, -30.0, 35.5));
		TestEqual(TEXT("its leg's edges"), Spec.Props[0].Boxes[1].SizeCm, FVector(5.0, 5.0, 71.0));
		TestEqual(TEXT("where it stands"), Spec.Props[0].LocationCm, FVector(10.0, 20.0, 0.0));
		TestEqual(TEXT("a box has none"), Spec.Props[1].Boxes.Num(), 0);
		TestEqual(TEXT("a box has edges"), Spec.Props[1].SizeCm, FVector(40.0, 40.0, 40.0));
	}
	const TCHAR* Bad[] = {
		TEXT(R"({ "name": "t", "mass_kg": 30, "location_cm": [0, 0, 0] })"),
		TEXT(R"({ "name": "t", "mass_kg": 30, "location_cm": [0, 0, 0], "size_cm": [10, 10, 10], "boxes": [ { "center_cm": [0, 0, 0], "size_cm": [10, 10, 10] } ] })"),
		TEXT(R"({ "name": "t", "mass_kg": 30, "location_cm": [0, 0, 0], "boxes": [] })"),
		TEXT(R"({ "name": "t", "mass_kg": 30, "location_cm": [0, 0, 0], "boxes": [ { "center_cm": [0, 0, 0] } ] })"),
		TEXT(R"({ "name": "t", "mass_kg": 30, "location_cm": [0, 0, 0], "boxes": [ { "center_cm": [0, 0, 0], "size_cm": [0.1, 10, 10] } ] })"),
	};
	for (const TCHAR* Thing : Bad)
	{
		FScenarioSpec Refused;
		TestFalse(FString::Printf(TEXT("rejects: %s"), Thing), FScenarioSpec::FromString(FString::Printf(TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "props": [ %s ] })"), Thing), Refused, Error));
	}

	// The room with movable furniture: its pieces come with the map, under their own names, unless the scenario has one of the name.
	FScenarioSpec Room;
	const TCHAR* Text = TEXT(R"({ "name": "a", "duration_s": 1, "map": "/Game/Generated/Maps/MovableRoom", "body": { "mesh": "/Game/M.M" },
		"props": [ { "name": "chair", "mass_kg": 8, "location_cm": [0, 0, 20], "size_cm": [40, 40, 40] } ] })");
	if (TestTrue(TEXT("the room's scenario parses"), FScenarioSpec::FromString(Text, Room, Error)))
	{
		TestEqual(TEXT("its own chair and the room's table"), Room.Props.Num(), 2);
		const FScenarioProp* Table = Room.Props.FindByPredicate([](const FScenarioProp& Each) { return Each.Name == FName(TEXT("table")); });
		const FScenarioProp* Chair = Room.Props.FindByPredicate([](const FScenarioProp& Each) { return Each.Name == FName(TEXT("chair")); });
		TestTrue(TEXT("the table has the spec's mass and boxes"), Table && Table->MassKg == 30.0 && Table->Boxes.Num() == 5);
		TestTrue(TEXT("the scenario's chair is its own"), Chair && Chair->MassKg == 8.0 && Chair->Boxes.Num() == 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScenarioSpecInvalidTest, "DictatorSandbox.Scenario.Spec.RejectsInvalidSpecs", SandboxTestFlags)
bool FScenarioSpecInvalidTest::RunTest(const FString& Parameters)
{
	const TCHAR* Invalid[] = {
		TEXT("not json"),
		TEXT(R"({ "duration_s": 1, "body": { "mesh": "/Game/M.M" } })"),
		TEXT(R"({ "name": "a", "duration_s": 0, "body": { "mesh": "/Game/M.M" } })"),
		TEXT(R"({ "name": "a", "duration_s": 1 })"),
		TEXT(R"({ "name": "a", "duration_s": 1, "body": {} })"),
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "events": [ { "t": 0, "type": "teleport" } ] })"),
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "events": [ { "t": 0, "type": "impulse", "body": "head", "impulse_ns": 10, "direction": [0, 0, 0] } ] })"),
		// A misspelt expectation would never be checked, so it must not parse.
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "expect": { "max_pelvis_drift": 5 } })"),
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "expect": { "max_pelvis_drift_cm": "small" } })"),
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M", "control": "puppet" } })"),
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M", "control": "active", "simulate": false } })"),
		TEXT(R"({ "name": "a", "duration_s": 1, "settle_s": -1, "body": { "mesh": "/Game/M.M" } })"),
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "events": [ { "t": 0, "type": "console" } ] })"),
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "events": [ { "t": 0, "type": "edit_tuning_file", "values": {} } ] })"),
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "events": [ { "t": 0, "type": "edit_tuning_file", "values": { "solver.position_iterations": "many" } } ] })"),
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "expect": { "fell": 1 } })"),
		// A state that does not exist could never be reached.
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "expect": { "end_state": "sitting" } })"),
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "expect": { "end_state": "standing>lying" } })"),
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "expect": { "state_sequence": "standing>>lying" } })"),
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "expect": { "state_sequence": 3 } })"),
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "events": [ { "t": 0, "type": "impulse", "body": "head", "impulse_ns": 10, "direction": [0, 1, 0], "duration_s": -1 } ] })"),
		// An effector that does not exist, and paths that cannot be played.
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "events": [ { "t": 0, "type": "effector", "effector": "tail", "path": [ { "t": 0, "location_cm": [0, 0, 0] } ] } ] })"),
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "events": [ { "t": 0, "type": "effector", "effector": "hand_left" } ] })"),
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "events": [ { "t": 0, "type": "effector", "effector": "hand_left", "path": [] } ] })"),
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "events": [ { "t": 0, "type": "effector", "effector": "hand_left", "path": [ { "t": 0, "location_cm": [0, 0] } ] } ] })"),
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "events": [ { "t": 0, "type": "effector", "effector": "hand_left", "path": [ { "t": 0.1, "location_cm": [0, 0, 0] } ] } ] })"),
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "events": [ { "t": 0, "type": "effector", "effector": "hand_left", "path": [ { "t": 0, "location_cm": [0, 0, 0] }, { "t": 0, "location_cm": [1, 0, 0] } ] } ] })"),
		TEXT(R"({ "name": "a", "duration_s": 1, "body": { "mesh": "/Game/M.M" }, "expect": { "min_effector_force_n": 5 } })"),
	};

	for (const TCHAR* Text : Invalid)
	{
		FScenarioSpec Spec;
		FString Error;
		const bool bParsed = FScenarioSpec::FromString(Text, Spec, Error);
		TestFalse(FString::Printf(TEXT("rejects: %s"), Text), bParsed);
		TestFalse(TEXT("a rejection carries a reason"), !bParsed && Error.IsEmpty());
	}
	return true;
}

/** Every scenario file committed to the repo must parse, so a broken file is caught without running it. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScenarioRepoFilesTest, "DictatorSandbox.Scenario.Spec.RepoScenariosParse", SandboxTestFlags)
bool FScenarioRepoFilesTest::RunTest(const FString& Parameters)
{
	const FString Dir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Scenarios"));
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(Dir, TEXT("*.json")), true, false);
	TestTrue(TEXT("at least one scenario exists"), Files.Num() > 0);

	for (const FString& File : Files)
	{
		FScenarioSpec Spec;
		FString Error;
		if (!FScenarioSpec::LoadFile(FPaths::Combine(Dir, File), Spec, Error))
		{
			AddError(Error);
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s: name matches file name"), *File), Spec.Name, FPaths::GetBaseFilename(File));
	}
	return true;
}

/** `body.laid_deg`: which way the body lies for each turn, and the files the rig refuses (R2, step 7). */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScenarioLaidTest, "DictatorSandbox.Scenario.Spec.LaysTheBodyDown", SandboxTestFlags)
bool FScenarioLaidTest::RunTest(const FString& Parameters)
{
	const auto Laid = [](const FString& Body, FScenarioSpec& OutSpec, FString& OutError)
	{
		return FScenarioSpec::FromString(FString::Printf(TEXT(R"({ "name": "laid", "duration_s": 1.0, "body": { "mesh": "/Game/Some.Mesh", %s } })"), *Body), OutSpec, OutError);
	};
	const auto Near = [this](const TCHAR* What, const FVector& Got, const FVector& Want)
	{
		TestTrue(FString::Printf(TEXT("%s: (%.3f, %.3f, %.3f), want (%.0f, %.0f, %.0f)"), What, Got.X, Got.Y, Got.Z, Want.X, Want.Y, Want.Z), Got.Equals(Want, 1e-6));
	};
	// Unturned, the body faces +y with its head up +z and its right toward -x.
	const FVector Head = FVector::ZAxisVector;
	const FVector Face = FVector::YAxisVector;
	const FVector Right = -FVector::XAxisVector;
	FScenarioSpec Spec;
	FString Error;

	TestTrue(TEXT("on its back parses"), Laid(TEXT(R"("location_cm": [0, 90, 0], "laid_deg": 0)"), Spec, Error));
	TestTrue(TEXT("laid"), Spec.bLaid);
	Near(TEXT("on its back, the head where its back faced"), Spec.LaidRotation().RotateVector(Head), -FVector::YAxisVector);
	Near(TEXT("on its back, the face up"), Spec.LaidRotation().RotateVector(Face), FVector::ZAxisVector);

	TestTrue(TEXT("on its right side parses"), Laid(TEXT(R"("location_cm": [0, 0, 0], "laid_deg": 90)"), Spec, Error));
	Near(TEXT("on its right side, the right down"), Spec.LaidRotation().RotateVector(Right), -FVector::ZAxisVector);
	TestTrue(TEXT("on its left side parses"), Laid(TEXT(R"("location_cm": [0, 0, 0], "laid_deg": -90)"), Spec, Error));
	Near(TEXT("on its left side, the left down"), Spec.LaidRotation().RotateVector(-Right), -FVector::ZAxisVector);
	TestTrue(TEXT("on its front parses"), Laid(TEXT(R"("location_cm": [0, 0, 0], "laid_deg": 180)"), Spec, Error));
	Near(TEXT("on its front, the face down"), Spec.LaidRotation().RotateVector(Face), -FVector::ZAxisVector);

	// Spawned turned 90 degrees about up (facing -x), on its back: the head toward +x, where its back faced.
	TestTrue(TEXT("turned and on its back parses"), Laid(TEXT(R"("location_cm": [0, 0, 0], "rotation_deg": [0, 90, 0], "laid_deg": 0)"), Spec, Error));
	Near(TEXT("turned, the head where its back faced"), Spec.LaidRotation().RotateVector(Spec.SpawnRotation.RotateVector(Head)), FVector::XAxisVector);

	// Refused: no place, a place off the floor, a body that does not simulate, a height without a turn, a turn out of range.
	TestFalse(TEXT("no location refused"), Laid(TEXT(R"("laid_deg": 0)"), Spec, Error));
	TestFalse(TEXT("off the floor refused"), Laid(TEXT(R"("location_cm": [0, 0, 20], "laid_deg": 0)"), Spec, Error));
	TestFalse(TEXT("kinematic refused"), Laid(TEXT(R"("location_cm": [0, 0, 0], "laid_deg": 0, "simulate": false)"), Spec, Error));
	TestFalse(TEXT("height alone refused"), Laid(TEXT(R"("location_cm": [0, 0, 0], "laid_height_cm": 5)"), Spec, Error));
	TestFalse(TEXT("turn out of range refused"), Laid(TEXT(R"("location_cm": [0, 0, 0], "laid_deg": 200)"), Spec, Error));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
