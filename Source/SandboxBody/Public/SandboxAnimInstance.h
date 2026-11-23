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
	TArray<FSandboxPoseTurn> Turns;
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
	 * Turns added on top of the pose from the next evaluation on, with no blend: whoever sets them shapes
	 * them over time (a flinch, a lean). Replaces the previous set; an empty set adds nothing.
	 */
	void SetTurns(const TArray<FSandboxPoseTurn>& InTurns) { Turns = InTurns; }

	/**
	 * Places the end of a limb at a point (two-bone placement, for a leg), after the pose and its turns. The
	 * end bone keeps the orientation the pose gives it, so a foot set down flat stays flat. The placement's
	 * weight goes from where it is to Weight over BlendS, eased in and out, as poses do: a target that jumped
	 * would have the muscles snap the limb (84 rad/s at the knee). The point itself is taken as given. Weight
	 * 0 lets the limb go back to the pose, and the target is dropped once it is there.
	 */
	void SetLimbTarget(int32 EndMeshBoneIndex, const FVector& WorldLocation, double Weight, double BlendS);

protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;

private:
	friend struct FSandboxAnimInstanceProxy;

	TArray<FSandboxPosedBone> Bones;
	TArray<FSandboxPoseTurn> Turns;

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

	double BlendS = 0.0;
	/** Time since the current blend started. */
	double BlendElapsedS = 0.0;
};
