#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "ScenarioSpec.h"
#include "Subsystems/WorldSubsystem.h"
#include "SandboxPlaySubsystem.generated.h"

class ASandboxPawn;
class ASandboxRagdollActor;
struct FSandboxTargetRecording;

/**
 * An interactive session: the scene of a scenario file, handed to a person. Exists only when the process
 * was started with -Play=.
 *
 *   -Play=<file or name>   Scenarios/<name>.json, or a path. Its map, body and camera are used; its events
 *                          and expectations are not
 *   -PlaySmoke             no person: the session drives the pawn's tools itself for a few seconds, checks
 *                          what they did, writes <Out>/play.json and exits (0 pass, 1 a check failed)
 *   -PlayDemo              no person either: the session uses the hands alone (the right one reaches into the
 *                          chest, then the left), records them, writes the recording, <Out>/play.json and exits.
 *                          What the recording's own check is made of (Tools\record-replay.ps1)
 *   -PlayDemoTakes         as -PlayDemo, in three takes (ASandboxPawn::RequestNewTake): the character is put
 *                          back before each, and each is written as a recording of its own (<name>, <name>_2,
 *                          <name>_3)
 *   -Record=<name>         the hands are recorded from the first frame until the session ends, or until the K
 *                          key ends it; K starts and ends further recordings. Each is written to
 *                          Saved/Recordings/<name>/ as <name>.recording.json, with a scenario that replays it
 *                          (<name>.json) and what the session saw of its outcome (<name>.session.json)
 *   -vr                    the engine's own switch: the world is shown in the headset, the pawn's hands follow
 *                          its controllers, and the headset paces the frames. Without a headset the session
 *                          ends with an error. The player stands at arm's length from the character
 *                          (tools.headset.stand_distance_cm)
 *   -PlaySeconds=<s>       the session ends by itself after this many simulated seconds, as Escape would end
 *                          it: a recording that runs is written first
 *   -Out=<dir>             output directory of the self-test (default Saved/Play/<name>)
 *   -Shot=<file.png>       screenshot at the end of the self-test (rendered runs only)
 *   -Tune=, -TuningDir=    as for scenarios
 *   -Overlay=<layers>      debug overlays from the start (USandboxOverlaySubsystem); the V key and the
 *                          console command Sandbox.Overlay switch them in a running session
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
	void RequestReset();
	void ResetCharacter();
	void AddHudLines(TArray<FString>& OutLines) const;
	void HoldFrameRate(bool bWait);
	void Fail(const FString& Message);

	// The self-test.
	void TickSmoke();
	void Check(const TCHAR* Name, bool bPass, const FString& Detail);
	void FinishSmoke();

	// Recordings of the hands. The pawn keeps the targets; the session, which knows the scene and the character,
	// writes them down with a scenario that replays them against a body of the same age.
	void SaveRecording(const TSharedRef<FSandboxTargetRecording>& Recording, int64 FirstFrame);
	void TickDemo();
	void FinishDemo();

	FString ScenarioFile;
	FString RecordName;
	bool bRecordFromStart = false;
	bool bRecordStarted = false;
	int32 RecordStartTries = 0;
	/** Simulated seconds after which the session ends by itself; 0 for a session that a person ends. */
	double PlaySeconds = 0.0;
	int32 RecordingsSaved = 0;
	FString LastRecordingScenario;
	FString LastRecordingError;
	/** The pawn's count of stepped frames when the character now in the world was put there. */
	int64 BodySpawnFrame = 0;
	bool bDemo = false;
	bool bDemoTakes = false;
	/** -PlayDemoGrab: the hands take hold of the character, it is put back while it is held, and held again. */
	bool bDemoGrab = false;
	bool bGrabHeld = false;
	bool bGrabLostOnReset = false;
	bool bGrabHeldAgain = false;
	bool bGrabLetGo = false;
	/** And the desktop's kick at the end of that demo: the foot reached the character. */
	bool bKickLanded = false;
	double KickImpulseBeforeNs = 0.0;
	int32 KickHitsBefore = 0;
	double KickImpulseNs = 0.0;
	int32 DemoFrame = 0;

	FScenarioSpec Spec;
	FString OutDir;
	FString ShotPath;
	bool bActive = false;
	bool bPawnAdopted = false;
	/** A reset the pawn has asked for, carried out in the next tick of this subsystem. */
	bool bResetPending = false;
	/** A session that cannot start ends the process instead of leaving a window that does nothing. */
	bool bExitOnFail = false;
	int32 Resets = 0;
	/** Clock time at which the next frame is due, and the count and start of the frames being timed for the log. */
	double NextFrameS = 0.0;
	int32 PacedFrames = 0;
	double PacedSinceS = 0.0;

	UPROPERTY()
	TObjectPtr<ASandboxRagdollActor> Character;

	TWeakObjectPtr<ASandboxPawn> Pawn;
	/** The scene's things with mass, put back where the scenario has them whenever the character is. */
	TArray<TWeakObjectPtr<AActor>> Props;
	void RespawnProps();

	bool bSmoke = false;
	bool bSmokePass = true;
	int32 SmokeFrame = 0;
	int32 ExitCountdown = -1;
	TArray<TSharedPtr<FJsonValue>> SmokeChecks;
	uint32 SmokeOverlayBefore = 0;
	int64 SmokeFramesAtPause = 0;
	int64 SmokeFramesAtSlow = 0;
	FTransform SmokePelvisAtPause = FTransform::Identity;
	FVector SmokeGripStart = FVector::ZeroVector;
	FVector SmokeSpawnPelvis = FVector::ZeroVector;
};
