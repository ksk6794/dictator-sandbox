#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "SandboxBodyTuning.h"
#include "SandboxClipPose.h"
#include "SandboxFingerRig.h"
#include "SandboxIdleMotion.h"
#include "SandboxPoseRig.h"
#include "SandboxWeightTorques.h"
#include "SandboxAnimInstance.h"
#include "SandboxBodyController.generated.h"

class FSandboxParamStore;
class UPhysicalMaterial;
class UPhysicsControlComponent;
class USkeletalMeshComponent;

/**
 * How hard the balance assist is working at one instant, estimated from the assist springs' settings and
 * the bodies' distance from their targets. Both values are sums over all assist bodies.
 */
struct SANDBOXBODY_API FSandboxAssistLoad
{
	/** Commanded linear force as a fraction of the character's weight. */
	double ForceShare = 0.0;
	/** Commanded torque as a fraction of weight times standing centre-of-mass height. */
	double TorqueShare = 0.0;

	/**
	 * The force share by the body the assist holds (pelvis, chest, head, and both feet together), split into
	 * its horizontal and vertical parts. Each part is the size of that component of the commanded force over
	 * the character's weight, so a slot's two parts need not add up to its share of ForceShare.
	 */
	static constexpr int32 SlotCount = 4;
	double HorizontalShare[SlotCount] = { 0.0, 0.0, 0.0, 0.0 };
	double VerticalShare[SlotCount] = { 0.0, 0.0, 0.0, 0.0 };
	/** "pelvis", "chest", "head", "feet": the slots in the order above, named as in the tuning ("assist.feet..."). */
	static const TCHAR* SlotName(int32 Slot);
};

/** One change of the body's state, for result files and overlays. */
struct FSandboxStateChange
{
	/** Simulated time since the controller started, at the beginning of the first step in the new state. */
	double TimeS = 0.0;
	ESandboxBodyState State = ESandboxBodyState::Standing;
	/** What caused it, in a few words. */
	FString Reason;
};

/** The kinds of thing that happen to the body and that others may want to know of. */
enum class ESandboxBodyEventType : uint8
{
	/** The body was hit: an impulse beyond what was pressing on it already, as large as makes a standing body flinch. */
	Hit,
	/** The body's state changed. */
	StateChanged,
	/** Something took hold of a part of the body, or let go of it (USandboxBodyController::TellHolds). */
	Grabbed,
	Released,
	/** The body doubled over: a hit into the belly or the solar plexus (reactions.double_over_*). */
	DoubledOver
};

/**
 * A hold on the body from outside, as whoever knows of both sides tells it: who holds (a hand's name), which
 * part, and with what force in the last step, N.
 */
struct FSandboxBodyHold
{
	FName Holder;
	FName Bone;
	double ForceN = 0.0;
	/** The pull on the body, world, N (its size is ForceN), and where it is held, cm. */
	FVector PullN = FVector::ZeroVector;
	FVector PointCm = FVector::ZeroVector;
};

/** What moves near the body (N5), told once a frame by whoever knows both sides: a hand, a foot, a thing, the player's head. */
enum class ESandboxSeenKind : uint8
{
	Hand,
	Foot,
	Thing,
	Viewer
};
struct FSandboxSeen
{
	FName Name;
	ESandboxSeenKind Kind = ESandboxSeenKind::Thing;
	/** World, cm and cm/s; its size, cm (0: perception.thing_radius_cm). */
	FVector LocationCm = FVector::ZeroVector;
	FVector VelocityCmS = FVector::ZeroVector;
	double RadiusCm = 0.0;
};
/** A thing the body took for a threat, at the moment it did. */
struct FSandboxThreatEvent
{
	FName Name;
	/** What it was closing on: the head or the trunk. */
	FName Part;
	double TimeS = 0.0;
	/** How long it would take to touch, at the speed it closed with then; its gap; that speed. */
	double TimeToContactS = 0.0;
	double GapCm = 0.0;
	double ClosingMps = 0.0;
	bool bNear = false;
};
/** What the body's perception made of what it was told (USandboxBodyController::GetPerceptionView). */
struct FSandboxPerceptionView
{
	bool bOn = false;
	int32 TellFrames = 0;
	int32 SeenFrames = 0;
	int32 Noticed = 0;
	TArray<FSandboxThreatEvent> Threats;
	/** Gaze: frames it looked at something, the mean angle between where the head faced and that thing, frames at the neck's bound, and changes of what it looked at. */
	int32 GazeFrames = 0;
	double GazeErrorSumDeg = 0.0;
	int32 GazeAtLimitFrames = 0;
	int32 GazeShifts = 0;
	/** Of those, the frames it looked at where it is held, and the mean angle then. */
	int32 GazeHoldFrames = 0;
	double GazeHoldErrorSumDeg = 0.0;
	/** Anticipation: how often it began, how long before the contact that followed (sum, count), and how often no contact followed within a second. */
	int32 Anticipations = 0;
	/** Guards raised. */
	int32 Guards = 0;
	double AnticipationLeadSumS = 0.0;
	int32 AnticipationLeads = 0;
	int32 FalseAlarms = 0;
	double GazeErrorMeanDeg() const { return GazeFrames > 0 ? GazeErrorSumDeg / GazeFrames : 0.0; }
};

/**
 * One thing that happened to the body, for whoever listens (USandboxBodyController::OnBodyEvent): sound, a
 * face, a health system, a result file. The body tells; it does not know who hears, and a listener changes
 * nothing of the body. A grab is the one thing the body is told of itself (TellHolds): it feels contacts, and
 * a hold is a joint.
 */
struct FSandboxBodyEvent
{
	ESandboxBodyEventType Type = ESandboxBodyEventType::Hit;
	/** Simulated time since the controller started. */
	double TimeS = 0.0;
	/** A hit: the bone that was struck and the hit's impulse, N*s, in the world. A grab and a release: the bone that is held. */
	FName Bone;
	FVector ImpulseNs = FVector::ZeroVector;
	/** A grab and a release: who holds, as it was told. */
	FName Holder;
	/** The state the body is in after the event; for a change of state, also the one it left and why. */
	ESandboxBodyState State = ESandboxBodyState::Standing;
	ESandboxBodyState PreviousState = ESandboxBodyState::Standing;
	FString Reason;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnSandboxBodyEvent, const FSandboxBodyEvent& /*Event*/);

/** One muscle as an overlay shows it: what it turns its body toward, and how strong it is right now. */
struct FSandboxMuscleView
{
	FName ParentBone;
	FName ChildBone;
	ESandboxMuscleGroup Group = ESandboxMuscleGroup::Spine;
	/** Strength as a fraction of the tuning: below 1 only for the hit region during a flinch. */
	double Strength = 1.0;
	/** Whether the group's stiffness is above zero at all. */
	bool bDriven = false;
	/** World orientation the muscle turns the child bone toward, given where the parent body is now. */
	FQuat TargetRotation = FQuat::Identity;
	/** Angle between that and where the child bone is, radians. */
	double ErrorRad = 0.0;
	/** How far the target is turned from the reference pose, radians: what the pose asks of this joint. */
	double TargetOffsetRad = 0.0;
	/** The child's orientation relative to the parent that the muscle holds. */
	FQuat TargetRelative = FQuat::Identity;
	/** The drive as it was last handed to the solver: N*m/rad, N*m*s/rad, N*m, and the share of the target's own turning speed it is damped against. */
	double StiffnessNmPerRad = 0.0;
	double DampingNmsPerRad = 0.0;
	double MaxTorqueNm = 0.0;
	double PoseVelocityShare = 0.0;
};

/** One balance-assist control as an overlay shows it. */
struct FSandboxAssistView
{
	FName Bone;
	/** Where the assist holds the bone, world space. */
	FTransform Target = FTransform::Identity;
	/** False when every stiffness of the control is zero in the current state: it pulls on nothing. */
	bool bActive = false;
};

/**
 * What the body senses of its own balance at one instant: which feet stand, the support they give, where
 * the centre of mass is against it and where the capture point is. Lengths in cm, world space.
 */
struct FSandboxBalanceView
{
	/** False before the body has made a physics step under the controller: nothing has been sensed yet. */
	bool bValid = false;
	/** Force of the floor on the left and on the right foot, upward, averaged over the last frame. N. */
	double FootForceN[2] = { 0.0, 0.0 };
	/** The feet that stand: those the floor pushes on with at least balance.foot_contact_min_n. */
	TArray<FName, TInlineAllocator<2>> StandingFeet;
	/** Hull of the soles of the standing feet, seen from above, counter-clockwise. Empty when no foot stands. */
	TArray<FVector2D> SupportPolygon;
	/** Height of the support: the lowest sole point, or where the support last was while no foot stands. */
	double SupportHeightCm = 0.0;
	FVector CenterOfMass = FVector::ZeroVector;
	/** Velocity of the centre of mass, cm/s. */
	FVector Velocity = FVector::ZeroVector;
	/** Where the body would have to be supported to come to rest: ahead of the centre of mass by its velocity. */
	FVector2D CapturePoint = FVector2D::ZeroVector;
	/**
	 * How far the centre of mass and the capture point are inside the support polygon, positive inside.
	 * Meaningless while no foot stands (IsSupported).
	 */
	double CenterOfMassMarginCm = 0.0;
	double CaptureMarginCm = 0.0;

