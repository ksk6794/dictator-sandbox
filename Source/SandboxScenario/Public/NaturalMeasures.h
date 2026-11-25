#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "SandboxBodyController.h"
#include "SandboxBodyProbe.h"

class FSandboxPosePrior;
class USkeletalMeshComponent;

/** One frame of a run as the measures of naturalness see it: the state after the frame's physics step. */
struct FNaturalFrame
{
	int32 Frame = 0;
	/** Whether the run's settle time is over. */
	bool bSettled = false;
	const USkeletalMeshComponent* Mesh = nullptr;
	/** Null for a body without muscles. */
	const USandboxBodyController* Controller = nullptr;
	const TArray<FSandboxBodySample>* Bodies = nullptr;
	/** The targets and drives the step was made with. Null without a controller. */
	const FSandboxBodyControlView* View = nullptr;
};

/**
 * Measures of naturalness (docs/natural-body.md, N1): how a fall meets the floor, how soon the body answers a hit,
 * what its joints work, how long they are beyond their range, what jumps, how smoothly the head moves, how long
 * the body is still, how far a push moves it and how it sways. Looks at the bodies and at what the controller
 * hands out, after every step, and changes nothing: a run has the same hashes with it and without.
 *
 * Times are the run's own. "t" of something that happened in a step is the time that step began.
 */
class SANDBOXSCENARIO_API FNaturalMeasures
{
public:
	/** Forward is the way the body faces as it is spawned, over the floor. */
	void Begin(double InFixedDtS, const FVector& Forward);

	/** A scripted push with a duration has begun: its first share is applied before the step of Frame. */
	void NotePush(int32 Frame, FName Bone, const FVector& ForceN, int32 Frames, const TArray<FSandboxBodySample>& Bodies);
	/** How far one joint is outside its range in this frame, degrees (FSandboxPoseRig::LimitExcessDeg). */
	void NoteLimit(const USandboxBodyController* Controller, FName Joint, double ExcessDeg);
	void Sample(const FNaturalFrame& Frame);
	/**
	 * Makes the pose database of the clips listed (animation paths) and writes it, or reads one: from then on every
	 * third frame is held against it. The database is kept between the runs of a process. False with a reason.
	 */
	static bool BuildPrior(USandboxBodyController& Controller, const USkeletalMeshComponent* Mesh, const TArray<FString>& ClipPaths, const FString& File, FString& OutError);
	bool UsePrior(const USandboxBodyController& Controller, const USkeletalMeshComponent* Mesh, const FString& File, FString& OutError);

	/** Into "control.fall", "control", "control.pose", "metrics" and the entry of one joint of "body.joints". */
	void WriteFall(FJsonObject& Fall) const;
	void WriteControl(FJsonObject& Control);
	void WritePose(FJsonObject& Pose) const;
	void WriteMetrics(FJsonObject& Metrics);
	void WriteJoint(FName Joint, FJsonObject& Entry) const;

	/**
	 * After each substep: adds up where the world that does not move pushed up on the body, from the points of the
	 * solver's own contacts, each with its impulse. (The contact events will not do: an event has the impulse of the
	 * whole contact and one of its points, the nearest. And the last substep alone will not do: a frame's two substeps
	 * do not load the soles alike.) Reads only. Called from the solver's step, which the game thread waits for.
	 */
	void AddPressure(const USkeletalMeshComponent* Mesh, double SubstepS);

	/**
	 * How far a part lies ahead of the chest along the way the body goes over the floor (the velocity's horizontal
	 * part), cm; negative: behind it. Unset for a body that goes nowhere: it has no ahead.
	 */
	static TOptional<double> AheadCm(const FVector& Part, const FVector& Chest, const FVector& VelocityCmS);

private:
	double Dt = 1.0 / 90.0;
	FVector2D Forward = FVector2D(0.0, 1.0);

