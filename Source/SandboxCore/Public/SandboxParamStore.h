#pragma once

#include "CoreMinimal.h"

/** One number of the store as `Sandbox.TuneList` prints it. */
struct FSandboxParamEntry
{
	/** Full name, "muscles.legs.stiffness_nm_per_rad" or "states.falling.muscles.legs.stiffness_nm_per_rad". */
	FString Key;
	/** The value in force: the override if there is one, the file's value otherwise. */
	double Value = 0.0;
	bool bOverridden = false;
	/** False for a state value that exists only as an override. */
	bool bInFile = false;
	double FileValue = 0.0;
	/** Name of the file that defines it; empty when bInFile is false. */
	FString File;
};

/**
 * Tunable parameters, read from the JSON files of one directory (Config/Tuning).
 *
 *   Names      A value's name is its path in the file: { "muscles": { "legs": { "damping_s": 0.06 } } } is
 *              "muscles.legs.damping_s". Several files are merged; a name may be defined only once.
 *   Schema     Modules declare the names they understand. A file with an undeclared name, or without a
 *              declared one, is rejected, so the code holds no default values and a misspelt name cannot
 *              pass unnoticed. Modules may also add validators (range checks).
 *   States     The top-level section "states" holds per-state values: "states.falling.muscles.legs.damping_s"
 *              replaces "muscles.legs.damping_s" while the reader asks for the state "falling".
 *   Overrides  A second layer on top of the files, set from the console (Sandbox.Tune) or the command line
 *              (-Tune=). Reloading the files leaves it in place.
 *   Hot reload PollFiles compares the files' size and modification time with what was loaded and re-reads
 *              them when they differ. It never reads the clock; with unchanged files it changes nothing.
 *
 * Every change (a reload or an override) is applied as a whole or not at all: if the schema or a validator
 * rejects the new set, the previous values stay in force. Readers follow GetRevision, which moves only when
 * a value really changed.
 *
 * Game thread only.
 */
class SANDBOXCORE_API FSandboxParamStore
{
public:
	using FValidator = TFunction<bool(const FSandboxParamStore& Store, FString& OutError)>;

	/**
	 * The process-wide store on Config/Tuning, or on the directory given with -TuningDir=. Overrides from
	 * -Tune="name=value,..." are applied by its first load. While the engine runs it polls its files every
	 * AutoPollIntervalFrames engine frames, unless SetAutoPoll(false) hands the polling to someone else.
	 */
	static FSandboxParamStore& Get();

	static constexpr int32 AutoPollIntervalFrames = 30;

	// Schema.
	void DeclareNumber(const FString& Key);
	void DeclareString(const FString& Key);
	void DeclareState(const FString& State);
	void AddValidator(FValidator Validator);

	// Files.

	/** Points the store at another directory and forgets the values read so far. Overrides are kept. */
	void SetDirectory(const FString& InDirectory);
	const FString& GetDirectory() const { return Directory; }

	/** Reads every *.json of the directory. False with a reason if anything is wrong; nothing changes then. */
	bool Load(FString& OutError);
	/** Load, unless the files are loaded already. */
	bool EnsureLoaded(FString& OutError);
	bool IsLoaded() const { return bLoaded; }

	/** Loads from text instead of a directory (file name to JSON text). For tests. */
	bool LoadFromText(const TMap<FString, FString>& Documents, FString& OutError);

	/**
	 * Re-reads the files if their size or modification time changed since they were last read. True if that
	 * changed a value. A changed file that cannot be applied leaves the values as they were; the reason goes
	 * to OutError, or to the log when OutError is null.
	 */
	bool PollFiles(FString* OutError = nullptr);

	void SetAutoPoll(bool bEnabled) { bAutoPoll = bEnabled; }
	/** Called once per engine frame for the process-wide store. */
	void TickAutoPoll();

	/** Names of the loaded files, sorted. */
	void GetFileNames(TArray<FString>& OutNames) const;
	/** Name of the file that defines a number or a text value; empty if none does. */
	FString FindFile(const FString& Key) const;

	// Values.

	/**
	 * The number in force for a name. With a state, "states.<state>.<key>" wins over "<key>"; in both places
	 * an override wins over the file. An empty state reads the base value.
	 */
	bool FindNumber(const FString& Key, const FString& State, double& OutValue) const;
	bool FindString(const FString& Key, FString& OutValue) const;

	/** States that have at least one value of their own, in the files or as an override. Sorted. */
	void GetStatesInUse(TArray<FString>& OutStates) const;

	/** Every number, base and per-state, sorted by name. */
	void ListNumbers(TArray<FSandboxParamEntry>& OutEntries) const;

	// Overrides.

	/** Applies "name=value,name=value". All of them or none: false with a reason if one is refused. */
	bool SetOverrides(const FString& Assignments, FString& OutError);
	/** Removes the overrides of the names in "name,name", or all of them when the list is empty. */
	bool ClearOverrides(const FString& Keys, FString& OutError);
	const TMap<FString, double>& GetOverrides() const { return Overrides; }

	/** Moves whenever a value changes. */
	uint32 GetRevision() const { return Revision; }

private:
	struct FFileValues
	{
		TMap<FString, double> Numbers;
		TMap<FString, FString> Strings;
		/** Name of the file each key came from. */
		TMap<FString, FString> Files;
	};

	struct FFileStamp
	{
		FDateTime Modified;
		int64 Size = 0;
		bool operator==(const FFileStamp& Other) const { return Modified == Other.Modified && Size == Other.Size; }
	};

	bool ReadDirectory(FFileValues& OutValues, FString& OutError) const;
	void StatDirectory(TMap<FString, FFileStamp>& OutStamps) const;
	/** Puts the new set in force if the schema and the validators accept it; restores the old one if not. */
	bool Commit(FFileValues&& NewValues, TMap<FString, double>&& NewOverrides, FString& OutError);
	bool CheckSchema(FString& OutError) const;
	bool CheckNumberKey(const FString& Key, FString& OutError) const;

	FString Directory;
	bool bLoaded = false;
	bool bAutoPoll = true;
	int32 AutoPollCounter = 0;
	uint32 Revision = 0;

	FFileValues FileValues;
	TMap<FString, FFileStamp> SeenStamps;
	TMap<FString, double> Overrides;
	/** -Tune= from the command line, waiting for the first load. Only the process-wide store has any. */
	FString StartupOverrides;

	TSet<FString> DeclaredNumbers;
	TSet<FString> DeclaredStrings;
	TSet<FString> DeclaredStates;
	TArray<FValidator> Validators;
};
