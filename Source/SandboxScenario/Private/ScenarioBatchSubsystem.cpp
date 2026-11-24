#include "ScenarioBatchSubsystem.h"

#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/OutputDeviceHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "SandboxCore.h"
#include "SandboxJson.h"
#include "SandboxParamStore.h"
#include "ScenarioSpec.h"

/** The engine's log lines while one job runs, into that job's run.log. */
class FScenarioJobLog : public FOutputDevice
{
public:
	explicit FScenarioJobLog(const FString& Path)
		: Writer(IFileManager::Get().CreateFileWriter(*Path))
	{
	}
	virtual ~FScenarioJobLog() override
	{
		FScopeLock Lock(&Guard);
		Writer.Reset();
	}
	virtual void Serialize(const TCHAR* Text, ELogVerbosity::Type Verbosity, const FName& Category) override
	{
		FScopeLock Lock(&Guard);
		if (!Writer)
		{
			return;
		}
		const FString Line = FOutputDeviceHelper::FormatLogLine(Verbosity, Category, Text, ELogTimes::None) + LINE_TERMINATOR;
		const FTCHARToUTF8 Utf8(*Line);
		Writer->Serialize(const_cast<ANSICHAR*>(Utf8.Get()), Utf8.Length());
	}
	virtual void Flush() override
	{
		FScopeLock Lock(&Guard);
		if (Writer)
		{
			Writer->Flush();
		}
	}
	virtual bool CanBeUsedOnAnyThread() const override { return true; }

private:
	FCriticalSection Guard;
	TUniquePtr<FArchive> Writer;
};

bool UScenarioBatchSubsystem::IsBatch()
{
	FString Unused;
	return FParse::Value(FCommandLine::Get(), TEXT("ScenarioQueue="), Unused);
}

bool UScenarioBatchSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return IsBatch() && Super::ShouldCreateSubsystem(Outer);
}

void UScenarioBatchSubsystem::Deinitialize()
{
	CloseLog();
	Super::Deinitialize();
}

void UScenarioBatchSubsystem::CloseLog()
{
	if (Log)
	{
		GLog->Flush();
		GLog->RemoveOutputDevice(Log);
		delete Log;
		Log = nullptr;
	}
}

const UScenarioBatchSubsystem::FJob* UScenarioBatchSubsystem::BeginWorld(UWorld& World)
{
	if (StartMap.IsEmpty())
	{
		StartMap = World.GetOutermost()->GetName();
		FParse::Value(FCommandLine::Get(), TEXT("ScenarioQueue="), QueueDir);
		QueueDir = FPaths::ConvertRelativePathToFull(QueueDir);
	}
	if (!bOpening)
	{
		// The first run of a process differs from every later one, bit for bit (the engine's own state after a first
		// physics scene; which run comes first makes no difference to the later ones). A short warm-up run goes first,
		// so that every job of every process starts from the same state.
		FString Warmup;
		if (!bWarmedUp && FParse::Value(FCommandLine::Get(), TEXT("ScenarioWarmup="), Warmup))
		{
			bWarmedUp = true;
			bWarmupRunning = true;
			Current = FJob();
			Current.Scenario = FPaths::ConvertRelativePathToFull(Warmup);
			Current.Out = FPaths::Combine(QueueDir, TEXT("taken"), FString::Printf(TEXT("warmup_%u"), FPlatformProcess::GetCurrentProcessId()));
			Current.Map = StartMap;
			OpenJob(World);
			return nullptr;
		}
		OpenNext(World);
		return nullptr;
	}
	bOpening = false;
	return &Current;
}

void UScenarioBatchSubsystem::JobDone(UWorld& World, uint8 Code)
{
	if (bWarmupRunning)
	{
		bWarmupRunning = false;
		UE_LOG(LogSandbox, Display, TEXT("Scenario batch: warm-up done (%d)"), Code);
	}
	else
	{
		++Done;
		if (Code != 0)
		{
			++Failed;
		}
		UE_LOG(LogSandbox, Display, TEXT("Scenario batch: job %d done (%d)"), Done, Code);
	}
	CloseLog();
	OpenNext(World);
}

bool UScenarioBatchSubsystem::TakeJob(FJob& OutJob)
{
	TArray<FString> Names;
	IFileManager::Get().FindFiles(Names, *FPaths::Combine(QueueDir, TEXT("*.json")), /*Files*/ true, /*Directories*/ false);
	Names.Sort();
	const FString TakenDir = FPaths::Combine(QueueDir, TEXT("taken"));
	for (const FString& Name : Names)
	{
		// Only one process can move the file: the others find it gone and try the next.
		const FString Taken = FPaths::Combine(TakenDir, FString::Printf(TEXT("%s.%u"), *Name, FPlatformProcess::GetCurrentProcessId()));
		if (!IFileManager::Get().Move(*Taken, *FPaths::Combine(QueueDir, Name), /*bReplace*/ false, /*bEvenIfReadOnly*/ false, /*bAttributes*/ false, /*bDoNotRetryOrError*/ true))
		{
			continue;
		}
		TSharedPtr<FJsonObject> Json;
		FString Error;
		if (!SandboxJson::LoadObject(Taken, Json, Error))
		{
			UE_LOG(LogSandbox, Error, TEXT("Scenario batch: %s"), *Error);
			continue;
		}
		OutJob = FJob();
		Json->TryGetStringField(TEXT("scenario"), OutJob.Scenario);
		Json->TryGetStringField(TEXT("out"), OutJob.Out);
		Json->TryGetStringField(TEXT("tune"), OutJob.Tune);
		Json->TryGetStringField(TEXT("tuning_dir"), OutJob.TuningDir);
		Json->TryGetStringField(TEXT("shot"), OutJob.Shot);
		Json->TryGetStringField(TEXT("shot_at"), OutJob.ShotAt);
		OutJob.Map = StartMap;
		FScenarioSpec Spec;
		if (FScenarioSpec::LoadFile(OutJob.Scenario, Spec, Error) && !Spec.MapPath.IsEmpty())
		{
			OutJob.Map = Spec.MapPath;
		}
		return true;
	}
	return false;
}

void UScenarioBatchSubsystem::OpenNext(UWorld& World)
{
	if (!TakeJob(Current))
	{
		UE_LOG(LogSandbox, Display, TEXT("Scenario batch: queue empty after %d job(s), %d not passed"), Done, Failed);
		FPlatformMisc::RequestExitWithStatus(/*Force*/ false, Failed > 0 ? 1 : 0);
		return;
	}
	OpenJob(World);
}

void UScenarioBatchSubsystem::OpenJob(UWorld& World)
{
	IFileManager::Get().MakeDirectory(*Current.Out, /*Tree*/ true);
	Log = new FScenarioJobLog(FPaths::Combine(Current.Out, TEXT("run.log")));
	GLog->AddOutputDevice(Log);
	// The job's own tuning: its directory and overrides, read anew by the body that its world spawns.
	FSandboxParamStore::Get().ResetForRun(Current.TuningDir, Current.Tune);
	UE_LOG(LogSandbox, Display, TEXT("Scenario batch: job %d, %s in %s"), Done + 1, *Current.Scenario, *Current.Map);
	bOpening = true;
	UGameplayStatics::OpenLevel(&World, FName(*Current.Map));
}
