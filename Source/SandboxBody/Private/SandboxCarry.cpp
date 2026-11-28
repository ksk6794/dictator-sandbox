#include "SandboxCarry.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsControlComponent.h"
#include "PhysicsEngine/BodyInstance.h"

namespace
{
	const FName CarrySet(TEXT("Carry"));

	// As in the body controller: Physics Control's strength is a spring of (2 pi strength)^2, used as it is in force mode.
	float CarryStrengthForSpring(double SpringUnrealUnits)
	{
		return static_cast<float>(FMath::Sqrt(FMath::Max(SpringUnrealUnits, 0.0)) / UE_DOUBLE_TWO_PI);
	}

	// 1 N*m = 1e4 kg*cm^2/s^2; Chaos multiplies angular drive stiffness and damping by 1.5, divided out here.
	constexpr double CarryTorqueToUnreal = 1.0e4;
	constexpr double CarryChaosAngularDriveScale = 1.5;
	constexpr double CarryGravityMps2 = 9.81;
}

bool FSandboxCarry::Create(UPhysicsControlComponent* InControl, USkeletalMeshComponent* InMesh, FString& OutError)
{
	if (!InControl || !InMesh)
	{
		OutError = TEXT("no Physics Control component or mesh to carry");
		return false;
	}
	Bodies.Reset();
	for (const FBodyInstance* Body : InMesh->Bodies)
	{
		if (!Body || !Body->IsValidBodyInstance())
		{
			continue;
		}
		FBody Entry;
		Entry.Bone = InMesh->GetBoneName(Body->InstanceBoneIndex);
		Entry.ControlName = FName(*FString::Printf(TEXT("Carry_%s"), *Entry.Bone.ToString()));
		Entry.MassKg = Body->GetBodyMass();
		Entry.Target = Body->GetUnrealWorldTransform();
		Entry.Target.SetScale3D(FVector::OneVector);

		// The target is the pose of the body's bone; the spring itself acts at the centre of mass.
		FPhysicsControlTarget Target;
		Target.TargetPosition = Entry.Target.GetLocation();
		Target.TargetOrientation = Entry.Target.Rotator();
		Target.bApplyControlPointToTarget = true;
		FPhysicsControlData Data;
		Data.bUseAccelerationDriveMode = false;
		Data.LinearStrength = 0.0f;
		Data.LinearDampingRatio = 0.0f;
		Data.LinearExtraDamping = 0.0f;
		Data.AngularStrength = 0.0f;
		Data.AngularDampingRatio = 0.0f;
		Data.AngularExtraDamping = 0.0f;
		Data.bUseSkeletalAnimation = false;
		if (!InControl->CreateNamedControl(Entry.ControlName, nullptr, NAME_None, InMesh, Entry.Bone, Data, Target, CarrySet))
		{
			OutError = FString::Printf(TEXT("Could not create the carry control for '%s'"), *Entry.Bone.ToString());
			Bodies.Reset();
			return false;
		}
		InControl->SetControlEnabled(Entry.ControlName, false);
		Bodies.Add(Entry);
	}
	if (Bodies.Num() == 0)
	{
		// A carry of nothing would count as led and lead nothing (decision 17).
		OutError = TEXT("the mesh has no simulated bodies to carry");
		return false;
	}
	// Which body each hangs from: the nearest ancestor bone that has a body (for the chain of a hold, decision 14).
	Parents.Init(INDEX_NONE, Bodies.Num());
	if (const USkeletalMesh* Asset = InMesh->GetSkeletalMeshAsset())
	{
		const FReferenceSkeleton& Skeleton = Asset->GetRefSkeleton();
		for (int32 Index = 0; Index < Bodies.Num(); ++Index)
		{
			int32 Bone = Skeleton.FindBoneIndex(Bodies[Index].Bone);
			while (Bone != INDEX_NONE && Parents[Index] == INDEX_NONE)
			{
				Bone = Skeleton.GetParentIndex(Bone);
				if (Bone != INDEX_NONE)
				{
					Parents[Index] = IndexOf(Skeleton.GetBoneName(Bone));
				}
			}
		}
	}
	Control = InControl;
	Mesh = InMesh;
	AppliedShare = 0.0;
	bEnabled = false;
	return true;
}