	bool IsSupported() const { return SupportPolygon.Num() > 0; }
};

/** One recovery step the controller made, for result files and overlays. World space, cm, seconds since Start. */
struct FSandboxStepRecord
{
	/** The foot that stepped: 0 left, 1 right; and its bone. */
	int32 Side = 0;
	FName Foot;
	/** Which step of its stagger this was, from 1. */
	int32 Number = 1;
	double StartS = 0.0;
	/** When the foot touched the floor again; below 0 while it is in the air. */
	double LandS = -1.0;
	/** Where the foot's bone was when it lifted, where the step was aimed, and where it came down. */
	FVector From = FVector::ZeroVector;
	FVector Target = FVector::ZeroVector;
	FVector Landed = FVector::ZeroVector;
	/** The capture point the step was planned on (with a held push's shift), and where it was then expected at landing. */
	FVector2D CapturePoint = FVector2D::ZeroVector;
	FVector2D CaptureAtLanding = FVector2D::ZeroVector;
	/** How far inside the support the step would make (the other sole and this one at its target) the capture point at landing is, cm. */
	double PlannedMarginCm = 0.0;
	/** Where the capture point really was when the foot came down, and its margin to the support the decision was made on then, cm. */
	FVector2D CaptureAtTouch = FVector2D::ZeroVector;
	double MarginAtTouchCm = 0.0;
	/** Whether the plan had to be cut short: by the reach, or by keeping to its own side of the other foot. */
	bool bReachLimited = false;
	bool bWidthLimited = false;
	/**
	 * A step across the other foot (its target past the other foot's line, toward the other side): +1 passing in
	 * front of the other foot and landing ahead of it, -1 behind; 0 a step that keeps to its own side.
	 */
	int32 Cross = 0;
	/** Whether the swing goes round the other foot (it starts on one side of it and ends on the other): +1 in front, -1 behind, 0 not. */
	int32 Pass = 0;
	/** How far the target was put from the aimed point (the capture point at landing and the margin beyond it): along its way and across it, cm. */
	FVector2D AimOffsetCm = FVector2D::ZeroVector;
	/** False when the swing ran out of time without the foot finding the floor. */
	bool bTouched = false;
	/** A step that brings the feet back to their stance once balance is back, not one that catches the body. */
	bool bRegroup = false;
};

/** The body's get-ups so far, for result files. Seconds since Start. */
struct FSandboxGetUpView
{
	/** How many times the body began to get up, and how many of those ended standing. */
	int32 Attempts = 0;
	int32 Successes = 0;
	/** The last attempt: its clip, when it began, how long it took to stand (below 0: it did not, or not yet), and how long its clip waited for the body. */
	FString Clip;
	double StartedS = -1.0;
	double DurationS = -1.0;
	double StallS = 0.0;
	/**
	 * The last attempt: how long its clip ran at under a tenth of its rate, all of it (what reads as a pause, whether the clip
	 * stood or crept; its end held for the body to stand up included), and how long the body had lain before it began.
	 */
	double PausedS = 0.0;
	double LayS = 0.0;
	/** Of PausedS: while the body eased in, with the clip short of its end (the body behind it), and at its end. */
	double PausedEaseS = 0.0;
	double PausedBehindS = 0.0;
	double PausedEndS = 0.0;
	/** Of the time the clip waited for the body: the pelvis too low or high, too far aside, a foot in the air, a press. */
	double BehindHeightS = 0.0;
	double BehindSideS = 0.0;
	double BehindFootS = 0.0;
	double BehindHeldS = 0.0;
	/** Why the last attempt that was given up was given up. */
	FString GivenUp;
	/** Hits felt while getting up (at least thresholds.flinch_impulse_ns each). */
	int32 Hits = 0;
	/** The most the body was pressed with while getting up: the force felt in every frame of the last moments from other simulated bodies. N. */
	double PressedMaxN = 0.0;
};

/** What the controller is asking of the body at this instant. Read by debug overlays, used by nothing else. */
struct FSandboxBodyControlView
{
	TArray<FSandboxMuscleView> Muscles;
	TArray<FSandboxAssistView> Assists;
};

/**
 * Turns a fully simulated skeletal mesh into an active ragdoll.
 *
 *   Muscles         one parent-space control per joint of the physics asset: an angular spring that turns
 *                   the child body toward the animation pose (the reference pose while no animation plays).
 *   Balance assist  world-space controls on pelvis, chest, head and both feet. A foot's anchor is where it
 *                   was planted (at Start, or where it landed); the anchors of pelvis, chest and head are
 *                   kept in the frame of the feet's and follow them (FollowSupport), and lean with a held push.
 *   States          Standing, Flinch, Falling, Lying. Each state reads its own tuning ("states.falling...");
 *                   the thresholds that move the body between them are tuning values too.
 *   Balance sense   Which feet stand (the floor's push on them, from the body's own contact events), the
 *                   support polygon under them, the centre of mass and the capture point (GetBalance,
 *                   MeasureBalance).
 *   Balance         The capture point decides (M3 part 3): inside the support, or outside by no more than the
 *                   ankles and hips bring back, the body stands; further out it needs a step (held by the
 *                   assist until the step exists); beyond a step's reach it falls. The ankle and hip
 *                   strategies are turns of the leg targets (UpdateStrategies); a held push is leant into.
 *   Feeling         Impulses on the body (contacts, scripted blows) are gathered over a frame (FeelFrame):
 *                   what exceeds the pressure felt so far is a hit and may start a flinch; a force felt in
 *                   every frame for a while is a held push.
 *
 *                     Standing --hit--> Flinch --recovered--> Standing
 *                     Standing or Flinch --a step is needed--> Stagger --centre of mass over the support for a while--> Standing
 *                     Standing, Flinch or Stagger --capture point beyond a step, or pelvis too low or too tilted--> Falling
 *                     Falling --at rest for a while--> Lying
 *
 * All bodies stay dynamic, and awake: a body under control never sleeps (see Start). The controller owns a
 * UPhysicsControlComponent and drives it by hand each tick,
 * after the mesh has evaluated its pose and before physics, so the order of work is the same in every run.
 */
UCLASS()
class SANDBOXBODY_API USandboxBodyController : public UActorComponent
{
	GENERATED_BODY()

public:
	USandboxBodyController();

	/**
	 * Creates the controls on a mesh whose bodies are already simulating. The mesh's current pose becomes
	 * the standing target. The tuning is read from the store, and the controller keeps following it: when a
	 * value changes there (an edited file, an override), the controls get the new values on the next tick.
	 * The store must outlive the controller. Returns false with a reason if the tuning cannot be read or a
	 * rig bone has no body.
	 */
	bool Start(USkeletalMeshComponent* InMesh, const FSandboxParamStore& InParams, FString& OutError);

	/** Replaces strengths and damping of the existing controls. The rig cannot change after Start. */
	bool ApplyTuning(const FSandboxBodyTuning& InTuning, FString& OutError);

	/**
	 * A blow to one body: applies the impulse (world space, N*s) and lets the character feel it. A standing
	 * character flinches if the impulse reaches thresholds.flinch_impulse_ns. This is how a scripted hit
	 * enters. A hit by another body is not applied here, the solver has done that already; the character
	 * feels it through its own contact events (OnBodyHit), by the same rules.
	 */
	void ApplyImpulse(FName Bone, const FVector& ImpulseNs);

	/**
	 * Asks the target pose to put a foot's bone origin at a world point: the thigh and the calf turn (two-bone
	 * placement), the foot keeps the orientation the pose gives it. Weight 0 to 1; 0 lets the foot go back to
	 * the pose. The muscles then do the work; nothing moves the foot directly. False with a reason if the bone
	 * is not the end of two segments that have bodies.
	 */
	bool SetFootTarget(FName Foot, const FVector& WorldLocation, double Weight, FString& OutError, double BlendS = -1.0);

	/** Times a fall reached for the floor (fall.aim), and when a hand first touched it in the last one (-1 never). */
	int32 GetFallReachCount() const { return FallReaches; }
	double GetFallHandTouchS() const { return FallHandTouchS; }

	/**
	 * Makes a step with one foot to a point of the floor (x and y; the foot comes down at the height it stands
	 * at), as a recovery step is made: swing, landing, the foot planted there. For testing the swing on its own.
	 * False with a reason if the body is not upright, a step is under way or the bone is not a foot of the rig.
	 */
	bool RequestStep(FName Foot, const FVector& WorldTarget, FString& OutError);

	/**
	 * Asks the muscles for the pose a recorded clip has at a time, in place of the pose of the body's state: the
	 * target goes over to it within poses.blend_s, from the state's pose or from the clip pose asked for before,
	 * and stays there. With a rate the time then runs on (1: as recorded) up to the clip's end. The clip is an
	 * animation sequence's path; it may be made for another skeleton with the same bone names (FSandboxClipPose),
	 * and is read the first time it is asked for. The body stays what it is, simulated, with the muscles of its
	 * state; nothing is played on it. While a pose is asked for this way the body leaves its balance alone: it
	 * neither steps nor adds its idle's motion or its balance strategies to the pose. False with a reason if the
	 * clip cannot be read.
	 */
	bool SetClipPose(const FString& ClipPath, double TimeS, double Rate, FString& OutError);
	/**
	 * Whether a body that lies gets up by itself (getup.*). It does unless told otherwise; a scenario about a fall
	 * tells it otherwise.
	 */
	void SetGetUpAllowed(bool bAllowed) { bGetUpAllowed = bAllowed; }

