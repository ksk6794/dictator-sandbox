#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "ScenarioSpec.h"
#include "Subsystems/WorldSubsystem.h"
#include "SandboxPlaySubsystem.generated.h"

class ASandboxPawn;
class ASandboxRagdollActor;

/**
 * An interactive session: the scene of a scenario file, handed to a person. Exists only when the process
 * was started with -Play=.
 *
 *   -Play=<file or name>   Scenarios/<name>.json, or a path. Its map, body and camera are used; its events
 *                          and expectations are not
 *   -PlaySmoke             no person: the session drives the pawn's tools itself for a few seconds, checks
 *                          what they did, writes <Out>/play.json and exits (0 pass, 1 a check failed)
 *   -Out=<dir>             output directory of the self-test (default Saved/Play/<name>)
 *   -Shot=<file.png>       screenshot at the end of the self-test (rendered runs only)
 *   -Tune=, -TuningDir=    as for scenarios
 *
 * This is where the player's side and the character meet in code, which neither of their modules may do.
 * The session puts the character into the world, and carries out the two requests of the pawn that need
 * the character itself: the calibrated impulse goes through the same USandboxBodyController::ApplyImpulse
 * as a scenario's "impulse" event, and a reset replaces the character with a fresh one.
 *
 * The session runs on the same fixed timestep as scenarios and holds the frame rate to it, so that one
 * second of the session is one second on the clock. Holding the rate reads the clock; nothing in the
 * simulation does.
 */
UCLASS()
class SANDBOXSCENARIO_API USandboxPlaySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return bActive; }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	bool AdoptPawn(UWorld& World);
	void ApplyImpulse(const FHitResult& Hit, const FVector& ImpulseNs);
	void ResetCharacter();
	void AddHudLines(TArray<FString>& OutLines) const;
	void HoldFrameRate();
	void Fail(const FString& Message);

	// The self-test.
	void TickSmoke();
	void Check(const TCHAR* Name, bool bPass, const FString& Detail);
	void FinishSmoke();

	FScenarioSpec Spec;
	FString OutDir;
	FString ShotPath;
	bool bActive = false;
	bool bPawnAdopted = false;
	int32 Resets = 0;
	/** Clock time at which the next frame is due, and the count and start of the frames being timed for the log. */
	double NextFrameS = 0.0;
	int32 PacedFrames = 0;
	double PacedSinceS = 0.0;

	UPROPERTY()
	TObjectPtr<ASandboxRagdollActor> Character;

	TWeakObjectPtr<ASandboxPawn> Pawn;

	bool bSmoke = false;
	bool bSmokePass = true;
	int32 SmokeFrame = 0;
	int32 ExitCountdown = -1;
	TArray<TSharedPtr<FJsonValue>> SmokeChecks;
	int64 SmokeFramesAtPause = 0;
	int64 SmokeFramesAtSlow = 0;
	FTransform SmokePelvisAtPause = FTransform::Identity;
	FVector SmokeGripStart = FVector::ZeroVector;
	FVector SmokeSpawnPelvis = FVector::ZeroVector;
};
