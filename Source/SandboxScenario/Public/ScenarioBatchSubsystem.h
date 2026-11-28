#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ScenarioBatchSubsystem.generated.h"

class FScenarioJobLog;

/**
 * Runs many scenarios in one engine process, so that start-up (about 90% of a single headless run's time) is paid
 * once. Exists only when the process was started with -ScenarioQueue=<dir>.
 *
 * The queue is a directory of job files, <dir>/<anything>.json, each
 *   { "scenario": "<file>", "out": "<dir>", "tune": "name=value,...", "tuning_dir": "<dir>", "shot": "<file.png>",
 *     "shot_at": "3.05,3.2" }
 * (all but scenario and out optional; shot and shot_at as -Shot= and -ShotAt= of a single run, rendered processes only).
 * Several processes may share one queue: a process takes a job by writing its marker, <dir>/taken/<file>.taken with its
 * process id, while it holds a lock file open for writing, which only one of them can do (TakeJob), and takes the next when
 * the job is done. When every file of a job is written and its run.log closed, <dir>/taken/<file>.done says so (with the
 * run's exit code) to whoever waits for it from another process. It exits when the queue is empty: at once, without the
 * engine's orderly shutdown, when it renders nothing and writes no trace (every result is written by then).
 *
 * The first run of a process differs bit for bit from every later run of the same scenario, whatever ran before it
 * (measured: three processes with different jobs before it gave a later run the same hashes). So with
 * -ScenarioWarmup=<file> a short scenario runs first and its results are thrown away; every job then starts from the
 * same state, in any process and in any order.
 *
 * Every job runs in a world of its own: the scenario's map, or the map the process started in, opened anew (a new
 * physics scene). The tuning store starts over with the job's directory and overrides. The engine's log lines of a
 * job are also written to <out>/run.log. Rendering, the headset, overlays and console variables (-dpcvars) are the
 * process's, so jobs that differ in them go to another queue.
 *
 * With -ScenarioKeep=<file> (docs/fast-checks.md, SP-6) the assets that file lists ({ "assets": [ "<object path>", ... ] }) are
 * loaded after the warm-up and kept for the life of the process: a fixed set, the same in every process whatever its jobs, so
 * that a job's world does not load and build them again (the NPC's meshes were built anew for every job, about 0.4 s). Only in
 * a process that renders nothing: a rendered one (films, shots, the headset) keeps nothing, so that its pictures are as before.
 *
 * With -ScenarioPoolIdleS=<s> (docs/fast-checks.md, SP-4; Tools\pool.ps1) the process is one of a pool: when the queue is empty
 * it opens the map it started in and waits, sleeping 0.1 s a frame, for the next job, and exits when it has waited that long,
 * when a file 'stop' or 'yield_<pid>' is in the queue, when pool.json beside the queue no longer names the fingerprint it was started
 * with (-ScenarioPoolPrint=), after 500 jobs, or after a job when it uses over 2.2 GB. Why it exited: exited_<pid>.txt.
 */
UCLASS()
class SANDBOXSCENARIO_API UScenarioBatchSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	struct FJob
	{
		FString Scenario;
		FString Out;
		FString Tune;
		FString TuningDir;
		/** Pictures of a rendered run: the final frame's file, and simulated times for more ("3.05,3.2"). */
		FString Shot;
		FString ShotAt;
		/** The world to run it in: the scenario's map, or the map the process started in. */
		FString Map;
		/** The job's file name in the queue; empty for the warm-up. */
		FString File;
	};

	/** Whether this process runs a queue of scenarios. */
	static bool IsBatch();

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;

	/**
	 * Called by the runner as a world begins play: the job that world was opened for, or null when it is not one
	 * (the map the process started in, or a job that could not be read): the next job is then opened, or the process
	 * ends.
	 */
	const FJob* BeginWorld(UWorld& World);

	/** The runner has written the job's results: the next job is opened, or the process ends. */
	void JobDone(UWorld& World, uint8 Code);

private:
	/** Takes the next job from the queue and opens its world; ends the process when there is none. */
	void OpenNext(UWorld& World);
	/** Opens the world of Current, with its log and tuning. */
	void OpenJob(UWorld& World);
	bool TakeJob(FJob& OutJob);
	void CloseLog();
	/** Loads the assets -ScenarioKeep=<file> lists and keeps them (SP-6); once, after the warm-up. */
	void KeepAssets();
	/** A pool's process with nothing to do: waits in the start map (SP-4). */
	void BeginIdle(UWorld& World);
	/** Every frame while idle: sleeps, looks for a job and for a reason to exit. */
	bool TickIdle(float DeltaTime);
	/** Why a pool's process should exit now, or empty. */
	FString PoolExitReason(bool bAfterJob) const;
	/** Ends the process (as when the queue is empty), saying why in exited_<pid>.txt for a pool's process. */
	void ExitProcess(const FString& Why);

	FString QueueDir;
	FString StartMap;
	FJob Current;
	bool bOpening = false;
	/** -ScenarioWarmup=<file>: run once before the first job, results not counted. */
	bool bWarmedUp = false;
	bool bWarmupRunning = false;
	int32 Done = 0;
	int32 Failed = 0;
	/** Owned; deleted by CloseLog. */
	FScenarioJobLog* Log = nullptr;
	/** The assets of -ScenarioKeep, held for the life of the process. */
	UPROPERTY()
	TArray<TObjectPtr<UObject>> Kept;
	/** -ScenarioPoolIdleS (0: not a pool's process) and -ScenarioPoolPrint. */
	double PoolIdleS = 0.0;
	FString PoolPrint;
	bool bIdle = false;
	/** The start map, opened to wait in, has begun play. */
	bool bIdleOpened = false;
	double IdleSinceS = 0.0;
	/** Since when pool.json has been missing (-1: it is there); read by the const PoolExitReason. */
	mutable double PoolFileMissingSinceS = -1.0;
	FTSTicker::FDelegateHandle IdleTick;
};