	/**
	 * What moves near the body this frame (N5). The body only judges it (in view, noticed, a threat) and keeps count; step 1
	 * of N5 does nothing with it. Judged only once SetPerceptionAllowed and perception.enabled.
	 */
	void TellSeen(TConstArrayView<FSandboxSeen> InSeen) { Seen = InSeen; }
	void SetPerceptionAllowed(bool bAllowed) { bPerceptionAllowed = bAllowed; }
	const FSandboxPerceptionView& GetPerceptionView() const { return PerceptionView; }
	const FSandboxGetUpView& GetGetUpView() const { return GetUpView; }

	/** Lets the clip's pose go: the target goes back to the state's pose within poses.blend_s. */
	void ReleaseClipPose();
	/** Whether a clip's pose is asked for (also while it is being let go), which clip, and its time now. */
	bool GetClipPose(FString& OutClipPath, double& OutTimeS, double& OutWeight) const;
	/**
	 * Where the clip whose pose is asked for has the pelvis, in the space of the clip's root (FSandboxClipPose::PelvisAt;
	 * gone over from the clip pose before as the pose is), and how much of the clip's pose the target has, 0 to 1. For
	 * whoever carries the body's pelvis through the clip. False without a clip.
	 */
	bool GetClipPelvis(FTransform& OutPelvis, double& OutWeight) const;
	/** Reads a clip for this body's joints without asking any pose of it: for whoever measures. Null with a reason if it cannot be read. */
	const FSandboxClipPose* ReadClip(const FString& ClipPath, FString& OutError);
	/** Every joint that has a muscle: its body and the body it hangs on, in the muscles' order. */
	void GetMuscleJoints(TArray<TPair<FName, FName>>& OutJoints) const;
	/** Every clip read so far, by path: for result files (how each joint of a clip fits the body). */
	const TMap<FString, FSandboxClipPose>& GetClips() const { return Clips; }

	/**
	 * What the body has taken from contacts with other simulated bodies since it was started: the sum of
	 * the contact impulses' sizes in N*s and the number of contact reports. Contacts with the static world
	 * (the floor under the feet) and between the character's own bodies are not hits and are not counted.
	 */
	double GetContactImpulseNs() const { return ContactImpulseNs; }
	int32 GetContactReportCount() const { return ContactReports; }
	/** Times the body doubled over (reactions.double_over_*), how much of that pose it has now, 0 to 1, and the largest hit a contact brought into the belly, N*s. */
	int32 GetDoubleOverCount() const { return DoubleOvers; }
	double GetDoubledWeight() const { return DoubledWeight; }
	double GetMaxBellyHitNs() const { return MaxBellyHitNs; }
	/** Times a hit on the head took the legs from under the body (reactions.knockdown_head_ns), and the largest hit a contact brought onto the head, N*s. */
	int32 GetKnockdownCount() const { return Knockdowns; }
	double GetMaxHeadHitNs() const { return MaxHeadHitNs; }

	/**
	 * Asks the whole body for a tone (limp, relaxed, ready, braced) for so long; it goes there and back by the tone's own
	 * time constants. Seconds 0 lets go of a level asked for.
	 */
	void RequestTone(ESandboxTone Level, double Seconds);
	/** Times a hit braced the body, and times one on the head dazed it. */
	int32 GetBraceCount() const { return Braces; }
	int32 GetDazeCount() const { return Dazes; }
	/** Each group's tone now, as a factor on its stiffness (spine, neck, arms, legs, toes). */
	double GetToneStiffness(ESandboxMuscleGroup Group) const { return ToneStiffness[static_cast<int32>(Group)]; }

	/** Whether the muscles give weight torques now; the largest one so far and the largest change of one in a frame, N*m, with its joint and time. */
	bool IsCarryingWeight() const { return bWeightOn; }
	double GetWeightMaxNm() const { return WeightMaxNm; }
	double GetWeightStepMaxNm(FName& OutBone, double& OutTimeS) const { OutBone = WeightStepBone; OutTimeS = WeightStepAtS; return WeightStepMaxNm; }
	/** The share of the weight that the world holds up, as the weight torques take it. */
	double GetWeightHeld() const { return WeightHeld; }

	bool IsStarted() const { return bStarted; }
	/** The values of the current state, as the tuning files and overrides give them. */
	const FSandboxBodyTuning& GetTuning() const { return Tuning; }
	/** How many times an edited file or an override changed the values of the state the body was in. */
	int32 GetTuningChangeCount() const { return TuningChangeCount; }

	ESandboxBodyState GetState() const { return State; }
	/** Standing, flinching or staggering: on its feet, with its balance in the controller's hands. */
	bool IsUpright() const { return IsUprightState(State); }
	static bool IsUprightState(ESandboxBodyState InState)
	{
		return InState == ESandboxBodyState::Standing || InState == ESandboxBodyState::Flinch || InState == ESandboxBodyState::Stagger;
	}
	/** Every state the body has been in, the first entry being Standing at time 0. */
	const TArray<FSandboxStateChange>& GetStateLog() const { return StateLog; }

	/**
	 * Told of every hit and every change of state as it happens, in the body's own tick. A hit is told in
	 * every state, when it has brought what makes a standing body flinch (thresholds.flinch_impulse_ns beyond
	 * the pressure already felt), and once a contact: a hand that lands and keeps pressing is one hit. The
	 * next is a contact after nothing was felt for thresholds.flinch_window_s, or a blow that lands while
	 * something is pressing already.
	 */
	FOnSandboxBodyEvent OnBodyEvent;
	/** Every event so far, in order. */
	const TArray<FSandboxBodyEvent>& GetEvents() const { return Events; }
	/**
	 * Which parts of the body are held from outside now. Called once a frame by whoever knows both the body
	 * and what holds it (a session, a scenario run); the body itself feels a hold only as the motion it
	 * causes. It tells its listeners when a hold begins and when it ends, and keeps the list. It does nothing
	 * else with it: no force, no target and no decision reads it.
	 */
	void TellHolds(TConstArrayView<FSandboxBodyHold> InHolds);
	const TArray<FSandboxBodyHold>& GetHolds() const { return Holds; }
	int32 GetMuscleCount() const { return Muscles.Num(); }
	int32 GetAssistCount() const { return Assists.Num(); }

	/** Assist load at the start of the current frame's physics step. */
	const FSandboxAssistLoad& GetAssistLoad() const { return AssistLoad; }

	/** The body's balance as the controller saw it at the start of the current frame's physics step. */
	const FSandboxBalanceView& GetBalance() const { return Balance; }

	/**
	 * The body's balance as it is now: the bodies where they are, and the floor's push on the feet in the
	 * physics step made since the controller last ticked. Result files and overlays call this after a step
	 * and get that step's state. Reads only; the controller's own view (GetBalance) is not touched.
	 */
	void MeasureBalance(FSandboxBalanceView& OutView) const;

	/**
	 * What the world that does not move has pushed on each body with in the last step, N*s, by the body's index in the
	 * mesh. For measuring after a step; cleared by the next tick. (Where it pushed is not in the contact events: an
	 * event has the one nearest point of a contact, not where its force goes through.)
	 */
	TConstArrayView<FVector> GetSupportImpulsesNs() const { return SupportImpulseNs; }
	/** Whether a bone is one of the feet, toes included. */
	bool IsFootBone(FName Bone) const { return Bone == FootBoneName[0] || Bone == FootBoneName[1] || (!Bone.IsNone() && (Bone == ToeBoneName[0] || Bone == ToeBoneName[1])); }

	/**
	 * The targets of every muscle and assist control, as last handed to the solver. The muscles' targets
	 * are the pose Physics Control has cached, which is the one the drives really use. Read-only and never
	 * called by the simulation: this is for debug overlays. Empty before the first tick.
	 */
	void GetControlView(FSandboxBodyControlView& OutView) const;

	/** Simulated time since Start, at the beginning of the coming physics step. */
	double GetElapsedS() const { return ElapsedS; }

	/**
	 * Whether the capture point is further outside the support than the ankles and hips can bring back
	 * (balance.recoverable_cm), but within a step's reach, and has been for balance.decision_s: the body needs
	 * a step. A standing or flinching body that needs one staggers; result files report the frames.
	 */
	bool IsStepNeeded() const { return bStepNeeded; }
	/** The capture point's margin to the support that the last decision was made on, cm; positive inside. */
	double GetDecisionMarginCm() const { return LastDecisionMarginCm; }
	/** How many times a foot was planted anew, its assist anchor moved to where it landed. */
	int32 GetReplantCount() const { return ReplantCount; }

