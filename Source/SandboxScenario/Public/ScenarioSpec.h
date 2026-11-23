#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "SandboxScriptedTargetSource.h"

/**
 * A scripted stimulus applied at a fixed simulation time.
 *   "impulse"           an instantaneous impulse on one body
 *   "pin"               fixes one body where it is with a ball joint to the world; the rest hangs from it
 *   "console"           runs a console command, as typed in a running session
 *   "edit_tuning_file"  writes new values into the run's own copy of the tuning files, as an editor would
 *   "effector"          starts a trajectory of one of the player's hands: its target moves along the keys
 */
struct FScenarioEvent
{
	double TimeS = 0.0;
	FString Type;
	FName Body;
	/** Impulse magnitude in newton-seconds. */
	double ImpulseNs = 0.0;
	/** "impulse": time over which it is delivered, as a steady force. 0 is a single blow. */
	double DurationS = 0.0;
	/** Unit direction in world space. */
	FVector Direction = FVector::ForwardVector;
	/** "console": the command line. */
	FString Command;
	/** "edit_tuning_file": tuning names and their new values. */
	TArray<TPair<FString, double>> Values;
	/** "effector": which hand ("hand_left", "hand_right"), and where its target is at times after the event. */
	FName Effector;
	TArray<FSandboxPathKey> Path;
};

/** Optional pass criteria. Unset values are not checked. Field names are listed in Scenarios/README.md. */
struct FScenarioExpect
{
	TOptional<double> RestWithinS;
	TOptional<double> MaxJointStretchCm;
	/** Furthest the pelvis may get from where it was spawned, over the whole run. */
	TOptional<double> MaxPelvisDriftCm;
	/** Lowest the head body's centre of mass may get above the spawn height: the character stayed upright. */
	TOptional<double> MinHeadHeightCm;
	/** Fastest any body may move or turn once the settle time has passed: no jitter at rest. */
	TOptional<double> MaxSettledSpeedMps;
	TOptional<double> MaxSettledAngularSpeedRadS;
	/**
	 * Some body must turn at least this fast once the settle time has passed. For scenarios that make the
	 * body less steady on purpose and have to show that the change took effect.
	 */
	TOptional<double> MinSettledAngularSpeedRadS;
	/** Fastest relative rotation allowed in any joint, over the whole run. */
	TOptional<double> MaxJointSpeedRadS;
	/** Largest allowed mean time of one frame's physics (all substeps), in milliseconds of wall-clock time. */
	TOptional<double> MaxPhysicsStepMeanMs;
	/** Whether the body must have fallen (the pelvis below half its starting height) during the run. */
	TOptional<bool> Fell;
	/** The body must not have fallen before this time. */
	TOptional<double> MinFallTimeS;
	/** Exact number of times the running body took new tuning values. */
	TOptional<double> TuningChanges;
	/** Furthest any joint may be found outside its limits, in degrees. */
	TOptional<double> MaxJointLimitViolationDeg;
	/** State the body must be in at the end: "standing", "flinch", "falling" or "lying". Active control only. */
	TOptional<FString> EndState;
	/** Every state the body must go through, in order: "standing>flinch>standing". Active control only. */
	TOptional<FString> StateSequence;
	/** The effectors together must have delivered at least this much contact impulse, N*s: they touched. */
	TOptional<double> MinEffectorImpulseNs;
	/** ...and at most this much: for a hand that must miss. */
	TOptional<double> MaxEffectorImpulseNs;
	/** Largest contact force any effector may have applied over one frame, N. Impacts count, not only pushing. */
	TOptional<double> MaxEffectorForceN;
	/** Bounds on the contact force of the hardest-pressing effector in the run's last frame, N: the force cap. */
	TOptional<double> MinEffectorFinalForceN;
	TOptional<double> MaxEffectorFinalForceN;
	/** Deepest any effector's sphere may be found inside a body of the character or the static world, cm. */
	TOptional<double> MaxEffectorPenetrationCm;
	/** Every effector that touched something must have arrived at least this fast, m/s: a strike was a strike. */
	TOptional<double> MinEffectorImpactSpeedMps;
	/** The body of the character each effector touched first must have moved at least this far from where it was, cm. */
	TOptional<double> MinStruckDisplacementCm;
	/** Lowest the centre of mass and the capture point may get inside the support polygon while upright, cm. Active control only. */
	TOptional<double> MinComMarginCm;
	TOptional<double> MinCaptureMarginCm;
	/** Largest rise of the body's energy (kinetic plus gravitational) that no input explains, joules. */
	TOptional<double> MaxEnergyGainJ;
	/** The body must have gone into Falling and be at rest again within this time of it, seconds. Active control only. */
	TOptional<double> MaxRestAfterFallS;
	/** Deepest any shape of the body may be found inside the world, and two of its bodies inside each other, cm. */
	TOptional<double> MaxBodyPenetrationCm;
	TOptional<double> MaxSelfPenetrationCm;
};

/** How the body is driven. */
enum class EScenarioBodyControl : uint8
{
	/** No drives: a plain ragdoll. */
	Passive,
	/** Muscles and balance assist (USandboxBodyController): the character holds its spawn pose. */
	Active
};

/**
 * One scenario file from Scenarios/*.json. See Scenarios/README.md for the format.
 * Parsing is strict: unknown event types and malformed fields are errors, so a typo cannot silently pass.
 */
struct SANDBOXSCENARIO_API FScenarioSpec
{
	FString Name;
	double DurationS = 0.0;
	int32 FixedFps = 90;
	double CheckpointIntervalS = 0.5;

	/** Optional map package (e.g. /Game/Generated/Maps/TestRoom). Empty: the runner builds a bare floor. */
	FString MapPath;

	FString MeshPath;
	FString PhysicsAssetPath;
	FVector SpawnLocationCm = FVector(0.0, 0.0, 150.0);
	/** Pitch, yaw, roll in degrees. */
	FRotator SpawnRotation = FRotator::ZeroRotator;
	/** Extra skeletal meshes that follow the body's pose (a MetaHuman face, for example). No collision. */
	TArray<FString> AttachedMeshPaths;
	/** False keeps the body kinematic in its reference pose: for portraits and rendering-cost scenes. */
	bool bSimulate = true;
	EScenarioBodyControl Control = EScenarioBodyControl::Passive;

	/** Start-up transient: "settled" metrics and expectations only look at the run after this time. */
	double SettleS = 2.0;

	/** Optional fixed camera for rendered runs. When unset the runner frames the body itself. */
	bool bHasCamera = false;
	FVector CameraLocationCm = FVector::ZeroVector;
	FVector CameraLookAtCm = FVector::ZeroVector;

	TArray<FScenarioEvent> Events;
	FScenarioExpect Expect;

	bool HasEvent(const TCHAR* Type) const
	{
		return Events.ContainsByPredicate([Type](const FScenarioEvent& Event) { return Event.Type == Type; });
	}

	double FixedDt() const { return 1.0 / FixedFps; }
	int32 TotalFrames() const { return FMath::RoundToInt32(DurationS * FixedFps); }

	static bool FromJson(const TSharedPtr<FJsonObject>& Json, FScenarioSpec& OutSpec, FString& OutError);
	static bool FromString(const FString& Text, FScenarioSpec& OutSpec, FString& OutError);
	static bool LoadFile(const FString& FilePath, FScenarioSpec& OutSpec, FString& OutError);
};
