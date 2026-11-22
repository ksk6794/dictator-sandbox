#pragma once

#include "CoreMinimal.h"
#include "PhysicsAssetSpec.h"

class UPhysicsAsset;
class USkeletalMesh;

/** A joint as the generator builds it, in the mesh's reference-pose space. Both frames sit at the child bone. */
struct FPhysicsJointFrames
{
	/** Child side: X along the limb (the twist axis), Z the swing1 axis, Y the swing2 axis. */
	FTransform Child = FTransform::Identity;
	/**
	 * Parent side: the child frame turned to the middle of the three ranges. The engine's limits are
	 * symmetric around this frame, which is how a range such as 0 to 150 degrees is expressed.
	 */
	FTransform Parent = FTransform::Identity;
	/** The ranges measured from the reference pose (for a "bend" axis the spec measures from the straight limb). */
	FPhysicsAxisSpec Swing1;
	FPhysicsAxisSpec Swing2;
	FPhysicsAxisSpec Twist;
};

/** Builds a physics asset from a text spec and saves it. Deterministic: the same spec gives the same asset. */
struct SANDBOXEDITOR_API FPhysicsAssetGenerator
{
	/**
	 * Creates (or rebuilds in place) the asset at Spec.OutputPath and saves its package.
	 * Returns the asset, or null with OutError set.
	 */
	static UPhysicsAsset* Generate(const FPhysicsAssetSpec& Spec, FString& OutError);

	/**
	 * Works out the frames and ranges of the joint whose child is ChildBone, from the spec's anatomical
	 * description and the mesh's reference pose. A mirrored joint is the reflection of the written one in
	 * the body's midplane (x = 0), with the same ranges. False with a reason if the description cannot be
	 * used: a bone is missing, an axis is degenerate, or the reference pose lies outside a range.
	 */
	static bool ComputeJointFrames(const FPhysicsAssetSpec& Spec, const USkeletalMesh* Mesh, FName ChildBone, FPhysicsJointFrames& OutFrames, FString& OutError);

	/** Bone of the nearest ancestor of Bone that has a body in the spec, or NAME_None for the root body. */
	static FName FindParentBodyBone(const FPhysicsAssetSpec& Spec, const USkeletalMesh* Mesh, FName Bone);
};
