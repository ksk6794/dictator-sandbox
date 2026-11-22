#include "SandboxParamStore.h"

#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "SandboxCore.h"
#include "SandboxJson.h"

namespace
{
	const TCHAR* const StatesSection = TEXT("states");

	/** "states.falling.muscles.legs.damping_s" is the state "falling" of "muscles.legs.damping_s". */
	bool SplitStateKey(const FString& Key, FString& OutState, FString& OutBaseKey)
	{
		FString Section, Rest;
		if (!Key.Split(TEXT("."), &Section, &Rest) || Section != StatesSection)
		{
			OutState.Reset();
			OutBaseKey = Key;
			return true;
		}
		return Rest.Split(TEXT("."), &OutState, &OutBaseKey) && !OutState.IsEmpty() && !OutBaseKey.IsEmpty();
	}

	FString StateKey(const FString& State, const FString& Key)
	{
		return FString::Printf(TEXT("%s.%s.%s"), StatesSection, *State, *Key);
	}

	/** Turns nested objects into dotted names. Only numbers, text and objects are allowed. */
	bool Flatten(const FString& File, const FJsonObject& Object, const FString& Prefix,
		TMap<FString, double>& Numbers, TMap<FString, FString>& Strings, TMap<FString, FString>& Files, FString& OutError)
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Object.Values)
		{
			const FString Key = Prefix.IsEmpty() ? Field.Key : Prefix + TEXT(".") + Field.Key;
			const EJson Type = Field.Value.IsValid() ? Field.Value->Type : EJson::None;
			if (Type == EJson::Object)
			{
				if (!Flatten(File, *Field.Value->AsObject(), Key, Numbers, Strings, Files, OutError))
				{
					return false;
				}
				continue;
			}
			if (Type != EJson::Number && Type != EJson::String)
			{
				OutError = FString::Printf(TEXT("'%s' in %s must be a number, text or an object"), *Key, *File);
				return false;
			}
			if (const FString* Other = Files.Find(Key))
			{
				OutError = FString::Printf(TEXT("'%s' is defined twice (%s and %s)"), *Key, **Other, *File);
				return false;
			}
			Files.Add(Key, File);
			if (Type == EJson::Number)
			{
				Numbers.Add(Key, Field.Value->AsNumber());
			}
			else
			{
				Strings.Add(Key, Field.Value->AsString());
			}
		}
		return true;
	}

	template <typename ValueType>
	bool SameMap(const TMap<FString, ValueType>& A, const TMap<FString, ValueType>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (const TPair<FString, ValueType>& Entry : A)
		{
			const ValueType* Other = B.Find(Entry.Key);
			if (!Other || !(*Other == Entry.Value))
			{
				return false;
			}
		}
		return true;
	}

	bool ParseAssignments(const FString& Assignments, TArray<TPair<FString, double>>& OutValues, FString& OutError)
	{
		TArray<FString> Entries;
		Assignments.ParseIntoArray(Entries, TEXT(","), /*bCullEmpty*/ true);
		for (const FString& Entry : Entries)
		{
			FString Key, ValueText;
			if (!Entry.Split(TEXT("="), &Key, &ValueText) || Key.TrimStartAndEnd().IsEmpty() || !ValueText.TrimStartAndEnd().IsNumeric())
			{
				OutError = FString::Printf(TEXT("Tuning override '%s' is not name=number"), *Entry.TrimStartAndEnd());
				return false;
			}
			OutValues.Emplace(Key.TrimStartAndEnd(), FCString::Atod(*ValueText.TrimStartAndEnd()));
		}
		if (OutValues.Num() == 0)
		{
			OutError = TEXT("No tuning override given; expected name=number");
			return false;
		}
		return true;
	}
}

FSandboxParamStore& FSandboxParamStore::Get()
{
	static FSandboxParamStore* Store = []
	{
		FSandboxParamStore* New = new FSandboxParamStore();
		FString Dir;
		if (!FParse::Value(FCommandLine::Get(), TEXT("TuningDir="), Dir))
		{
			Dir = FPaths::Combine(FPaths::ProjectConfigDir(), TEXT("Tuning"));
		}
		New->SetDirectory(Dir);
		FParse::Value(FCommandLine::Get(), TEXT("Tune="), New->StartupOverrides, /*bShouldStopOnSeparator*/ false);
		return New;
	}();
	return *Store;
}

