#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "ScenarioSpec.h"
#include "Subsystems/WorldSubsystem.h"
#include "ScenarioRunnerSubsystem.generated.h"

class ASandboxRagdollActor;
class ACameraActor;

/**
 * Runs one scenario and exits. Exists only when the process was started with -Scenario=.
 *
 *   -Scenario=<file or name>   Scenarios/<name>.json, or a path (relative paths resolve against the project dir)
 *   -Out=<dir>                 output directory (default Saved/Scenarios/<name>)
 *   -Shot=<file.png>           optional screenshot of the final frame (rendered runs only)
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