	/** Every recovery step so far, the one under way last (LandS below 0 while its foot is in the air). */
	const TArray<FSandboxStepRecord>& GetStepLog() const { return StepLog; }
	/**
	 * How far the feet stand from their stance: the right foot from where it would be beside the left one, as the
	 * two stood at Start (the places the feet are planted at, in the support's frame). cm.
	 */
	double GetStanceErrorCm() const;
	/** Whether a step is under way, and where its foot is meant to be now (the point the swing has reached). */
	bool IsStepping() const { return Swing.bActive; }
	FVector GetSwingPoint() const { return Swing.Point; }
	/** Where each foot is held by the pose after its steps (empty while the feet are where the pose puts them). */
	bool IsFootPlanted(int32 Side) const { return Side >= 0 && Side < 2 && Placement[Side].Mode != EPlacement::None; }
	FVector GetFootPoint(int32 Side) const { return Side >= 0 && Side < 2 ? Placement[Side].Point : FVector::ZeroVector; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void OnUnregister() override;

private:
	struct FMuscle
	{
		FName Control;
		FName ParentBone;
		FName ChildBone;
		ESandboxMuscleGroup Group = ESandboxMuscleGroup::Spine;
		/** The child's orientation relative to the parent in the reference pose. */
		FQuat ReferenceRelative = FQuat::Identity;
		/** The drive as PushTuningToControls last handed it over, for GetControlView. */
		double StiffnessNmPerRad = 0.0;
		double DampingNmsPerRad = 0.0;
		double MaxTorqueNm = 0.0;
		double PoseVelocityShare = 0.0;
		/** What it had as a fall began, which balance.fall_ease_s goes over from: N*m/rad, N*m*s/rad, share. */
		double FallFromStiffness = 0.0;
		double FallFromDamping = 0.0;
		double FallFromVelocityShare = 0.0;
		/** The weight torque the muscle gives on top of its spring in this step, world space, N*m (muscles.<group>.weight_share). */
		FVector WeightNm = FVector::ZeroVector;
	};

	enum class EAssistSlot : uint8 { Pelvis, Chest, Head, Feet };

	struct FAssist
	{
		FName Control;
		FName Bone;
		EAssistSlot Slot = EAssistSlot::Pelvis;
		/** Where the body's bone should be, in world space: what the solver is given now. */
		FTransform Target = FTransform::Identity;

		/** Pelvis, chest and head: the anchor in the support's frame (see SupportFrame), as it was at Start. */
		FVector SupportLocation = FVector::ZeroVector;
		FQuat SupportRotation = FQuat::Identity;
		/** Going over to a new support: from where, to where, for how long so far (below 0 when not), and in how long (GlideSeconds). */
		FTransform FollowFrom = FTransform::Identity;
		FTransform FollowTo = FTransform::Identity;
		double FollowElapsedS = -1.0;
		double FollowS = 0.0;

		/** Feet: 0 left, 1 right; -1 for the other bodies. */
		int32 Side = -1;
		/** Feet: the orientation the foot was planted with at Start, which the support's turn is counted from. */
		FQuat StartRotation = FQuat::Identity;
		/** Feet: whether it stood in the last tick, and how long it has been off the floor. */
		bool bStanding = true;
		double AirS = 0.0;
		/** Pelvis, chest and head: how far the anchor moves with the lean, per cm the centre of mass leans (its height over the centre of mass's). */
		double LeanFactor = 0.0;
		/** The lean last handed to the solver with this anchor, cm. */
		FVector2D AppliedLeanCm = FVector2D::ZeroVector;
	};

	/**
	 * Moves the assist's anchors with the support. A foot that lands after ReplantAirS in the air is planted
	 * where it landed: its anchor goes there. The anchors of pelvis, chest and head are fixed in the frame of
	 * the feet's anchors, and when that frame moves they follow it over AnchorFollowS, eased, so that no
	 * target jumps. While the feet stay where they are, nothing here touches a target.
	 */
	void FollowSupport(double DeltaTime);
	/** The support's frame from the feet's anchors: the point between them, and their mean turn about the vertical since Start. */
	void SupportFrame(FVector& OutLocation, FQuat& OutRotation) const;
	void SetAssistTarget(const FAssist& Assist);

	/** How far the capture point is inside the support it is judged against: the feet that stand, or the last ones that did. cm. */
	double DecisionMarginCm() const;
	/** Hull of the soles the last time a foot stood. */
	TArray<FVector2D> LastSupportPolygon;
	/** How long the capture point has been past the limit of a step being needed, and of balance being lost. */
	double StepNeededS = 0.0;
	double LostS = 0.0;
	bool bStepNeeded = false;
	double LastDecisionMarginCm = 0.0;
	/** Stagger: how long the centre of mass has been over the support with the capture point within reach of the ankles. */
	double RecoveredS = 0.0;
	/** Stagger: how long the body has been at rest on both feet with its centre of mass and capture point off the support that touches. */
	double RestOffSupportS = 0.0;
	/** Whether the weakening of the last flinch has run its course (the muscles are back to full strength). */
	bool bFlinchWeakeningOver = true;

	/** A contact of one of the character's bodies, reported by the physics scene after the step. */
	UFUNCTION()
	void OnBodyHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);

	/**
	 * The character notices an impulse on one of its bodies, however it got there, at a point (world space).
	 * Gathered over the frame and worked through in the next tick (FeelFrame).
	 */
	void FeelImpulse(FName Bone, const FVector& ImpulseNs, const FVector& PointCm);
	/**
	 * What was felt since the last tick: the pressure (the felt force, smoothed over reactions.pressure_s) and
	 * its moment about the support follow it; the impulse beyond the pressure is a hit, and a hit large enough
	 * makes a standing body flinch.
	 */
	void FeelFrame(double DeltaTime);
	/** Felt since the last tick: the impulses' sum, N*s; their moment about the support's height, horizontal part times height, N*s*cm; the body felt most. */
	FVector FrameImpulseNs = FVector::ZeroVector;
	FVector2D FrameMomentNsCm = FVector2D::ZeroVector;
	FName FrameBone;
	/** Where that largest contact of the frame touched the body, cm. */
	FVector FramePointCm = FVector::ZeroVector;
	/**
	 * Doubling over: what the contact that is going on has brought into the belly from the front, N*s, when
	 * the belly last felt it, whether that contact has doubled the body over already; until when the pose is
	 * held and how much of it is on now; counts for whoever measures.
	 */
	double BellyHitNs = 0.0;
	double BellyFeltS = 0.0;
	bool bBellyDoubled = false;
	double DoubledUntilS = -1.0;
	double DoubledWeight = 0.0;
	int32 DoubleOvers = 0;
	double MaxBellyHitNs = 0.0;
	bool IsIntoBelly(FName Bone, const FVector& PointCm, const FVector& HitNs) const;
	/** A knockdown: what the contact that is going on has brought onto the head, N*s, when the head last felt it, whether it has felled the body already; counts for whoever measures. */
	double HeadHitNs = 0.0;
	double HeadFeltS = 0.0;
	bool bHeadFelled = false;
	int32 Knockdowns = 0;
	double MaxHeadHitNs = 0.0;
	double FrameBoneImpulseNs = 0.0;
	/** The felt force smoothed over reactions.pressure_s, N: what an impulse has to exceed to be a hit. */
	FVector PressureN = FVector::ZeroVector;
	/**
	 * The force felt in every frame of the last reactions.held_push_s (the frame with the least of it), N, and
	 * its horizontal part times its height above the support, N*cm: a push that is held. A blow, over in a
	 * frame or two, leaves nothing here; its momentum is in the capture point already.
	 */
	FVector SustainedN = FVector::ZeroVector;
	FVector2D SustainedMomentNCm = FVector2D::ZeroVector;
	/** The force and moment of each of the last frames, for SustainedN, oldest overwritten first. */
	TArray<FVector> RecentForceN;
	TArray<FVector2D> RecentMomentNCm;
	int32 RecentNext = 0;
	/** How far the body leans into a held push, after the lag: its centre of mass's offset from where it stands, cm (world x, y). */
	FVector2D LeanCm = FVector2D::ZeroVector;
	/** The assist's target of a body: its anchor, moved with the lean in proportion to the body's height. */
	FTransform AssistGoal(const FAssist& Assist) const;

