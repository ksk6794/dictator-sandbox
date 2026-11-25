#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "NaturalMeasures.h"
#include "ScenarioSpec.h"

namespace
{
	const EAutomationTestFlags NaturalTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

/** Time beyond a joint's range: 10 frames of 100 outside by more than the half degree, on one joint of two. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNaturalLimitTest, "DictatorSandbox.Scenario.Natural.TimeBeyondRange", NaturalTestFlags)
bool FNaturalLimitTest::RunTest(const FString& Parameters)
{
	FNaturalMeasures Measures;
	Measures.Begin(1.0 / 90.0, FVector(0, 1, 0));
	for (int32 Frame = 0; Frame < 100; ++Frame)
	{
		Measures.NoteLimit(nullptr, TEXT("knee"), Frame < 10 ? 0.6 : 0.4);
		Measures.NoteLimit(nullptr, TEXT("elbow"), 0.0);
	}
	FJsonObject Metrics;
	Measures.WriteMetrics(Metrics);
	TestEqual(TEXT("share of all joint-frames"), Metrics.GetNumberField(TEXT("limit_excess_share")), 0.05, 1.0e-9);
	TestEqual(TEXT("share of the worst joint"), Metrics.GetNumberField(TEXT("limit_excess_joint_share")), 0.1, 1.0e-9);
	TestEqual(TEXT("the worst joint"), Metrics.GetStringField(TEXT("limit_excess_joint")), FString(TEXT("knee")));
	FJsonObject Joint;
	Measures.WriteJoint(TEXT("knee"), Joint);
	TestEqual(TEXT("the joint's own share"), Joint.GetNumberField(TEXT("limit_excess_share")), 0.1, 1.0e-9);
	return true;
}

/** Ahead of the chest along the way the body goes, whatever way that is; a body that goes nowhere has no ahead. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNaturalAheadTest, "DictatorSandbox.Scenario.Natural.AheadOfChest", NaturalTestFlags)
bool FNaturalAheadTest::RunTest(const FString& Parameters)
{
	const FVector Chest(10, 20, 80);
	const FVector Falling(0, 200, -300);
	TestEqual(TEXT("30 cm ahead, going +y"), FNaturalMeasures::AheadCm(Chest + FVector(15, 30, -70), Chest, Falling).Get(0.0), 30.0, 1.0e-6);
	TestEqual(TEXT("beside the hips"), FNaturalMeasures::AheadCm(Chest + FVector(15, -45, -70), Chest, Falling).Get(0.0), -45.0, 1.0e-6);
	TestEqual(TEXT("the same place is ahead going the other way"), FNaturalMeasures::AheadCm(Chest + FVector(15, -45, -70), Chest, FVector(0, -200, -300)).Get(0.0), 45.0, 1.0e-6);
	TestEqual(TEXT("aslant"), FNaturalMeasures::AheadCm(Chest + FVector(10, 0, 0), Chest, FVector(100, 100, 0)).Get(0.0), 10.0 / FMath::Sqrt(2.0), 1.0e-6);
	TestFalse(TEXT("straight down has no ahead"), FNaturalMeasures::AheadCm(Chest + FVector(0, 30, 0), Chest, FVector(0, 5, -400)).IsSet());
	return true;
}

/** Bounds on any figure of the result are read, and what is no bound is refused. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScenarioMeasuresTest, "DictatorSandbox.Scenario.Spec.Measures", NaturalTestFlags)
bool FScenarioMeasuresTest::RunTest(const FString& Parameters)
{
	const auto Parse = [](const FString& Measures, FScenarioSpec& OutSpec, FString& OutError)
	{
		const FString Json = FString::Printf(TEXT("{ \"name\": \"m\", \"duration_s\": 1, \"body\": { \"mesh\": \"/Game/X.X\" }, \"expect\": { \"measures\": %s } }"), *Measures);
		return FScenarioSpec::FromString(Json, OutSpec, OutError);
	};
	FScenarioSpec Spec;
	FString Error;
	TestTrue(TEXT("bounds parse"), Parse(TEXT("{ \"a.b.0\": { \"min\": 1, \"max\": 2 }, \"c\": { \"equals\": true }, \"d\": { \"equals\": \"foot_l\" }, \"e\": { \"equals\": null } }"), Spec, Error));
	TestEqual(TEXT("four of them"), Spec.Expect.Measures.Num(), 4);
	if (Spec.Expect.Measures.Num() == 4)
	{
		TestEqual(TEXT("path"), Spec.Expect.Measures[0].Path, FString(TEXT("a.b.0")));
		TestEqual(TEXT("min"), Spec.Expect.Measures[0].Min.Get(0.0), 1.0);
		TestEqual(TEXT("max"), Spec.Expect.Measures[0].Max.Get(0.0), 2.0);
		TestTrue(TEXT("true"), Spec.Expect.Measures[1].Equals.Get(false));
		TestEqual(TEXT("text"), Spec.Expect.Measures[2].EqualsText.Get(FString()), FString(TEXT("foot_l")));
		TestTrue(TEXT("null"), Spec.Expect.Measures[3].bNull);
	}
	for (const TCHAR* Wrong : { TEXT("{ \"a\": {} }"), TEXT("{ \"a\": { \"minimum\": 1 } }"), TEXT("{ \"a\": { \"min\": true } }"), TEXT("{ \"a\": 1 }"), TEXT("{ \"a\": { \"min\": 1, \"equals\": true } }"), TEXT("{ \".\": { \"min\": 1 } }"), TEXT("[]") })
	{
		FScenarioSpec Other;
		TestFalse(FString::Printf(TEXT("refused: %s"), Wrong), Parse(Wrong, Other, Error));
	}
	return true;
}

#endif
