#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SandboxChance.h"

namespace
{
	const EAutomationTestFlags ChanceTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSandboxChanceRepeatsTest, "DictatorSandbox.Core.Chance.SameSeedDrawsTheSame", ChanceTestFlags)
bool FSandboxChanceRepeatsTest::RunTest(const FString& Parameters)
{
	// R10, step 2: a run's seed gives the same draws every time; another seed others; seed 0 draws nothing and takes the fixed answer.
	FSandboxChance A;
	FSandboxChance B;
	FSandboxChance C;
	A.Reset(7);
	B.Reset(7);
	C.Reset(8);
	bool bSame = true;
	bool bOther = false;
	for (int32 Index = 0; Index < 20; ++Index)
	{
		const double First = A.Draw(FSandboxChance::EPurpose::Vigour, 0.5);
		bSame &= First == B.Draw(FSandboxChance::EPurpose::Vigour, 0.5);
		bOther |= First != C.Draw(FSandboxChance::EPurpose::Vigour, 0.5);
		TestTrue(TEXT("a draw lies in [0, 1)"), First >= 0.0 && First < 1.0);
	}
	TestTrue(TEXT("the same seed draws the same"), bSame);
	TestTrue(TEXT("another seed draws otherwise"), bOther);
	TestEqual(TEXT("every draw is kept"), A.GetDraws().Num(), 20);

	FSandboxChance None;
	None.Reset(0);
	TestEqual(TEXT("seed 0 takes the fixed answer"), None.Draw(FSandboxChance::EPurpose::Vigour, 0.75), 0.75);
	TestTrue(TEXT("and a choice its fixed answer"), None.Choose(FSandboxChance::EPurpose::Struggle, 0.0, true));
	TestEqual(TEXT("and draws nothing"), None.GetDraws().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSandboxChancePurposesTest, "DictatorSandbox.Core.Chance.PurposesKeepTheirStreams", ChanceTestFlags)
bool FSandboxChancePurposesTest::RunTest(const FString& Parameters)
{
	// A purpose's draws do not move when another purpose draws between them.
	FSandboxChance Alone;
	FSandboxChance Mixed;
	Alone.Reset(3);
	Mixed.Reset(3);
	bool bSame = true;
	for (int32 Index = 0; Index < 10; ++Index)
	{
		Mixed.Draw(FSandboxChance::EPurpose::Vigour, 0.5);
		Mixed.Draw(FSandboxChance::EPurpose::Vigour, 0.5);
		bSame &= Alone.Draw(FSandboxChance::EPurpose::Struggle, 0.5) == Mixed.Draw(FSandboxChance::EPurpose::Struggle, 0.5);
	}
	TestTrue(TEXT("the struggle's draws are the same with the vigour's between them"), bSame);
	TestNotEqual(TEXT("two purposes of one seed start in other places"), FSandboxChance::StreamSeed(3, FSandboxChance::EPurpose::Struggle),
		FSandboxChance::StreamSeed(3, FSandboxChance::EPurpose::Vigour));
	return true;
}

#endif