	const FSandboxAssistTuning& AssistTuning(EAssistSlot Slot) const;
	bool ReadStateTuning(FSandboxBodyTuning& OutTuning, FString& OutError) const;
	void EnterState(ESandboxBodyState NewState, const FString& Reason);
	void UpdateState(float DeltaTime);
	/** Strength of a muscle group right now, as a fraction of its tuning: below 1 only while flinching. */
	double MuscleScale(ESandboxMuscleGroup Group) const;
	void FollowParams();
	void PushTuningToControls();
	void RefreshSolverIsland();
	void ReleaseIslandRefresh();
	void UpdateAssistLoad();
	/** Hands the pose of the tuning to the animation instance, which blends to it if it is new. */
	void ApplyPose();
	/** Works out the turns added on top of the pose this tick (the flinch's offset) and hands them over. */
	void UpdatePoseTurns();
	/** Whether the animation instance holds turns from an earlier tick. */
	bool bPoseTurnsSet = false;
	/**
	 * What a standing body does while nothing happens: the motion of the idle clip (poses.idle.*), added to the pose
	 * as turns of trunk, neck and arms, and a lean of the whole body about its ankles by the clip's sway. Carried in
	 * full only while Standing; eased out over poses.blend_s in any other state and in again after it, by its own
	 * clock like every other offset of the pose. The legs are left to the balance.
	 */
	FSandboxIdleMotion IdleMotion;
	/** The fingers: their axes, how much of each pose each hand has now (rest, fist, splayed, flat), and whether any has been handed over. */
	FSandboxFingerRig FingerRig;
	double FingerShare[2][4] = { { 0.0, 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0, 0.0 } };
	bool bFingersSet = false;
	/** Chooses each hand's pose of the fingers by what the body does and hands it to the animation (poses.fingers.*). */
	void UpdateFingers(double DeltaTime);
	void UpdateIdle(double DeltaTime);
	/** How much of the idle the body carries now, 0 to 1, before easing. */
	double IdleShare = 0.0;
	/** The idle's sway now: the lean it asks of the whole body, cm (world x, y). */
	FVector2D IdleSwayCm = FVector2D::ZeroVector;
	/**
	 * A clip's pose asked for in place of the state's (SetClipPose). The clips read so far are kept by path. Share
	 * is how much of the clip's pose the target has, before easing: it rises while the pose is held and falls once
	 * it is let go. A new clip or time starts from the pose handed over last (From) and goes over by CrossShare, so
	 * that the target never jumps.
	 */
	struct FClipHold
	{
		FString Path;
		double TimeS = 0.0;
		double Rate = 0.0;
		double Share = 0.0;
		bool bReleased = false;
		/** Asked for from outside (SetClipPose), not by the body's own get-up: the body then leaves its balance alone. */
		bool bOutside = false;
		TArray<FSandboxPoseBone> From;
		double CrossShare = 1.0;
		/** What was handed to the animation instance last. */
		TArray<FSandboxPoseBone> Last;
		/** The clip's pelvis, as the pose: where it goes over from, and where it is now. */
		FTransform FromPelvis = FTransform::Identity;
		FTransform LastPelvis = FTransform::Identity;
	};
	FClipHold ClipHold;
	TMap<FString, FSandboxClipPose> Clips;
	void UpdateClipPose(double DeltaTime);
	/** The clip at a path, read if it has not been yet; OutPath is the path it is kept by. Null with a reason if it cannot be read. */
	const FSandboxClipPose* FindOrReadClip(const FString& ClipPath, FString& OutPath, FString& OutError);
	/** Makes a clip's pose the target (SetClipPose without the rule that the body then leaves its balance alone). */
	bool HoldClip(const FString& ClipPath, double TimeS, double Rate, FString& OutError);
	bool IsPosedFromOutside() const { return !ClipHold.Path.IsEmpty() && ClipHold.bOutside; }

	/**
	 * The get-up (ESandboxBodyState::GetUp). A body that has lain still for getup.rest_s takes the clip whose pelvis
	 * lies most like its own, turned about the vertical to how the body lies and put where its pelvis is (Frame: the
	 * clip's root in the world). The muscles are asked for the clip's pose; the pelvis's assist is aimed at where the
	 * clip has the pelvis; both come up from nothing over getup.ease_s. The clip's time runs while the pelvis keeps
	 * up with it and waits while it does not. Through the clip, upright and on both feet, the body stands; thrown
	 * off, held up or kept waiting too long, it lets go and lies again.
	 */
	struct FGetUpRun
	{
		FString Path;
		FTransform Frame = FTransform::Identity;
		/** How long the clip's time has stood still now, and how long it has waited in all. */
		double StallS = 0.0;
		double WaitedS = 0.0;
		/** How long it has waited in all while nothing pressed on the body (getup.wait_s). */
		double FreeWaitS = 0.0;
		/** How long the clip has now waited for a foot in the air (getup.foot_wait_s). */
		double FootWaitS = 0.0;
		bool bEased = false;
	};
	FGetUpRun GetUpRun;
	FSandboxGetUpView GetUpView;
	bool bGetUpAllowed = true;
	/** Set when a get-up could not begin (a clip is missing): it is not tried again. */
	bool bGetUpBroken = false;
	/** The clip of the attempt that was given up last: the next attempt goes another way if there is one. Empty after a get-up. */
	FString GivenUpClip;
	/** How long the body has lain still. */
	double LyingS = 0.0;
	/**
	 * N7: the hardest hit felt in the last second and when (any state), and the hardest that felled the body: from a second
	 * before its fall to its get-up. N*s, s.
	 */
	double RecentHitNs = 0.0;
	double RecentHitS = -1.0;
	double FellByNs = 0.0;
	/** How long a body that lies waits before it gets up (getup.rest_s, and getup.rest_per_ns_s of FellByNs). Seconds. */
	double GetUpRestS() const;
	/** The mesh's own space when the body was started: the space a clip's root is in for a body that stands as this one stood. */
	FTransform StartSpace = FTransform::Identity;
	bool StartGetUp(FString& OutError);
	void UpdateGetUp(double DeltaTime);
	void GiveUpGetUp(const FString& Reason);
	/** How far the muscles and the assist have come up since the get-up began, 0 to 1 (1 in any other state). */
	double GetUpEase() const;
	/** Whether the body as a whole moves and turns no faster than thresholds.rest_*. */
	bool IsAtRest() const;
	/** How much of a clip's pose the target has now, eased: 0 without one. */
	double ClipWeight() const { return ClipHold.Path.IsEmpty() ? 0.0 : FMath::SmoothStep(0.0, 1.0, ClipHold.Share); }
	/** The way the body goes down, chosen as it starts to fall: which of the fall's poses it takes (ApplyPose). */
	enum class EFallWay : uint8 { Back, Forward, Left, Right };
	EFallWay FallWay = EFallWay::Back;
	/** Chooses FallWay from where the pelvis leans and where the body moves. */
	void ChooseFallWay();
	/**
	 * A fall that does not happen: the body is not upright, touches nothing of the world and does not come down
	 * (thresholds.held_*). It is held up, by a hand or a hook, and hangs in its stance's pose instead of the pose of
	 * a fall, until it touches something or comes down again. bWorldTouched is set by contact events with anything
	 * that does not move under physics, since the last tick.
	 */
	void UpdateHeld(double DeltaTime);
	bool bHeldUp = false;
	bool bWorldTouched = false;
	double HeldWatchS = 0.0;
	double HeldLowestCm = 0.0;
	/**
	 * The end of a fall: the trunk or the head on the floor (a contact with the world of a body of the spine's or the
	 * neck's group, since the last tick), and when that first happened in this fall; below 0 before it has. The
	 * muscles let go thresholds.impact_let_go_s after it (UpdateState).
	 */
	bool bTrunkTouched = false;
	double TrunkTouchedS = -1.0;
	/** Flinch: the body that was hit and the hit's impulse, N*s. */
	FName FlinchBone;
	FVector FlinchImpulseNs = FVector::ZeroVector;
	/** When the last flinch began; below 0 before the first. */
	double FlinchStartedS = -1.0;

	/** Turns the tuning's joint angles into bone rotations for this mesh. */
	FSandboxPoseRig PoseRig;

	/**
	 * A constraint that restricts nothing, alive for one frame after the solver iteration counts change.
	 * See RefreshSolverIsland.
	 */
	TUniquePtr<FConstraintInstance> IslandRefresh;

	/**
	 * The surface of the body's bodies (surface.*): one physical material for the feet and one for the rest, made
	 * here and given to every body as its override. Held here because nothing else keeps them alive.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UPhysicalMaterial> SurfaceMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UPhysicalMaterial> FeetSurfaceMaterial;
	/** The surface values the bodies have now; nothing is touched while the tuning's are the same. */
	FSandboxSurfaceTuning AppliedSurface;
	bool bSurfaceApplied = false;
	void ApplySurface();

	/** Where the tuning comes from, and the revision of it the controls were last brought up to. */
	const FSandboxParamStore* Params = nullptr;
	uint32 ParamsRevision = 0;
	int32 TuningChangeCount = 0;

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY()
	TObjectPtr<UPhysicsControlComponent> Control;

	FSandboxBodyTuning Tuning;

	ESandboxBodyState State = ESandboxBodyState::Standing;
	TArray<FSandboxStateChange> StateLog;
	/** Simulated time since Start: the sum of the tick intervals, never the clock. */
	double ElapsedS = 0.0;
	double StateEnteredS = 0.0;
	/** Flinch: the muscle group that was hit. */
	ESandboxMuscleGroup FlinchGroup = ESandboxMuscleGroup::Spine;
	/** Hits (impulse beyond the pressure) received since HitStartedS, added up: a blow delivered over several frames is one hit. */
	FVector HitImpulseNs = FVector::ZeroVector;
	double HitStartedS = 0.0;
	/** The same sum kept for the stream of events, which counts hits in every state and is read by nothing here. */
	FVector EventHitNs = FVector::ZeroVector;
	/** When something was last felt, whether the hit of that contact has been told, and whether its force has stopped rising since. */
	double EventFeltS = 0.0;
	bool bEventHitTold = false;
	bool bEventRiseOver = false;
	TArray<FSandboxBodyEvent> Events;
	TArray<FSandboxBodyHold> Holds;
	void TellEvent(const FSandboxBodyEvent& Event);
	double ContactImpulseNs = 0.0;
	int32 ContactReports = 0;
	/**
	 * What the world that does not move (the floor, a wall) has pushed on each body with since the last tick,
	 * N*s, by the body's index in the mesh. This is what the body stands on, not a hit.
	 */
	TArray<FVector> SupportImpulseNs;
	/** Length of the physics step those impulses were delivered in; 0 before the first tick. */
	double SupportStepS = 0.0;
	/** Where the support was when a foot last stood, for the time no foot does. */
	double LastSupportHeightCm = 0.0;
	double GravityCmS2 = 980.0;
	FSandboxBalanceView Balance;
	/** Falling: how long every body has been below the rest speeds. */
	double RestS = 0.0;

