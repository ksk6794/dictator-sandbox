#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SandboxOverlaySubsystem.h"

namespace
{
	const EAutomationTestFlags OverlayTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

/** The argument of Sandbox.Overlay and -Overlay=: which layers a text switches on, starting from which. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOverlayLayersTest, "DictatorSandbox.Scenario.Overlay.ParsesLayers", OverlayTestFlags)
bool FOverlayLayersTest::RunTest(const FString& Parameters)
{
	using namespace SandboxOverlay;
	struct FCase
	{
		const TCHAR* Text;
		uint32 Current;
		uint32 Expected;
	};
	const FCase Cases[] = {
		{ TEXT("all"), None, All },
		{ TEXT("off"), All, None },
		{ TEXT("none"), Com | Joints, None },
		{ TEXT("toggle"), None, All },
		{ TEXT("toggle"), Com, None },
		// Names switch on exactly those layers, whatever was on before.
		{ TEXT("com"), All, Com },
		{ TEXT("com,support,joints"), None, Com | Support | Joints },
		{ TEXT("capture"), All, Capture },
		{ TEXT("Contacts State  hands"), Com, Contacts | State | Hands },
		{ TEXT("com, assist"), None, Com | Assist },
		// A minus takes a layer away from what is on.
		{ TEXT("-joints"), All, All & ~Joints },
		{ TEXT("-joints,-assist"), Com | Joints | Assist, Com },
		{ TEXT("all -hands"), None, All & ~Hands },
		{ TEXT("-com"), None, None } };
	for (const FCase& Case : Cases)
	{
		uint32 Layers = 0xFFFFFFFF;
		FString Error;
		const bool bParsed = ParseLayers(Case.Text, Case.Current, Layers, Error);
		TestTrue(FString::Printf(TEXT("'%s' parses"), Case.Text), bParsed);
		TestEqual(FString::Printf(TEXT("'%s' from '%s'"), Case.Text, *LayerNames(Case.Current)), LayerNames(Layers), LayerNames(Case.Expected));
		TestTrue(FString::Printf(TEXT("'%s' sets no bit that is not a layer"), Case.Text), (Layers & ~static_cast<uint32>(All)) == 0);
	}

	for (const TCHAR* Text : { TEXT(""), TEXT(" , "), TEXT("skeleton"), TEXT("com,jointz"), TEXT("-everything") })
	{
		uint32 Layers = 0;
		FString Error;
		const bool bParsed = ParseLayers(Text, None, Layers, Error);
		TestFalse(FString::Printf(TEXT("'%s' is rejected"), Text), bParsed);
		TestFalse(TEXT("a rejection carries a reason"), !bParsed && Error.IsEmpty());
	}

	TestEqual(TEXT("names of all layers"), LayerNames(All), FString(TEXT("com support capture joints assist contacts state hands")));
	TestEqual(TEXT("name of no layer"), LayerNames(None), FString(TEXT("off")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