void FSandboxParamStore::DeclareNumber(const FString& Key) { DeclaredNumbers.Add(Key); }
void FSandboxParamStore::DeclareString(const FString& Key) { DeclaredStrings.Add(Key); }
void FSandboxParamStore::DeclareState(const FString& State) { DeclaredStates.Add(State); }
void FSandboxParamStore::AddValidator(FValidator Validator) { Validators.Add(MoveTemp(Validator)); }

void FSandboxParamStore::SetDirectory(const FString& InDirectory)
{
	// The revision moves with the next load, when there are values to read again.
	Directory = FPaths::ConvertRelativePathToFull(InDirectory);
	bLoaded = false;
	FileValues = FFileValues();
	SeenStamps.Reset();
}

void FSandboxParamStore::StatDirectory(TMap<FString, FFileStamp>& OutStamps) const
{
	OutStamps.Reset();
	IFileManager::Get().IterateDirectoryStat(*Directory, [&OutStamps](const TCHAR* Path, const FFileStatData& Stat)
	{
		if (!Stat.bIsDirectory && FPaths::GetExtension(Path).Equals(TEXT("json"), ESearchCase::IgnoreCase))
		{
			OutStamps.Add(FPaths::GetCleanFilename(Path), FFileStamp{ Stat.ModificationTime, Stat.FileSize });
		}
		return true;
	});
}

bool FSandboxParamStore::ReadDirectory(FFileValues& OutValues, FString& OutError) const
{
	TArray<FString> Names;
	IFileManager::Get().FindFiles(Names, *FPaths::Combine(Directory, TEXT("*.json")), /*Files*/ true, /*Directories*/ false);
	if (Names.Num() == 0)
	{
		OutError = FString::Printf(TEXT("No tuning files (*.json) in '%s'"), *Directory);
		return false;
	}
	Names.Sort();
	for (const FString& Name : Names)
	{
		TSharedPtr<FJsonObject> Json;
		if (!SandboxJson::LoadObject(FPaths::Combine(Directory, Name), Json, OutError) ||
			!Flatten(Name, *Json, FString(), OutValues.Numbers, OutValues.Strings, OutValues.Files, OutError))
		{
			return false;
		}
	}
	return true;
}

bool FSandboxParamStore::Load(FString& OutError)
{
	// The stamps are taken before the files are read: a file that changes in between is then seen as
	// changed by the next poll and read again.
	TMap<FString, FFileStamp> Stamps;
	StatDirectory(Stamps);

	FFileValues NewValues;
	if (!ReadDirectory(NewValues, OutError))
	{
		return false;
	}

	TMap<FString, double> NewOverrides = Overrides;
	if (!StartupOverrides.IsEmpty())
	{
		TArray<TPair<FString, double>> Values;
		if (!ParseAssignments(StartupOverrides, Values, OutError))
		{
			return false;
		}
		for (const TPair<FString, double>& Value : Values)
		{
			NewOverrides.Add(Value.Key, Value.Value);
		}
	}

	if (!Commit(MoveTemp(NewValues), MoveTemp(NewOverrides), OutError))
	{
		return false;
	}
	StartupOverrides.Reset();
	SeenStamps = MoveTemp(Stamps);
	return true;
}

bool FSandboxParamStore::EnsureLoaded(FString& OutError)
{
	return bLoaded || Load(OutError);
}

bool FSandboxParamStore::LoadFromText(const TMap<FString, FString>& Documents, FString& OutError)
{
	FFileValues NewValues;
	for (const TPair<FString, FString>& Document : Documents)
	{
		TSharedPtr<FJsonObject> Json;
		if (!SandboxJson::ParseObject(Document.Value, Json, OutError))
		{
			OutError = FString::Printf(TEXT("%s (in '%s')"), *OutError, *Document.Key);
			return false;
		}
		if (!Flatten(Document.Key, *Json, FString(), NewValues.Numbers, NewValues.Strings, NewValues.Files, OutError))
		{
			return false;
		}
	}
	TMap<FString, double> SameOverrides = Overrides;
	return Commit(MoveTemp(NewValues), MoveTemp(SameOverrides), OutError);
}

