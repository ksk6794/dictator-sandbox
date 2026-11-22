#include "PhysicsAssetGenerator.h"

#include "AnimationRuntime.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/PackageName.h"
#include "PhysicsAssetSpec.h"
#include "PhysicsAssetUtils.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "ReferenceSkeleton.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	/** Re-expresses every element of an array of collision shapes in another bone's space. */
	template <typename ElemType>
	void AppendTransformed(TArray<ElemType>& Destination, const TArray<ElemType>& Source, const FTransform& SourceToDestination)
	{
		for (const ElemType& Elem : Source)
		{
			ElemType Copy = Elem;
			Copy.SetTransform(Elem.GetTransform() * SourceToDestination);
			Destination.Add(Copy);
		}
	}

	const UPhysicsConstraintTemplate* FindRigidTemplateJoint(const UPhysicsAsset* Template, FName ChildBone)
	{
		for (const UPhysicsConstraintTemplate* Constraint : Template->ConstraintSetup)
		{
			if (!Constraint)
			{
				continue;
			}
			const FConstraintInstance& Instance = Constraint->DefaultInstance;
			const bool bRigid =
				Instance.GetLinearXMotion() == LCM_Locked && Instance.GetLinearYMotion() == LCM_Locked && Instance.GetLinearZMotion() == LCM_Locked;
			if (bRigid && Instance.ConstraintBone1 == ChildBone)
			{
				return Constraint;
			}
		}
		return nullptr;
	}
}

FName FPhysicsAssetGenerator::FindParentBodyBone(const FPhysicsAssetSpec& Spec, const USkeletalMesh* Mesh, FName Bone)
{
	const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
	int32 Index = Skeleton.FindBoneIndex(Bone);
	while (Index != INDEX_NONE)
	{
		Index = Skeleton.GetParentIndex(Index);
		if (Index != INDEX_NONE && Spec.FindBody(Skeleton.GetBoneName(Index)))
		{
			return Skeleton.GetBoneName(Index);
		}
	}
	return NAME_None;
}

