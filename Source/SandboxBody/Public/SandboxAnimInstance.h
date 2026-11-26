#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "SandboxPoseRig.h"
#include "SandboxAnimInstance.generated.h"

/** One bone the animation instance poses: where it comes from and where it goes in the current blend. */
struct FSandboxPosedBone
{
	int32 MeshBoneIndex = INDEX_NONE;
	FQuat From = FQuat::Identity;
	FQuat To = FQuat::Identity;
};

/** A limb whose end the animation instance places at a point by turning its two segments (a leg: thigh, calf, foot). */
struct FSandboxLimbTarget
{
	/** The end bone (the foot), in the mesh's reference skeleton. Its parent and grandparent are the two segments. */
	int32 EndMeshBoneIndex = INDEX_NONE;
	/** Where the end bone's origin should be, world space. */
	FVector WorldLocation = FVector::ZeroVector;
	/** 0 leaves the limb as the pose has it, 1 puts the end on the point. */
	double Weight = 0.0;
	/**
	 * Where the component would be for the pose to be solved in, world space; used when bInFrame is set. The
	 * point is carried into the pose's space through this transform instead of the component's own. A leg
	 * solved in the frame of a pelvis that stands upright at its anchor gives the angles of that posture, so
	 * the muscles pull the body toward it, rather than angles that follow wherever the pelvis is now.
	 */
	bool bInFrame = false;
	FTransform Frame = FTransform::Identity;
	/**
	 * The end bone keeps the pose's orientation relative to this world rotation instead of relative to the
	 * component; used when bEndInFrame is set. A foot in the air is held level this way, whatever the pelvis does.
	 */
	bool bEndInFrame = false;
	FQuat EndFrame = FQuat::Identity;
	/**
	 * The range of the end's joint (the ankle): the end's orientation is brought into it after it is set. Held
	 * level while the leg reaches out or back, a foot was asked to turn 34 degrees about the shin against a
	 * range of 15; its muscle drove it into the limit and the limit turned the whole leg away from its pose.
	 */
	FSandboxJointLimits EndLimits;
	/**
	 * How much of that (0 to 1): a foot in the air is held to its ankle's range, a foot on the floor is not (the
	 * floor holds it flat; asked to tilt onto its edge, a planted foot was rolled over by its own ankle), and
	 * one that has just landed goes from the one to the other (at once, the ankle flipped the landed foot over).
	 */
	double EndLimitWeight = 0.0;
	/**
	 * The end is put no further from the limb's root than this share of the limb's length (0: as far as it
	 * reaches). A point out of reach straightened the leg at once: a knee that went from 20 degrees to none in
	 * one frame kicked the body off the floor.
	 */
	double ReachShare = 0.0;
	/** The share of its length the limb is held to comes in with the placement's weight (whole at once, a leg that is placed for the first time jumps). */
	bool bReachWithWeight = false;
};

/**
 * Worker-thread side of USandboxAnimInstance. Builds the pose the muscles are asked to hold. Everything it
 * needs is copied from the instance in PreUpdate, on the game thread, so evaluation reads no shared state.
 */
struct FSandboxAnimInstanceProxy : public FAnimInstanceProxy
{
	FSandboxAnimInstanceProxy() = default;
	explicit FSandboxAnimInstanceProxy(UAnimInstance* InAnimInstance) : FAnimInstanceProxy(InAnimInstance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	/** Places the end of each limb target, after the pose and its turns. */
	void PlaceLimbs(FPoseContext& Output) const;

	TArray<FSandboxPosedBone> Bones;
	TArray<FSandboxPoseBone> Layer;
	double LayerWeight = 0.0;
	TArray<FSandboxPoseTurn> Turns;
	TArray<FSandboxPoseTurn> FingerTurns;
	TArray<FSandboxLimbTarget> Limbs;
	/** How far the blend from From to To has come, eased: 0 to 1. */
	double Weight = 1.0;
};

/**
 * The source of the target pose for an active ragdoll. Physics Control takes its muscle targets from the
 * pose the mesh evaluates (see USandboxBodyController), and this is what evaluates it: no Animation
 * Blueprint, no graph. It starts from the reference pose and sets only the bones a pose turns, so with
 * nothing set, or a pose of zeros, the body is asked for exactly what it held before it had an animation
 * instance.
 *
 * Time comes from the mesh's own tick (the fixed simulation step), never from the clock.
 */
UCLASS(Transient, NotBlueprintable)
class SANDBOXBODY_API USandboxAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/**
	 * Goes from the pose the body is asked for now to this one over BlendS seconds, eased in and out. Bones
	 * the new pose does not list go back to the reference pose. A pose equal to the current target changes
	 * nothing, and in particular does not restart a blend.
	 */
	void SetTargetPose(const TArray<FSandboxPoseBone>& Pose, double BlendS);

