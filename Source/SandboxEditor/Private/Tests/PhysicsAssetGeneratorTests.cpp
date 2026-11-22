#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

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

		TestEqual(Label + TEXT(": body count"), Asset->SkeletalBodySetups.Num(), Spec.Bodies.Num());
		TestEqual(Label + TEXT(": joint count"), Asset->ConstraintSetup.Num(), Spec.Bodies.Num() - 1);

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
			TestTrue(BodyLabel + TEXT(" has collision shapes"), Body->AggGeom.GetElementCount() > 0);
			TotalMass += Body->DefaultInstance.GetMassOverride();
		}
		TestEqual(Label + TEXT(": total mass"), TotalMass, Spec.TotalMassKg, 1.0e-2);

		for (const FPhysicsJointSpec& JointSpec : Spec.Joints)
		{
			const FString JointLabel = FString::Printf(TEXT("%s: joint %s"), *Label, *JointSpec.Child.ToString());
			const int32 Index = Asset->FindConstraintIndex(JointSpec.Child);
			if (!TestTrue(JointLabel + TEXT(" exists"), Index != INDEX_NONE))
			{
				continue;
			}
			const FConstraintInstance& Instance = Asset->ConstraintSetup[Index]->DefaultInstance;
			TestEqual(JointLabel + TEXT(" child"), Instance.ConstraintBone1, JointSpec.Child);
			TestEqual(JointLabel + TEXT(" parent"), Instance.ConstraintBone2, FPhysicsAssetGenerator::FindParentBodyBone(Spec, Mesh, JointSpec.Child));
			TestTrue(JointLabel + TEXT(" is rigid"),
				Instance.GetLinearXMotion() == LCM_Locked && Instance.GetLinearYMotion() == LCM_Locked && Instance.GetLinearZMotion() == LCM_Locked);

			TestEqual(JointLabel + TEXT(" swing1 motion"), static_cast<int32>(Instance.GetAngularSwing1Motion()), static_cast<int32>(JointSpec.Swing1.Motion));
			TestEqual(JointLabel + TEXT(" swing2 motion"), static_cast<int32>(Instance.GetAngularSwing2Motion()), static_cast<int32>(JointSpec.Swing2.Motion));
			TestEqual(JointLabel + TEXT(" twist motion"), static_cast<int32>(Instance.GetAngularTwistMotion()), static_cast<int32>(JointSpec.Twist.Motion));
			if (JointSpec.Swing1.Motion == ACM_Limited) { TestEqual(JointLabel + TEXT(" swing1 limit"), static_cast<double>(Instance.GetAngularSwing1Limit()), JointSpec.Swing1.LimitDeg, 1.0e-3); }
			if (JointSpec.Swing2.Motion == ACM_Limited) { TestEqual(JointLabel + TEXT(" swing2 limit"), static_cast<double>(Instance.GetAngularSwing2Limit()), JointSpec.Swing2.LimitDeg, 1.0e-3); }
			if (JointSpec.Twist.Motion == ACM_Limited) { TestEqual(JointLabel + TEXT(" twist limit"), static_cast<double>(Instance.GetAngularTwistLimit()), JointSpec.Twist.LimitDeg, 1.0e-3); }

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

#endif // WITH_DEV_AUTOMATION_TESTS
