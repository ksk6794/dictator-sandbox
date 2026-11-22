#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "ScenarioSpec.h"
#include "Subsystems/WorldSubsystem.h"
#include <atomic>
#include "ScenarioRunnerSubsystem.generated.h"

class ASandboxRagdollActor;
class ACameraActor;

/**
 * Runs one scenario and exits. Exists only when the process was started with -Scenario=.
 *
 *   -Scenario=<file or name>   Scenarios/<name>.json, or a path (relative paths resolve against the project dir)
 *   -Out=<dir>                 output directory (default Saved/Scenarios/<name>)
 *   -Shot=<file.png>           optional screenshot of the final frame (rendered runs only)
 *   -Tune="name=value,..."     tuning overrides for this run (read by FSandboxParamStore, active control only)
 *   -TuningDir=<dir>           tuning files to use instead of Config/Tuning (also read by the store)
 *   -vr                        engine flag: run in the headset. The camera becomes the tracking origin on
 *                              the floor, and metrics.json gains the headset's render size
 *
 * Writes <Out>/metrics.json (summary, checks, pass) and <Out>/trace.json (body and bone transforms at
 * checkpoints). Exit code: 0 pass, 1 a check failed, 2 the scenario could not run.
 *
 * The run forces a fixed engine timestep, so it is independent of wall-clock time and runs as fast as
 * the machine allows.
 */
UCLASS()
class SANDBOXSCENARIO_API UScenarioRunnerSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return Phase != EPhase::Idle; }
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	enum class EPhase : uint8 { Idle, Running, Finishing };

	/** A body held at a fixed world position by a "pin" event. */
	struct FPin
	{
		FName Body;
		FVector WorldLocation = FVector::ZeroVector;
		TUniquePtr<FConstraintInstance> Constraint;
	};
	TArray<FPin> Pins;

	void SpawnEnvironment(UWorld& World);
	bool SpawnBody(UWorld& World, FString& OutError);
	void SampleFrame(float DeltaTime);
	void ApplyDueEvents(double SimTimeS);
	bool CopyTuningFiles(FString& OutError);
	bool EditTuningFiles(const FScenarioEvent& Event, FString& OutError);
	void Finish();
	void FailAndExit(const FString& Message);
	void RequestExit(uint8 Code);

	FScenarioSpec Spec;
	FString ScenarioFile;
	FString OutDir;
	FString ShotPath;

	EPhase Phase = EPhase::Idle;
	int32 FrameIndex = 0;
	int32 TotalFrames = 0;
	double FixedDt = 0.0;
	int32 NextEventIndex = 0;
	double NextCheckpointTimeS = 0.0;
	double WallStartS = 0.0;
	int32 FinishCountdown = 0;
	uint8 ExitCode = 0;

	UPROPERTY()
	TObjectPtr<ASandboxRagdollActor> Ragdoll;

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	// Accumulated over the run.
	double MaxJointStretchCm = 0.0;
	double InitialJointOffsetCm = 0.0;
	double MaxPinErrorCm = 0.0;
	FName WorstJoint;
	double MaxBodySpeedCmS = 0.0;
	double MaxAngularSpeedRadS = 0.0;
	double MinBodyZCm = TNumericLimits<double>::Max();
	double FinalKineticEnergyJ = 0.0;
	double TotalMassKg = 0.0;
	int32 LastActiveFrame = -1;
	int32 DtViolations = 0;
	int32 EventsApplied = 0;
	bool bSawNaN = false;
	bool bLeftBounds = false;

	// Standing metrics. Drift is measured from the pose the body was spawned in.
	FName PelvisBone;
	FName HeadBone;
	FVector PelvisStartCm = FVector::ZeroVector;
	FVector ComStartCm = FVector::ZeroVector;
	double MaxPelvisDriftCm = 0.0;
	double FinalPelvisDriftCm = 0.0;
	double MaxComDriftCm = 0.0;
	double MinHeadHeightCm = TNumericLimits<double>::Max();
	double MaxSettledSpeedCmS = 0.0;
	double MaxSettledAngularSpeedRadS = 0.0;
	/** Per body, by bone: fastest linear (X, cm/s) and angular (Y, rad/s) speed after the settle time. */
	TMap<FName, FVector2D> SettledBodySpeeds;
	double MaxJointSpeedRadS = 0.0;
	FName FastestJoint;

	// Falling, judged by the pelvis alone: fallen once it is below half the height above the spawn point
	// that it started at. A body that sits down with a straight back has fallen; its head has not dropped far.
	double PelvisStartHeightCm = 0.0;
	double FinalPelvisHeightCm = 0.0;
	int32 FallFrame = -1;

	// Tuning. A scenario with an "edit_tuning_file" event runs on its own copy of the tuning files, so the
	// event never writes to Config/Tuning. Every change of the running body's values is recorded.
	bool bOwnTuningFiles = false;
	int32 SeenTuningChanges = 0;
	TArray<TPair<FString, double>> LastTuningValues;
	TArray<TSharedPtr<FJsonValue>> TuningChanges;

	// Joint angles in the spawn pose (twist, swing1, swing2 in degrees), by joint name, and how far the
	// worst one lies outside its limits. A pose outside the limits is snapped inside by the first steps.
	TMap<FName, FVector> SpawnJointAnglesDeg;
	/** Distance between each joint's two anchors in the spawn pose, cm. */
	TMap<FName, double> SpawnJointOffsetCm;
	double SpawnLimitViolationDeg = 0.0;
	FName SpawnWorstLimitJoint;

	// Balance-assist load after the settle time (active control only).
	double AssistForceShareSum = 0.0;
	double AssistForceShareMax = 0.0;
	double AssistTorqueShareSum = 0.0;
	double AssistTorqueShareMax = 0.0;
	int32 AssistSamples = 0;

	// Wall-clock time the solver spent on each frame, summed over its substeps. The two callbacks run on
	// the physics task while the game thread waits for it, hence the atomics.
	void StartPhysicsTimer(UWorld& World);
	void StopPhysicsTimer();
	FDelegateHandle PreSolveHandle;
	FDelegateHandle PostSolveHandle;
	std::atomic<uint64> SolveStartCycles{ 0 };
	std::atomic<uint64> FrameSolveCycles{ 0 };
	std::atomic<int32> FrameSolveSteps{ 0 };
	TArray<float> PhysicsStepMs;
	int64 SolverStepsTotal = 0;
	uint32 BodyHash = 0;
	uint32 BoneHash = 0;
	TArray<TSharedPtr<FJsonValue>> Checkpoints;

	// Rendered runs only: per-frame timings in milliseconds, collected after a warm-up.
	bool IsStereo() const;
	double LastFrameWallS = 0.0;
	TArray<float> FrameWallMs;
	TArray<float> FrameGpuMs;
	TArray<float> FrameGameMs;
	TArray<float> FrameRenderMs;
};