bool FSandboxParamStore::PollFiles(FString* OutError)
{
	if (!bLoaded)
	{
		return false;
	}
	TMap<FString, FFileStamp> Stamps;
	StatDirectory(Stamps);
	if (SameMap(Stamps, SeenStamps))
	{
		return false;
	}
	// Whatever comes of it, these stamps have been dealt with: a file that cannot be applied is reported
	// once and tried again when it changes again.
	SeenStamps = MoveTemp(Stamps);

	const uint32 RevisionBefore = Revision;
	const TMap<FString, double> NumbersBefore = FileValues.Numbers;
	FFileValues NewValues;
	FString Error;
	TMap<FString, double> SameOverrides = Overrides;
	if (!ReadDirectory(NewValues, Error) || !Commit(MoveTemp(NewValues), MoveTemp(SameOverrides), Error))
	{
		if (OutError)
		{
			*OutError = Error;
		}
		else
		{
			UE_LOG(LogSandbox, Error, TEXT("Tuning: the changed files in '%s' were not applied, the previous values stay in force: %s"), *Directory, *Error);
		}
		return false;
	}
	if (Revision == RevisionBefore)
	{
		return false;
	}

	int32 Changed = 0;
	FString Summary;
	for (const TPair<FString, double>& Entry : FileValues.Numbers)
	{
		const double* Before = NumbersBefore.Find(Entry.Key);
		if (Before && *Before == Entry.Value)
		{
			continue;
		}
		if (++Changed <= 8)
		{
			Summary += FString::Printf(TEXT("%s%s %s -> %g"), Summary.IsEmpty() ? TEXT("") : TEXT(", "), *Entry.Key,
				Before ? *FString::Printf(TEXT("%g"), *Before) : TEXT("(new)"), Entry.Value);
		}
	}
	UE_LOG(LogSandbox, Display, TEXT("Tuning: reloaded '%s', %d number(s) changed: %s%s"), *Directory, Changed, *Summary, Changed > 8 ? TEXT(", ...") : TEXT(""));
	return true;
}

void FSandboxParamStore::TickAutoPoll()
{
	if (bAutoPoll && bLoaded && ++AutoPollCounter >= AutoPollIntervalFrames)
	{
		AutoPollCounter = 0;
		PollFiles();
	}
}

void FSandboxParamStore::GetFileNames(TArray<FString>& OutNames) const
{
	OutNames.Reset();
	for (const TPair<FString, FString>& Entry : FileValues.Files)
	{
		OutNames.AddUnique(Entry.Value);
	}
	OutNames.Sort();
}

FString FSandboxParamStore::FindFile(const FString& Key) const
{
	const FString* File = FileValues.Files.Find(Key);
	return File ? *File : FString();
}

bool FSandboxParamStore::CheckNumberKey(const FString& Key, FString& OutError) const
{
	FString State, BaseKey;
	if (!SplitStateKey(Key, State, BaseKey))
	{
		OutError = FString::Printf(TEXT("'%s' is not states.<state>.<name>"), *Key);
		return false;
	}
	if (!State.IsEmpty() && !DeclaredStates.Contains(State))
	{
		OutError = FString::Printf(TEXT("'%s': '%s' is not a state"), *Key, *State);
		return false;
	}
	if (DeclaredStrings.Contains(BaseKey))
	{
		OutError = State.IsEmpty()
			? FString::Printf(TEXT("'%s' must be text, not a number"), *Key)
			: FString::Printf(TEXT("'%s': a text value cannot differ between states"), *Key);
		return false;
	}
	if (!DeclaredNumbers.Contains(BaseKey))
	{
		OutError = FString::Printf(TEXT("'%s' is not a tuning value"), *BaseKey);
		return false;
	}
	return true;
}

bool FSandboxParamStore::CheckSchema(FString& OutError) const
{
	const auto InFile = [this](const FString& Key, FString& Error)
	{
		Error = FString::Printf(TEXT("%s (in %s)"), *Error, *FindFile(Key));
		return false;
	};

	for (const TPair<FString, double>& Entry : FileValues.Numbers)
	{
		if (!CheckNumberKey(Entry.Key, OutError))
		{
			return InFile(Entry.Key, OutError);
		}
	}
	for (const TPair<FString, FString>& Entry : FileValues.Strings)
	{
		if (!DeclaredStrings.Contains(Entry.Key))
		{
			OutError = DeclaredNumbers.Contains(Entry.Key)
				? FString::Printf(TEXT("'%s' must be a number, not text"), *Entry.Key)
				: FString::Printf(TEXT("'%s' is not a tuning value"), *Entry.Key);
			return InFile(Entry.Key, OutError);
		}
	}

	// The files are the only source of values: the code has no defaults to fall back on.
	TArray<FString> Missing;
	for (const FString& Key : DeclaredNumbers)
	{
		if (!FileValues.Numbers.Contains(Key))
		{
			Missing.Add(Key);
		}
	}
	for (const FString& Key : DeclaredStrings)
	{
		if (!FileValues.Strings.Contains(Key))
		{
			Missing.Add(Key);
		}
	}
	if (Missing.Num() > 0)
	{
		Missing.Sort();
		OutError = FString::Printf(TEXT("'%s' is missing from the tuning files (%d value(s) missing in all)"), *Missing[0], Missing.Num());
		return false;
	}

	for (const TPair<FString, double>& Entry : Overrides)
	{
		if (!CheckNumberKey(Entry.Key, OutError))
		{
			return false;
		}
	}
	return true;
}

