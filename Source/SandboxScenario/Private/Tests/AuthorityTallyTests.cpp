#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AuthorityTally.h"
#include "Misc/AutomationTest.h"

namespace
{
	const EAutomationTestFlags AuthorityTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	FSandboxAuthorityEntry Entry(const TCHAR* Bone, FSandboxAuthorityEntry::ESource Source, const FVector& ForceN, const FVector& TorqueNm = FVector::ZeroVector)
	{
		FSandboxAuthorityEntry Result;
		Result.Bone = FName(Bone);
		Result.Source = Source;
		Result.ForceN = ForceN;
		Result.TorqueNm = TorqueNm;
		return Result;
	}
}

/**
 * control.authority (decision 15): the world's forces added up by state and, where asked, body by body; by direction; as shares of
 * the body's weight. Made-up frames whose answers are worked out by hand.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAuthorityTallyTest, "DictatorSandbox.Scenario.Authority.TalliesByStateBodyAndDirection", AuthorityTestFlags)
bool FAuthorityTallyTest::RunTest(const FString& Parameters)
{
	using ESource = FSandboxAuthorityEntry::ESource;
	constexpr double WeightN = 800.0;
	constexpr double FrameS = 0.01;
	FSandboxAuthorityTally Tally;

	// Two frames standing: the assist on the pelvis over the floor, 8 N and then nothing. Not by body.
	Tally.AddFrame(TEXT("standing"), { Entry(TEXT("pelvis"), ESource::Assist, FVector(8.0, 0.0, 0.0)) }, WeightN, FrameS, false);
	Tally.AddFrame(TEXT("standing"), {}, WeightN, FrameS, false);
	// Two frames getting up, body by body. First: the pelvis carried up with 400 N, a thigh pulled 300 N over the floor and
	// 400 N up (500 N in all), a torque of 3 N*m. Second: the pelvis pressed down with 80 N (the old assist may push down).
	Tally.AddFrame(TEXT("get_up"), {
		Entry(TEXT("pelvis"), ESource::Carry, FVector(0.0, 0.0, 400.0)),
		Entry(TEXT("thigh_l"), ESource::Carry, FVector(300.0, 0.0, 400.0), FVector(0.0, 3.0, 0.0)) }, WeightN, FrameS, true);
	Tally.AddFrame(TEXT("get_up"), { Entry(TEXT("pelvis"), ESource::Assist, FVector(0.0, 0.0, -80.0)) }, WeightN, FrameS, true);

	const FSandboxAuthorityTally::FSums* Standing = Tally.FindState(TEXT("standing"));
	const FSandboxAuthorityTally::FSums* GetUp = Tally.FindState(TEXT("get_up"));
	if (!TestNotNull(TEXT("standing is counted"), Standing) || !TestNotNull(TEXT("the get-up is counted"), GetUp))
	{
		return false;
	}
	TestEqual(TEXT("standing: two frames"), Standing->Frames, 2);
	TestEqual(TEXT("standing: the mean is half of the one frame's 1%"), Standing->Mean(FSandboxAuthorityTally::All), 0.005, 1.0e-12);
	TestEqual(TEXT("standing: the peak is that frame's"), Standing->Peak[FSandboxAuthorityTally::All], 0.01, 1.0e-12);
	TestEqual(TEXT("standing: all of it over the floor"), Standing->Mean(FSandboxAuthorityTally::Horizontal), 0.005, 1.0e-12);
	TestEqual(TEXT("standing: nothing up"), Standing->Mean(FSandboxAuthorityTally::Up), 0.0, 1.0e-12);

	// Frame 1: sizes 400 + 500 = 900 N (1.125 weights), up 800 (1.0), over the floor 300 (0.375). Frame 2: 80 N down (0.1).
	TestEqual(TEXT("get-up: the mean of the sizes"), GetUp->Mean(FSandboxAuthorityTally::All), (1.125 + 0.1) / 2.0, 1.0e-12);
	TestEqual(TEXT("get-up: the peak of the sizes"), GetUp->Peak[FSandboxAuthorityTally::All], 1.125, 1.0e-12);
	TestEqual(TEXT("get-up: up"), GetUp->Mean(FSandboxAuthorityTally::Up), 0.5, 1.0e-12);
	TestEqual(TEXT("get-up: over the floor"), GetUp->Mean(FSandboxAuthorityTally::Horizontal), 0.1875, 1.0e-12);
	TestEqual(TEXT("get-up: down, only the second frame's"), GetUp->Mean(FSandboxAuthorityTally::Down), 0.05, 1.0e-12);
	TestEqual(TEXT("get-up: down's peak"), GetUp->Peak[FSandboxAuthorityTally::Down], 0.1, 1.0e-12);
	TestEqual(TEXT("get-up: the torque's peak"), GetUp->TorquePeakNm, 3.0, 1.0e-12);

	// Body by body: the thigh (0.625 in one frame of two: 0.3125), the carried pelvis (0.25), the assisted pelvis (0.05), each
	// source of the pelvis apart; nothing for standing.
	const TArray<FSandboxAuthorityTally::FBodySums> Bodies = Tally.BodiesOf(TEXT("get_up"));
	if (TestEqual(TEXT("three bodies and sources"), Bodies.Num(), 3))
	{
		TestEqual(TEXT("the thigh first"), Bodies[0].Bone, FName(TEXT("thigh_l")));
		TestEqual(TEXT("its sum"), Bodies[0].Sum[FSandboxAuthorityTally::All], 0.625, 1.0e-12);
		TestEqual(TEXT("its peak"), Bodies[0].Peak, 0.625, 1.0e-12);
		TestTrue(TEXT("then the carried pelvis"), Bodies[1].Bone == FName(TEXT("pelvis")) && Bodies[1].Source == ESource::Carry);
		TestTrue(TEXT("then the assisted pelvis, pushed down"), Bodies[2].Source == ESource::Assist && FMath::IsNearlyEqual(Bodies[2].Sum[FSandboxAuthorityTally::Down], 0.1, 1.0e-12));
	}
	TestEqual(TEXT("standing is not counted body by body"), Tally.BodiesOf(TEXT("standing")).Num(), 0);

	// The JSON: a state's mean and a body's mean over every frame of its state.
	const TSharedRef<FJsonObject> Json = Tally.ToJson();
	const TSharedPtr<FJsonObject>* States = nullptr;
	const TSharedPtr<FJsonObject>* GetUpJson = nullptr;
	if (TestTrue(TEXT("states"), Json->TryGetObjectField(TEXT("states"), States)) && TestTrue(TEXT("the get-up's"), (*States)->TryGetObjectField(TEXT("get_up"), GetUpJson)))
	{
		TestEqual(TEXT("its seconds"), (*GetUpJson)->GetNumberField(TEXT("seconds")), 0.02, 1.0e-12);
		TestEqual(TEXT("its mean"), (*GetUpJson)->GetNumberField(TEXT("mean")), 0.6125, 1.0e-12);
	}
	const TArray<TSharedPtr<FJsonValue>>* BodiesJson = nullptr;
	if (TestTrue(TEXT("the get-up's bodies"), Json->TryGetArrayField(TEXT("get_up_bodies"), BodiesJson)) && BodiesJson->Num() == 3)
	{
		TestEqual(TEXT("the thigh's mean over both frames"), (*BodiesJson)[0]->AsObject()->GetNumberField(TEXT("mean")), 0.3125, 1.0e-12);
		TestEqual(TEXT("the thigh is carried"), (*BodiesJson)[0]->AsObject()->GetStringField(TEXT("source")), FString(TEXT("carry")));
	}
	TestEqual(TEXT("the weight"), Json->GetNumberField(TEXT("weight_n")), WeightN, 1.0e-12);
	return true;
}

#endif
