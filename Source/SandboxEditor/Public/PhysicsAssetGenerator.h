#pragma once

#include "CoreMinimal.h"

class UPhysicsAsset;
class USkeletalMesh;
struct FPhysicsAssetSpec;

/** Builds a physics asset from a text spec and saves it. Deterministic: the same spec gives the same asset. */
struct SANDBOXEDITOR_API FPhysicsAssetGenerator
{
	/**
	 * Creates (or rebuilds in place) the asset at Spec.OutputPath and saves its package.
	 * Returns the asset, or null with OutError set.
	 */
	static UPhysicsAsset* Generate(const FPhysicsAssetSpec& Spec, FString& OutError);

	/** Bone of the nearest ancestor of Bone that has a body in the spec, or NAME_None for the root body. */
	static FName FindParentBodyBone(const FPhysicsAssetSpec& Spec, const USkeletalMesh* Mesh, FName Bone);
};