void FSandboxCarry::SetTarget(int32 Index, const FTransform& World, double DeltaTime)
{
	FBody& Body = Bodies[Index];
	FTransform Next = World;
	Next.SetScale3D(FVector::OneVector);
	if (Body.bHasTarget && Body.bHasVelocity && DeltaTime > 0.0)
	{
		Body.VelocityCmS = (Next.GetLocation() - Body.Target.GetLocation()) / DeltaTime;
		const FQuat Turn = (Next.GetRotation() * Body.Target.GetRotation().Inverse()).GetShortestArcWith(FQuat::Identity);
		Body.AngularVelocityRadS = Turn.ToRotationVector() / DeltaTime;
	}
	else
	{
		Body.VelocityCmS = FVector::ZeroVector;
		Body.AngularVelocityRadS = FVector::ZeroVector;
	}
	Body.Target = Next;
	Body.bHasTarget = true;
	Body.bHasVelocity = true;
}

void FSandboxCarry::ForgetTargets()
{
	for (FBody& Body : Bodies)
	{
		Body.bHasVelocity = false;
		Body.VelocityCmS = FVector::ZeroVector;
		Body.AngularVelocityRadS = FVector::ZeroVector;
	}
}

void FSandboxCarry::Apply(const FSandboxCarrySettings& Settings, double Share, TConstArrayView<double> BodyShares, TConstArrayView<double> VerticalShares)
{
	if (!Control)
	{
		return;
	}
	Share = FMath::Clamp(Share, 0.0, 1.0);
	// A body's own share (a hold, decision 14): 1 for every body unless one is given for each.
	const bool bByBody = BodyShares.Num() == Bodies.Num();
	// And its vertical share (step 10): only when given, so that a run without it sets no per-axis strength at all.
	const bool bByVertical = VerticalShares.Num() == Bodies.Num();
	for (int32 Index = 0; Index < Bodies.Num(); ++Index)
	{
		Bodies[Index].BodyShare = bByBody ? FMath::Clamp(BodyShares[Index], 0.0, 1.0) : 1.0;
		Bodies[Index].VerticalShare = bByVertical ? FMath::Clamp(VerticalShares[Index], 0.0, 1.0) : 1.0;
	}
	// A cap of 0 would mean no cap to the engine: with next to nothing left of the caps, nothing pulls.
	const bool bWanted = Share > 0.0 && Settings.CapShare > 0.01;
	if (!bWanted && !bEnabled)
	{
		AppliedShare = 0.0;
		return;
	}
	for (const FBody& Body : Bodies)
	{
		// A body a hold has taken all of is let go as one never aimed at is.
		const bool bOn = bWanted && Body.bHasTarget && Body.BodyShare > 0.0;
		Control->SetControlEnabled(Body.ControlName, bOn);
		if (!bOn)
		{
			continue;
		}
		// Without a hold the share is exactly 1 and every product is what it was.
		const double LinearSpring = Settings.LinearNPerMPerKg * Body.MassKg * Share * Body.BodyShare;
		const double AngularSpring = Settings.AngularNmPerRadPerKg * Body.MassKg * Share * Body.BodyShare * CarryTorqueToUnreal / CarryChaosAngularDriveScale;
		FPhysicsControlData Data;
		Data.bUseAccelerationDriveMode = false;
		Data.LinearStrength = CarryStrengthForSpring(LinearSpring);
		Data.LinearDampingRatio = 0.0f;
		Data.LinearExtraDamping = static_cast<float>(LinearSpring * Settings.DampingS);
		Data.AngularStrength = CarryStrengthForSpring(AngularSpring);
		Data.AngularDampingRatio = 0.0f;
		Data.AngularExtraDamping = static_cast<float>(AngularSpring * Settings.DampingS);
		// 1 N = 100 kg*cm/s^2. The caps do not go down with the share: the springs do, and a cap of 0 would mean none.
		Data.MaxForce = static_cast<float>(Settings.CapShare * Settings.MaxWeights * Body.MassKg * CarryGravityMps2 * 100.0);
		Data.MaxTorque = static_cast<float>(Settings.CapShare * Settings.MaxNmPerKg * Body.MassKg * CarryTorqueToUnreal);
		// The reference's own velocity is given with the target; nothing is taken from the mesh's pose.
		Data.LinearTargetVelocityMultiplier = 0.0f;
		Data.AngularTargetVelocityMultiplier = 0.0f;
		Data.bUseSkeletalAnimation = false;
		Control->SetControlData(Body.ControlName, Data);
		// The vertical cap is its own: what the world may carry of the body's weight.
		FPhysicsControlMultiplier Multiplier;
		Multiplier.MaxForceMultiplier = FVector(1.0, 1.0, Settings.MaxWeights > 0.0 ? Settings.MaxUpWeights / Settings.MaxWeights : 1.0);
		if (bByVertical)
		{
			Multiplier.LinearStrengthMultiplier = FVector(1.0, 1.0, Body.VerticalShare);
			Multiplier.LinearExtraDampingMultiplier = FVector(1.0, 1.0, Body.VerticalShare);
		}
		Control->SetControlMultiplier(Body.ControlName, Multiplier);

		FPhysicsControlTarget Target;
		Target.TargetPosition = Body.Target.GetLocation() + FVector(0.0, 0.0, RaiseCm(Body));
		Target.TargetOrientation = Body.Target.Rotator();
		Target.TargetVelocity = Body.VelocityCmS;
		// Physics Control takes an angular velocity in revolutions per second.
		Target.TargetAngularVelocity = Body.AngularVelocityRadS / UE_DOUBLE_TWO_PI;
		Target.bApplyControlPointToTarget = true;
		Control->SetControlTarget(Body.ControlName, Target);
	}
	AppliedShare = Share;
	bEnabled = bWanted;
}

