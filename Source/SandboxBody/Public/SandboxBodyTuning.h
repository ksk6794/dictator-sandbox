#pragma once

#include "CoreMinimal.h"

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
	double PositionIterations = 64.0;
	double VelocityIterations = 2.0;
	double ProjectionIterations = 1.0;
};

/** Which bones the balance assist holds. Each must have a body in the physics asset. */
struct FSandboxBodyRig
{
	FName Pelvis = TEXT("pelvis");
	FName Chest = TEXT("spine_04");
	/** The body that carries the head; in the NPC's physics asset head and neck are one body on neck_01. */
	FName Head = TEXT("neck_01");
	FName FootLeft = TEXT("foot_l");
	FName FootRight = TEXT("foot_r");
};

/**
 * Every number that shapes how the active ragdoll behaves. The defaults are the M2 standing values;
 * from the parameter-file step on they are read from Config/Tuning.
 */
struct SANDBOXBODY_API FSandboxBodyTuning
{
	FSandboxBodyRig Rig;

	FSandboxMuscleTuning Spine{ 600.0, 0.06, 0.0 };
	FSandboxMuscleTuning Neck{ 80.0, 0.06, 0.0 };
	FSandboxMuscleTuning Arms{ 150.0, 0.06, 0.0 };
	FSandboxMuscleTuning Legs{ 800.0, 0.06, 0.0 };

	FSandboxAssistTuning Pelvis{ 1500.0, 0.0, 60.0, 0.2 };
	FSandboxAssistTuning Chest{ 0.0, 0.0, 60.0, 0.2 };
	FSandboxAssistTuning Head{ 0.0, 0.0, 10.0, 0.2 };
	FSandboxAssistTuning Feet{ 1500.0, 1500.0, 15.0, 0.2 };

	FSandboxSolverTuning Solver;

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

	/** Applies "name=value,name=value". False with a reason on the first malformed or unknown entry. */
	bool ApplyOverrides(const FString& Overrides, FString& OutError);

	/** Group a joint belongs to, from the name of its child bone (UE5 mannequin and MetaHuman naming). */
	static ESandboxMuscleGroup GroupOfBone(FName ChildBone);
	static const TCHAR* GroupName(ESandboxMuscleGroup Group);
};
