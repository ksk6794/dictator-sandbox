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

#endif // WITH_DEV_AUTOMATION_TESTS