UPhysicsAsset* FPhysicsAssetGenerator::Generate(const FPhysicsAssetSpec& Spec, FString& OutError)
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *Spec.SkeletalMeshPath);
	if (!Mesh)
	{
		OutError = FString::Printf(TEXT("Cannot load skeletal mesh '%s'"), *Spec.SkeletalMeshPath);
		return nullptr;
	}
	const UPhysicsAsset* Template = LoadObject<UPhysicsAsset>(nullptr, *Spec.TemplatePath);
	if (!Template)
	{
		OutError = FString::Printf(TEXT("Cannot load template physics asset '%s'"), *Spec.TemplatePath);
		return nullptr;
	}

	// Reference pose in component space: used to move shapes and joint frames between bones.
	const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
	TArray<FTransform> ComponentSpace;
	FAnimationRuntime::FillUpComponentSpaceTransforms(Skeleton, Skeleton.GetRefBonePose(), ComponentSpace);
	const auto BoneTransform = [&Skeleton, &ComponentSpace](FName Bone, FTransform& OutTransform)
	{
		const int32 Index = Skeleton.FindBoneIndex(Bone);
		if (Index == INDEX_NONE)
		{
			return false;
		}
		OutTransform = ComponentSpace[Index];
		return true;
	};

	// Create the asset, or empty the existing one so regeneration keeps the same object and references.
	const FString AssetName = FPackageName::GetLongPackageAssetName(Spec.OutputPath);
	UPackage* Package = CreatePackage(*Spec.OutputPath);
	Package->FullyLoad();
	UPhysicsAsset* Asset = FindObject<UPhysicsAsset>(Package, *AssetName);
	const bool bCreated = Asset == nullptr;
	if (bCreated)
	{
		Asset = NewObject<UPhysicsAsset>(Package, *AssetName, RF_Public | RF_Standalone);
	}
	Asset->SkeletalBodySetups.Reset();
	Asset->ConstraintSetup.Reset();
	Asset->CollisionDisableTable.Reset();
	Asset->UpdateBodySetupIndexMap();

	// ---- Bodies: shapes from the template, mass from the spec.
	// Which new body each template body ended up in, for carrying over the collision table.
	TMap<int32, int32> TemplateToNewBody;
	const FPhysAssetCreateParams CreateParams;
	for (const FPhysicsBodySpec& BodySpec : Spec.Bodies)
	{
		FTransform BodyBone;
		if (!BoneTransform(BodySpec.Bone, BodyBone))
		{
			OutError = FString::Printf(TEXT("Bone '%s' does not exist in '%s'"), *BodySpec.Bone.ToString(), *Spec.SkeletalMeshPath);
			return nullptr;
		}

		const int32 BodyIndex = FPhysicsAssetUtils::CreateNewBody(Asset, BodySpec.Bone, CreateParams);
		USkeletalBodySetup* Body = Asset->SkeletalBodySetups[BodyIndex];
		Body->AggGeom = FKAggregateGeom();

		for (const FName SourceBone : BodySpec.ShapesFrom)
		{
			const int32 SourceIndex = Template->FindBodyIndex(SourceBone);
			FTransform SourceBoneTransform;
			if (SourceIndex == INDEX_NONE || !BoneTransform(SourceBone, SourceBoneTransform))
			{
				OutError = FString::Printf(TEXT("Body '%s': template has no body on bone '%s'"), *BodySpec.Bone.ToString(), *SourceBone.ToString());
				return nullptr;
			}
			if (TemplateToNewBody.Contains(SourceIndex))
			{
				OutError = FString::Printf(TEXT("Template body '%s' is used by more than one body"), *SourceBone.ToString());
				return nullptr;
			}
			TemplateToNewBody.Add(SourceIndex, BodyIndex);

			const USkeletalBodySetup* Source = Template->SkeletalBodySetups[SourceIndex];
			const FTransform SourceToBody = SourceBoneTransform.GetRelativeTransform(BodyBone);
			AppendTransformed(Body->AggGeom.SphereElems, Source->AggGeom.SphereElems, SourceToBody);
			AppendTransformed(Body->AggGeom.BoxElems, Source->AggGeom.BoxElems, SourceToBody);
			AppendTransformed(Body->AggGeom.SphylElems, Source->AggGeom.SphylElems, SourceToBody);
			AppendTransformed(Body->AggGeom.TaperedCapsuleElems, Source->AggGeom.TaperedCapsuleElems, SourceToBody);
			AppendTransformed(Body->AggGeom.ConvexElems, Source->AggGeom.ConvexElems, SourceToBody);
			if (Body->PhysMaterial == nullptr)
			{
				Body->PhysMaterial = Source->PhysMaterial;
			}
		}
		if (Body->AggGeom.GetElementCount() == 0)
		{
			OutError = FString::Printf(TEXT("Body '%s' ended up with no collision shapes"), *BodySpec.Bone.ToString());
			return nullptr;
		}

		Body->DefaultInstance.SetMassOverride(static_cast<float>(BodySpec.MassFraction * Spec.TotalMassKg), true);
	}

	// ---- Joints: frames from the template, limits from the spec.
	TSet<FRigidBodyIndexPair> JointedPairs;
	for (const FPhysicsBodySpec& BodySpec : Spec.Bodies)
	{
		const FName ParentBone = FindParentBodyBone(Spec, Mesh, BodySpec.Bone);
		if (ParentBone.IsNone())
		{
			continue; // the root body has no joint
		}
		const FPhysicsJointSpec* JointSpec = Spec.FindJoint(BodySpec.Bone);
		if (!JointSpec)
		{
			OutError = FString::Printf(TEXT("Body '%s' has a parent body ('%s') but no entry in 'joints'"), *BodySpec.Bone.ToString(), *ParentBone.ToString());
			return nullptr;
		}
		const UPhysicsConstraintTemplate* TemplateJoint = FindRigidTemplateJoint(Template, BodySpec.Bone);
		if (!TemplateJoint)
		{
			OutError = FString::Printf(TEXT("Template has no rigid joint whose child is '%s'"), *BodySpec.Bone.ToString());
			return nullptr;
		}

		// Passing the template joint copies its frames and drive settings.
		const int32 JointIndex = FPhysicsAssetUtils::CreateNewConstraint(Asset, BodySpec.Bone, const_cast<UPhysicsConstraintTemplate*>(TemplateJoint));
		UPhysicsConstraintTemplate* Joint = Asset->ConstraintSetup[JointIndex];
		FConstraintInstance& Instance = Joint->DefaultInstance;
		Instance.ConstraintBone1 = BodySpec.Bone;
		Instance.ConstraintBone2 = ParentBone;

		// If the parent body changed (a template body was merged away), move the parent-side frame into
		// the new parent's space so the joint keeps its rest orientation.
		const FName TemplateParent = TemplateJoint->DefaultInstance.ConstraintBone2;
		if (TemplateParent != ParentBone)
		{
			FTransform OldParent, NewParent;
			if (!BoneTransform(TemplateParent, OldParent) || !BoneTransform(ParentBone, NewParent))
			{
				OutError = FString::Printf(TEXT("Joint '%s': cannot resolve parent bones"), *BodySpec.Bone.ToString());
				return nullptr;
			}
			const FTransform Frame2 = TemplateJoint->DefaultInstance.GetRefFrame(EConstraintFrame::Frame2);
			Instance.SetRefFrame(EConstraintFrame::Frame2, (Frame2 * OldParent).GetRelativeTransform(NewParent));
		}

		Instance.SetLinearLimits(LCM_Locked, LCM_Locked, LCM_Locked, 0.0f);
		Instance.SetAngularSwing1Limit(JointSpec->Swing1.Motion, static_cast<float>(JointSpec->Swing1.LimitDeg));
		Instance.SetAngularSwing2Limit(JointSpec->Swing2.Motion, static_cast<float>(JointSpec->Swing2.LimitDeg));
		Instance.SetAngularTwistLimit(JointSpec->Twist.Motion, static_cast<float>(JointSpec->Twist.LimitDeg));
		Joint->SetDefaultProfile(Instance);

		JointedPairs.Add(FRigidBodyIndexPair(Asset->FindBodyIndex(BodySpec.Bone), Asset->FindBodyIndex(ParentBone)));
	}
	for (const FPhysicsJointSpec& JointSpec : Spec.Joints)
	{
		if (FindParentBodyBone(Spec, Mesh, JointSpec.Child).IsNone())
		{
			OutError = FString::Printf(TEXT("Joint '%s' has no parent body: it is the root and must not be listed"), *JointSpec.Child.ToString());
			return nullptr;
		}
	}

	// ---- Collision pairs.
	// Jointed bodies never collide. Other pairs inherit "off" if any pair of the template bodies they were
	// built from was off (their shapes may overlap at rest). The spec can then switch pairs off or on.
	TSet<FRigidBodyIndexPair> Disabled = JointedPairs;
	for (const TPair<int32, int32>& A : TemplateToNewBody)
	{
		for (const TPair<int32, int32>& B : TemplateToNewBody)
		{
			if (A.Value < B.Value && !Template->IsCollisionEnabled(A.Key, B.Key))
			{
				Disabled.Add(FRigidBodyIndexPair(A.Value, B.Value));
			}
		}
	}
	const auto ResolvePair = [&](const TPair<FName, FName>& Pair, FRigidBodyIndexPair& OutPair)
	{
		const int32 A = Asset->FindBodyIndex(Pair.Key);
		const int32 B = Asset->FindBodyIndex(Pair.Value);
		if (A == INDEX_NONE || B == INDEX_NONE || A == B)
		{
			OutError = FString::Printf(TEXT("Collision pair [%s, %s] does not name two different bodies"), *Pair.Key.ToString(), *Pair.Value.ToString());
			return false;
		}
		OutPair = FRigidBodyIndexPair(A, B);
		return true;
	};
	for (const TPair<FName, FName>& Pair : Spec.DisableCollision)
	{
		FRigidBodyIndexPair IndexPair(0, 0);
		if (!ResolvePair(Pair, IndexPair)) { return nullptr; }
		Disabled.Add(IndexPair);
	}
	for (const TPair<FName, FName>& Pair : Spec.EnableCollision)
	{
		FRigidBodyIndexPair IndexPair(0, 0);
		if (!ResolvePair(Pair, IndexPair)) { return nullptr; }
		if (JointedPairs.Contains(IndexPair))
		{
			OutError = FString::Printf(TEXT("Collision cannot be enabled between jointed bodies [%s, %s]"), *Pair.Key.ToString(), *Pair.Value.ToString());
			return nullptr;
		}
		Disabled.Remove(IndexPair);
	}
	for (const FRigidBodyIndexPair& Pair : Disabled)
	{
		Asset->DisableCollision(Pair.Indices[0], Pair.Indices[1]);
	}

	Asset->UpdateBodySetupIndexMap();
	Asset->UpdateBoundsBodiesArray();
	Asset->SetPreviewMesh(Mesh, false);

	// ---- Save.
	if (bCreated)
	{
		FAssetRegistryModule::AssetCreated(Asset);
	}
	Package->MarkPackageDirty();
	const FString FileName = FPackageName::LongPackageNameToFilename(Spec.OutputPath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!UPackage::SavePackage(Package, Asset, *FileName, SaveArgs))
	{
		OutError = FString::Printf(TEXT("Failed to save '%s'"), *FileName);
		return nullptr;
	}
	return Asset;
}
