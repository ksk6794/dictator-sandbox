#include "Misc/AutomationTest.h"
#include "SandboxBodyController.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	const EAutomationTestFlags EventTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	FSandboxBodyHold Hold(const TCHAR* Holder, const TCHAR* Bone, double ForceN = 0.0)
	{
		FSandboxBodyHold Result;
		Result.Holder = FName(Holder);
		Result.Bone = FName(Bone);
		Result.ForceN = ForceN;
		return Result;
	}
}

// The body is told every frame what is held of it, and tells its listeners only of what changed: a hold that
// begins, a hold that ends. A controller that has not been started is enough for this: nothing of a body is read.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBodyHoldEventsTest, "DictatorSandbox.Body.Events.HoldsAreToldAsGrabAndRelease", EventTestFlags)
bool FBodyHoldEventsTest::RunTest(const FString& Parameters)
{
	USandboxBodyController* Controller = NewObject<USandboxBodyController>(GetTransientPackage());
	TArray<FSandboxBodyEvent> Heard;
	Controller->OnBodyEvent.AddLambda([&Heard](const FSandboxBodyEvent& Event) { Heard.Add(Event); });

	Controller->TellHolds({});
	TestEqual(TEXT("nothing held, nothing told"), Heard.Num(), 0);

	// A hand takes the forearm, and goes on holding it with another force: one grab, told once.
	Controller->TellHolds({ Hold(TEXT("hand_right"), TEXT("lowerarm_l"), 100.0) });
	Controller->TellHolds({ Hold(TEXT("hand_right"), TEXT("lowerarm_l"), 250.0) });
	TestEqual(TEXT("one grab"), Heard.Num(), 1);
	if (Heard.Num() == 1)
	{
		TestTrue(TEXT("it is a grab"), Heard[0].Type == ESandboxBodyEventType::Grabbed);
		TestEqual(TEXT("of the forearm"), Heard[0].Bone, FName(TEXT("lowerarm_l")));
		TestEqual(TEXT("by the right hand"), Heard[0].Holder, FName(TEXT("hand_right")));
	}
	TestEqual(TEXT("the list is kept, with the latest force"), Controller->GetHolds().Num() == 1 ? Controller->GetHolds()[0].ForceN : -1.0, 250.0);

	// The other hand takes the same bone: a grab of its own.
	Controller->TellHolds({ Hold(TEXT("hand_right"), TEXT("lowerarm_l")), Hold(TEXT("hand_left"), TEXT("lowerarm_l")) });
	TestEqual(TEXT("a second grab"), Heard.Num(), 2);

	// The right hand lets go of the forearm and takes the chest in the same frame: the release is told first.
	Controller->TellHolds({ Hold(TEXT("hand_left"), TEXT("lowerarm_l")), Hold(TEXT("hand_right"), TEXT("spine_04")) });
	TestEqual(TEXT("a release and a grab"), Heard.Num(), 4);
	if (Heard.Num() == 4)
	{
		TestTrue(TEXT("the release first"), Heard[2].Type == ESandboxBodyEventType::Released && Heard[2].Bone == FName(TEXT("lowerarm_l")) && Heard[2].Holder == FName(TEXT("hand_right")));
		TestTrue(TEXT("then the grab"), Heard[3].Type == ESandboxBodyEventType::Grabbed && Heard[3].Bone == FName(TEXT("spine_04")));
	}

	// Everything let go.
	Controller->TellHolds({});
	TestEqual(TEXT("two releases"), Heard.Num(), 6);
	TestEqual(TEXT("nothing is held"), Controller->GetHolds().Num(), 0);
	TestEqual(TEXT("the controller's own list has them all, in order"), Controller->GetEvents().Num(), 6);
	return true;
}

#endif