bool FSandboxParamStore::Commit(FFileValues&& NewValues, TMap<FString, double>&& NewOverrides, FString& OutError)
{
	const bool bWasLoaded = bLoaded;
	const bool bChanged = !bWasLoaded || !SameMap(NewValues.Numbers, FileValues.Numbers) ||
		!SameMap(NewValues.Strings, FileValues.Strings) || !SameMap(NewOverrides, Overrides);

	Swap(FileValues, NewValues);
	Swap(Overrides, NewOverrides);
	bLoaded = true;

	bool bAccepted = CheckSchema(OutError);
	for (int32 Index = 0; bAccepted && Index < Validators.Num(); ++Index)
	{
		bAccepted = Validators[Index](*this, OutError);
	}
	if (!bAccepted)
	{
		Swap(FileValues, NewValues);
		Swap(Overrides, NewOverrides);
		bLoaded = bWasLoaded;
		return false;
	}

	// The file names may differ while the values do not (a value moved to another file).
	if (bChanged)
	{
		++Revision;
	}
	return true;
}

bool FSandboxParamStore::FindNumber(const FString& Key, const FString& State, double& OutValue) const
{
	const auto Find = [this, &OutValue](const FString& FullKey)
	{
		const double* Value = Overrides.Find(FullKey);
		if (!Value)
		{
			Value = FileValues.Numbers.Find(FullKey);
		}
		if (Value)
		{
			OutValue = *Value;
		}
		return Value != nullptr;
	};
	return (!State.IsEmpty() && Find(StateKey(State, Key))) || Find(Key);
}

bool FSandboxParamStore::FindString(const FString& Key, FString& OutValue) const
{
	const FString* Value = FileValues.Strings.Find(Key);
	if (Value)
	{
		OutValue = *Value;
	}
	return Value != nullptr;
}

void FSandboxParamStore::GetStatesInUse(TArray<FString>& OutStates) const
{
	OutStates.Reset();
	const auto Collect = [&OutStates](const TMap<FString, double>& Values)
	{
		for (const TPair<FString, double>& Entry : Values)
		{
			FString State, BaseKey;
			if (SplitStateKey(Entry.Key, State, BaseKey) && !State.IsEmpty())
			{
				OutStates.AddUnique(State);
			}
		}
	};
	Collect(FileValues.Numbers);
	Collect(Overrides);
	OutStates.Sort();
}

void FSandboxParamStore::ListNumbers(TArray<FSandboxParamEntry>& OutEntries) const
{
	OutEntries.Reset();
	for (const TPair<FString, double>& Entry : FileValues.Numbers)
	{
		FSandboxParamEntry& Out = OutEntries.AddDefaulted_GetRef();
		Out.Key = Entry.Key;
		Out.Value = Out.FileValue = Entry.Value;
		Out.bInFile = true;
		Out.File = FindFile(Entry.Key);
		if (const double* Override = Overrides.Find(Entry.Key))
		{
			Out.Value = *Override;
			Out.bOverridden = true;
		}
	}
	for (const TPair<FString, double>& Entry : Overrides)
	{
		if (!FileValues.Numbers.Contains(Entry.Key))
		{
			FSandboxParamEntry& Out = OutEntries.AddDefaulted_GetRef();
			Out.Key = Entry.Key;
			Out.Value = Entry.Value;
			Out.bOverridden = true;
		}
	}
	OutEntries.Sort([](const FSandboxParamEntry& A, const FSandboxParamEntry& B) { return A.Key < B.Key; });
}

bool FSandboxParamStore::SetOverrides(const FString& Assignments, FString& OutError)
{
	if (!bLoaded)
	{
		OutError = TEXT("The tuning files are not loaded");
		return false;
	}
	TArray<TPair<FString, double>> Values;
	if (!ParseAssignments(Assignments, Values, OutError))
	{
		return false;
	}
	TMap<FString, double> NewOverrides = Overrides;
	for (const TPair<FString, double>& Value : Values)
	{
		NewOverrides.Add(Value.Key, Value.Value);
	}
	FFileValues SameValues = FileValues;
	return Commit(MoveTemp(SameValues), MoveTemp(NewOverrides), OutError);
}

