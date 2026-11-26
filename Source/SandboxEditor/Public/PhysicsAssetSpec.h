#pragma once

#include "CoreMinimal.h"
#include "PhysicsEngine/ConstraintTypes.h"

/**
 * One angular axis of a joint: locked, free, or limited to a range of degrees that need not be symmetric
 * (a knee bends from 0 to 150). What the angles are measured from is said at FPhysicsJointSpec.
 */
struct FPhysicsAxisSpec
{
	EAngularConstraintMotion Motion = ACM_Locked;
	double MinDeg = 0.0;
	double MaxDeg = 0.0;

	/** The engine's limits are symmetric: plus or minus this much around the middle of the range. */
	double HalfRangeDeg() const { return (MaxDeg - MinDeg) * 0.5; }
	double MidDeg() const { return (MaxDeg + MinDeg) * 0.5; }
};

enum class EPhysicsShapeKind : uint8
{
	Box,
	Sphere,
	Capsule
};

/**
 * A collision shape written out in the spec, in the mesh's reference-pose space (floor at z = 0, z up).
 * Used where the template's shapes do not do the job: a foot needs a flat sole on the floor, a head needs a face.
 */
struct FPhysicsShapeSpec
{
	EPhysicsShapeKind Kind = EPhysicsShapeKind::Box;
	FVector CenterCm = FVector::ZeroVector;
	/** Box: full lengths along the box's own x, y and z. */
	FVector SizeCm = FVector::ZeroVector;
	/** Box: rotation about the vertical axis, degrees. */
	double YawDeg = 0.0;
	/** Sphere and capsule. */
	double RadiusCm = 0.0;
	/** Capsule: the ends of the line its round side is drawn around; the centre is the middle of the two. */
	FVector FromCm = FVector::ZeroVector;
	FVector ToCm = FVector::ZeroVector;
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
	/** Factor on the inertia the engine works out from the mass and the shapes (FBodyInstance::InertiaTensorScale). */
	double InertiaScale = 1.0;
	/** Continuous collision detection for this body ("ccd"): swept against the world when it moves fast, so that it does not pass into it between two steps. */
	bool bCcd = false;
};

/** How a joint's swing1 axis (the one its main bending turns about) is found. */
enum class EPhysicsSwingAxis : uint8
{
	/** A direction written out in the mesh's reference-pose space. Angles are measured from the reference pose. */
	Vector,
	/** The direction from one bone to another in the reference pose. Angles are measured from the reference pose. */
	Bones,
	/**
	 * The normal of the plane in which the limb is already bent in the reference pose (an elbow in an
	 * A-pose). Swing1 angles are then measured from the straight limb, and positive bends it further.
	 */
	Bend
};

/**
 * One joint, described by its anatomy instead of by a frame:
 *
 *   twist   turns about the limb's own length, from the joint toward the bone `Along`
 *   swing1  turns about the swing1 axis (made perpendicular to the twist axis)
 *   swing2  turns about the third axis, swing1 axis x twist axis
 *
 * A positive angle turns the child body about its axis the way FQuat(Axis, Angle) does. With `Mirror`
 * the joint of the opposite side is generated as the exact mirror image, with the same ranges.
 */
struct FPhysicsJointSpec
{
	/** Child body's bone. The parent is the nearest ancestor bone that has a body. */
	FName Child;
	/** Child bone of the mirrored joint on the other side of the body; none for joints on the midline. */
	FName Mirror;
	/** The twist axis points from the child bone toward this bone. None if a direction is given instead. */
	FName Along;
	/** The twist axis as a direction in the mesh's reference-pose space, for a limb with no bone beyond it (the head). */
	FVector AlongDirection = FVector::ZeroVector;

	EPhysicsSwingAxis Swing1AxisKind = EPhysicsSwingAxis::Vector;
	FVector Swing1Axis = FVector::ZeroVector;
	FName Swing1AxisFrom;
	FName Swing1AxisTo;

	FPhysicsAxisSpec Swing1;
	FPhysicsAxisSpec Swing2;
	FPhysicsAxisSpec Twist;

	/**
	 * What the joint does at the end of its range ("limit"). Hard: it stops there, as a hinge of bone does. Soft:
	 * it gives, held back by a spring and a damper (the engine's stiffness and damping of a soft limit, the same
	 * for the cone of the two swings and for the twist), as where ligaments and muscle hold a joint.
	 */
	bool bSoftLimit = false;
	double LimitStiffness = 0.0;
	double LimitDamping = 0.0;

	/**
	 * "limits_in": "child". The two swings are limited about the child's own axes, after the twist, instead of about
	 * the parent's, before it: a hand turned palm down bends back by its own range and not by the range of the
	 * sideways bend. The engine's solver limits the swings in the frame of the joint's second body. So the asset gets two
	 * joints for this one: the rigid one as any other, which limits the twist and leaves the swings free, and beside it
	 * one that holds no place, only the two swings, with the bodies the other way round ("<child>_swings").
	 */
	bool bLimitsInChild = false;
};

/**
 * Text description of a physics asset, read from Specs/physics/*.json (format in Specs/README.md).
 *
 * Collision shapes are taken from a template physics asset, because fitting capsules to a mesh by hand in
 * text is error-prone. Everything that tuning touches (which bodies exist, their masses, joint axes and
 * limits, collision pairs) comes from the spec.
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
	/** One entry per written joint; a mirrored joint has no entry of its own. */
	TArray<FPhysicsJointSpec> Joints;
	/**
	 * Bone pairs that do not collide with each other, besides the jointed ones, which never do. Every other
	 * pair of bodies collides.
	 */
	TArray<TPair<FName, FName>> DisableCollision;

	const FPhysicsBodySpec* FindBody(FName Bone) const;
	/** The joint whose child, or mirrored child, is the bone. bOutMirrored tells which. */
	const FPhysicsJointSpec* FindJoint(FName Child, bool* bOutMirrored = nullptr) const;

	static bool LoadFile(const FString& FilePath, FPhysicsAssetSpec& OutSpec, FString& OutError);

	/** Every *.json under <project>/Specs/physics. */
	static TArray<FString> FindSpecFiles();
};