	/** The blend's progress, 0 to 1, eased. 1 when no blend is running. */
	double GetBlendWeight() const;

	/**
	 * A pose laid over the base pose from the next evaluation on: these bones go from where the base pose has them
	 * to the rotations given, by Weight (0 the base pose, 1 this one). With no blend of its own: whoever sets it
	 * hands it over every tick and shapes both the pose and the weight over time (a recorded clip that is held or
	 * played, USandboxBodyController::SetClipPose). Turns and limb targets come on top of it as they do on the base
	 * pose. An empty set, or weight 0, is no layer: the evaluation is then exactly what it is without one.
	 */
	void SetLayer(const TArray<FSandboxPoseBone>& InBones, double InWeight)
	{
		Layer = InBones;
		LayerWeight = FMath::Clamp(InWeight, 0.0, 1.0);
	}

	/**
	 * Turns added on top of the pose from the next evaluation on, with no blend: whoever sets them shapes
	 * them over time (a flinch, a lean). Replaces the previous set; an empty set adds nothing.
	 */
	void SetTurns(const TArray<FSandboxPoseTurn>& InTurns) { Turns = InTurns; }

	/** The same for the bones of the fingers, which nothing else turns; kept until the next set. An empty set leaves them as modelled. */
	void SetFingerTurns(const TArray<FSandboxPoseTurn>& InTurns) { FingerTurns = InTurns; }

	/**
	 * Places the end of a limb at a point (two-bone placement, for a leg), after the pose and its turns. The
	 * end bone keeps the orientation the pose gives it, so a foot set down flat stays flat. The placement's
	 * weight goes from where it is to Weight over BlendS, eased in and out, as poses do: a target that jumped
	 * would have the muscles snap the limb (84 rad/s at the knee). The point itself is taken as given. Weight
	 * 0 lets the limb go back to the pose, and the target is dropped once it is there.
	 */
	void SetLimbTarget(int32 EndMeshBoneIndex, const FVector& WorldLocation, double Weight, double BlendS);

	/**
	 * Moves the point of a limb target that exists, and sets the frame it is solved in (nullptr: the component
	 * itself), without touching the blend of its weight. Does nothing for a limb that has no target. For a
	 * point that travels (a foot in a step) and a frame that moves with the body, every tick.
	 */
	void MoveLimbTarget(int32 EndMeshBoneIndex, const FVector& WorldLocation, const FTransform* Frame, const FQuat* EndFrame = nullptr,
		double EndLimitWeight = 0.0, double ReachShare = 0.0);

	/** From now on the share of its length a limb is held to comes in with its target's weight (FSandboxLimbTarget::bReachWithWeight). */
	void SetReachWithWeight(bool bInReachWithWeight) { bReachWithWeight = bInReachWithWeight; }

	/** The weight a limb target has now, part way through its blend; 0 without one. */
	double GetLimbWeight(int32 EndMeshBoneIndex) const;

	/** The range of a limb end's joint, kept for every target of that end from now on (FSandboxLimbTarget::EndLimits). */
	void SetLimbEndLimits(int32 EndMeshBoneIndex, const FSandboxJointLimits& Limits) { EndLimits.Add(EndMeshBoneIndex, Limits); }

protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:
	friend struct FSandboxAnimInstanceProxy;

	TArray<FSandboxPosedBone> Bones;
	TArray<FSandboxPoseBone> Layer;
	double LayerWeight = 0.0;
	TArray<FSandboxPoseTurn> Turns;
	TArray<FSandboxPoseTurn> FingerTurns;

	/** A limb target on the game thread, with the blend of its weight. */
	struct FLimbState
	{
		FSandboxLimbTarget Target;
		double FromWeight = 0.0;
		double ToWeight = 0.0;
		double BlendS = 0.0;
		double ElapsedS = 0.0;

		double CurrentWeight() const
		{
			return BlendS <= 0.0 || ElapsedS >= BlendS ? ToWeight : FMath::Lerp(FromWeight, ToWeight, FMath::SmoothStep(0.0, 1.0, ElapsedS / BlendS));
		}
	};
	TArray<FLimbState> LimbStates;
	TMap<int32, FSandboxJointLimits> EndLimits;
	bool bReachWithWeight = false;

	double BlendS = 0.0;
	/** Time since the current blend started. */
	double BlendElapsedS = 0.0;
};
