#include "Misc/AutomationTest.h"
#include "SandboxCarry.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	const EAutomationTestFlags CarryTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

/**
 * Decision 14: a hold takes the led get-up's springs off its chain toward the pelvis. A made-up tree: 0 pelvis (the root), 1 spine,
 * 2 chest, 3 upper arm, 4 forearm, 5 hand, 6 thigh, 7 calf, 8 head.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCarryChainHoldsTest, "DictatorSandbox.Body.Carry.ChainHoldsReachThePelvis", CarryTestFlags)
bool FCarryChainHoldsTest::RunTest(const FString& Parameters)
{
	const TArray<int32> Parents = { INDEX_NONE, 0, 1, 2, 3, 4, 0, 6, 2 };
	const auto Holds = [&Parents](int32 Held, int32 Mode, double Falloff, double Hold)
	{
		TArray<double> Result;
		Result.Init(0.0, Parents.Num());
		FSandboxCarry::ChainHolds(Parents, Held, Mode, Falloff, Hold, Result);
		return Result;
	};

	// The forearm held, the whole chain to the pelvis: forearm, upper arm, chest, spine, pelvis; not the hand, the leg or the head.
	const TArray<double> Whole = Holds(4, 2, 0.0, 0.6);
	const double WholeExpected[] = { 0.6, 0.6, 0.6, 0.6, 0.6, 0.0, 0.0, 0.0, 0.0 };
	for (int32 Index = 0; Index < Parents.Num(); ++Index)
	{
		TestEqual(*FString::Printf(TEXT("mode 2, body %d"), Index), Whole[Index], WholeExpected[Index], 1.0e-12);
	}
	// Without the pelvis.
	const TArray<double> NoRoot = Holds(4, 1, 0.0, 0.6);
	TestEqual(TEXT("mode 1 leaves the pelvis"), NoRoot[0], 0.0, 1.0e-12);
	TestEqual(TEXT("mode 1 takes the spine"), NoRoot[1], 0.6, 1.0e-12);
	// The held body alone.
	const TArray<double> Alone = Holds(4, 0, 0.0, 0.6);
	TestEqual(TEXT("mode 0 takes the forearm"), Alone[4], 0.6, 1.0e-12);
	TestEqual(TEXT("mode 0 leaves the upper arm"), Alone[3], 0.0, 1.0e-12);
	// A falloff of a half: the forearm 1, the upper arm 0.5, the chest 0.25, the spine 0.125, the pelvis 0.0625; a hold above its
	// full force counts as 1.
	const TArray<double> Falling = Holds(4, 2, 0.5, 3.0);
	TestEqual(TEXT("falloff: forearm"), Falling[4], 1.0, 1.0e-12);
	TestEqual(TEXT("falloff: upper arm"), Falling[3], 0.5, 1.0e-12);
	TestEqual(TEXT("falloff: pelvis"), Falling[0], 0.0625, 1.0e-12);
	// Mode 3: the chain and what hangs from the held body. The chest held: the arm, the hand and the head below it too, by depth with
	// a falloff of a half (upper arm 0.5, forearm 0.25, hand 0.125); not the leg.
	const TArray<double> Tree = Holds(2, 3, 0.5, 1.0);
	TestEqual(TEXT("mode 3: the chest"), Tree[2], 1.0, 1.0e-12);
	TestEqual(TEXT("mode 3: the pelvis, two below"), Tree[0], 0.25, 1.0e-12);
	TestEqual(TEXT("mode 3: the upper arm"), Tree[3], 0.5, 1.0e-12);
	TestEqual(TEXT("mode 3: the hand"), Tree[5], 0.125, 1.0e-12);
	TestEqual(TEXT("mode 3: the head"), Tree[8], 0.5, 1.0e-12);
	TestEqual(TEXT("mode 3: not the thigh"), Tree[6], 0.0, 1.0e-12);
	// The pelvis held in mode 1 is still held itself.
	TestEqual(TEXT("the held pelvis is held"), Holds(0, 1, 0.0, 0.4)[0], 0.4, 1.0e-12);
	// Two holds: each body keeps the larger.
	TArray<double> Two;
	Two.Init(0.0, Parents.Num());
	FSandboxCarry::ChainHolds(Parents, 7, 2, 0.0, 0.3, Two);
	FSandboxCarry::ChainHolds(Parents, 8, 2, 0.0, 0.8, Two);
	TestEqual(TEXT("the calf by the first"), Two[7], 0.3, 1.0e-12);
	TestEqual(TEXT("the head by the second"), Two[8], 0.8, 1.0e-12);
	TestEqual(TEXT("the pelvis by the larger"), Two[0], 0.8, 1.0e-12);
	TestEqual(TEXT("the thigh by the calf's"), Two[6], 0.3, 1.0e-12);
	TestEqual(TEXT("the arm by neither"), Two[3], 0.0, 1.0e-12);
	// A bad index or a list of another size changes nothing.
	TArray<double> Short;
	Short.Init(0.0, 3);
	FSandboxCarry::ChainHolds(Parents, 4, 2, 0.0, 1.0, Short);
	TestEqual(TEXT("a short list is left alone"), Short[0], 0.0, 1.0e-12);
	TestEqual(TEXT("a bad index changes nothing"), Holds(42, 2, 0.0, 1.0)[0], 0.0, 1.0e-12);
	return true;
}

#endif
