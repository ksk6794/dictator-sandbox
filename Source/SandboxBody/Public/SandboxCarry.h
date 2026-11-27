#pragma once

#include "CoreMinimal.h"

class UPhysicsControlComponent;
class USkeletalMeshComponent;

/** How hard the world may pull a carried body toward its reference: every value per kilogram of the body pulled, so that a hand and a pelvis follow alike. */
struct FSandboxCarrySettings
{
	/** Spring toward the reference's place, N/m for each kg of the body. */
	double LinearNPerMPerKg = 0.0;
	/** Spring toward the reference's orientation, N*m/rad for each kg of the body. */
	double AngularNmPerRadPerKg = 0.0;
	/** Damping of both springs against the reference's own motion, as a time: damping = stiffness * this. */
	double DampingS = 0.0;
	/** The most force on a body over the floor, along each horizontal axis, in weights of that body. The engine caps a drive axis by axis. */
	double MaxWeights = 0.0;
	/**
	 * The most force upward, in weights of that body: what the world may carry of it. The rest is for the legs and the arms.
	 * Nothing is ever pushed down: a body above its reference is given the reference at its own height (gravity brings it down,
	 * unless the floor holds it there, and then a push would only press it into the floor).
	 */
	double MaxUpWeights = 0.0;
	/** The most torque on a body, N*m for each kg of it. */
	double MaxNmPerKg = 0.0;
	/**
	 * A factor on every cap, 0 to 1: what a press or a hold from outside leaves of the world's pull. The springs' share alone left
	 * the caps whole, and a body that a hand held back was pulled out of the hand as its error grew. At 0 nothing pulls.
	 */
	double CapShare = 1.0;
};

/**
 * The authority of a reference motion over the simulated body: one spring from the world to every body of the character,
 * aimed at where a reference (a recorded clip laid into the world) has that body, with a force cap in proportion to the
 * body's own weight. The body stays simulated: it collides, it is pushed, and a force above the caps wins. What the world
 * gives through these springs is the price of following a motion that was recorded on another body with other contacts;
 * it is measured (LoadN) and counted as assist, and the plan is to bring it down, not to hide it.
 *
 * A share of 0 disables every spring. The controls are made the first time they are needed, so a run that never carries
 * has no constraint of this class in its solver.
 */
class SANDBOXBODY_API FSandboxCarry
{
public:
	/** Makes one world-space control for every body of the mesh, all disabled. False with a reason if one cannot be made. */
	bool Create(UPhysicsControlComponent* InControl, USkeletalMeshComponent* InMesh, FString& OutError);
	bool IsCreated() const { return Control != nullptr; }

	int32 Num() const { return Bodies.Num(); }
	FName BoneAt(int32 Index) const { return Bodies[Index].Bone; }
	/** The index of a bone's body, INDEX_NONE if it has none. */
	int32 IndexOf(FName Bone) const { return Bodies.IndexOfByPredicate([Bone](const FBody& Body) { return Body.Bone == Bone; }); }
	/** For each body the index of the body it hangs from (the nearest ancestor bone with a body), INDEX_NONE for the root (the pelvis). */
	const TArray<int32>& GetParents() const { return Parents; }
	/** The share of its springs each body was last given (Apply), 1 unless a hold took some away. */
	double GetBodyShare(int32 Index) const { return Bodies[Index].BodyShare; }

	/**
	 * How far a hold on one body reaches (decision 14 of docs/active-ragdoll.md): the held body, and toward the root the bodies of
	 * its chain, by Mode (0 the held body alone, 1 its chain to the root without the root, 2 with it, 3 with it and every body that
	 * hangs from the held one), each further body counting Falloff less (0: alike). Hold is the held body's share, 0 to 1; each body
	 * of the chain takes the larger of what it had and this. Pure arithmetic on the parents.
	 */
	static void ChainHolds(TConstArrayView<int32> InParents, int32 Held, int32 Mode, double Falloff, double Hold, TArrayView<double> InOutHolds);

	/**
	 * Where a body's bone should be at the start of the coming step, in the world. The reference's velocity is the difference
	 * from the target before it over DeltaTime; a body given its first target since ForgetTargets has none (a velocity made
	 * across a jump of the target would be a blow).
	 */
	void SetTarget(int32 Index, const FTransform& World, double DeltaTime);
	/** The next targets are first ones: no velocity is made from the ones before. Bodies keep their targets. */
	void ForgetTargets();

	/**
	 * Hands the springs to the solver at a share of their strength (0 to 1). Bodies that never had a target stay free. BodyShares,
	 * when given, has one share for each body (0 to 1) on top of Share: what a hold leaves of that body's springs; empty, every
	 * body has all of Share.
	 */
	void Apply(const FSandboxCarrySettings& Settings, double Share, TConstArrayView<double> BodyShares = TConstArrayView<double>(),
		TConstArrayView<double> VerticalShares = TConstArrayView<double>());
	double GetShare() const { return AppliedShare; }

	/** What the springs ask of the world at the start of this step, from the spring law (an estimate, as the assist's load is): the sum of the forces' sizes, N. */
	double LoadN(const FSandboxCarrySettings& Settings, double Share, FVector* OutNetN = nullptr) const;
	/** The force one body's spring asks for, N, in the world. */
	FVector ForceOn(int32 Index, const FSandboxCarrySettings& Settings, double Share) const;
	/** The torque one body's angular spring asks for, N*m, in the world (capped by its size; the engine caps axis by axis). */
	FVector TorqueOn(int32 Index, const FSandboxCarrySettings& Settings, double Share) const;
	/** Where one body's centre of mass is to be and where it is, cm, world: for whoever looks. False before the body has a target. */
	bool ComOf(int32 Index, FVector& OutTargetComCm, FVector& OutComCm) const;
	/** For a log line: the Count bodies pulled hardest, each with its force and how far it is from its target. */
	FString Describe(const FSandboxCarrySettings& Settings, double Share, int32 Count) const;

private:
	struct FBody
	{
		FName Bone;
		FName ControlName;
		double MassKg = 0.0;
		FTransform Target = FTransform::Identity;
		FVector VelocityCmS = FVector::ZeroVector;
		FVector AngularVelocityRadS = FVector::ZeroVector;
		bool bHasTarget = false;
		bool bHasVelocity = false;
		/** What a hold left of this body's springs in the last Apply. */
		double BodyShare = 1.0;
		/**
		 * R2, step 10 (getup.fit_free_z): what is left of this body's vertical spring and damping, 0 to 1 (0: a body the reference has
		 * on its floor and that touches the world now is carried by the floor, not the world). 1 unless Apply is given one for each body.
		 */
		double VerticalShare = 1.0;
	};
	TArray<int32> Parents;

	/** How far the body's centre of mass is above where its target has it (0 when below): the target is raised by this. cm. */
	double RaiseCm(const FBody& Body) const;

	UPhysicsControlComponent* Control = nullptr;
	USkeletalMeshComponent* Mesh = nullptr;
	TArray<FBody> Bodies;
	double AppliedShare = 0.0;
	bool bEnabled = false;
};
