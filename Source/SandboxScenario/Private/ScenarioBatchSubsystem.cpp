#include "ScenarioBatchSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
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
	if (IdleTick.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(IdleTick);
		IdleTick.Reset();
	}
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
		FParse::Value(FCommandLine::Get(), TEXT("ScenarioPoolIdleS="), PoolIdleS);
		FParse::Value(FCommandLine::Get(), TEXT("ScenarioPoolPrint="), PoolPrint);
	}
	if (bIdle)
	{
		// The start map, opened for a pool's process to wait in (BeginIdle): no job, and nothing to open.
		bIdleOpened = true;
		return nullptr;
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
	const bool bWarmup = bWarmupRunning;
	if (bWarmupRunning)
	{
		bWarmupRunning = false;
		UE_LOG(LogSandbox, Display, TEXT("Scenario batch: warm-up done (%d)"), Code);
		KeepAssets();
	}
	else
	{
		++Done;
		if (Code != 0)
		{
			++Failed;
		}
		// With the process's memory, read only (SP-6: what a process keeps must not grow from job to job).
		UE_LOG(LogSandbox, Display, TEXT("Scenario batch: job %d done (%d); the process uses %.0f MB"), Done, Code,
			static_cast<double>(FPlatformMemory::GetStats().UsedPhysical) / (1024.0 * 1024.0));
	}
	CloseLog();
	// Every file of the job is written and its run.log closed: its done marker says so to a tool that waits for it from another
	// process (a check set, Tools\common.ps1, Submit-CheckJobs) and may then read the job's files or remove its folder.
	if (!bWarmup && !Current.File.IsEmpty())
	{
		const FString Marker = FPaths::Combine(QueueDir, TEXT("taken"), Current.File + TEXT(".done"));
		if (!FFileHelper::SaveStringToFile(FString::FromInt(Code), *Marker))
		{
			UE_LOG(LogSandbox, Error, TEXT("Scenario batch: cannot write '%s'"), *Marker);
		}
	}
	if (PoolIdleS > 0.0 && !bWarmup)
	{
		const FString Why = PoolExitReason(/*bAfterJob*/ true);
		if (!Why.IsEmpty())
		{
			ExitProcess(Why);
			return;
		}
	}
	OpenNext(World);
}

FString UScenarioBatchSubsystem::PoolExitReason(bool bAfterJob) const
{
	IPlatformFile& Platform = FPlatformFileManager::Get().GetPlatformFile();
	if (Platform.FileExists(*FPaths::Combine(QueueDir, TEXT("stop"))))
	{
		return TEXT("a stop file");
	}
	if (Platform.FileExists(*FPaths::Combine(QueueDir, FString::Printf(TEXT("yield_%u"), FPlatformProcess::GetCurrentProcessId()))))
	{
		return TEXT("asked to give way");
	}
	if (bAfterJob)
	{
		const FPlatformMemoryStats Memory = FPlatformMemory::GetStats();
		if (Done >= 500)
		{
			return TEXT("500 jobs");
		}
		if (static_cast<double>(Memory.UsedPhysical) > 2.2 * 1024.0 * 1024.0 * 1024.0)
		{
			return TEXT("over 2.2 GB of memory");
		}
		if (static_cast<double>(Memory.AvailablePhysical) < 1.5 * 1024.0 * 1024.0 * 1024.0)
		{
			return TEXT("under 1.5 GB of the machine's memory free");
		}
	}
	else if (bIdle && FPlatformTime::Seconds() - IdleSinceS > PoolIdleS)
	{
		return TEXT("idle");
	}
	// What it read once at its start (the build, the settings, the assets): the pool's file names it. A file being written cannot be
	// read for a moment, which is not a change.
	if (!PoolPrint.IsEmpty())
	{
		// Beside the queue, not in it (a .json in the queue is a job). Missing for 2 s, not for a moment.
		const FString PoolFile = FPaths::Combine(FPaths::GetPath(QueueDir), TEXT("pool.json"));
		FString Text;
		if (!Platform.FileExists(*PoolFile))
		{
			const double Now = FPlatformTime::Seconds();
			if (PoolFileMissingSinceS < 0.0)
			{
				PoolFileMissingSinceS = Now;
			}
			return Now - PoolFileMissingSinceS > 2.0 ? TEXT("the pool's file is gone") : FString();
		}
		PoolFileMissingSinceS = -1.0;
		if (FFileHelper::LoadFileToString(Text, *PoolFile) && !Text.Contains(PoolPrint))
		{
			return TEXT("the pool's fingerprint changed");
		}
	}
	return FString();
}

