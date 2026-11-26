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

bool FPhysicsAssetGenerator::ComputeJointFrames(const FPhysicsAssetSpec& Spec, const USkeletalMesh* Mesh, FName ChildBone, FPhysicsJointFrames& OutFrames, FString& OutError)
{
	bool bMirrored = false;
	const FPhysicsJointSpec* Joint = Spec.FindJoint(ChildBone, &bMirrored);
	if (!Joint || !Mesh)
	{
		OutError = FString::Printf(TEXT("No joint is described for '%s'"), *ChildBone.ToString());
		return false;
	}

	const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
	TArray<FTransform> ComponentSpace;
	FAnimationRuntime::FillUpComponentSpaceTransforms(Skeleton, Skeleton.GetRefBonePose(), ComponentSpace);
	const auto BoneLocation = [&](FName Bone, FVector& OutLocation)
	{
		const int32 Index = Skeleton.FindBoneIndex(Bone);
		if (Index == INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("Joint '%s': bone '%s' does not exist"), *Joint->Child.ToString(), *Bone.ToString());
			return false;
		}
		OutLocation = ComponentSpace[Index].GetLocation();
		return true;
	};

	// Everything is worked out on the written side; a mirrored joint is reflected at the end.
	FVector JointLocation, AlongLocation = FVector::ZeroVector, ParentLocation, MirroredLocation;
	const FName ParentBone = FindParentBodyBone(Spec, Mesh, Joint->Child);
	if (ParentBone.IsNone())
	{
		OutError = FString::Printf(TEXT("Joint '%s' has no parent body: it is the root and must not be listed"), *Joint->Child.ToString());
		return false;
	}
	if (!BoneLocation(Joint->Child, JointLocation) || (!Joint->Along.IsNone() && !BoneLocation(Joint->Along, AlongLocation)) || !BoneLocation(ParentBone, ParentLocation) ||
		(bMirrored && !BoneLocation(ChildBone, MirroredLocation)))
	{
		return false;
	}

	const FVector TwistAxis = Joint->Along.IsNone() ? Joint->AlongDirection : (AlongLocation - JointLocation).GetSafeNormal();
	if (TwistAxis.IsNearlyZero())
	{
		OutError = FString::Printf(TEXT("Joint '%s': bone '%s' sits on the joint and gives no direction"), *Joint->Child.ToString(), *Joint->Along.ToString());
		return false;
	}

	OutFrames.Swing1 = Joint->Swing1;
	OutFrames.Swing2 = Joint->Swing2;
	OutFrames.Twist = Joint->Twist;

	FVector SwingAxis = Joint->Swing1Axis;
	if (Joint->Swing1AxisKind == EPhysicsSwingAxis::Bones)
	{
		FVector From, To;
		if (!BoneLocation(Joint->Swing1AxisFrom, From) || !BoneLocation(Joint->Swing1AxisTo, To))
		{
			return false;
		}
		SwingAxis = To - From;
	}
	else if (Joint->Swing1AxisKind == EPhysicsSwingAxis::Bend)
	{
		// The limb is already bent in the reference pose: it bends in the plane of its two segments, and
		// turning further about that plane's normal folds the child back toward the parent.
		const FVector ParentSegment = (JointLocation - ParentLocation).GetSafeNormal();
		const double BendDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(ParentSegment, TwistAxis), -1.0, 1.0)));
		if (BendDeg < 5.0)
		{
			OutError = FString::Printf(TEXT("Joint '%s': the limb is bent by only %.1f degrees in the reference pose, too little to tell its bending plane; give swing1_axis as a direction"),
				*Joint->Child.ToString(), BendDeg);
			return false;
		}
		SwingAxis = FVector::CrossProduct(ParentSegment, TwistAxis);
		// The spec measures a bending joint from the straight limb; the frames are built on the reference pose.
		OutFrames.Swing1.MinDeg -= BendDeg;
		OutFrames.Swing1.MaxDeg -= BendDeg;
	}
	// Made perpendicular to the twist axis. An axis that runs nearly along the limb cannot say which way
	// swing1 turns.
	SwingAxis = SwingAxis.GetSafeNormal();
	SwingAxis -= TwistAxis * FVector::DotProduct(SwingAxis, TwistAxis);
	if (SwingAxis.Size() < 0.2)
	{
		OutError = FString::Printf(TEXT("Joint '%s': the swing1 axis runs along the limb"), *Joint->Child.ToString());
		return false;
	}

	const TPair<const TCHAR*, const FPhysicsAxisSpec*> Axes[] = { { TEXT("swing1"), &OutFrames.Swing1 }, { TEXT("swing2"), &OutFrames.Swing2 }, { TEXT("twist"), &OutFrames.Twist } };
	for (const TPair<const TCHAR*, const FPhysicsAxisSpec*>& Axis : Axes)
	{
		// A body spawned outside its limits is thrown back inside by the first physics step.
		if (Axis.Value->Motion == ACM_Limited && (Axis.Value->MinDeg > 0.01 || Axis.Value->MaxDeg < -0.01))
		{
			OutError = FString::Printf(TEXT("Joint '%s': the reference pose lies outside the %s range (%.1f to %.1f degrees from it)"),
				*Joint->Child.ToString(), Axis.Key, Axis.Value->MinDeg, Axis.Value->MaxDeg);
			return false;
		}
	}

	// X = twist, Z = swing1, Y = Z x X = swing2: the engine's joint axes.
	const FQuat ChildRotation = FRotationMatrix::MakeFromXZ(TwistAxis, SwingAxis).ToQuat();
	const auto Middle = [](const FPhysicsAxisSpec& Axis) { return Axis.Motion == ACM_Limited ? FMath::DegreesToRadians(Axis.MidDeg()) : 0.0; };
	const FQuat ToMiddle =
		FQuat(FVector::ZAxisVector, Middle(OutFrames.Swing1)) * FQuat(FVector::YAxisVector, Middle(OutFrames.Swing2)) * FQuat(FVector::XAxisVector, Middle(OutFrames.Twist));
	OutFrames.Child = FTransform(ChildRotation, JointLocation);
	OutFrames.Parent = FTransform(ChildRotation * ToMiddle, JointLocation);

	if (bMirrored)
	{
		// Reflection in the plane x = 0. A rotation axis is reflected and reversed, which for the three
		// axes of a frame together is half a turn about x; the ranges then carry over unchanged.
		const FQuat HalfTurn(FVector::XAxisVector, UE_DOUBLE_PI);
		OutFrames.Child = FTransform(HalfTurn * OutFrames.Child.GetRotation(), MirroredLocation);
		OutFrames.Parent = FTransform(HalfTurn * OutFrames.Parent.GetRotation(), MirroredLocation);
	}
	return true;
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
	// Which new body each template body ended up in: a template body may give its shapes to one body only.
	TMap<int32, int32> TemplateToNewBody;
	// The engine's default switches collision off between a new body and every body made before it, which
	// left the whole asset without self-collision: the table below only ever takes pairs away. Bodies start
	// out colliding with each other, and the table decides which pairs do not.
	FPhysAssetCreateParams CreateParams;
	CreateParams.bDisableCollisionsByDefault = false;
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
		// Shapes written out in the spec. They are given in the mesh's reference-pose space, where the
		// floor is z = 0, so that a sole can be laid flat on it whatever the bone's own orientation is.
		for (const FPhysicsShapeSpec& Shape : BodySpec.Shapes)
		{
			if (Shape.Kind == EPhysicsShapeKind::Sphere)
			{
				FKSphereElem Sphere(static_cast<float>(Shape.RadiusCm));
				Sphere.SetTransform(FTransform(Shape.CenterCm).GetRelativeTransform(BodyBone));
				Body->AggGeom.SphereElems.Add(Sphere);
				continue;
			}
			if (Shape.Kind == EPhysicsShapeKind::Capsule)
			{
				const FVector Way = Shape.ToCm - Shape.FromCm;
				FKSphylElem Capsule(static_cast<float>(Shape.RadiusCm), static_cast<float>(Way.Size()));
				Capsule.SetTransform(FTransform(FQuat::FindBetweenVectors(FVector::UpVector, Way), Shape.CenterCm).GetRelativeTransform(BodyBone));
				Body->AggGeom.SphylElems.Add(Capsule);
				continue;
			}
			const FTransform ShapeInMesh(FRotator(0.0, Shape.YawDeg, 0.0), Shape.CenterCm);
			FKBoxElem Box(static_cast<float>(Shape.SizeCm.X), static_cast<float>(Shape.SizeCm.Y), static_cast<float>(Shape.SizeCm.Z));
			Box.SetTransform(ShapeInMesh.GetRelativeTransform(BodyBone));
			Body->AggGeom.BoxElems.Add(Box);
		}
		if (Body->AggGeom.GetElementCount() == 0)
		{
			OutError = FString::Printf(TEXT("Body '%s' ended up with no collision shapes"), *BodySpec.Bone.ToString());
			return nullptr;
		}
		// What the body collides with, in the space its shapes are written in: the way to see a template's shapes.
		{
			FString Shapes;
			const auto Add = [&Shapes, &BodyBone](const TCHAR* Kind, const FTransform& InBody, const FString& Size)
			{
				const FTransform InMesh = InBody * BodyBone;
				const FVector Center = InMesh.GetLocation();
				const FVector Up = InMesh.GetRotation().GetUpVector();
				Shapes += FString::Printf(TEXT(" %s %s at (%.2f, %.2f, %.2f) up (%.2f, %.2f, %.2f);"), Kind, *Size, Center.X, Center.Y, Center.Z, Up.X, Up.Y, Up.Z);
			};
			for (const FKSphereElem& Elem : Body->AggGeom.SphereElems) { Add(TEXT("sphere"), Elem.GetTransform(), FString::Printf(TEXT("r %.2f"), Elem.Radius)); }
			for (const FKSphylElem& Elem : Body->AggGeom.SphylElems) { Add(TEXT("capsule"), Elem.GetTransform(), FString::Printf(TEXT("r %.2f length %.2f"), Elem.Radius, Elem.Length)); }
			for (const FKBoxElem& Elem : Body->AggGeom.BoxElems) { Add(TEXT("box"), Elem.GetTransform(), FString::Printf(TEXT("%.2f x %.2f x %.2f"), Elem.X, Elem.Y, Elem.Z)); }
			UE_LOG(LogTemp, Display, TEXT("Physics asset '%s': body '%s' at (%.2f, %.2f, %.2f), %d other shapes:%s"), *Spec.Name, *BodySpec.Bone.ToString(),
				BodyBone.GetLocation().X, BodyBone.GetLocation().Y, BodyBone.GetLocation().Z, Body->AggGeom.TaperedCapsuleElems.Num() + Body->AggGeom.ConvexElems.Num(), *Shapes);
		}

		Body->DefaultInstance.SetMassOverride(static_cast<float>(BodySpec.MassFraction * Spec.TotalMassKg), true);
		Body->DefaultInstance.InertiaTensorScale = FVector(BodySpec.InertiaScale);
		Body->DefaultInstance.bUseCCD = BodySpec.bCcd;
	}

	// ---- Joints: axes and limits from the spec; softness and projection settings from the template's joint.
	TSet<FRigidBodyIndexPair> JointedPairs;
	for (const FPhysicsBodySpec& BodySpec : Spec.Bodies)
	{
		const FName ParentBone = FindParentBodyBone(Spec, Mesh, BodySpec.Bone);
		if (ParentBone.IsNone())
		{
			continue; // the root body has no joint
		}
		if (!Spec.FindJoint(BodySpec.Bone))
		{
			OutError = FString::Printf(TEXT("Body '%s' has a parent body ('%s') but no entry in 'joints'"), *BodySpec.Bone.ToString(), *ParentBone.ToString());
			return nullptr;
		}
		FPhysicsJointFrames Frames;
		if (!ComputeJointFrames(Spec, Mesh, BodySpec.Bone, Frames, OutError))
		{
			return nullptr;
		}
		const UPhysicsConstraintTemplate* TemplateJoint = FindRigidTemplateJoint(Template, BodySpec.Bone);
		if (!TemplateJoint)
		{
			OutError = FString::Printf(TEXT("Template has no rigid joint whose child is '%s'"), *BodySpec.Bone.ToString());
			return nullptr;
		}

		// Passing the template joint copies its profile (limit softness, projection); frames, limits and
		// drives are replaced below.
		const int32 JointIndex = FPhysicsAssetUtils::CreateNewConstraint(Asset, BodySpec.Bone, const_cast<UPhysicsConstraintTemplate*>(TemplateJoint));
		UPhysicsConstraintTemplate* Joint = Asset->ConstraintSetup[JointIndex];
		FConstraintInstance& Instance = Joint->DefaultInstance;
		Instance.ConstraintBone1 = BodySpec.Bone;
		Instance.ConstraintBone2 = ParentBone;

		// The frames are the generator's own, not the template's. The stock MetaHuman asset places its
		// joints for another body's proportions (anchors centimetres apart on this mesh, which the solver
		// would close in the first step) and its axes are not mirror images on the two sides. Both anchors
		// sit on the child bone, so they coincide in the reference pose by construction.
		{
			FTransform ChildBone, NewParentBone;
			if (!BoneTransform(BodySpec.Bone, ChildBone) || !BoneTransform(ParentBone, NewParentBone))
			{
				OutError = FString::Printf(TEXT("Joint '%s': cannot resolve its bones"), *BodySpec.Bone.ToString());
				return nullptr;
			}
			Instance.SetRefFrame(EConstraintFrame::Frame1, Frames.Child.GetRelativeTransform(ChildBone));
			Instance.SetRefFrame(EConstraintFrame::Frame2, Frames.Parent.GetRelativeTransform(NewParentBone));
		}

		// The stock MetaHuman joints carry orientation motors (SLERP drives, stiffness 1000 to 5000) that pull
		// each joint toward the zero of its frames, which is not the reference pose: left in, they flex the
		// elbows and knees of a simulated body. The generated asset has no motors of its own; muscles are
		// added at run time by USandboxBodyController.
		Instance.ProfileInstance.AngularDrive = FAngularDriveConstraint();
		Instance.ProfileInstance.LinearDrive = FLinearDriveConstraint();

		Instance.SetLinearLimits(LCM_Locked, LCM_Locked, LCM_Locked, 0.0f);
		Instance.SetAngularSwing1Limit(Frames.Swing1.Motion, static_cast<float>(Frames.Swing1.HalfRangeDeg()));
		Instance.SetAngularSwing2Limit(Frames.Swing2.Motion, static_cast<float>(Frames.Swing2.HalfRangeDeg()));
		Instance.SetAngularTwistLimit(Frames.Twist.Motion, static_cast<float>(Frames.Twist.HalfRangeDeg()));

		// What the joint does at the end of its range is the spec's, not the template's. (The stock MetaHuman
		// asset has every limit soft, stiffness 50: a limp wrist bent 40 degrees past its range, a relaxed hip
		// 23, a neck 15.)
		{
			const FPhysicsJointSpec* JointSpec = Spec.FindJoint(BodySpec.Bone);
			for (FConstraintBaseParams* Limit : { static_cast<FConstraintBaseParams*>(&Instance.ProfileInstance.ConeLimit), static_cast<FConstraintBaseParams*>(&Instance.ProfileInstance.TwistLimit) })
			{
				Limit->bSoftConstraint = JointSpec->bSoftLimit;
				Limit->Stiffness = static_cast<float>(JointSpec->bSoftLimit ? JointSpec->LimitStiffness : 0.0);
				Limit->Damping = static_cast<float>(JointSpec->bSoftLimit ? JointSpec->LimitDamping : 0.0);
			}
		}
		Joint->SetDefaultProfile(Instance);

		JointedPairs.Add(FRigidBodyIndexPair(Asset->FindBodyIndex(BodySpec.Bone), Asset->FindBodyIndex(ParentBone)));
	}
	for (const FPhysicsJointSpec& JointSpec : Spec.Joints)
	{
		if (FindParentBodyBone(Spec, Mesh, JointSpec.Child).IsNone() || (!JointSpec.Mirror.IsNone() && FindParentBodyBone(Spec, Mesh, JointSpec.Mirror).IsNone()))
		{
			OutError = FString::Printf(TEXT("Joint '%s' has no parent body: it is the root and must not be listed"), *JointSpec.Child.ToString());
			return nullptr;
		}
	}

	// ---- Collision pairs.
	// Jointed bodies never collide. Every other pair does, unless the spec switches it off: that is for
	// bodies whose shapes overlap in the reference pose and would push each other apart from the first step.
	// The template's own table is not carried over: it is not the same on the left and on the right.
	TSet<FRigidBodyIndexPair> Disabled = JointedPairs;
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
	for (const FRigidBodyIndexPair& Pair : Disabled)
	{
		Asset->DisableCollision(Pair.Indices[0], Pair.Indices[1]);
	}

	// Report colliding pairs whose bounds already overlap in the reference pose. Bounding boxes overstate
	// capsules, so a small overlap here may be a false alarm; a deep one means the two bodies push each
	// other apart from the first step on.
	{
		TArray<FBox> Bounds;
		for (const USkeletalBodySetup* Body : Asset->SkeletalBodySetups)
		{
			FTransform Bone;
			BoneTransform(Body->BoneName, Bone);
			Bounds.Add(Body->AggGeom.CalcAABB(Bone));
		}
		for (int32 A = 0; A < Bounds.Num(); ++A)
		{
			for (int32 B = A + 1; B < Bounds.Num(); ++B)
			{
				if (Disabled.Contains(FRigidBodyIndexPair(A, B)) || !Bounds[A].Intersect(Bounds[B]))
				{
					continue;
				}
				const FVector Depth = Bounds[A].Overlap(Bounds[B]).GetSize();
				UE_LOG(LogTemp, Display, TEXT("Physics asset '%s': colliding bodies '%s' and '%s' have bounds that overlap by %.1f cm in the reference pose"),
					*Spec.Name, *Asset->SkeletalBodySetups[A]->BoneName.ToString(), *Asset->SkeletalBodySetups[B]->BoneName.ToString(), Depth.GetMin());
			}
		}
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