	/** How a fall met the floor: each body (but the feet) the first time the world touched it. */
	struct FTouch
	{
		FName Bone;
		int32 Frame = 0;
		/** The body's speed at the start of the step it touched in, and its downward part. */
		double SpeedMps = 0.0;
		double DownMps = 0.0;
		/** How far ahead of the chest it lay then, along the way the body was going over the floor, cm (AheadCm). */
		TOptional<double> AheadCm;
	};
	struct FFall
	{
		int32 StartFrame = 0;
		bool bOpen = true;
		TArray<FTouch> Touches;
		/** The fastest the head came down at the floor, of every time it met it; whether it touched in the last frame. */
		double HeadDownMps = 0.0;
		bool bHeadTouching = false;
	};
	TArray<FFall> Falls;
	void SampleFall(const FNaturalFrame& Frame, ESandboxBodyState State);
	void WriteOneFall(const FFall& Fall, FJsonObject& Json) const;

	/** What the body answered to a hit: the first frame in which something it decides is no longer what it was. */
	struct FReaction
	{
		/** The frame whose step was the first after the hit was felt. */
		int32 Frame = 0;
		FName Bone;
		double ImpulseNs = 0.0;
		TArray<FQuat> Targets;
		TArray<double> Stiffness;
		double TargetSpeedBeforeDps = 0.0;
		int32 TargetFrame = -1;
		FName TargetJoint;
		int32 StrengthFrame = -1;
		int32 LiftFrame = -1;
		int32 LandFrame = -1;
		int32 FallFrame = -1;
		int32 StepsBefore = 0;
	};
	TArray<FReaction> Reactions;
	int32 SeenEvents = 0;
	void SampleReactions(const FNaturalFrame& Frame, ESandboxBodyState State);

	/** The muscles' targets and stiffnesses of the frame before, and the fastest target over the last frames, degrees a second. */
	TArray<FQuat> LastTargets;
	TArray<double> LastStiffness;
	TArray<double> RecentTargetDps;

	/** What each joint's muscle works, from the spring law. */
	struct FEffort
	{
		TArray<float> TorqueNm;
		double CapNm = 0.0;
		int32 CapFrames = 0;
		int32 UprightFrames = 0;
		int32 UprightCapFrames = 0;
		double PowerMaxW = 0.0;
		double WorkJ = 0.0;
		/** The joint's torque in the last frame, world, and its mean over the last second of the run. */
		TArray<float> Recent;
	};
	TArray<FName> MuscleJoints;
	TArray<FEffort> Efforts;
	int32 UprightFrames = 0;
	int32 AnyCapUprightFrames = 0;
	double PositiveWorkJ = 0.0;
	double PowerPeakW = 0.0;
	int32 PowerPeakFrame = 0;
	void SampleMuscles(const FNaturalFrame& Frame, ESandboxBodyState State, double& OutTargetStepDeg);

	struct FLimit
	{
		int32 Frames = 0;
		int32 OutsideFrames = 0;
	};
	TMap<FName, FLimit> Limits;

	/** Jumps. */
	double TargetStepMaxDeg = 0.0;
	FName TargetStepJoint;
	int32 TargetStepFrame = -1;
	/** What a new target or strength, all else as it was, changes of a muscle's torque in one frame, N*m: the jump the body is asked to make. */
	double CommandStepMaxNm = 0.0;
	FName CommandStepJoint;
	int32 CommandStepFrame = -1;
	TArray<FSandboxMuscleView> LastMuscles;
	double StrengthStepMax = 0.0;
	FName StrengthStepJoint;
	int32 StrengthStepFrame = -1;
	double JointSpeedStepMax = 0.0;
	FName JointSpeedStepJoint;
	int32 JointSpeedStepFrame = -1;
	TMap<FName, FVector> LastJointSpeeds;

