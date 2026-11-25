#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "SandboxScriptedTargetSource.h"

/**
 * A scripted stimulus applied at a fixed simulation time.
 *   "impulse"           an instantaneous impulse on one body
 *   "pin"               fixes one body where it is with a ball joint to the world; the rest hangs from it
 *                       ("lock_rotation": a weld; "follow_clip": the weld goes where a clip has the pelvis)
 *   "console"           runs a console command, as typed in a running session
 *   "edit_tuning_file"  writes new values into the run's own copy of the tuning files, as an editor would
 *   "effector"          starts a trajectory of one of the player's hands: its target moves along the keys
 *   "foot_target"       asks the body's target pose to put a foot at a point: offset_cm from where the foot
 *                       is when the event fires, with weight 0 to 1 (0 lets the foot go back to the pose)
 *   "step"              asks the body for a step with one foot, to offset_cm from where the foot is
 *   "clip_pose"         asks the body's muscles for the pose a recorded clip has at time_s, and holds it; with a
 *                       rate the clip's time runs on from there; "release": true lets the pose go again
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
	/**
	 * "effector" with "at_bone": the keys after the first are measured from where that body of the character
	 * is (its centre of mass) at the moment the event begins, along the world's axes.
	 */
	FName AtBone;
	/** "pin": also hold the body's orientation, not only its position (a weld instead of a ball joint). */
	bool bLockRotation = false;
	/**
	 * "pin": the pin carries the body to where the clip whose pose is asked for ("clip_pose") has the pelvis, raised by
	 * RaiseCm: the body is seen lying, kneeling and standing as the clip has it, above the floor or on it.
	 */
	bool bFollowClip = false;
	double RaiseCm = 0.0;
	/** "foot_target": where to, from the foot's position when the event fires, cm; and how strongly. */
	FVector OffsetCm = FVector::ZeroVector;
	double Weight = 0.0;
	/** "clip_pose": the animation sequence's path, the time in it, how fast that time runs on (0 holds), or letting go. */
	FString Clip;
	double ClipTimeS = 0.0;
	double ClipRate = 0.0;
	bool bRelease = false;
};

/** Optional pass criteria. Unset values are not checked. Field names are listed in Scenarios/README.md. */
/** A thing with mass that a scenario puts into its world: a box that can be pushed, taken hold of and fallen on. */
struct FScenarioProp
{
	FName Name;
	/** Its edges, cm, along its own axes. */
	FVector SizeCm = FVector::ZeroVector;
	double MassKg = 0.0;
	/** Where its middle is put, and how it is turned. */
	FVector LocationCm = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	/** "handle_cm": a point of the thing, in its own frame from its middle, that a hand takes it by when it closes near it. */
	TOptional<FVector> HandleCm;
};

