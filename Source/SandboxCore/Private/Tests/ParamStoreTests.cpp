#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "SandboxParamStore.h"

namespace
{
	const EAutomationTestFlags ParamTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	const TCHAR* ValidText = TEXT(R"({ "motor": { "gain": 2.5, "limit": 10 }, "names": { "root": "pelvis" } })");

	/** A store that understands two numbers, one text value and one state. */
	void Declare(FSandboxParamStore& Store)
	{
		Store.DeclareNumber(TEXT("motor.gain"));
		Store.DeclareNumber(TEXT("motor.limit"));
		Store.DeclareString(TEXT("names.root"));
		Store.DeclareState(TEXT("slow"));
	}

	TMap<FString, FString> OneFile(const TCHAR* Text)
	{
		return TMap<FString, FString>{ { FString(TEXT("a.json")), FString(Text) } };
	}

	double Number(const FSandboxParamStore& Store, const TCHAR* Key, const TCHAR* State = TEXT(""))
	{
		double Value = TNumericLimits<double>::Lowest();
		Store.FindNumber(Key, State, Value);
		return Value;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParamStoreNamesTest, "DictatorSandbox.Core.Params.NamesAreFilePaths", ParamTestFlags)
bool FParamStoreNamesTest::RunTest(const FString& Parameters)
{
	FSandboxParamStore Store;
	Declare(Store);
	FString Error;
	TestFalse(TEXT("nothing is loaded at first"), Store.IsLoaded());
	if (!TestTrue(TEXT("a valid file loads"), Store.LoadFromText(OneFile(ValidText), Error)))
	{
		AddError(Error);
		return false;
	}
	TestTrue(TEXT("loaded"), Store.IsLoaded());
	TestEqual(TEXT("a nested number"), Number(Store, TEXT("motor.gain")), 2.5);
	TestEqual(TEXT("another one"), Number(Store, TEXT("motor.limit")), 10.0);
	FString Root;
	TestTrue(TEXT("a text value"), Store.FindString(TEXT("names.root"), Root));
	TestEqual(TEXT("its text"), Root, FString(TEXT("pelvis")));
	TestEqual(TEXT("the file a value came from"), Store.FindFile(TEXT("motor.gain")), FString(TEXT("a.json")));
	double Unused = 0.0;
	TestFalse(TEXT("an unknown name is not found"), Store.FindNumber(TEXT("motor.torque"), FString(), Unused));

	// Two files are merged into one set of names.
	FSandboxParamStore Two;
	Declare(Two);
	const TMap<FString, FString> Split{
		{ FString(TEXT("a.json")), FString(TEXT(R"({ "motor": { "gain": 3 } })")) },
		{ FString(TEXT("b.json")), FString(TEXT(R"({ "motor": { "limit": 4 }, "names": { "root": "hips" } })")) } };
	TestTrue(TEXT("two files load as one set"), Two.LoadFromText(Split, Error));
	TestEqual(TEXT("value of the first file"), Number(Two, TEXT("motor.gain")), 3.0);
	TestEqual(TEXT("value of the second file"), Number(Two, TEXT("motor.limit")), 4.0);
	TestEqual(TEXT("its file"), Two.FindFile(TEXT("motor.limit")), FString(TEXT("b.json")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParamStoreSchemaTest, "DictatorSandbox.Core.Params.RejectsWhatIsNotDeclared", ParamTestFlags)
bool FParamStoreSchemaTest::RunTest(const FString& Parameters)
{
	FSandboxParamStore Store;
	Declare(Store);
	FString Error;
	TestTrue(TEXT("a valid file loads"), Store.LoadFromText(OneFile(ValidText), Error));
	const uint32 Revision = Store.GetRevision();

	const TCHAR* Invalid[] = {
		TEXT("not json"),
		// A misspelt name.
		TEXT(R"({ "motor": { "gian": 2.5, "limit": 10 }, "names": { "root": "pelvis" } })"),
		// A value the code needs is not there: the code has no default to fall back on.
		TEXT(R"({ "motor": { "gain": 2.5 }, "names": { "root": "pelvis" } })"),
		TEXT(R"({ "motor": { "gain": 2.5, "limit": 10 } })"),
		// Wrong kinds of value.
		TEXT(R"({ "motor": { "gain": "high", "limit": 10 }, "names": { "root": "pelvis" } })"),
		TEXT(R"({ "motor": { "gain": 2.5, "limit": 10 }, "names": { "root": 7 } })"),
		TEXT(R"({ "motor": { "gain": [2.5], "limit": 10 }, "names": { "root": "pelvis" } })"),
		TEXT(R"({ "motor": { "gain": true, "limit": 10 }, "names": { "root": "pelvis" } })"),
		// An unknown section.
		TEXT(R"({ "motor": { "gain": 2.5, "limit": 10 }, "names": { "root": "pelvis" }, "extra": { "x": 1 } })"),
		// A state nobody declared, a state value of an unknown name, a text value per state.
		TEXT(R"({ "motor": { "gain": 2.5, "limit": 10 }, "names": { "root": "pelvis" }, "states": { "fast": { "motor": { "gain": 1 } } } })"),
		TEXT(R"({ "motor": { "gain": 2.5, "limit": 10 }, "names": { "root": "pelvis" }, "states": { "slow": { "motor": { "gian": 1 } } } })"),
		TEXT(R"({ "motor": { "gain": 2.5, "limit": 10 }, "names": { "root": "pelvis" }, "states": { "slow": { "names": { "root": "hips" } } } })"),
	};
	for (const TCHAR* Text : Invalid)
	{
		Error.Reset();
		TestFalse(FString::Printf(TEXT("rejects: %s"), Text), Store.LoadFromText(OneFile(Text), Error));
		TestFalse(TEXT("a rejection carries a reason"), Error.IsEmpty());
	}

	// The same name in two files.
	const TMap<FString, FString> Twice{
		{ FString(TEXT("a.json")), FString(ValidText) },
		{ FString(TEXT("b.json")), FString(TEXT(R"({ "motor": { "gain": 3 } })")) } };
	TestFalse(TEXT("a name defined twice is rejected"), Store.LoadFromText(Twice, Error));

	TestEqual(TEXT("a rejected file leaves the values as they were"), Number(Store, TEXT("motor.gain")), 2.5);
	TestEqual(TEXT("and the revision too"), Store.GetRevision(), Revision);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParamStoreOverridesTest, "DictatorSandbox.Core.Params.OverridesLieOnTopOfFiles", ParamTestFlags)
bool FParamStoreOverridesTest::RunTest(const FString& Parameters)
{
	FSandboxParamStore Store;
	Declare(Store);
	FString Error;
	TestFalse(TEXT("no override before the files are loaded"), Store.SetOverrides(TEXT("motor.gain=4"), Error));
	TestTrue(TEXT("a valid file loads"), Store.LoadFromText(OneFile(ValidText), Error));

	uint32 Revision = Store.GetRevision();
	TestTrue(TEXT("a list of overrides applies"), Store.SetOverrides(TEXT("motor.gain=4, motor.limit=20"), Error));
	TestEqual(TEXT("first value"), Number(Store, TEXT("motor.gain")), 4.0);
	TestEqual(TEXT("second value"), Number(Store, TEXT("motor.limit")), 20.0);
	TestNotEqual(TEXT("the revision moved"), Store.GetRevision(), Revision);

	Revision = Store.GetRevision();
	TestTrue(TEXT("the same override again is accepted"), Store.SetOverrides(TEXT("motor.gain=4"), Error));
	TestEqual(TEXT("and changes nothing"), Store.GetRevision(), Revision);

	// All or nothing: one bad entry and the good one before it is not applied either.
	TestFalse(TEXT("an unknown name is rejected"), Store.SetOverrides(TEXT("motor.gain=5,motor.torque=1"), Error));
	TestFalse(TEXT("a rejection carries a reason"), Error.IsEmpty());
	TestEqual(TEXT("nothing of a rejected list is applied"), Number(Store, TEXT("motor.gain")), 4.0);
	TestFalse(TEXT("a missing value is rejected"), Store.SetOverrides(TEXT("motor.gain"), Error));
	TestFalse(TEXT("a non-numeric value is rejected"), Store.SetOverrides(TEXT("motor.gain=strong"), Error));
	TestFalse(TEXT("a text value cannot be overridden"), Store.SetOverrides(TEXT("names.root=3"), Error));
	TestFalse(TEXT("an empty list is rejected"), Store.SetOverrides(TEXT(""), Error));
	TestEqual(TEXT("rejections leave the revision alone"), Store.GetRevision(), Revision);

	// Reloading the files keeps the overrides.
	TestTrue(TEXT("a changed file loads"), Store.LoadFromText(OneFile(TEXT(R"({ "motor": { "gain": 3, "limit": 11 }, "names": { "root": "pelvis" } })")), Error));
	TestEqual(TEXT("the override still wins"), Number(Store, TEXT("motor.gain")), 4.0);

	TArray<FSandboxParamEntry> Entries;
	Store.ListNumbers(Entries);
	const FSandboxParamEntry* Gain = Entries.FindByPredicate([](const FSandboxParamEntry& Entry) { return Entry.Key == TEXT("motor.gain"); });
	if (TestNotNull(TEXT("the value is listed"), Gain))
	{
		TestTrue(TEXT("listed as overridden"), Gain->bOverridden);
		TestEqual(TEXT("with the value in force"), Gain->Value, 4.0);
		TestEqual(TEXT("and the file's value"), Gain->FileValue, 3.0);
	}

	TestFalse(TEXT("clearing a name that is not overridden is rejected"), Store.ClearOverrides(TEXT("names.root"), Error));
	TestTrue(TEXT("one override can be cleared"), Store.ClearOverrides(TEXT("motor.gain"), Error));
	TestEqual(TEXT("the file's value is back"), Number(Store, TEXT("motor.gain")), 3.0);
	TestEqual(TEXT("the other override stays"), Number(Store, TEXT("motor.limit")), 20.0);
	TestTrue(TEXT("all overrides can be cleared"), Store.ClearOverrides(FString(), Error));
	TestEqual(TEXT("none are left"), Store.GetOverrides().Num(), 0);
	TestEqual(TEXT("the file's value applies"), Number(Store, TEXT("motor.limit")), 11.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParamStoreStatesTest, "DictatorSandbox.Core.Params.StatesReplaceBaseValues", ParamTestFlags)
bool FParamStoreStatesTest::RunTest(const FString& Parameters)
{
	FSandboxParamStore Store;
	Declare(Store);
	FString Error;
	const TCHAR* Text = TEXT(R"({
		"motor": { "gain": 2.5, "limit": 10 },
		"names": { "root": "pelvis" },
		"states": { "slow": { "motor": { "gain": 0.5 } } }
	})");
	if (!TestTrue(TEXT("a file with a state loads"), Store.LoadFromText(OneFile(Text), Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("the base value"), Number(Store, TEXT("motor.gain")), 2.5);
	TestEqual(TEXT("the state's own value"), Number(Store, TEXT("motor.gain"), TEXT("slow")), 0.5);
	TestEqual(TEXT("what the state does not set comes from the base"), Number(Store, TEXT("motor.limit"), TEXT("slow")), 10.0);

	TArray<FString> States;
	Store.GetStatesInUse(States);
	TestTrue(TEXT("states in use"), States.Num() == 1 && States[0] == TEXT("slow"));

	// An override of the base value does not reach a state that has its own.
	TestTrue(TEXT("base override"), Store.SetOverrides(TEXT("motor.gain=4,motor.limit=20"), Error));
	TestEqual(TEXT("the state keeps its own value"), Number(Store, TEXT("motor.gain"), TEXT("slow")), 0.5);
	TestEqual(TEXT("and follows the base where it has none"), Number(Store, TEXT("motor.limit"), TEXT("slow")), 20.0);

	// A state value can be overridden, also one the files do not have.
	TestTrue(TEXT("state overrides"), Store.SetOverrides(TEXT("states.slow.motor.gain=0.25,states.slow.motor.limit=5"), Error));
	TestEqual(TEXT("overridden state value"), Number(Store, TEXT("motor.gain"), TEXT("slow")), 0.25);
	TestEqual(TEXT("state value that exists only as an override"), Number(Store, TEXT("motor.limit"), TEXT("slow")), 5.0);
	TestEqual(TEXT("the base is untouched"), Number(Store, TEXT("motor.limit")), 20.0);

	TestFalse(TEXT("an override for an unknown state is rejected"), Store.SetOverrides(TEXT("states.fast.motor.gain=1"), Error));
	TestFalse(TEXT("a state without a name is rejected"), Store.SetOverrides(TEXT("states.slow=1"), Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParamStoreValidatorTest, "DictatorSandbox.Core.Params.ValidatorsGuardEveryChange", ParamTestFlags)
bool FParamStoreValidatorTest::RunTest(const FString& Parameters)
{
	FSandboxParamStore Store;
	Declare(Store);
	Store.AddValidator([](const FSandboxParamStore& Candidate, FString& OutError)
	{
		if (Number(Candidate, TEXT("motor.gain")) > Number(Candidate, TEXT("motor.limit")))
		{
			OutError = TEXT("motor.gain is above motor.limit");
			return false;
		}
		return true;
	});

	FString Error;
	TestFalse(TEXT("an invalid first file is rejected"), Store.LoadFromText(OneFile(TEXT(R"({ "motor": { "gain": 11, "limit": 10 }, "names": { "root": "pelvis" } })")), Error));
	TestFalse(TEXT("and nothing is loaded"), Store.IsLoaded());

	TestTrue(TEXT("a valid file loads"), Store.LoadFromText(OneFile(ValidText), Error));
	const uint32 Revision = Store.GetRevision();

	TestFalse(TEXT("an invalid file is rejected"), Store.LoadFromText(OneFile(TEXT(R"({ "motor": { "gain": 11, "limit": 10 }, "names": { "root": "pelvis" } })")), Error));
	TestEqual(TEXT("with the validator's reason"), Error, FString(TEXT("motor.gain is above motor.limit")));
	TestFalse(TEXT("an invalid override is rejected"), Store.SetOverrides(TEXT("motor.gain=12"), Error));
	TestEqual(TEXT("the values are as they were"), Number(Store, TEXT("motor.gain")), 2.5);
	TestEqual(TEXT("no override is left behind"), Store.GetOverrides().Num(), 0);
	TestEqual(TEXT("the revision is as it was"), Store.GetRevision(), Revision);

	// A combination is judged as a whole.
	TestTrue(TEXT("a valid combination applies"), Store.SetOverrides(TEXT("motor.gain=12,motor.limit=15"), Error));
	TestFalse(TEXT("clearing one half of it would be invalid"), Store.ClearOverrides(TEXT("motor.limit"), Error));
	TestEqual(TEXT("so it stays"), Number(Store, TEXT("motor.limit")), 15.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParamStoreReloadTest, "DictatorSandbox.Core.Params.PicksUpEditedFiles", ParamTestFlags)
bool FParamStoreReloadTest::RunTest(const FString& Parameters)
{
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Tests"), TEXT("ParamStore_") + FGuid::NewGuid().ToString());
	const FString File = FPaths::Combine(Dir, TEXT("a.json"));
	ON_SCOPE_EXIT { IFileManager::Get().DeleteDirectory(*Dir, /*RequireExists*/ false, /*Tree*/ true); };

	// Each version has a different length, so that the edit shows in the file's size even when two writes
	// fall within one tick of the file system's clock.
	const auto Write = [this, &File](const TCHAR* Text)
	{
		TestTrue(TEXT("test file written"), FFileHelper::SaveStringToFile(FString(Text), *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
	};

	FSandboxParamStore Store;
	Declare(Store);
	Store.SetDirectory(Dir);
	FString Error;
	TestFalse(TEXT("a directory without files does not load"), Store.Load(Error));
	TestFalse(TEXT("polling does nothing before the first load"), Store.PollFiles(&Error));

	Write(ValidText);
	if (!TestTrue(TEXT("the directory loads"), Store.EnsureLoaded(Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("value from disk"), Number(Store, TEXT("motor.gain")), 2.5);
	TestTrue(TEXT("an override is set"), Store.SetOverrides(TEXT("motor.limit=20"), Error));

	uint32 Revision = Store.GetRevision();
	TestFalse(TEXT("unchanged files: the poll finds nothing"), Store.PollFiles(&Error));
	TestEqual(TEXT("and the revision stays"), Store.GetRevision(), Revision);

	Write(TEXT(R"({ "motor": { "gain": 3.75, "limit": 10 }, "names": { "root": "pelvis" } })"));
	TestTrue(TEXT("an edited file is picked up"), Store.PollFiles(&Error));
	TestEqual(TEXT("with its new value"), Number(Store, TEXT("motor.gain")), 3.75);
	TestEqual(TEXT("the override is kept"), Number(Store, TEXT("motor.limit")), 20.0);
	TestNotEqual(TEXT("the revision moved"), Store.GetRevision(), Revision);
	TestFalse(TEXT("the next poll finds nothing new"), Store.PollFiles(&Error));

	// A half-written or wrong file: reported once, the previous values stay in force.
	Revision = Store.GetRevision();
	Error.Reset();
	Write(TEXT(R"({ "motor": { "gain": )"));
	TestFalse(TEXT("a broken file is not applied"), Store.PollFiles(&Error));
	TestFalse(TEXT("the reason is reported"), Error.IsEmpty());
	TestEqual(TEXT("the previous value stays"), Number(Store, TEXT("motor.gain")), 3.75);
	Error.Reset();
	TestFalse(TEXT("the same broken file is not read again"), Store.PollFiles(&Error));
	TestTrue(TEXT("and not reported again"), Error.IsEmpty());
	TestEqual(TEXT("the revision stays"), Store.GetRevision(), Revision);

	Write(TEXT(R"({ "motor": { "gain": 1.125, "limit": 10.0 }, "names": { "root": "pelvis" } })"));
	TestTrue(TEXT("the repaired file is picked up"), Store.PollFiles(&Error));
	TestEqual(TEXT("with its value"), Number(Store, TEXT("motor.gain")), 1.125);

	// A file rewritten with the same values is read, but nobody is told.
	Revision = Store.GetRevision();
	Write(TEXT(R"({ "motor": { "gain": 1.125, "limit": 10 }, "names": { "root": "pelvis" } })"));
	TestFalse(TEXT("same values in a rewritten file: no change"), Store.PollFiles(&Error));
	TestEqual(TEXT("the revision stays"), Store.GetRevision(), Revision);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
