#pragma once

#include "CoreMinimal.h"

class FSandboxParamStore;

/** Joints are tuned in groups, not one by one. */
enum class ESandboxMuscleGroup : uint8
{
	Spine,
	Neck,
	Arms,
	Legs
};

/**
 * Drive of one muscle group: a rotational spring-damper on every joint of the group that turns the child
 * body toward its target pose, relative to the parent body. Values are physical, so they can be held
 * against measured human joints (an ankle in quiet standing is a few hundred N*m/rad).
 */
struct FSandboxMuscleTuning
{
	/** Spring stiffness in N*m per radian of error. 0 switches the muscle off. */
	double StiffnessNmPerRad = 0.0;
	/** Damping as a time: damping torque = stiffness x this x angular velocity. Seconds. */
	double DampingS = 0.0;
	/** Largest torque the muscle can produce, N*m. 0 = no cap. */
	double MaxTorqueNm = 0.0;
};

/**
 * Balance assist on one body: springs between the body and a fixed target in the world. This is the one
 * non-physical force acting on the character, so every value here should be as low as standing allows.
 */
struct FSandboxAssistTuning
{
	/** Pull toward the target position in the horizontal plane, N/m. 0 leaves the body free to move sideways. */
	double HorizontalNPerM = 0.0;
	/** Pull toward the target height, N/m. 0 leaves the weight to the legs. */
	double VerticalNPerM = 0.0;
	/** Turn toward the target orientation, N*m/rad. */
	double AngularNmPerRad = 0.0;
	/** Damping of all three springs as a time: damping = stiffness x this x velocity. Seconds. */
	double DampingS = 0.0;
};

/**
 * Solver effort spent on the character's bodies. The engine runs every simulation island with the highest
 * counts any of its bodies asks for, so these apply to the character and whatever it touches, not to the
 * rest of the world.
 */
struct FSandboxSolverTuning
{
	/**
	 * Position iterations per substep. The engine default of 8 does not converge for a heavy body standing
	 * on light feet through stiff joints: the feet rock and creep across the floor.
	 */
	double PositionIterations = 0.0;
	double VelocityIterations = 0.0;
	double ProjectionIterations = 0.0;
};

/** Which bones the balance assist holds ("rig.pelvis" and so on). Each must have a body in the physics asset. */
struct FSandboxBodyRig
{
	FName Pelvis;
	FName Chest;
	/** The body that carries the head; in the NPC's physics asset head and neck are one body on neck_01. */
	FName Head;
	FName FootLeft;
	FName FootRight;

	bool operator==(const FSandboxBodyRig& Other) const
	{
		return Pelvis == Other.Pelvis && Chest == Other.Chest && Head == Other.Head && FootLeft == Other.FootLeft && FootRight == Other.FootRight;
	}
};

/**
 * Everything that shapes how the active ragdoll behaves. The values live in Config/Tuning/body.json and
 * reach the code through FromParams; a default-constructed tuning is empty and does not validate.
 */
struct SANDBOXBODY_API FSandboxBodyTuning
{
	FSandboxBodyRig Rig;

	FSandboxMuscleTuning Spine;
	FSandboxMuscleTuning Neck;
	FSandboxMuscleTuning Arms;
	FSandboxMuscleTuning Legs;

	FSandboxAssistTuning Pelvis;
	FSandboxAssistTuning Chest;
	FSandboxAssistTuning Head;
	FSandboxAssistTuning Feet;

	FSandboxSolverTuning Solver;

	/**
	 * Tells a parameter store which names the body understands and which states may carry values of their
	 * own, and adds the range checks of Validate to it, so the store refuses a file or an override the body
	 * could not use.
	 */
	static void DeclareParams(FSandboxParamStore& Params);

	/**
	 * Reads the tuning for one state of the body ("falling"). An empty state reads the base values, which
	 * are also what a state uses for everything it does not set itself. False with a reason if a value is
	 * missing or does not validate.
	 */
	static bool FromParams(const FSandboxParamStore& Params, const FString& State, FSandboxBodyTuning& OutTuning, FString& OutError);

	/** True if every number is the same. The rig is not compared. */
	bool HasSameValues(const FSandboxBodyTuning& Other) const;

	const FSandboxMuscleTuning& Muscle(ESandboxMuscleGroup Group) const;

	/** False with a reason if a value is negative, not finite or absurdly large. */
	bool Validate(FString& OutError) const;

	/**
	 * Every tunable number by name, "muscles.legs.stiffness_nm_per_rad", "assist.pelvis.horizontal_n_per_m"
	 * or "solver.position_iterations", in a fixed order. The names are the ones used in parameter files, overrides and result files.
	 */
	void ListValues(TArray<TPair<FString, double>>& OutValues) const;

	/** Sets one number by name. False with a reason if the name is unknown. Does not validate the range. */
	bool SetValue(const FString& Key, double Value, FString& OutError);

	/** Group a joint belongs to, from the name of its child bone (UE5 mannequin and MetaHuman naming). */
	static ESandboxMuscleGroup GroupOfBone(FName ChildBone);
	static const TCHAR* GroupName(ESandboxMuscleGroup Group);
};
