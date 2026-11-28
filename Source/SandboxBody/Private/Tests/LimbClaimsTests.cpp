#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SandboxLimbClaims.h"

namespace
{
	const EAutomationTestFlags LimbClaimsTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSandboxLimbClaimsPriorityTest, "DictatorSandbox.Body.LimbClaims.PriorityDecides", LimbClaimsTestFlags)
bool FSandboxLimbClaimsPriorityTest::RunTest(const FString& Parameters)
{
	// R10, step 3: the asker of the highest priority holds a limb; nobody asking leaves it to the pose; asks last one tick.
	FSandboxLimbClaims Claims;
	Claims.BeginTick();
	TestTrue(TEXT("nobody asks: the pose has it"), Claims.Holder(ESandboxLimb::LeftArm) == ESandboxLimbUser::None);
	TestTrue(TEXT("and anyone would hold it"), Claims.WouldHold(ESandboxLimb::LeftArm, ESandboxLimbUser::Disposition));
	Claims.Ask(ESandboxLimb::LeftArm, ESandboxLimbUser::HoldAnswer);
	Claims.AskAll(ESandboxLimbUser::Protect);
	TestTrue(TEXT("protection comes before the held arm's answer"), Claims.Holder(ESandboxLimb::LeftArm) == ESandboxLimbUser::Protect);
	TestFalse(TEXT("the held arm's answer does not hold it"), Claims.WouldHold(ESandboxLimb::LeftArm, ESandboxLimbUser::HoldAnswer));
	TestTrue(TEXT("the fall would"), Claims.WouldHold(ESandboxLimb::LeftArm, ESandboxLimbUser::Fall));
	TestTrue(TEXT("the other arm is protection's too"), Claims.Holder(ESandboxLimb::RightArm) == ESandboxLimbUser::Protect);
	Claims.Ask(ESandboxLimb::LeftArm, ESandboxLimbUser::Script);
	TestTrue(TEXT("a scenario's own target comes first"), Claims.Holder(ESandboxLimb::LeftArm) == ESandboxLimbUser::Script);
	TestTrue(TEXT("an ask is kept within the tick"), Claims.Asks(ESandboxLimb::LeftArm, ESandboxLimbUser::HoldAnswer));
	Claims.EndTick(0.0, 0.01);
	Claims.BeginTick();
	TestFalse(TEXT("and forgotten at the next"), Claims.Asks(ESandboxLimb::LeftArm, ESandboxLimbUser::HoldAnswer));
	TestTrue(TEXT("so the pose has it again"), Claims.Holder(ESandboxLimb::LeftArm) == ESandboxLimbUser::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSandboxLimbClaimsRecordTest, "DictatorSandbox.Body.LimbClaims.RecordsChangesAndContests", LimbClaimsTestFlags)
bool FSandboxLimbClaimsRecordTest::RunTest(const FString& Parameters)
{
	// The record: a change of holder at the tick it happens, the seconds each held, the seconds two asked at once.
	FSandboxLimbClaims Claims;
	for (int32 Tick = 0; Tick < 10; ++Tick)
	{
		Claims.BeginTick();
		if (Tick >= 2)
		{
			Claims.Ask(ESandboxLimb::RightArm, ESandboxLimbUser::HoldAnswer);
		}
		if (Tick >= 5 && Tick < 8)
		{
			Claims.Ask(ESandboxLimb::RightArm, ESandboxLimbUser::Protect);
		}
		Claims.EndTick(Tick * 0.1, 0.1);
	}
	const TArray<FSandboxLimbClaims::FChange>& Changes = Claims.GetChanges();
	TestEqual(TEXT("three changes: to the hold's answer, to protection, back"), Changes.Num(), 3);
	if (Changes.Num() == 3)
	{
		TestTrue(TEXT("the first at 0.2 s"), FMath::IsNearlyEqual(Changes[0].TimeS, 0.2));
		TestTrue(TEXT("from the pose"), Changes[0].From == ESandboxLimbUser::None);
		TestTrue(TEXT("to the hold's answer"), Changes[0].To == ESandboxLimbUser::HoldAnswer);
		TestTrue(TEXT("protection takes it at 0.5 s"), Changes[1].To == ESandboxLimbUser::Protect && FMath::IsNearlyEqual(Changes[1].TimeS, 0.5));
		TestTrue(TEXT("and gives it back at 0.8 s"), FMath::IsNearlyEqual(Changes[2].TimeS, 0.8));
		TestTrue(TEXT("to the hold's answer"), Changes[2].To == ESandboxLimbUser::HoldAnswer);
	}
	TestTrue(TEXT("the hold's answer held it 0.5 s"), FMath::IsNearlyEqual(Claims.GetHeldS(ESandboxLimb::RightArm, ESandboxLimbUser::HoldAnswer), 0.5));
	TestTrue(TEXT("protection 0.3 s"), FMath::IsNearlyEqual(Claims.GetHeldS(ESandboxLimb::RightArm, ESandboxLimbUser::Protect), 0.3));
	TestTrue(TEXT("holding the hold's answer off all that time"),
		FMath::IsNearlyEqual(Claims.GetContestedS(ESandboxLimb::RightArm, ESandboxLimbUser::Protect, ESandboxLimbUser::HoldAnswer), 0.3));
	TestEqual(TEXT("the left arm was nobody's"), Claims.GetHeldS(ESandboxLimb::LeftArm, ESandboxLimbUser::HoldAnswer), 0.0);
	return true;
}

#endif
