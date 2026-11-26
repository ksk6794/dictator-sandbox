#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AnimationRuntime.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PhysicsAssetGenerator.h"
#include "PhysicsAssetSpec.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsEngine/SkeletalBodySetup.h"

/**
 * Every generated physics asset on disk must match its spec: bodies, masses, joint parents and limits.
 * Run Tools\gen-assets.ps1 first; a missing asset is a failure.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeneratedPhysicsAssetMatchesSpecTest, "DictatorSandbox.Generated.PhysicsAsset.MatchesSpec",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FGeneratedPhysicsAssetMatchesSpecTest::RunTest(const FString& Parameters)
{
	const TArray<FString> SpecFiles = FPhysicsAssetSpec::FindSpecFiles();
	TestTrue(TEXT("at least one physics spec exists"), SpecFiles.Num() > 0);

	for (const FString& SpecFile : SpecFiles)
	{
		const FString Label = FPaths::GetBaseFilename(SpecFile);
		FPhysicsAssetSpec Spec;
		FString Error;
		if (!FPhysicsAssetSpec::LoadFile(SpecFile, Spec, Error))
		{
			AddError(Error);
			continue;
		}

		const FString ObjectPath = Spec.OutputPath + TEXT(".") + FPackageName::GetLongPackageAssetName(Spec.OutputPath);
		const UPhysicsAsset* Asset = LoadObject<UPhysicsAsset>(nullptr, *ObjectPath);
		const USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *Spec.SkeletalMeshPath);
		if (!Asset || !Mesh)
		{
			AddError(FString::Printf(TEXT("%s: generated asset '%s' or its mesh is missing. Run Tools\\gen-assets.ps1."), *Label, *ObjectPath));
			continue;
		}

		const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
		TArray<FTransform> ComponentSpace;
		FAnimationRuntime::FillUpComponentSpaceTransforms(Skeleton, Skeleton.GetRefBonePose(), ComponentSpace);

		TestEqual(Label + TEXT(": body count"), Asset->SkeletalBodySetups.Num(), Spec.Bodies.Num());
		TestEqual(Label + TEXT(": joint count"), Asset->ConstraintSetup.Num(), Spec.Bodies.Num() - 1);

		// The body must be able to collide with itself. An asset with every pair switched off passes through
		// itself (an arm through the chest), and nothing else here would notice: that is how the asset was
		// generated until M3. Left and right must agree, and the pairs the spec names must be as it says.
		const auto Mirrored = [](FName Bone)
		{
			const FString Name = Bone.ToString();
			return Name.EndsWith(TEXT("_l")) ? FName(*(Name.LeftChop(2) + TEXT("_r"))) : Name.EndsWith(TEXT("_r")) ? FName(*(Name.LeftChop(2) + TEXT("_l"))) : Bone;
		};
		int32 CollidingPairs = 0;
		for (int32 A = 0; A < Asset->SkeletalBodySetups.Num(); ++A)
		{
			for (int32 B = A + 1; B < Asset->SkeletalBodySetups.Num(); ++B)
			{
				const bool bCollide = Asset->IsCollisionEnabled(A, B);
				CollidingPairs += bCollide ? 1 : 0;
				const int32 MirrorA = Asset->FindBodyIndex(Mirrored(Asset->SkeletalBodySetups[A]->BoneName));
				const int32 MirrorB = Asset->FindBodyIndex(Mirrored(Asset->SkeletalBodySetups[B]->BoneName));
				if (MirrorA != INDEX_NONE && MirrorB != INDEX_NONE && MirrorA != MirrorB)
				{
					TestEqual(FString::Printf(TEXT("%s: %s and %s collide as their mirror images do"), *Label,
						*Asset->SkeletalBodySetups[A]->BoneName.ToString(), *Asset->SkeletalBodySetups[B]->BoneName.ToString()), Asset->IsCollisionEnabled(MirrorA, MirrorB), bCollide);
				}
			}
		}
		// Every pair collides except the jointed ones and those the spec switches off.
		const int32 Bodies = Asset->SkeletalBodySetups.Num();
		TestEqual(Label + TEXT(": pairs of bodies that can collide"), CollidingPairs, Bodies * (Bodies - 1) / 2 - (Spec.Bodies.Num() - 1) - Spec.DisableCollision.Num());
		for (const TPair<FName, FName>& Pair : Spec.DisableCollision)
		{
			TestFalse(FString::Printf(TEXT("%s: %s and %s do not collide, as the spec says"), *Label, *Pair.Key.ToString(), *Pair.Value.ToString()),
				Asset->IsCollisionEnabled(Asset->FindBodyIndex(Pair.Key), Asset->FindBodyIndex(Pair.Value)));
		}

		double TotalMass = 0.0;
		for (const FPhysicsBodySpec& BodySpec : Spec.Bodies)
		{
			const FString BodyLabel = FString::Printf(TEXT("%s: body %s"), *Label, *BodySpec.Bone.ToString());
			const int32 Index = Asset->FindBodyIndex(BodySpec.Bone);
			if (!TestTrue(BodyLabel + TEXT(" exists"), Index != INDEX_NONE))
			{
				continue;
			}
			const USkeletalBodySetup* Body = Asset->SkeletalBodySetups[Index];
			TestTrue(BodyLabel + TEXT(" overrides its mass"), Body->DefaultInstance.bOverrideMass);
			TestEqual(BodyLabel + TEXT(" mass"), static_cast<double>(Body->DefaultInstance.GetMassOverride()), BodySpec.MassFraction * Spec.TotalMassKg, 1.0e-3);
			TestEqual(BodyLabel + TEXT(" inertia scale"), FVector(Body->DefaultInstance.InertiaTensorScale), FVector(BodySpec.InertiaScale), 1.0e-6f);
			TestEqual(BodyLabel + TEXT(" continuous collision detection"), Body->DefaultInstance.bUseCCD != 0, BodySpec.bCcd);
			TestTrue(BodyLabel + TEXT(" has collision shapes"), Body->AggGeom.GetElementCount() > 0);
			TestTrue(BodyLabel + TEXT(" has every shape written out in the spec"), Body->AggGeom.GetElementCount() >= BodySpec.Shapes.Num());
			if (BodySpec.ShapesFrom.Num() == 0)
			{
				TestEqual(BodyLabel + TEXT(" has only the shapes written out in the spec"), Body->AggGeom.GetElementCount(), BodySpec.Shapes.Num());
			}
			TotalMass += Body->DefaultInstance.GetMassOverride();
		}
		TestEqual(Label + TEXT(": total mass"), TotalMass, Spec.TotalMassKg, 1.0e-2);

		// Every body but the root has a joint: one written in the spec, or the mirror image of one.
		for (const FPhysicsBodySpec& ChildBody : Spec.Bodies)
		{
			const FName ChildName = ChildBody.Bone;
			if (FPhysicsAssetGenerator::FindParentBodyBone(Spec, Mesh, ChildName).IsNone())
			{
				continue;
			}
			const FString JointLabel = FString::Printf(TEXT("%s: joint %s"), *Label, *ChildName.ToString());
			const int32 Index = Asset->FindConstraintIndex(ChildName);
			FPhysicsJointFrames Frames;
			if (!TestTrue(JointLabel + TEXT(" exists"), Index != INDEX_NONE) ||
				!TestTrue(JointLabel + TEXT(" is described in the spec"), FPhysicsAssetGenerator::ComputeJointFrames(Spec, Mesh, ChildName, Frames, Error)))
			{
				continue;
			}
			const FConstraintInstance& Instance = Asset->ConstraintSetup[Index]->DefaultInstance;
			const FName ParentName = FPhysicsAssetGenerator::FindParentBodyBone(Spec, Mesh, ChildName);
			TestEqual(JointLabel + TEXT(" child"), Instance.ConstraintBone1, ChildName);
			TestEqual(JointLabel + TEXT(" parent"), Instance.ConstraintBone2, ParentName);
			TestTrue(JointLabel + TEXT(" is rigid"),
				Instance.GetLinearXMotion() == LCM_Locked && Instance.GetLinearYMotion() == LCM_Locked && Instance.GetLinearZMotion() == LCM_Locked);

			// The engine's limit is half the range, around the parent-side frame.
			TestEqual(JointLabel + TEXT(" swing1 motion"), static_cast<int32>(Instance.GetAngularSwing1Motion()), static_cast<int32>(Frames.Swing1.Motion));
			TestEqual(JointLabel + TEXT(" swing2 motion"), static_cast<int32>(Instance.GetAngularSwing2Motion()), static_cast<int32>(Frames.Swing2.Motion));
			TestEqual(JointLabel + TEXT(" twist motion"), static_cast<int32>(Instance.GetAngularTwistMotion()), static_cast<int32>(Frames.Twist.Motion));
			if (Frames.Swing1.Motion == ACM_Limited) { TestEqual(JointLabel + TEXT(" swing1 limit"), static_cast<double>(Instance.GetAngularSwing1Limit()), Frames.Swing1.HalfRangeDeg(), 1.0e-3); }
			if (Frames.Swing2.Motion == ACM_Limited) { TestEqual(JointLabel + TEXT(" swing2 limit"), static_cast<double>(Instance.GetAngularSwing2Limit()), Frames.Swing2.HalfRangeDeg(), 1.0e-3); }
			if (Frames.Twist.Motion == ACM_Limited) { TestEqual(JointLabel + TEXT(" twist limit"), static_cast<double>(Instance.GetAngularTwistLimit()), Frames.Twist.HalfRangeDeg(), 1.0e-3); }

			// What the joint does at the end of its range is the spec's: hard, or soft with its stiffness and damping.
			if (const FPhysicsJointSpec* JointSpec = Spec.FindJoint(ChildName))
			{
				for (const FConstraintBaseParams* Limit : { static_cast<const FConstraintBaseParams*>(&Instance.ProfileInstance.ConeLimit), static_cast<const FConstraintBaseParams*>(&Instance.ProfileInstance.TwistLimit) })
				{
					TestEqual(JointLabel + TEXT(" limit softness"), Limit->bSoftConstraint != 0, JointSpec->bSoftLimit);
					if (JointSpec->bSoftLimit)
					{
						TestEqual(JointLabel + TEXT(" limit stiffness"), static_cast<double>(Limit->Stiffness), JointSpec->LimitStiffness, 1.0e-3);
						TestEqual(JointLabel + TEXT(" limit damping"), static_cast<double>(Limit->Damping), JointSpec->LimitDamping, 1.0e-3);
					}
				}
			}

			// No motors in the asset: a drive copied from the template would pull the joint away from the
			// reference pose. Muscles are created at run time.
			const FAngularDriveConstraint& Drive = Instance.ProfileInstance.AngularDrive;
			TestFalse(JointLabel + TEXT(" has no angular motor"),
				Drive.SlerpDrive.bEnablePositionDrive || Drive.SlerpDrive.bEnableVelocityDrive ||
				Drive.TwistDrive.bEnablePositionDrive || Drive.TwistDrive.bEnableVelocityDrive ||
				Drive.SwingDrive.bEnablePositionDrive || Drive.SwingDrive.bEnableVelocityDrive);

			// The two anchors of a joint must coincide in the mesh's reference pose. A gap would be closed
			// by the solver in the first step and deform the skeleton.
			const int32 ChildBone = Skeleton.FindBoneIndex(ChildName);
			const int32 ParentBone = Skeleton.FindBoneIndex(ParentName);
			if (TestTrue(JointLabel + TEXT(" bones exist"), ChildBone != INDEX_NONE && ParentBone != INDEX_NONE))
			{
				const FVector ChildAnchor = (Instance.GetRefFrame(EConstraintFrame::Frame1) * ComponentSpace[ChildBone]).GetLocation();
				const FVector ParentAnchor = (Instance.GetRefFrame(EConstraintFrame::Frame2) * ComponentSpace[ParentBone]).GetLocation();
				TestEqual(JointLabel + TEXT(" anchor gap in the reference pose, cm"), FVector::Dist(ChildAnchor, ParentAnchor), 0.0, 1.0e-2);

				// The asset's frames are the ones the spec describes.
				const FQuat ChildFrame = (Instance.GetRefFrame(EConstraintFrame::Frame1) * ComponentSpace[ChildBone]).GetRotation();
				const FQuat ParentFrame = (Instance.GetRefFrame(EConstraintFrame::Frame2) * ComponentSpace[ParentBone]).GetRotation();
				TestEqual(JointLabel + TEXT(" child frame differs from the spec, degrees"), FMath::RadiansToDegrees(ChildFrame.AngularDistance(Frames.Child.GetRotation())), 0.0, 0.05);
				TestEqual(JointLabel + TEXT(" parent frame differs from the spec, degrees"), FMath::RadiansToDegrees(ParentFrame.AngularDistance(Frames.Parent.GetRotation())), 0.0, 0.05);
			}

			// Jointed bodies must not collide with each other.
			const int32 Child = Asset->FindBodyIndex(Instance.ConstraintBone1);
			const int32 Parent = Asset->FindBodyIndex(Instance.ConstraintBone2);
			if (Child != INDEX_NONE && Parent != INDEX_NONE)
			{
				TestFalse(JointLabel + TEXT(" bodies do not collide"), Asset->IsCollisionEnabled(Child, Parent));
			}
		}
	}
	return true;
}

/**
 * Joints are described by anatomy: a mirrored joint must be the exact mirror image of the written one, and
 * the ranges must open the way the body bends. Checked on the frames the generator works out, so it needs
 * the mesh but not the generated asset.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPhysicsJointAnatomyTest, "DictatorSandbox.Generated.PhysicsAsset.JointsFollowAnatomy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPhysicsJointAnatomyTest::RunTest(const FString& Parameters)
{
	for (const FString& SpecFile : FPhysicsAssetSpec::FindSpecFiles())
	{
		const FString Label = FPaths::GetBaseFilename(SpecFile);
		FPhysicsAssetSpec Spec;
		FString Error;
		if (!FPhysicsAssetSpec::LoadFile(SpecFile, Spec, Error))
		{
			AddError(Error);
			continue;
		}
		const USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *Spec.SkeletalMeshPath);
		if (!TestNotNull(*(Label + TEXT(": skeletal mesh")), Mesh))
		{
			continue;
		}
		const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
		TArray<FTransform> ComponentSpace;
		FAnimationRuntime::FillUpComponentSpaceTransforms(Skeleton, Skeleton.GetRefBonePose(), ComponentSpace);
		const auto BoneLocation = [&](FName Bone) { return ComponentSpace[FMath::Max(Skeleton.FindBoneIndex(Bone), 0)].GetLocation(); };

		// Reflection in the body's midplane, x = 0.
		const auto Mirror = [](const FVector& V) { return FVector(-V.X, V.Y, V.Z); };

		// Where a point of the child body ends up, relative to the joint, when the joint is turned from the
		// reference pose to the middle of its ranges.
		const auto AtMiddle = [](const FPhysicsJointFrames& Frames, const FVector& Point)
		{
			const FQuat Turn = Frames.Parent.GetRotation() * Frames.Child.GetRotation().Inverse();
			return Turn.RotateVector(Point - Frames.Child.GetLocation());
		};

		for (const FPhysicsJointSpec& Joint : Spec.Joints)
		{
			const FString JointLabel = FString::Printf(TEXT("%s: joint %s"), *Label, *Joint.Child.ToString());
			FPhysicsJointFrames Left;
			if (!TestTrue(JointLabel + TEXT(" can be built"), FPhysicsAssetGenerator::ComputeJointFrames(Spec, Mesh, Joint.Child, Left, Error)))
			{
				AddError(Error);
				continue;
			}

			// The twist axis runs along the limb.
			const FVector Limb = Joint.Along.IsNone() ? Joint.AlongDirection : BoneLocation(Joint.Along) - BoneLocation(Joint.Child);
			TestEqual(JointLabel + TEXT(" twist axis off the limb, degrees"),
				FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Left.Child.GetRotation().GetAxisX(), Limb.GetSafeNormal()), -1.0, 1.0))), 0.0, 0.05);

			if (Joint.Mirror.IsNone())
			{
				continue;
			}
			FPhysicsJointFrames Right;
			if (!TestTrue(JointLabel + TEXT(" mirror can be built"), FPhysicsAssetGenerator::ComputeJointFrames(Spec, Mesh, Joint.Mirror, Right, Error)))
			{
				AddError(Error);
				continue;
			}
			TestEqual(JointLabel + TEXT(" mirror: joint position off the mirror image, cm"), FVector::Dist(Right.Child.GetLocation(), Mirror(Left.Child.GetLocation())), 0.0, 0.05);
			TestEqual(JointLabel + TEXT(" mirror: swing1 range"), Right.Swing1.HalfRangeDeg(), Left.Swing1.HalfRangeDeg(), 1.0e-6);
			TestEqual(JointLabel + TEXT(" mirror: swing2 range"), Right.Swing2.HalfRangeDeg(), Left.Swing2.HalfRangeDeg(), 1.0e-6);
			TestEqual(JointLabel + TEXT(" mirror: twist range"), Right.Twist.HalfRangeDeg(), Left.Twist.HalfRangeDeg(), 1.0e-6);

			// Turning both sides by the same angles about their own axes must give mirror-image motion.
			// Tried with three turns, one about each axis, on a point off every axis.
			const FVector Probe(3.0, 5.0, 7.0);
			const FQuat Turns[] = { FQuat(FVector::XAxisVector, 0.4), FQuat(FVector::YAxisVector, 0.4), FQuat(FVector::ZAxisVector, 0.4) };
			for (const FQuat& Turn : Turns)
			{
				const FVector LeftPoint = Left.Child.GetRotation().RotateVector(Turn.RotateVector(Left.Child.GetRotation().UnrotateVector(Probe)));
				const FVector RightPoint = Right.Child.GetRotation().RotateVector(Turn.RotateVector(Right.Child.GetRotation().UnrotateVector(Mirror(Probe))));
				TestEqual(JointLabel + TEXT(" mirror: the same turn on both sides is off the mirror image, cm"), FVector::Dist(RightPoint, Mirror(LeftPoint)), 0.0, 1.0e-3);
			}

			// The middle of the ranges is a mirror-image pose too.
			const FVector AlongPoint = Joint.Along.IsNone() ? BoneLocation(Joint.Child) + Joint.AlongDirection * 10.0 : BoneLocation(Joint.Along);
			const FVector LeftEnd = AtMiddle(Left, AlongPoint);
			const FVector RightEnd = AtMiddle(Right, Mirror(AlongPoint));
			TestEqual(JointLabel + TEXT(" mirror: limb at the middle of the ranges is off the mirror image, cm"), FVector::Dist(RightEnd, Mirror(LeftEnd)), 0.0, 0.05);
		}

		// Which way the NPC's joints open. In the mesh's space the character faces +y, its left is +x.
		if (Spec.Name != TEXT("npc_body"))
		{
			continue;
		}
		const auto EndAtMiddle = [&](const TCHAR* Child, const TCHAR* Along, FVector& OutEnd)
		{
			FPhysicsJointFrames Frames;
			if (!FPhysicsAssetGenerator::ComputeJointFrames(Spec, Mesh, Child, Frames, Error))
			{
				AddError(Error);
				return false;
			}
			OutEnd = AtMiddle(Frames, BoneLocation(Along));
			return true;
		};
		FVector End;
		if (EndAtMiddle(TEXT("calf_l"), TEXT("foot_l"), End))
		{
			// A knee bent to the middle of its range has the foot well behind it and higher than the ankle of a straight leg.
			TestTrue(TEXT("npc_body: the knee bends backward"), End.Y < -30.0 && End.Z > -20.0);
		}
		if (EndAtMiddle(TEXT("thigh_l"), TEXT("calf_l"), End))
		{
			// The hip's range is 100 degrees of flexion against 30 of extension: its middle has the knee in front.
			TestTrue(TEXT("npc_body: the hip flexes forward"), End.Y > 15.0);
			// And more abduction than adduction: the knee of the left leg is further to the left.
			TestTrue(TEXT("npc_body: the hip opens outward"), End.X > BoneLocation(TEXT("calf_l")).X - BoneLocation(TEXT("thigh_l")).X);
		}
		if (EndAtMiddle(TEXT("lowerarm_l"), TEXT("hand_l"), End))
		{
			// An elbow bent to the middle of 0..140 degrees brings the hand closer to the shoulder.
			const FVector ElbowToShoulder = BoneLocation(TEXT("upperarm_l")) - BoneLocation(TEXT("lowerarm_l"));
			const FVector HandInReferencePose = BoneLocation(TEXT("hand_l")) - BoneLocation(TEXT("lowerarm_l"));
			TestTrue(TEXT("npc_body: the elbow folds the forearm toward the upper arm"), FVector::Dist(End, ElbowToShoulder) < FVector::Dist(HandInReferencePose, ElbowToShoulder) - 5.0);
		}
		if (EndAtMiddle(TEXT("foot_l"), TEXT("ball_l"), End))
		{
			// The ankle allows more dorsiflexion (30) than plantar flexion (20): at the middle the toes are raised.
			TestTrue(TEXT("npc_body: the ankle lifts the toes"), End.Z > BoneLocation(TEXT("ball_l")).Z - BoneLocation(TEXT("foot_l")).Z);
		}
		if (EndAtMiddle(TEXT("neck_01"), TEXT("head"), End))
		{
			// The neck extends further back (75) than it bends forward (60).
			TestTrue(TEXT("npc_body: the neck's middle leans back"), End.Y < BoneLocation(TEXT("head")).Y - BoneLocation(TEXT("neck_01")).Y);
		}
		if (EndAtMiddle(TEXT("spine_02"), TEXT("spine_04"), End))
		{
			// The spine bends further forward (22.5 per joint) than back (12.5).
			TestTrue(TEXT("npc_body: the spine's middle leans forward"), End.Y > BoneLocation(TEXT("spine_04")).Y - BoneLocation(TEXT("spine_02")).Y);
		}
		if (EndAtMiddle(TEXT("upperarm_l"), TEXT("lowerarm_l"), End))
		{
			// The shoulder raises the arm outward and forward much further than it lets it cross behind the body.
			const FVector Reference = BoneLocation(TEXT("lowerarm_l")) - BoneLocation(TEXT("upperarm_l"));
			TestTrue(TEXT("npc_body: the shoulder's middle has the arm raised and forward"), End.Z > Reference.Z + 5.0 && End.Y > Reference.Y + 5.0);
		}
		if (EndAtMiddle(TEXT("clavicle_l"), TEXT("upperarm_l"), End))
		{
			// The collarbone lifts the shoulder (40 degrees) much further than it lowers it (10).
			const FVector Reference = BoneLocation(TEXT("upperarm_l")) - BoneLocation(TEXT("clavicle_l"));
			TestTrue(TEXT("npc_body: the collarbone's middle has the shoulder raised"), End.Z > Reference.Z + 2.0);
		}
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