bool FSandboxParamStore::ClearOverrides(const FString& Keys, FString& OutError)
{
	if (!bLoaded)
	{
		OutError = TEXT("The tuning files are not loaded");
		return false;
	}
	TMap<FString, double> NewOverrides;
	TArray<FString> Names;
	Keys.ParseIntoArray(Names, TEXT(","), /*bCullEmpty*/ true);
	if (Names.Num() > 0)
	{
		NewOverrides = Overrides;
		for (const FString& Name : Names)
		{
			if (NewOverrides.Remove(Name.TrimStartAndEnd()) == 0)
			{
				OutError = FString::Printf(TEXT("'%s' is not overridden"), *Name.TrimStartAndEnd());
				return false;
			}
		}
	}
	FFileValues SameValues = FileValues;
	return Commit(MoveTemp(SameValues), MoveTemp(NewOverrides), OutError);
}

// Console commands. They act on the process-wide store; whoever follows its revision (the body controller)
// picks the change up on its next tick.
namespace
{
	FSandboxParamStore* LoadedStore()
	{
		FSandboxParamStore& Store = FSandboxParamStore::Get();
		FString Error;
		if (!Store.EnsureLoaded(Error))
		{
			UE_LOG(LogSandbox, Error, TEXT("Tuning: %s"), *Error);
			return nullptr;
		}
		return &Store;
	}

	FAutoConsoleCommand TuneCommand(
		TEXT("Sandbox.Tune"),
		TEXT("Overrides tuning values until they are reset: Sandbox.Tune muscles.legs.stiffness_nm_per_rad=400,solver.position_iterations=32"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (FSandboxParamStore* Store = LoadedStore())
			{
				FString Error;
				const FString Assignments = FString::Join(Args, TEXT(","));
				if (Store->SetOverrides(Assignments, Error))
				{
					UE_LOG(LogSandbox, Display, TEXT("Tuning: override %s"), *Assignments);
				}
				else
				{
					UE_LOG(LogSandbox, Error, TEXT("Tuning: nothing was changed: %s"), *Error);
				}
			}
		}));

	FAutoConsoleCommand TuneResetCommand(
		TEXT("Sandbox.TuneReset"),
		TEXT("Removes overrides, so the files' values apply again: Sandbox.TuneReset [name ...]. Without names it removes all of them."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (FSandboxParamStore* Store = LoadedStore())
			{
				FString Error;
				if (Store->ClearOverrides(FString::Join(Args, TEXT(",")), Error))
				{
					UE_LOG(LogSandbox, Display, TEXT("Tuning: %d override(s) left"), Store->GetOverrides().Num());
				}
				else
				{
					UE_LOG(LogSandbox, Error, TEXT("Tuning: nothing was changed: %s"), *Error);
				}
			}
		}));

	FAutoConsoleCommand TuneListCommand(
		TEXT("Sandbox.TuneList"),
		TEXT("Prints the tuning values in force: Sandbox.TuneList [part of a name]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (const FSandboxParamStore* Store = LoadedStore())
			{
				TArray<FSandboxParamEntry> Entries;
				Store->ListNumbers(Entries);
				UE_LOG(LogSandbox, Display, TEXT("Tuning from '%s':"), *Store->GetDirectory());
				for (const FSandboxParamEntry& Entry : Entries)
				{
					if (Args.Num() > 0 && !Entry.Key.Contains(Args[0]))
					{
						continue;
					}
					const FString Source = !Entry.bOverridden ? Entry.File
						: Entry.bInFile ? FString::Printf(TEXT("override; %s has %g"), *Entry.File, Entry.FileValue)
						: FString(TEXT("override"));
					UE_LOG(LogSandbox, Display, TEXT("  %s = %g  (%s)"), *Entry.Key, Entry.Value, *Source);
				}
			}
		}));

	FAutoConsoleCommand TuneReloadCommand(
		TEXT("Sandbox.TuneReload"),
		TEXT("Reads the tuning files again now, without waiting for the next poll. Overrides stay."),
		FConsoleCommandDelegate::CreateLambda([]
		{
			FString Error;
			if (FSandboxParamStore::Get().Load(Error))
			{
				UE_LOG(LogSandbox, Display, TEXT("Tuning: read '%s'"), *FSandboxParamStore::Get().GetDirectory());
			}
			else
			{
				UE_LOG(LogSandbox, Error, TEXT("Tuning: the files were not applied, the previous values stay in force: %s"), *Error);
			}
		}));
}