	TArray<FMuscle> Muscles;
	TArray<FAssist> Assists;
	int32 ReplantCount = 0;
	/**
	 * The balance strategies (ankle and hip), worked out every tick from the capture point's error and handed
	 * to the pose as turns (UpdatePoseTurns). The turns follow what is asked through a first-order lag
	 * (balance.response_s) and fade the same way once the body is no longer upright.
	 */
	void UpdateStrategies(double DeltaTime);
	/** Adds the strategies' turns to a set of pose turns, merged per bone. */
	void AddStrategyTurns(TArray<FSandboxPoseTurn>& Turns) const;
	/** Where the body's centre of mass stood at Start, in the support's frame: what the strategies bring the capture point back to. */
	FVector ComSupportLocation = FVector::ZeroVector;
	/** Height of the centre of mass above the feet's anchors at Start, cm: the radius of a lean about the ankles. */
	double ComHeightCm = 100.0;
	/** The strategies' current lean of the legs about the ankles (forward, sideways) and bend of the trunk at the hips, radians, after the lag. */
	FVector2D AnkleLeanRad = FVector2D::ZeroVector;
	FVector2D HipBendRad = FVector2D::ZeroVector;
	/** The ankle strategy's slow part: the shift of the centre of pressure it has built up, cm (forward, sideways). */
	FVector2D AnkleIntegralCm = FVector2D::ZeroVector;
	/** The support's forward and sideways directions this tick (the character faces +y at Start). */
	FVector SupportForward = FVector(0.0, 1.0, 0.0);
	FVector SupportSide = FVector(1.0, 0.0, 0.0);
	/** Capture-point error the strategies worked on this tick, cm, world x and y. */
	FVector2D StrategyErrorCm = FVector2D::ZeroVector;
	/** Whether the strategies hold turns that the pose must carry. */
	bool bStrategyActive = false;
	/**
	 * How much of each balance pose of the arms the body carries (poses.balance_*): going over backward, forward, to
	 * its left and to its right, 0 to 1 each, after the lag (balance.arm_response_s). Worked out by UpdateStrategies
	 * from the same capture-point error as the ankles.
	 */
	enum { ArmBack, ArmForward, ArmLeft, ArmRight, ArmWayCount };
	double ArmShare[ArmWayCount] = { 0.0, 0.0, 0.0, 0.0 };
	/** Adds the arms' balance poses to a set of pose turns, merged per bone. */
	void AddArmTurns(TArray<FSandboxPoseTurn>& Turns) const;
	/** The legs the strategies turn: foot and thigh bones, left and right (mesh bone indices and names). */
	int32 FootBoneIndex[2] = { INDEX_NONE, INDEX_NONE };
	int32 ThighBoneIndex[2] = { INDEX_NONE, INDEX_NONE };
	FName FootBoneName[2];
	/** The toes of each foot, if the body has any: the body that hangs on the foot's. */
	FName ToeBoneName[2];
	/**
	 * The thing that carries the feet, over the floor: how fast it goes, cm/s, smoothed, from what pushed up on them (the floor is still). A body on a
	 * plate that slides is in motion with the plate, and a speed that is the plate's is no loss of balance: the capture point is worked out from the
	 * velocity relative to it, and the places the body stands at go with it (CarrySupport).
	 */
	FVector2D SupportVelocityCmS = FVector2D::ZeroVector;
	FVector2D SupportVelocitySum = FVector2D::ZeroVector;
	double SupportVelocityWeight = 0.0;
	void CarrySupport(double DeltaTime);
	FName ThighBoneName[2];
	FName CalfBoneName[2];

public:
	/** The strategies' state for result files and overlays: ankle lean and hip bend (forward, sideways), degrees, and the slow part, cm. */
	FVector2D GetAnkleLeanDeg() const { return FVector2D(FMath::RadiansToDegrees(AnkleLeanRad.X), FMath::RadiansToDegrees(AnkleLeanRad.Y)); }
	FVector2D GetHipBendDeg() const { return FVector2D(FMath::RadiansToDegrees(HipBendRad.X), FMath::RadiansToDegrees(HipBendRad.Y)); }
	FVector2D GetAnkleIntegralCm() const { return AnkleIntegralCm; }
	/** The steady force the body feels (a held push), N, and how far it leans into it, cm (world x, y). */
	const FVector& GetPressureN() const { return SustainedN; }
	FVector2D GetLeanCm() const { return LeanCm; }

private:
	/** Feet the pose has been asked to place (SetFootTarget), by mesh bone index, with their points. */
	TMap<int32, FVector> PlacedFeet;
	/** Lets every placed foot go back to the pose, eased out. */
	void ReleaseFootTargets();

	/**
	 * The frames a leg's placement can be solved in (see FSandboxLimbTarget::Frame):
	 *   Actual   the component as it is: the foot goes to its point whatever the pelvis does (a swinging leg).
	 *   Here     the pelvis upright and at its anchor's height, where it is over the floor: the leg holds the
	 *            body up and level and lets it go where it goes.
	 *   Stance   the same, but only balance.stance_follow of the way from the anchor to where the pelvis is: the
	 *            leg that carries the body while the other swings, holding it back that much.
	 *   Anchor   the pelvis where the assist anchors it: the posture of a body standing on its planted feet,
	 *            which the muscles then pull the body toward.
	 * Upright frames are lowered for the given leg (Side) as far as it needs to reach its planted foot.
	 */
	enum class EFrame : uint8 { Actual, Here, Stance, Anchor };
	FTransform FrameTransform(EFrame Frame, int32 Side, bool bLowered = true) const;
	/** How far an upright frame's pelvis has to come down for a leg to reach Foot with balance.leg_extension_max of its length, cm. */
	double LoweringFor(const FTransform& Frame, int32 Leg, const FVector& Foot) const;
	/**
	 * How long an anchor takes to glide from one point to another over the floor: balance.anchor_follow_s, or
	 * longer, so that a body pulled along keeps its capture point (v / omega ahead of it) within
	 * balance.anchor_room_share of the way from the glide's end to the edge of the support, along the glide.
	 */
	double GlideSeconds(const FVector& From, const FVector& To) const;
	/** How far the upright frames are lowered now, cm, following what the planted (and the swinging) legs need. */
	double LowerCm = 0.0;
	void UpdateLowering(double DeltaTime);

	/** What the pose does with one foot of the controller's own (feet placed from outside are in PlacedFeet). */
	enum class EPlacement : uint8 { None, Swing, Planted };
	struct FFootPlacement
	{
		EPlacement Mode = EPlacement::None;
		/** Where the foot's bone is to be, world space. */
		FVector Point = FVector::ZeroVector;
		/** The frame the placement goes over to, the one it comes from, and how far that change has come. */
		EFrame Frame = EFrame::Anchor;
		EFrame FromFrame = EFrame::Anchor;
		double FrameElapsedS = 0.0;
		double FrameBlendS = 0.0;
		/** The frame the leg was last solved in (world), and whether a change of frame starts from it rather than from FromFrame. */
		FTransform LastFrame = FTransform::Identity;
		bool bHasLastFrame = false;
		/** The point the leg was last solved for, and how much of its ankle's range it was held to. */
		FVector LastPoint = FVector::ZeroVector;
		double LastEndLimitWeight = 0.0;
		/** The orientation the foot was last held to (a swing's level), if any. */
		FQuat LastEndFrame = FQuat::Identity;
		bool bHasLastEndFrame = false;
		bool bFromLastFrame = false;
		FTransform FromTransform = FTransform::Identity;
		/**
		 * A foot that has just landed: its point starts this far above Point (where the bone was as the sole
		 * touched) and comes down to it over SettleS, so that a foot that touched with its toe or edge is let down
		 * onto its sole rather than driven into the floor. cm, s.
		 */
		double SettleCm = 0.0;
		double SettleElapsedS = 0.0;
		double SettleS = 0.0;
		/** What is left of the settling, 1 at the landing to 0, eased. */
		double SettleLeft() const
		{
			return SettleS > 0.0 && SettleElapsedS < SettleS ? 1.0 - FMath::SmoothStep(0.0, 1.0, SettleElapsedS / SettleS) : 0.0;
		}
		/**
		 * A point moved to where its foot has got to (GlidePoint): it goes over from the old one over GlideS, so that
		 * the leg's target does not jump. cm, s.
		 */
		FVector GlideFrom = FVector::ZeroVector;
		double GlideElapsedS = 0.0;
		double GlideS = 0.0;
		/** Point, or where the glide to it has got to. */
		FVector GlidedPoint() const
		{
			return GlideS > 0.0 && GlideElapsedS < GlideS ? FMath::Lerp(GlideFrom, Point, FMath::SmoothStep(0.0, 1.0, GlideElapsedS / GlideS)) : Point;
		}
		/** The point the leg is solved for now: Point (or on its way there), raised by what is left of the settling. */
		FVector SettledPoint() const
		{
			return GlidedPoint() + FVector(0.0, 0.0, SettleCm * SettleLeft());
		}
	};
	FFootPlacement Placement[2];
	/** Each leg's hip (the thigh bone) in the reference pose, component space, and its length from hip to ankle, cm. */
	FVector HipReferenceCs[2] = { FVector::ZeroVector, FVector::ZeroVector };
	double LegLengthCm[2] = { 0.0, 0.0 };
	/**
	 * Puts a foot under the controller's placement (or changes its frame), easing in what changes over BlendS.
	 * From names the frame the change starts from when it is not the one the foot is in now; bFromLast starts it
	 * from the frame the leg was last solved in, held where it was (for a change that moves the frames themselves,
	 * as the anchors' jump to the body when it stands again).
	 */
	void PlaceFoot(int32 Side, EPlacement Mode, const FVector& Point, EFrame Frame, double BlendS, const EFrame* From = nullptr, bool bFromLast = false);
	/**
	 * Where the assist plants a foot that stands where it is now: over the floor where its bone is, turned only
	 * about the vertical, flat, at the height its bone had over the floor at Start. A foot on its toe or edge is
	 * not to be held so (the pelvis's anchor and the next step's height are taken from the feet's anchors).
	 */
	FTransform FlatFootTransform(const FAssist& Assist) const;
	/**
	 * The point a leg holds a foot at that stands where it is: over the floor where its bone is, at the height its
	 * bone has standing flat on the floor. (Taken from the bone as it was, with the heels up after a push forward the
	 * stance leg held its foot 1.5 cm above the floor for most of a second.)
	 */
	FVector FlatFootPoint(int32 Side) const;
	/** Height of each foot's bone over the lowest point of its sole at Start, standing flat, cm. */
	double FootStandHeightCm[2] = { 0.0, 0.0 };
	/**
	 * The weight torques (FSandboxWeightTorques): what the world pushes each body with is collected from the solver's own
	 * contacts after every substep, and each tick every muscle whose group has a share is asked for its torque on top of
	 * its spring. A muscle's drive acts between two bodies of the character: nothing is added to the body as a whole.
	 */
	void UpdateWeight(double DeltaTime);
	/**
	 * The tone each group has now (stiffness and damping factors), what it goes to, and a level asked for from outside
	 * until a time (RequestTone). It follows with tone.rise_s up and tone.fall_s down: no strength jumps.
	 */
	double ToneStiffness[5] = { 1.0, 1.0, 1.0, 1.0, 1.0 };
	double ToneDamping[5] = { 1.0, 1.0, 1.0, 1.0, 1.0 };
	int32 AskedTone = -1;
	double AskedToneUntilS = 0.0;
	/** A brace after a hit (tone.brace_*): from when to when. A daze (tone.daze_*): how deep, 0 to 1, and since when. */
	double BraceFromS = -1.0;
	double BraceUntilS = -1.0;
	double DazeDepth = 0.0;
	double DazedAtS = -1.0;
	int32 Dazes = 0;
	int32 Braces = 0;
	bool bToneMoving = false;
	/** Follows the level asked for, and hands the controls the strengths while they change. */
	void UpdateTone(double DeltaTime);