void FSandboxCarry::ChainHolds(TConstArrayView<int32> InParents, int32 Held, int32 Mode, double Falloff, double Hold, TArrayView<double> InOutHolds)
{
	if (!InParents.IsValidIndex(Held) || InOutHolds.Num() != InParents.Num() || Hold <= 0.0)
	{
		return;
	}
	const double Keep = 1.0 - FMath::Clamp(Falloff, 0.0, 1.0);
	const double HeldShare = FMath::Min(Hold, 1.0);
	double Share = HeldShare;
	int32 Index = Held;
	// At most as many steps as there are bodies: a tree has no loop, and a bad list must not make one.
	for (int32 Step = 0; Step < InParents.Num() && Index != INDEX_NONE && InParents.IsValidIndex(Index); ++Step)
	{
		const bool bRoot = InParents[Index] == INDEX_NONE;
		if (Step > 0 && (Mode <= 0 || (Mode == 1 && bRoot)))
		{
			break;
		}
		InOutHolds[Index] = FMath::Max(InOutHolds[Index], Share);
		Share *= Keep;
		Index = InParents[Index];
	}
	// Mode 3: also what hangs from the held body (a hand on the chest holds the neck, the head and the arms back as well), each body
	// by its depth below the held one.
	if (Mode >= 3)
	{
		for (int32 Body = 0; Body < InParents.Num(); ++Body)
		{
			int32 Depth = 0;
			int32 Up = Body;
			for (int32 Step = 0; Step < InParents.Num() && Up != INDEX_NONE && Up != Held && InParents.IsValidIndex(Up); ++Step)
			{
				Up = InParents[Up];
				++Depth;
			}
			if (Up == Held && Body != Held)
			{
				InOutHolds[Body] = FMath::Max(InOutHolds[Body], HeldShare * FMath::Pow(Keep, static_cast<double>(Depth)));
			}
		}
	}
}

double FSandboxCarry::RaiseCm(const FBody& Body) const
{
	const FBodyInstance* Instance = Mesh ? Mesh->GetBodyInstance(Body.Bone) : nullptr;
	if (!Instance || !Instance->IsValidBodyInstance())
	{
		return 0.0;
	}
	const double TargetComZ = Body.Target.TransformPositionNoScale(Instance->GetMassSpaceLocal().GetLocation()).Z;
	return FMath::Max(Instance->GetCOMPosition().Z - TargetComZ, 0.0);
}

FVector FSandboxCarry::ForceOn(int32 Index, const FSandboxCarrySettings& Settings, double Share) const
{
	const FBody& Body = Bodies[Index];
	const FBodyInstance* Instance = Mesh && Body.bHasTarget && Share > 0.0 ? Mesh->GetBodyInstance(Body.Bone) : nullptr;
	if (!Instance || !Instance->IsValidBodyInstance())
	{
		return FVector::ZeroVector;
	}
	const FVector TargetCom = Body.Target.TransformPositionNoScale(Instance->GetMassSpaceLocal().GetLocation());
	FVector ErrorM = (TargetCom - Instance->GetCOMPosition()) * 0.01;
	// A body above its reference is given the reference at its own height (Apply, RaiseCm).
	ErrorM.Z = FMath::Max(ErrorM.Z, 0.0);
	// The reference's velocity at the centre of mass, as the control has it.
	const FVector Arm = TargetCom - Body.Target.GetLocation();
	const FVector GoalVelocityMps = (Body.VelocityCmS + FVector::CrossProduct(Body.AngularVelocityRadS, Arm)) * 0.01;
	const FVector VelocityMps = Instance->GetUnrealWorldVelocity() * 0.01 - GoalVelocityMps;
	const double Stiffness = Settings.LinearNPerMPerKg * Body.MassKg * Share * Body.BodyShare;
	FVector CommandN = Stiffness * (ErrorM - VelocityMps * Settings.DampingS);
	if (Body.VerticalShare < 1.0)
	{
		CommandN.Z *= Body.VerticalShare;
	}
	if (Settings.CapShare <= 0.01)
	{
		return FVector::ZeroVector;
	}
	const double CapN = Settings.CapShare * Settings.MaxWeights * Body.MassKg * CarryGravityMps2;
	const double CapUpN = Settings.CapShare * Settings.MaxUpWeights * Body.MassKg * CarryGravityMps2;
	if (CapN > 0.0)
	{
		CommandN.X = FMath::Clamp(CommandN.X, -CapN, CapN);
		CommandN.Y = FMath::Clamp(CommandN.Y, -CapN, CapN);
		CommandN.Z = FMath::Clamp(CommandN.Z, -CapUpN, CapUpN);
	}
	return CommandN;
}