void UScenarioBatchSubsystem::ExitProcess(const FString& Why)
{
	// A process without a renderer and without a trace ends at once (docs/fast-checks.md, SP-2): the engine tells its
	// will-terminate delegate, flushes the log and ends the process, without the orderly shutdown that took 0.4 to 1.3 s
	// (WindowsPlatformMisc.cpp, RequestExitWithStatus). Every result is written by then: a job's files before JobDone,
	// its run.log closed with it. Not a rendered process, whose pictures the engine writes in the background, nor one
	// with a trace, which only an orderly exit finishes writing.
	const bool bTraced = FTraceAuxiliary::IsConnected() || FCString::Stristr(FCommandLine::Get(), TEXT("-trace")) != nullptr;
	const bool bForce = !FApp::CanEverRender() && !bTraced;
	if (PoolIdleS > 0.0)
	{
		FFileHelper::SaveStringToFile(FString::Printf(TEXT("%s after %d job(s)"), *Why, Done),
			*FPaths::Combine(QueueDir, FString::Printf(TEXT("exited_%u.txt"), FPlatformProcess::GetCurrentProcessId())));
		// Its own request to give way, so that a later process with the same id does not take it for its own.
		IFileManager::Get().Delete(*FPaths::Combine(QueueDir, FString::Printf(TEXT("yield_%u"), FPlatformProcess::GetCurrentProcessId())),
			/*RequireExists*/ false, /*EvenReadOnly*/ true, /*Quiet*/ true);
	}
	UE_LOG(LogSandbox, Display, TEXT("Scenario batch: %s after %d job(s), %d not passed%s"), *Why, Done, Failed, bForce ? TEXT("; exiting at once") : TEXT(""));
	FPlatformMisc::RequestExitWithStatus(bForce, Failed > 0 ? 1 : 0);
}

void UScenarioBatchSubsystem::BeginIdle(UWorld& World)
{
	bIdle = true;
	bIdleOpened = false;
	IdleSinceS = FPlatformTime::Seconds();
	UE_LOG(LogSandbox, Display, TEXT("Scenario batch: waiting for jobs (a pool's process, %.0f s at most)"), PoolIdleS);
	if (!IdleTick.IsValid())
	{
		IdleTick = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UScenarioBatchSubsystem::TickIdle));
	}
	// The start map anew, so that the last job's world (a body still simulating) is gone while it waits.
	UGameplayStatics::OpenLevel(&World, FName(*StartMap));
}

bool UScenarioBatchSubsystem::TickIdle(float DeltaTime)
{
	if (!bIdle)
	{
		IdleTick.Reset();
		return false;
	}
	// With a fixed timestep and no renderer the engine's loop runs as fast as it can: an idle process would spin a core. A frame
	// every 0.1 s (at 0.05 s an idle process took 3 to 4.5% of a core; a job then waits 0.1 s at most).
	FPlatformProcess::Sleep(0.1f);
	// A reason to exit is heeded even before the start map has opened (one that never opens would otherwise hold the process).
	const FString Why = PoolExitReason(/*bAfterJob*/ false);
	if (!Why.IsEmpty())
	{
		bIdle = false;
		IdleTick.Reset();
		ExitProcess(Why);
		return false;
	}
	UGameInstance* Instance = GetGameInstance();
	UWorld* World = Instance ? Instance->GetWorld() : nullptr;
	if (!bIdleOpened || !World)
	{
		return true;
	}
	if (TakeJob(Current))
	{
		bIdle = false;
		IdleTick.Reset();
		OpenJob(*World);
		return false;
	}
	return true;
}