	/** The fall with an aim (fall.*): the way the body goes down, followed every tick, and the hands sent to the floor along it. */
	void UpdateFallAim(double DeltaTime);
	void StopFallAim();
	/**
	 * N4, the order of reactions (reactions.reflex_delay_s): until when the body moves by mechanics alone after a hit, and
	 * when a doubling over may begin. The flinch's state begins at the hit (the assist lets go of the body, as it does in a
	 * flinch), its pose and weakening at FlinchStartedS, the end of the delay.
	 */
	double MechanicsUntilS = -1.0;
	double DoubledFromS = -1.0;
	bool InMechanics() const { return MechanicsUntilS >= 0.0 && ElapsedS < MechanicsUntilS - KINDA_SMALL_NUMBER; }
	/** N5: what the body was told moves near it, what it makes of each thing, and its count. */
	struct FSeenTrack
	{
		FName Name;
		double InViewS = 0.0;
		bool bNoticed = false;
		bool bThreat = false;
		/** The time to the contact it reckoned last, for choosing which threat to look at. */
		double TimeToContactS = 0.0;
	};
	TArray<FSandboxSeen> Seen;
	TArray<FSeenTrack> SeenTracks;
	bool bPerceptionAllowed = false;
	FSandboxPerceptionView PerceptionView;
	/** Where the head body faces, in its own frame (the mesh faces +y in the reference pose). */
	FVector HeadForwardLocal = FVector::YAxisVector;
	/** Gaze (N5): the neck's and the chest's reference rotations in the component, the neck's bone, what it looks at and since when, and the turn now. */
	FQuat NeckRefRotation = FQuat::Identity;
	FQuat ChestRefRotation = FQuat::Identity;
	int32 NeckMeshIndex = INDEX_NONE;
	FName GazeOn;
	double GazeOnSinceS = 0.0;
	double GazeYawDeg = 0.0;
	double GazePitchDeg = 0.0;
	double GazeYawRateDps = 0.0;
	double GazePitchRateDps = 0.0;
	/** Moves the gaze's turn toward a goal at its limited speed and acceleration. */
	void StepGaze(double DeltaTime, double GoalYawDeg, double GoalPitchDeg);
	void UpdateGaze(double DeltaTime, const FVector& Facing);
	/** Anticipation (N5 step 4): when it is due, from when to when it holds, how much of its pose is on, and the hit that followed. */
	double AnticipateDueS = -1.0;
	double AnticipateFromS = -1.0;
	double AnticipateUntilS = -1.0;
	/** Until when its pose holds; and the flinch's own pose weight this tick, which the anticipation's is not laid on top of. */
	double AnticipatePoseUntilS = -1.0;
	double FlinchPoseWeightNow = 0.0;
	double AnticipateWeight = 0.0;
	double LastHitS = -1.0;
	bool bAnticipationJudged = true;
	void UpdateAnticipation(double DeltaTime);
	/** The guarding arm (N5 step 5): which hand (-1 none), until when, toward what, and where its point was last. */
	int32 GuardSide = -1;
	int32 GuardWantedSide = -1;
	FName GuardWantedOn;
	FName GuardOn;
	double GuardUntilS = -1.0;
	FVector GuardPointCm = FVector::ZeroVector;
	/** Whether the guard's hand has been placed yet; and the hand a fall's reach leaves to the guard's release for that whole fall. */
	bool bGuardPlaced = false;
	int32 FallSkipSide = -1;
	void UpdateGuard();
	void StopGuard();
	/** An arm's hand placed at a point, as SetFootTarget does, but not one of the placed feet (which keep the body from stepping). */
	bool PlaceArm(FName Hand, const FVector& WorldLocation, double Weight, double BlendS, bool bElbowDown = false);
	void UpdatePerception(double DeltaTime);
	bool bFallAiming = false;
	FVector2D FallWayNow = FVector2D::ZeroVector;
	double FallFloorZ = 0.0;
	bool bHandTouchedFrame[2] = { false, false };
	double HandTouchedS[2] = { -1.0, -1.0 };
	int32 FallReaches = 0;
	double FallHandTouchS = -1.0;

	/** Hands a muscle's weight torque to its drive. */
	void GiveWeightTorque(const FMuscle& Muscle);
	/** Collects the world's pushes of one substep; called by the solver after it. */
	void AddPushes(double SubstepS);
	void StartCollectingPushes();
	void StopCollectingPushes();
	/** A body of the weight's chain, after the one it hangs on. */
	struct FWeightBody
	{
		FName Bone;
		int32 Parent = INDEX_NONE;
		/** Index into Muscles, none for the root. */
		int32 Muscle = INDEX_NONE;
		/** The push, smoothed (N), and its place from the body's centre of mass times its size (N*cm). */
		FVector PushN = FVector::ZeroVector;
		FVector PushOffsetNcm = FVector::ZeroVector;
		double PushSizeN = 0.0;
	};
	TArray<FWeightBody> WeightBodies;
	/** Collected since the last tick, by body instance: the impulse (N*s), and its place times its size (N*s*cm). */
	TArray<FVector> PushImpulseNs;
	TArray<FVector> PushMomentNsCm;
	TArray<double> PushSizeNs;
	FDelegateHandle PushHandle;
	/** Each group's share as it is now: it follows the tuning's with the smoothing, and never jumps with a state. */
	double WeightShareNow[5] = { 0.0, 0.0, 0.0, 0.0, 0.0 };
	/** The share of the weight the world holds up, smoothed: none in a free fall. */
	double WeightHeld = 0.0;
	/** The shift of the centre of pressure that the ankle strategy asks for as torques (balance.ankle_as_torque), cm, with the strategies' lag. */
	FVector2D StrategyShiftCm = FVector2D::ZeroVector;
	/** Whether the legs' torques are worked out from a push at a place, so that the strategies and the assist can move that place. */
	bool IsPushByPlace() const { return Tuning.Weight.LegsBy > 0.5 && IsUpright(); }
	/** The share of an assist's force and torque that still comes from the world (assist.internal_share). */
	double WorldShareOf(EAssistSlot Slot) const { return Slot != EAssistSlot::Feet && IsPushByPlace() ? 1.0 - Tuning.Weight.InternalShare : 1.0; }
	/**
	 * What the assist wants of an upright body, as the legs and the trunk can give it: the horizontal force on the pelvis
	 * (N), the torque on the pelvis, the chest and the head together (N*m), and each trunk joint's part of it by muscle.
	 */
	void InternalAssist(FVector& OutForceN, FVector& OutTorqueNm, TMap<int32, FVector>& OutJointNm) const;
	/** The weight's share on each foot and its place under it (weight_legs_by 1), smoothed. */
	double FootPushN[2] = { 0.0, 0.0 };
	FVector2D FootPushAt[2] = { FVector2D::ZeroVector, FVector2D::ZeroVector };
	bool bWeightOn = false;
	/** The largest weight torque a joint has had, and the largest change of one from one frame to the next. */
	double WeightMaxNm = 0.0;
	double WeightStepMaxNm = 0.0;
	FName WeightStepBone;
	double WeightStepAtS = 0.0;

