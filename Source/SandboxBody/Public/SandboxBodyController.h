#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "SandboxBodyTuning.h"
#include "SandboxPoseRig.h"
#include "SandboxAnimInstance.h"
#include "SandboxBodyController.generated.h"

class FSandboxParamStore;
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
 *                     Standing or Flinch --capture point beyond a step, or pelvis too low or too tilted--> Falling
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
	bool SetFootTarget(FName Foot, const FVector& WorldLocation, double Weight, FString& OutError);

	/**
	 * What the body has taken from contacts with other simulated bodies since it was started: the sum of
	 * the contact impulses' sizes in N*s and the number of contact reports. Contacts with the static world
	 * (the floor under the feet) and between the character's own bodies are not hits and are not counted.
	 */
	double GetContactImpulseNs() const { return ContactImpulseNs; }
	int32 GetContactReportCount() const { return ContactReports; }

	bool IsStarted() const { return bStarted; }
	/** The values of the current state, as the tuning files and overrides give them. */
	const FSandboxBodyTuning& GetTuning() const { return Tuning; }
	/** How many times an edited file or an override changed the values of the state the body was in. */
	int32 GetTuningChangeCount() const { return TuningChangeCount; }

	ESandboxBodyState GetState() const { return State; }
	/** Every state the body has been in, the first entry being Standing at time 0. */
	const TArray<FSandboxStateChange>& GetStateLog() const { return StateLog; }
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
	 * a step. It cannot step yet (M3 part 4), so the assist holds it; result files report the frames.
	 */
	bool IsStepNeeded() const { return bStepNeeded; }
	/** The capture point's margin to the support that the last decision was made on, cm; positive inside. */
	double GetDecisionMarginCm() const { return LastDecisionMarginCm; }
	/** How many times a foot was planted anew, its assist anchor moved to where it landed. */
	int32 GetReplantCount() const { return ReplantCount; }

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
		/** Going over to a new support: from where, to where, and for how long so far. Below 0 when not. */
		FTransform FollowFrom = FTransform::Identity;
		FTransform FollowTo = FTransform::Identity;
		double FollowElapsedS = -1.0;

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
	/** The legs the strategies turn: foot and thigh bones, left and right (mesh bone indices and names). */
	int32 FootBoneIndex[2] = { INDEX_NONE, INDEX_NONE };
	int32 ThighBoneIndex[2] = { INDEX_NONE, INDEX_NONE };
	FName FootBoneName[2];
	FName ThighBoneName[2];

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
	FSandboxAssistLoad AssistLoad;
	double WeightN = 0.0;
	double StandingComHeightM = 0.0;
	bool bStarted = false;
};