struct FScenarioExpect
{
	TOptional<double> RestWithinS;
	TOptional<double> MaxJointStretchCm;
	/** Furthest the pelvis may get from where it was spawned, over the whole run. */
	TOptional<double> MaxPelvisDriftCm;
	TOptional<double> MinPelvisDriftCm;
	/** Lowest the head body's centre of mass may get above the spawn height: the character stayed upright. */
	TOptional<double> MinHeadHeightCm;
	/** Fastest any body may move or turn once the settle time has passed: no jitter at rest. */
	TOptional<double> MaxSettledSpeedMps;
	TOptional<double> MaxSettledAngularSpeedRadS;
	/** The head must travel at least this far once the settle time has passed: a body that stands is not a statue. */
	TOptional<double> MinSettledHeadTravelCm;
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
	/** State the body must be in at the end: "standing", "flinch", "stagger", "falling", "lying" or "get_up". Active control only. */
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
	/** The body felt at least this much from the simulated bodies that touched it, hands and things alike (control.contact_impulse_ns). N*s. Active control only. */
	TOptional<double> MinFeltImpulseNs;
	/** The prop that went least far went at least this far from where it was put, at some moment; cm (props[].travel_cm). */
	TOptional<double> MinPropTravelCm;
	/** The prop that went furthest went at most this far; cm. */
	TOptional<double> MaxPropTravelCm;
	/** No prop was ever faster than this: nothing was launched; m/s (props[].max_speed_mps). */
	TOptional<double> MaxPropSpeedMps;
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
	/** Mean angle between the driven muscles' targets and the bodies while the body stands, settled. Degrees. */
	TOptional<double> MaxPoseErrorMeanDeg;
	/** At the end of the run, a muscle's target is turned at least this far from the reference pose. Degrees. */
	TOptional<double> MinPoseOffsetDeg;
	/** At the end of the run, every foot placed by a "foot_target" event is at most this far from its point. cm. */
	TOptional<double> MaxFootTargetErrorCm;
	/** The balance assist's commanded force over body weight, averaged over the settled frames, at most this. Active control only. */
	TOptional<double> MaxAssistForceShareMean;
	/** At most this many steps while upright (control.steps). Active control only. */
	TOptional<double> MaxSteps;
	/** At least this many steps while upright (control.steps): the body was made to step. Active control only. */
	TOptional<double> MinSteps;
	/** Every step the controller made (control.recovery_steps) landed at most this far from its target; there was at least one. cm. */
	TOptional<double> MaxLandingErrorCm;
	/** The controller made at most this many steps for its balance or asked for (control.recovery_steps without the regrouping ones). */
	TOptional<double> MaxRecoverySteps;
	/** At the end the feet stand at most this far from their stance (control.stance_error_cm). cm. Active control only. */
	TOptional<double> MaxStanceErrorCm;
	/** The body went into Flinch at most this many times. Active control only. */
	TOptional<double> MaxFlinches;
	/** The body doubled over exactly this many times (control.double_overs; reactions.double_over_*). Active control only. */
	TOptional<double> DoubleOvers;
	/** A hit on the head knocked the body down exactly this many times (control.knockdowns; reactions.knockdown_head_ns). Active control only. */
	TOptional<double> Knockdowns;
	/** The body got up at least this many times (control.get_up.successes). Active control only. */
	TOptional<double> MinGetUps;
	/** The body got up at most this many times: 0 for a body that is to be kept down. Active control only. */
	TOptional<double> MaxGetUps;
	/** The body began to get up at most this many times (control.get_up.attempts). Active control only. */
	TOptional<double> MaxGetUpAttempts;
	/** The body began to get up at least this many times: 2 for an attempt that has to be given up and tried again. Active control only. */
	TOptional<double> MinGetUpAttempts;
	/** The body told of exactly this many hits (control.hit_events; USandboxBodyController::OnBodyEvent). Active control only. */
	TOptional<double> HitEvents;
	/** The body told of exactly this many grabs (control.grab_events), and what it was last told is held is what the hands hold of it at the end. Active control only. */
	TOptional<double> GrabEvents;
	/** The hands changed between the ball and the open hand's plate exactly this many times in all (effectors.list.<hand>.shape_changes). */
	TOptional<double> EffectorShapeChanges;
	/** The hands' grips closed on something exactly this many times in all (effectors.list.<hand>.grips_on_body). */
	TOptional<double> EffectorGripsOnBody;
	/** The hands took hold of a body exactly this many times in all (effectors.list.<hand>.grabs). */
	TOptional<double> EffectorGrabs;
	/** The hands landed exactly this many blows in all: touches at a speed that gives the hand its blow's strength (effectors.list.<hand>.strikes). */
	TOptional<double> EffectorStrikes;
	/** The hands' grips slipped exactly this many times in all (effectors.list.<hand>.grab_slips). */
	TOptional<double> EffectorGrabSlips;
	/** The two points a grab holds together came at most this far apart (effectors.list.<hand>.max_grab_stretch_cm). cm. */
	TOptional<double> MaxGrabStretchCm;
	/** A hand held something for at least this many frames in all (effectors.list.<hand>.grab_frames). */
	TOptional<double> MinGrabFrames;
	/** The hands' grips were torn open by their load exactly this many times in all (effectors.list.<hand>.grab_overloads). */
	TOptional<double> EffectorGrabOverloads;
	/** The hands took a thing by its handle exactly this many times in all (effectors.list.<hand>.handle_grabs). */
	TOptional<double> EffectorHandleGrabs;
	/** The joint of a grab carried at most this force at the end of any frame it held (effectors.list.<hand>.max_grab_force_n; the last substep's). N. */
	TOptional<double> MaxGrabForceN;
	/** The pelvis's assist anchor ended at least this far, horizontally, from where it started: it followed the support. cm. */
	TOptional<double> MinPelvisAnchorShiftCm;
	/** The body leaned into a held push at least this far at some point (control.balance.lean_max_cm). cm. */
	TOptional<double> MinLeanCm;
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
	/**
	 * Above 0: the run ends before DurationS once the body, with every event behind it, has been in the Standing
	 * state for this long without a break. For runs that only ask whether a body ends on its feet (the stand of
	 * get-ups): most of them stand long before the time allowed for the slowest is over.
	 */
	double EndWhenStandingS = 0.0;

	/** Optional map package (e.g. /Game/Generated/Maps/TestRoom). Empty: the runner builds a bare floor. */
	FString MapPath;

	/** "props": things with mass put into the world before the first step. */
	TArray<FScenarioProp> Props;

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
	/**
	 * Whether a body that lies gets up by itself ("body.get_up"). Off unless a scenario asks for it: a scenario about
	 * a fall is about the fall, and its body stays where it came to rest. (In a session the body gets up.)
	 */
	bool bGetUp = false;

	/** Start-up transient: "settled" metrics and expectations only look at the run after this time. */
	double SettleS = 2.0;

	/** Optional fixed camera for rendered runs. When unset the runner frames the body itself. */
	bool bHasCamera = false;
	FVector CameraLocationCm = FVector::ZeroVector;
	FVector CameraLookAtCm = FVector::ZeroVector;

	TArray<FScenarioEvent> Events;
	FScenarioExpect Expect;

	/**
	 * Optional recording of the hands ("recording": a file of FSandboxTargetRecording, relative to the project
	 * directory unless absolute), replayed frame for frame in place of "effector" events; a scenario has one or
	 * the other. Its first frame is for the physics step that begins RecordingStartS after the start of the run:
	 * as old as the body of the session was when the recording began.
	 */
	FString Recording;
	double RecordingStartS = 0.0;

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
