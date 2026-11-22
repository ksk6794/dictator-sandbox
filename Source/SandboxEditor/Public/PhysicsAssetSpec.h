#pragma once

#include "CoreMinimal.h"
#include "PhysicsEngine/ConstraintTypes.h"

/** One angular axis of a joint: locked, free, or limited to +/- LimitDeg around the joint frame. */
struct FPhysicsAxisSpec
{
	EAngularConstraintMotion Motion = ACM_Locked;
	double LimitDeg = 0.0;
};

/**
 * A collision box written out in the spec, in the mesh's reference-pose space (floor at z = 0, z up).
 * Used where the template's shapes do not do the job: a foot needs a flat sole on the floor.
 */
struct FPhysicsShapeSpec
{
	FVector CenterCm = FVector::ZeroVector;
	/** Full lengths along the box's own x, y and z. */
	FVector SizeCm = FVector::ZeroVector;
	/** Rotation of the box about the vertical axis, degrees. */
	double YawDeg = 0.0;
};

struct FPhysicsBodySpec
{
	/** Bone the body is attached to. */
	FName Bone;
	/** Share of the total mass, 0..1. */
	double MassFraction = 0.0;
	/** Template bodies whose collision shapes this body takes over (the first is usually its own bone). */
	TArray<FName> ShapesFrom;
	/** Shapes written out in the spec, added to those taken from the template. */
	TArray<FPhysicsShapeSpec> Shapes;
};

struct FPhysicsJointSpec
{
	/** Child body's bone. The parent is the nearest ancestor bone that has a body. */
	FName Child;
	FPhysicsAxisSpec Swing1;
	FPhysicsAxisSpec Swing2;
	FPhysicsAxisSpec Twist;
};

/**
 * Text description of a physics asset, read from Specs/physics/*.json (format in Specs/README.md).
 *
 * Collision shapes and joint frames are taken from a template physics asset, because fitting capsules to
 * a mesh by hand in text is error-prone. Everything that tuning touches (which bodies exist, their masses,
 * joint limits, collision pairs) comes from the spec.
 */
struct SANDBOXEDITOR_API FPhysicsAssetSpec
{
	FString Name;
	/** Long package name of the generated asset, e.g. /Game/Generated/Physics/PA_SandboxNPC. */
	FString OutputPath;
	FString SkeletalMeshPath;
	FString TemplatePath;
	double TotalMassKg = 0.0;
	TArray<FPhysicsBodySpec> Bodies;
	TArray<FPhysicsJointSpec> Joints;
	/** Extra bone pairs whose collision is switched off or on. Jointed pairs are always off. */
	TArray<TPair<FName, FName>> DisableCollision;
	TArray<TPair<FName, FName>> EnableCollision;

	const FPhysicsBodySpec* FindBody(FName Bone) const;
	const FPhysicsJointSpec* FindJoint(FName Child) const;

	static bool LoadFile(const FString& FilePath, FPhysicsAssetSpec& OutSpec, FString& OutError);

	/** Every *.json under <project>/Specs/physics. */
	static TArray<FString> FindSpecFiles();
};