	/** How much of the swinging leg's own strength is left in a leg that has landed (balance.land_ease_s): 1 at the landing to 0. */
	double LandShare(FName ChildBone) const;
	/** How far the muscles have gone over from the strengths they had to those of the fall (balance.fall_ease_s). */
	double FallEaseShare() const;
	/** Hands the controls the strengths that go over in time, while they do. */
	void UpdateStrengthEases();
	/** The strength each muscle had as the fall began (balance.fall_ease_s): FMuscle's FallFrom*. */
	bool bFallEaseFromSet = false;
	/** The rig as the tuning files give it (Tuning.Rig may carry the head of an asset that has one of its own). */
	FSandboxBodyRig TunedRig;
	/** The stagger's brake on the legs' frame as it is now (balance.stagger_brake_follow_s), cm. */
	FVector2D StaggerBrakeCm = FVector2D::ZeroVector;

	/** Lets a foot of the controller's go back to the pose, eased out. */
	void ReleaseFoot(int32 Side, double BlendS);
	/** Hands every placement its point and frame for this tick. */
	void UpdatePlacements(double DeltaTime);

	/** The step under way. */
	struct FSwing
	{
		bool bActive = false;
		int32 Side = 0;
		double ElapsedS = 0.0;
		FVector From = FVector::ZeroVector;
		FVector Target = FVector::ZeroVector;
		/** Where the swing has brought the foot's point now. */
		FVector Point = FVector::ZeroVector;
		/** The sole as it will stand at the target, seen from above. */
		TArray<FVector2D> TargetSole;
		/** How fast the point moves, cm/s: the guiding assist's target velocity. */
		FVector VelocityCmS = FVector::ZeroVector;
		/** How far across (0 to 1) the point has come toward the target, seen from above, and how far down after that. */
		double Across = 0.0;
		double Descent = 0.0;
		/** How long the swing takes: balance.step_time_s for a recovery step, balance.step_asked_time_s for one asked for. */
		double DurationS = 0.0;
		/**
		 * A recovery step is aimed anew every tick until the foot starts down (a step asked for keeps its target): the
		 * capture point is predicted over the time left, so the prediction's error shrinks as the foot travels. The
		 * sole as it was when the foot lifted, which the aim moves.
		 */
		bool bReaim = false;
		TArray<FVector2D> LiftSole;
		/**
		 * A step across the other foot (FSandboxStepRecord::Cross): its point goes over the floor along a curve that
		 * passes PassPoint, in front of or behind the other foot, instead of the straight way through it.
		 */
		int32 Cross = 0;
		int32 Pass = 0;
		FVector2D PassPoint = FVector2D::ZeroVector;
		/**
		 * The leg's point at the lift less where the foot is (From): the swing starts from the point the leg was solved for
		 * and lets the difference go as the foot travels across, so that the leg's target does not jump. cm.
		 */
		FVector StartOffset = FVector::ZeroVector;
	};
	/** Whether an assist control is the one on the foot of a step under way. */
	bool IsSwingFoot(const FAssist& Assist) const { return Swing.bActive && Assist.Slot == EAssistSlot::Feet && Assist.Side == Swing.Side; }
	/** Whether a step is under way or waiting for its weight shift. */
	bool IsStepUnderWay() const { return Swing.bActive || Pending.bActive; }
	FSwing Swing;
	/**
	 * The sole of the foot a step has just set down, flat where it landed (the planned sole moved by the landing's
	 * error): the decisions' support for that foot until LandedSoleUntilS (balance.step_settle_s after the landing).
	 */
	TArray<FVector2D> LandedSole;
	int32 LandedSoleSide = -1;
	double LandedSoleUntilS = -1.0;
	TArray<FSandboxStepRecord> StepLog;
	/** Steps made in the current stagger. */
	int32 StaggerSteps = 0;
	/**
	 * Regrouping: after recovery steps the feet stand wherever those put them (a lunge, crossed legs). Once the body
	 * has stood still for balance.regroup_wait_s, a foot steps back to its place beside the other, as a person does:
	 * the one whose move leaves the stance nearer under the body. The weight goes onto the other foot first
	 * (UpdateShift, RegroupLead), the body rises on that leg as the foot to lift unloads (UpdateLowering), and the
	 * foot lifts when the step will hold (UpdateStep). RegroupLeft is how many such steps the last loss of balance
	 * still allows.
	 */
	void UpdateRegroup(double DeltaTime);
	/** The right foot's place from where the stance would have it beside the left one; the feet's places and the stance's vector with it (world, cm). */
	FVector StanceError(FVector OutFootAt[2], FVector& OutStance) const;
	int32 RegroupLeft = 0;
	double RegroupQuietS = 0.0;
	bool bRegroupAsked = false;
	/** Where the right foot stood from the left one at the start, in the support's frame, cm. */
	FVector2D StanceRightFromLeft = FVector2D::ZeroVector;
	/** A regrouping step: the point the capture point is brought to before the foot lifts and held at while it swings (world x, y, cm). */
	FVector2D RegroupPoint = FVector2D::ZeroVector;
	/** ...the weight shift that puts the pelvis's goal there, and what is added to it while the capture point is not there yet. */
	FVector2D RegroupBaseCm = FVector2D::ZeroVector;
	FVector2D RegroupIntegralCm = FVector2D::ZeroVector;
	/** The addition to a regrouping step's weight shift for this tick (advances RegroupIntegralCm). */
	FVector2D RegroupLead(double DeltaTime);
	/** Starts, carries on and ends recovery steps while the body staggers (M3 part 4). */
	void UpdateStep(double DeltaTime);
	/** Sets a planned step going: the log entry, the swing, the legs' placements, the swing leg's muscles. */
	void StartSwing(const FSandboxStepRecord& Plan, const TArray<FVector2D>& TargetSole, double DurationS, bool bReaim);
	/**
	 * Where a step of this foot should go now: the capture point as it will be when the foot lands, TimeS from now
	 * (it runs away from the other foot's sole), a margin beyond it, kept on this foot's own side of the other foot
	 * and within reach. The bone's point, at the height a flat foot has on the floor. False if a foot has no sole.
	 * For a foot in the air (a step aimed anew), LiftSole and LiftFrom give its sole and bone as they were when it
	 * lifted, which the reach is counted from. KeepCross (-1, 0, 1) keeps a decision to cross made earlier; any
	 * other value lets the plan decide. The foot is put where the capture point is left at least
	 * balance.step_margin_goal_cm inside the new support, near the aimed point (FSandboxStepRecord::AimOffsetCm);
	 * KeepAim keeps an offset chosen earlier.
	 */
	bool PlanStep(int32 Side, double TimeS, FSandboxStepRecord& OutPlan, TArray<FVector2D>& OutTargetSole,
		const TArray<FVector2D>* LiftSole = nullptr, const FVector* LiftFrom = nullptr, int32 KeepCross = 2,
		const FVector2D* KeepAim = nullptr) const;
	/** Forward of a foot standing on the floor (its turn about the vertical since Start), seen from above: a crossing step passes it ahead or behind along this. */
	FVector2D CrossForward(int32 StanceSide) const;
	/** The support a step under way will give: the hull of the stance sole and of the swinging sole put at its target. Empty without a step. */
	void PlannedSupport(TArray<FVector2D>& OutPolygon) const;
	/**
	 * The capture point (moved by a held push) as it will be when a step of the other foot lands, if it starts
	 * now: it runs away from the nearest point of StanceSide's sole by e^(omega TimeS).
	 */
	FVector2D CaptureAtLanding(int32 StanceSide, double TimeS) const;
	/** How far inside the support a step of Side to TargetSole would give that capture point lies, cm (negative: outside). */
	double LandingMarginCm(int32 Side, const TArray<FVector2D>& TargetSole, double TimeS, FVector2D& OutLanding) const;

	/**
	 * A step asked for (RequestStep) waits while the weight goes over onto the other foot: the legs move the
	 * pelvis toward the stance foot (ShiftCm), and the foot lifts once the step, started then, would catch the
	 * capture point, or after balance.step_shift_max_s.
	 */
	struct FPendingStep
	{
		bool bActive = false;
		FSandboxStepRecord Plan;
		TArray<FVector2D> TargetSole;
		double ElapsedS = 0.0;
	};
	FPendingStep Pending;
	/** How far the pelvis's goal in the legs' upright frames is moved for a weight shift (world x, y, cm), and toward what. */
	FVector2D ShiftCm = FVector2D::ZeroVector;
	FVector2D ShiftGoalCm = FVector2D::ZeroVector;
	/** The swing of a step that began with a weight shift: the shift is held until the foot is down. */
	bool bShiftHeld = false;
	/** The shift as it was when the foot lifted. */
	FVector2D ShiftAtLiftCm = FVector2D::ZeroVector;
	/** Moves the shift toward its goal, with balance.step_shift_s's lag. */
	void UpdateShift(double DeltaTime);
	FSandboxAssistLoad AssistLoad;
	double WeightN = 0.0;
	double StandingComHeightM = 0.0;
	bool bStarted = false;
};