	/** The head's velocity of the last frames, and what follows from it. */
	TArray<FVector> HeadVelocities;
	FVector LastHeadAcceleration = FVector::ZeroVector;
	bool bHaveHeadAcceleration = false;
	double HeadAccelMax = 0.0;
	int32 HeadAccelFrame = -1;
	double HeadJerkSquares = 0.0;
	int32 HeadJerkFrames = 0;
	void SampleHead(const FNaturalFrame& Frame);

	/** Stillness: where every body was in each frame of the last StillWindowS. */
	TArray<TArray<FVector>> Places;
	int32 StillFrames = 0;
	int32 LongestStillFrames = 0;
	int32 LongestStillStart = 0;
	ESandboxBodyState LongestStillState = ESandboxBodyState::Standing;
	struct FStillShare
	{
		int32 Frames = 0;
		int32 Still = 0;
		int32 Run = 0;
		int32 Longest = 0;
		int32 TargetRun = 0;
		int32 TargetLongest = 0;
		/** How far the part that went furthest went in each window that ended in this state, cm. */
		TArray<float> TravelCm;
	};
	TMap<ESandboxBodyState, FStillShare> StillByState;
	/** Frames since the body's state was entered: a state has not been still for longer than it has lasted. */
	int32 FramesInState = 0;
	void SampleStillness(const FNaturalFrame& Frame, ESandboxBodyState State, double TargetStepDeg);

	/** Pushes with a duration. */
	struct FPushed
	{
		int32 Frame = 0;
		int32 Frames = 0;
		FName Bone;
		FVector ForceN = FVector::ZeroVector;
		FVector Start = FVector::ZeroVector;
		FVector WholeStart = FVector::ZeroVector;
		double MaxCm = 0.0;
		/** Furthest along the push, and against it. */
		double AlongCm = 0.0;
		double AgainstCm = 0.0;
		double SettledSumCm = 0.0;
		double WholeSumCm = 0.0;
		int32 SettledFrames = 0;
		int32 ReturnFrame = -1;
	};
	TArray<FPushed> Pushes;
	void SamplePushes(const FNaturalFrame& Frame);

	/** Standing: the centre of mass and the centre of pressure over the floor, along and across the way the body faces, cm. */
	TArray<FVector2D> SwayCom;
	TArray<FVector2D> SwayCop;
	/** The longest stretch without a break so far: a body that stepped elsewhere in between stands at another place. */
	TArray<FVector2D> BestCom;
	TArray<FVector2D> BestCop;
	double BestFloorShareSum = 0.0;
	void EndSwayStretch();

	/** What the assist commands by state: force over body weight, torque over weight times height; sums and largest. */
	struct FAssistShare
	{
		int32 Frames = 0;
		double ForceSum = 0.0;
		double ForceMax = 0.0;
		double TorqueSum = 0.0;
		double TorqueMax = 0.0;
	};
	TMap<ESandboxBodyState, FAssistShare> AssistByState;
	/** Since the last frame: the upward impulse through the contacts, N*s, and its points times it. */
	FVector PressureMomentNsCm = FVector::ZeroVector;
	double PressureNs = 0.0;
	/** The floor's push on the body over its weight, summed: 1 on average for a body that nothing else holds. */
	double FloorShareSum = 0.0;
	void SampleSway(const FNaturalFrame& Frame, ESandboxBodyState State);

	/** The likeness of the pose to recorded ones, by state, degrees. */
	TSharedPtr<const FSandboxPosePrior> Prior;
	TMap<ESandboxBodyState, TArray<double>> PriorByState;
	double PriorWorstDeg = -1.0;
	int32 PriorWorstFrame = -1;
	ESandboxBodyState PriorWorstState = ESandboxBodyState::Standing;
	FString PriorWorstClip;
	double PriorWorstClipS = 0.0;
	void SamplePrior(const FNaturalFrame& Frame, ESandboxBodyState State);

	TArray<FSandboxBodySample> LastBodies;
	ESandboxBodyState LastState = ESandboxBodyState::Standing;
	bool bHaveLast = false;
};