void UScenarioBatchSubsystem::KeepAssets()
{
	FString File;
	if (!Kept.IsEmpty() || !FParse::Value(FCommandLine::Get(), TEXT("ScenarioKeep="), File))
	{
		return;
	}
	// Not in a rendered process (films, shots, the headset): kept meshes keep their textures streamed in from job to job, so the first
	// rendered job's pictures could differ from a later one's, and in the headset the load would be a hitch in a ticking world.
	if (FApp::CanEverRender())
	{
		UE_LOG(LogSandbox, Display, TEXT("Scenario batch: a rendered process keeps no assets"));
		return;
	}
	TSharedPtr<FJsonObject> Json;
	FString Error;
	const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
	if (!SandboxJson::LoadObject(FPaths::ConvertRelativePathToFull(File), Json, Error) || !Json->TryGetArrayField(TEXT("assets"), Paths))
	{
		UE_LOG(LogSandbox, Error, TEXT("Scenario batch: the assets to keep cannot be read from '%s': %s"), *File, *Error);
		return;
	}
	for (const TSharedPtr<FJsonValue>& Path : *Paths)
	{
		const FString Name = Path.IsValid() ? Path->AsString() : FString();
		UObject* Asset = Name.IsEmpty() ? nullptr : LoadObject<UObject>(nullptr, *Name);
		if (!Asset)
		{
			UE_LOG(LogSandbox, Warning, TEXT("Scenario batch: '%s' is not there to keep"), *Name);
			continue;
		}
		Kept.Add(Asset);
	}
	UE_LOG(LogSandbox, Display, TEXT("Scenario batch: %d asset(s) kept for the process's life (%s)"), Kept.Num(), *File);
}

bool UScenarioBatchSubsystem::TakeJob(FJob& OutJob)
{
	TArray<FString> Names;
	IFileManager::Get().FindFiles(Names, *FPaths::Combine(QueueDir, TEXT("*.json")), /*Files*/ true, /*Directories*/ false);
	Names.Sort();
	const FString TakenDir = FPaths::Combine(QueueDir, TEXT("taken"));
	IPlatformFile& Platform = FPlatformFileManager::Get().GetPlatformFile();
	for (const FString& Name : Names)
	{
		// A job belongs to the process that writes its marker, and the marker is looked for and written while a
		// lock file is held open for writing, which only one process can do at a time. (Taking a job by moving
		// its file does not work: Windows renames a file through a handle, two processes can each open one
		// before either renames, and then both renames succeed, the second taking the file from under the
		// first. With 16 processes on one queue about one job in a thousand was run by nobody.) A process that
		// cannot get the lock leaves the job to the one that holds it, which either takes it or finds it taken.
		const FString Marker = FPaths::Combine(TakenDir, Name + TEXT(".taken"));
		if (Platform.FileExists(*Marker))
		{
			continue;
		}
		{
			const TUniquePtr<IFileHandle> Lock(Platform.OpenWrite(*FPaths::Combine(TakenDir, Name + TEXT(".lock")), /*bAppend*/ true, /*bAllowRead*/ false));
			if (!Lock || Platform.FileExists(*Marker))
			{
				continue;
			}
			if (!FFileHelper::SaveStringToFile(FString::FromInt(FPlatformProcess::GetCurrentProcessId()), *Marker))
			{
				UE_LOG(LogSandbox, Error, TEXT("Scenario batch: cannot write '%s'"), *Marker);
				continue;
			}
		}
		// The job is this process's alone from here on: nobody else opens or moves its file.
		const FString Source = FPaths::Combine(QueueDir, Name);
		TSharedPtr<FJsonObject> Json;
		FString Error;
		const bool bRead = SandboxJson::LoadObject(Source, Json, Error);
		// Out of the queue, so that the processes still looking have less to look through.
		IFileManager::Get().Move(*FPaths::Combine(TakenDir, FString::Printf(TEXT("%s.%u"), *Name, FPlatformProcess::GetCurrentProcessId())), *Source,
			/*bReplace*/ true, /*bEvenIfReadOnly*/ false, /*bAttributes*/ false, /*bDoNotRetryOrError*/ true);
		if (!bRead)
		{
			UE_LOG(LogSandbox, Error, TEXT("Scenario batch: %s"), *Error);
			continue;
		}
		OutJob = FJob();
		OutJob.File = Name;
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
	// A pool's process told to stop, to give way, or started on another fingerprint takes no further job (after its warm-up as well).
	if (PoolIdleS > 0.0)
	{
		const FString Why = PoolExitReason(/*bAfterJob*/ false);
		if (!Why.IsEmpty())
		{
			ExitProcess(Why);
			return;
		}
	}
	if (!TakeJob(Current))
	{
		if (PoolIdleS > 0.0)
		{
			const FString Why = PoolExitReason(/*bAfterJob*/ false);
			if (Why.IsEmpty())
			{
				BeginIdle(World);
				return;
			}
			ExitProcess(Why);
			return;
		}
		ExitProcess(TEXT("queue empty"));
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
