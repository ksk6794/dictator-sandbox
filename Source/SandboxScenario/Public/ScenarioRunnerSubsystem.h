#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "SandboxBodyController.h"
#include "SandboxBodyProbe.h"
#include "ScenarioSpec.h"
#include "Subsystems/WorldSubsystem.h"
#include <atomic>
#include "ScenarioRunnerSubsystem.generated.h"

class ASandboxEffectorRig;
class USandboxEffectorComponent;
class ASandboxRagdollActor;
class ACameraActor;
class AGameModeBase;
class FSandboxRecordedTargetSource;
class FSandboxScriptedTargetSource;

/**
 * Runs one scenario and exits. Exists only when the process was started with -Scenario=, or with -ScenarioQueue=
 * (UScenarioBatchSubsystem: many scenarios in one process, each in a world of its own; the options below then come
 * from each job, and -Shot is not available).
 *
 *   -Scenario=<file or name>   Scenarios/<name>.json, or a path (relative paths resolve against the project dir)
 *   -Out=<dir>                 output directory (default Saved/Scenarios/<name>)
 *   -Shot=<file.png>           optional screenshot of the final frame (rendered runs only)
 *   -ShotAt=<s>[,<s>...]       with -Shot: also a screenshot at each of these simulated times, saved next to
 *                              the final one as shot_<time>.png (a strike is over long before the last frame)
 *   -Overlay=<layers>          debug overlays for the run (read by USandboxOverlaySubsystem; rendered runs only)
 *   -Tune="name=value,..."     tuning overrides for this run (read by FSandboxParamStore, active control only)
 *   -TuningDir=<dir>           tuning files to use instead of Config/Tuning (also read by the store)
 *   -vr                        engine flag: run in the headset. The camera becomes the tracking origin on
 *                              the floor, and metrics.json gains the headset's render size
 *
 * Writes <Out>/metrics.json (summary, checks, pass), <Out>/trace.json (body and bone transforms at
 * checkpoints) and, for a simulated body, <Out>/series.csv (energy, balance and penetration of every frame).
 * Exit code: 0 pass, 1 a check failed, 2 the scenario could not run.
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
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return Phase != EPhase::Idle; }
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	enum class EPhase : uint8 { Idle, Running, Finishing };

	/** A foot the body's target pose is asked to put at a point ("foot_target"), and where. */
	struct FFootTarget
	{
		FName Body;
		FVector WorldLocation = FVector::ZeroVector;
		FVector OffsetCm = FVector::ZeroVector;
		double Weight = 0.0;
	};
	TArray<FFootTarget> FootTargets;
	/** How far the foot's bone origin is from its point now, cm; -1 if the foot has no body. */
	double FootTargetErrorCm(const FFootTarget& Placed) const;

	/** A body held at a fixed world position by a "pin" event. */
	struct FPin
	{
		FName Body;
		FVector WorldLocation = FVector::ZeroVector;
		TUniquePtr<FConstraintInstance> Constraint;
		/** "follow_clip": where the body was pinned, and the mesh's own space then, which the clip's root is taken to be. */
		bool bFollowClip = false;
		double RaiseCm = 0.0;
		FTransform Start = FTransform::Identity;
		FTransform Space = FTransform::Identity;
	};
	TArray<FPin> Pins;
	/** Moves the pins that follow a clip to where the clip has the pelvis now. */
	void MovePins();

	/** An impulse event with a duration: one equal share of it is applied on each of the frames left. */
	struct FPush
	{
		FName Body;
		FVector ImpulsePerFrameNs = FVector::ZeroVector;
		int32 FramesLeft = 0;
	};
	TArray<FPush> Pushes;

	bool SpawnBody(UWorld& World, FString& OutError);
	bool SpawnEffectors(UWorld& World, FString& OutError);
	void SampleFrame(float DeltaTime);
	void ApplyDueEvents(double SimTimeS);
	void ApplyImpulse(FName Bone, const FVector& ImpulseNs);
	void ContinuePushes();
	bool CopyTuningFiles(FString& OutError);
	bool EditTuningFiles(const FScenarioEvent& Event, FString& OutError);
	void Finish();
	void FailAndExit(const FString& Message);
	/** Ends the run: the process exits, or in a batch (UScenarioBatchSubsystem) the next job begins. */
	void RequestExit(uint8 Code);
	/** The batch this run is a job of; null for a process started with -Scenario=. */
	class UScenarioBatchSubsystem* GetBatch() const;

	FScenarioSpec Spec;
	FString ScenarioFile;
	FString OutDir;
	FString ShotPath;
	/** In a batch, the job's shot_at (-ShotAt= otherwise). */
	FString JobShotAt;

	EPhase Phase = EPhase::Idle;
	int32 FrameIndex = 0;
	int32 TotalFrames = 0;
	/** For end_when_standing_s: when the last event is over, how long the body has stood without a break, and whether the run was cut short. */
	double InputsEndS = 0.0;
	int32 StandingRunFrames = 0;
	bool bEndedWhenStanding = false;
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

	// The player's hands, when the scenario has "effector" events: the same bodies and springs a pawn has,
	// with their targets read from the scenario's trajectories.
	UPROPERTY()
	TObjectPtr<ASandboxEffectorRig> EffectorRig;
	/** The scenario's things with mass, in its order, and what each did: where it started, how far it went, how fast. */
	UPROPERTY()
	TArray<TObjectPtr<AActor>> Props;
	struct FPropTrack
	{
		FVector Start = FVector::ZeroVector;
		double TravelCm = 0.0;
		double MaxSpeedCmS = 0.0;
	};
	TArray<FPropTrack> PropTracks;
	TSharedPtr<FSandboxScriptedTargetSource> EffectorSource;
	/** Or, with a "recording": the hands of a session, frame for frame, from RecordingStartFrame on. */
	TSharedPtr<FSandboxRecordedTargetSource> RecordedSource;
	int32 RecordingStartFrame = 0;
	/** Per effector, in the rig's order: the frame of its first contact, -1 while it has touched nothing. */
	TArray<int32> EffectorFirstContactFrame;
	FDelegateHandle GameModeInitializedHandle;

	/**
	 * What the runner measures about one effector from outside, by asking the scene where things are after
	 * every frame. None of it comes from the solver's contacts, so it also sees what the solver missed.
	 */
	struct FEffectorTrack
	{
		/** Where the effector's centre was after the frame before, and how often it had been snapped by then. */
		FVector LastCenter = FVector::ZeroVector;
		bool bHaveLast = false;
		int32 LastSnaps = 0;
		int32 LastShapeChanges = 0;
		/** Deepest the sphere was found inside a body of the character or the static world, cm, and what that was. */
		double MaxPenetrationCm = 0.0;
		FName DeepestInto;
		/** Frames in which the centre itself crossed a surface or lay inside a shape. */
		int32 TunnelFrames = 0;
		/** The body of the character it touched first, where that body was just before, and how far it has moved since. */
		bool bFirstContactSeen = false;
		FName StruckBone;
		FVector StruckStart = FVector::ZeroVector;
		double StruckDisplacementCm = 0.0;
	};
	TArray<FEffectorTrack> EffectorTracks;
	void TrackEffector(int32 Index, const USandboxEffectorComponent& Effector, const FVector& Center, const TArray<FSandboxBodySample>& Bodies);
	int32 FirstTunnelFrame = -1;
	FName FirstTunnelled;
	/** The body samples of the frame before: where a struck body was before it was struck. */
	TArray<FSandboxBodySample> PreviousBodies;

	/** Extra screenshots at moments of the run (-ShotAt=): the frame after which each is taken, and its file. */
	TArray<TPair<int32, FString>> MomentShots;

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
	/** Per body, by bone: the box its centre of mass has stayed in after the settle time. How far a body that stands moves. */
	TMap<FName, FBox> SettledBodyBoxes;
	double MaxJointSpeedRadS = 0.0;
	FName FastestJoint;
	double MaxJointLimitViolationDeg = 0.0;
	FName WorstLimitJoint;
	FVector WorstLimitAnglesDeg = FVector::ZeroVector;
	FVector WorstLimitLimitsDeg = FVector::ZeroVector;
	double WorstLimitTimeS = 0.0;
	/** How many entries of the controller's state log have been seen, and the timeline built from them. */
	int32 SeenStateChanges = 0;
	TArray<TSharedPtr<FJsonValue>> StateTimeline;

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
	/** The same by assist body (pelvis, chest, head, feet): horizontal and vertical parts, summed over the samples. */
	double AssistHorizontalShareSum[FSandboxAssistLoad::SlotCount] = { 0.0, 0.0, 0.0, 0.0 };
	double AssistVerticalShareSum[FSandboxAssistLoad::SlotCount] = { 0.0, 0.0, 0.0, 0.0 };

	/**
	 * Steps, as the body's own sense of its feet shows them: a foot that stops standing and stands again at
	 * least StepMinLengthCm from where it left the floor, after StepMinAirS or more in the air. Counted while the body is upright, from the first
	 * frame. A foot that is lifted and put back where it was is a lift, not a step.
	 */
	struct FFootTrack
	{
		bool bSeen = false;
		bool bStanding = false;
		FVector LiftLocation = FVector::ZeroVector;
		double LiftTimeS = 0.0;
	};
	FFootTrack FootTracks[2];
	struct FStep
	{
		FName Foot;
		double LiftTimeS = 0.0;
		double LandTimeS = 0.0;
		double LengthCm = 0.0;
		double DirectionDeg = 0.0;
	};
	TArray<FStep> Steps;
	int32 FootLifts = 0;
	/** Frames in which the controller found that a step was needed (while upright), and the first of them. */
	int32 StepNeededFrames = 0;
	int32 FirstStepNeededFrame = -1;
	/** Frames the body spent staggering, and the assist's force share over them (mean is the sum over the frames). */
	int32 StaggerFrames = 0;
	// What the assist does while the body gets up: its commanded force over body weight and its torque share.
	int32 GetUpFrames = 0;
	double GetUpAssistShareSum = 0.0;
	double GetUpAssistShareMax = 0.0;
	double GetUpTorqueShareMax = 0.0;
	double StaggerAssistShareSum = 0.0;
	double StaggerAssistShareMax = 0.0;
	/** The same for the assist's torque (over weight times centre-of-mass height): in Stagger it holds the pelvis upright. */
	double StaggerTorqueShareSum = 0.0;
	double StaggerTorqueShareMax = 0.0;
	/** Largest turn the balance strategies put on the ankles and the hips, degrees. */
	double AnkleLeanMaxDeg = 0.0;
	double HipBendMaxDeg = 0.0;
	/** Largest lean into a held push, cm, and the largest sustained force the body felt, N. */
	double LeanMaxCm = 0.0;
	double SustainedMaxN = 0.0;
	/** Where each assist anchor was in the first frame, by bone: anchors follow the support. */
	TMap<FName, FVector> StartAnchorsCm;
	/** How far the pelvis's anchor ended up from where it started, horizontally, cm. */
	double PelvisAnchorShiftCm() const;
	/** Lowest capture margin the controller's own decisions were made on, cm. */
	double MinDecisionMarginCm = TNumericLimits<double>::Max();
	void TrackFeet(const USandboxBodyController& Controller, const FSandboxBalanceView& Balance, const USkeletalMeshComponent& Mesh, double TimeS);
	/**
	 * How well the muscles hold the target pose while the body stands, after the settle time: the mean over
	 * the driven muscles of the angle between where each child body points and where its muscle turns it,
	 * per frame; then the mean and the largest of those, and the single worst muscle seen. Degrees.
	 */
	double PoseErrorSumDeg = 0.0;
	double PoseErrorMeanMaxDeg = 0.0;
	double PoseErrorWorstDeg = 0.0;
	FName PoseErrorWorstJoint;
	int32 PoseErrorFrames = 0;
	/** How far the muscles' targets are turned from the reference pose in the last frame, the largest. Degrees. */
	double PoseOffsetMaxDeg = 0.0;
	FName PoseOffsetJoint;

	// Balance as the body itself senses it (active control only), over the frames after the settle time in
	// which the body is upright (standing or flinching).
	int32 BalanceFrames = 0;
	int32 UnsupportedFrames = 0;
	int32 FeetFrames[3] = { 0, 0, 0 };
	double MinComMarginCm = TNumericLimits<double>::Max();
	double ComMarginSum = 0.0;
	double MinCaptureMarginCm = TNumericLimits<double>::Max();
	double CaptureMarginSum = 0.0;
	int32 CaptureOutsideFrames = 0;
	int32 SupportedFrames = 0;
	double SupportShareSum = 0.0;

	// Energy. The total (kinetic plus gravitational) may only fall while nothing feeds the body: a rise
	// above its lowest value since the last input is a gain. For a body under control the frames counted
	// are those in which it is falling or lying; for a passive body all of them.
	double GravityCmS2 = 980.0;
	double PeakKineticJ = 0.0;
	double FinalKineticJ = 0.0;
	double FinalPotentialJ = 0.0;
	double EnergyFloorJ = TNumericLimits<double>::Max();
	double MaxEnergyGainJ = 0.0;
	int32 MaxEnergyGainFrame = -1;
	/** Something acted on the body since the last sample: a scripted impulse, a pin, a command. */
	bool bInputPending = false;
	int32 SeenEffectorContactFrames = 0;

	// The fall, as the controller called it and as the runner measures its end: the body is at rest when
	// its mass-weighted speeds are below the rest thresholds.
	int32 FallingStartFrame = -1;
	int32 LastUnrestFrame = -1;
	FVector ComAtFallStartCm = FVector::ZeroVector;
	FVector ComFinalCm = FVector::ZeroVector;

	// Penetration of the body into the world and into itself, by scene queries after every frame.
	double MaxWorldPenetrationCm = 0.0;
	FName WorldPenetrationBone;
	FName WorldPenetrationInto;
	int32 WorldPenetrationFrame = -1;
	double MaxSelfPenetrationCm = 0.0;
	FName SelfPenetrationBoneA;
	FName SelfPenetrationBoneB;
	int32 SelfPenetrationFrame = -1;
	double SpawnWorldPenetrationCm = 0.0;
	double SpawnSelfPenetrationCm = 0.0;
	int32 UnmeasuredShapes = 0;
	int32 SelfCollisionPairs = 0;

	/** series.csv: one line per frame. */
	FString Series;
	/** hands.csv: one line per frame of a run with hands: each hand's gap to its target, speeds and forces. */
	FString HandSeries;

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
	double LastFrameWallS = 0.0;
	TArray<float> FrameWallMs;
	TArray<float> FrameGpuMs;
	TArray<float> FrameGameMs;
	TArray<float> FrameRenderMs;
};