FVector FSandboxCarry::TorqueOn(int32 Index, const FSandboxCarrySettings& Settings, double Share) const
{
	const FBody& Body = Bodies[Index];
	const FBodyInstance* Instance = Mesh && Body.bHasTarget && Share > 0.0 && Settings.CapShare > 0.01 ? Mesh->GetBodyInstance(Body.Bone) : nullptr;
	if (!Instance || !Instance->IsValidBodyInstance())
	{
		return FVector::ZeroVector;
	}
	const FQuat Delta = (Body.Target.GetRotation() * Instance->GetUnrealWorldTransform().GetRotation().Inverse()).GetShortestArcWith(FQuat::Identity);
	const FVector SpinRadS = Instance->GetUnrealWorldAngularVelocityInRadians() - Body.AngularVelocityRadS;
	const double Stiffness = Settings.AngularNmPerRadPerKg * Body.MassKg * Share * Body.BodyShare;
	FVector CommandNm = Stiffness * (Delta.ToRotationVector() - SpinRadS * Settings.DampingS);
	const double CapNm = Settings.CapShare * Settings.MaxNmPerKg * Body.MassKg;
	if (CapNm > 0.0)
	{
		CommandNm = CommandNm.GetClampedToMaxSize(CapNm);
	}
	return CommandNm;
}

bool FSandboxCarry::ComOf(int32 Index, FVector& OutTargetComCm, FVector& OutComCm) const
{
	const FBody& Body = Bodies[Index];
	const FBodyInstance* Instance = Mesh && Body.bHasTarget ? Mesh->GetBodyInstance(Body.Bone) : nullptr;
	if (!Instance || !Instance->IsValidBodyInstance())
	{
		return false;
	}
	OutTargetComCm = Body.Target.TransformPositionNoScale(Instance->GetMassSpaceLocal().GetLocation());
	OutComCm = Instance->GetCOMPosition();
	return true;
}

double FSandboxCarry::LoadN(const FSandboxCarrySettings& Settings, double Share, FVector* OutNetN) const
{
	double ForceN = 0.0;
	FVector NetN = FVector::ZeroVector;
	for (int32 Index = 0; Index < Bodies.Num(); ++Index)
	{
		const FVector CommandN = ForceOn(Index, Settings, Share);
		ForceN += CommandN.Size();
		NetN += CommandN;
	}
	if (OutNetN)
	{
		*OutNetN = NetN;
	}
	return ForceN;
}

FString FSandboxCarry::Describe(const FSandboxCarrySettings& Settings, double Share, int32 Count) const
{
	TArray<TPair<double, int32>> BySize;
	for (int32 Index = 0; Index < Bodies.Num(); ++Index)
	{
		BySize.Emplace(ForceOn(Index, Settings, Share).Size(), Index);
	}
	BySize.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key > B.Key; });
	FString Text;
	for (int32 Rank = 0; Rank < FMath::Min(Count, BySize.Num()); ++Rank)
	{
		const int32 Index = BySize[Rank].Value;
		const FVector CommandN = ForceOn(Index, Settings, Share);
		const FBodyInstance* Instance = Mesh->GetBodyInstance(Bodies[Index].Bone);
		const FVector OffCm = Instance ? Instance->GetCOMPosition() - Bodies[Index].Target.TransformPositionNoScale(Instance->GetMassSpaceLocal().GetLocation()) : FVector::ZeroVector;
		Text += FString::Printf(TEXT("%s%s %.0f N (%.0f, %.0f, %.0f), %.0f cm off (%.0f up)"), Rank > 0 ? TEXT("; ") : TEXT(""),
			*Bodies[Index].Bone.ToString(), CommandN.Size(), CommandN.X, CommandN.Y, CommandN.Z, OffCm.Size(), OffCm.Z);
	}
	return Text;
}
