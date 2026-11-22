#include "SandboxBodyProbe.h"

#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/ConstraintInstance.h"

void FSandboxBodyProbe::SampleBodies(const USkeletalMeshComponent* Mesh, TArray<FSandboxBodySample>& OutSamples)
{
	OutSamples.Reset();
	if (!Mesh)
	{
		return;
	}

	OutSamples.Reserve(Mesh->Bodies.Num());
	for (const FBodyInstance* Body : Mesh->Bodies)
	{
		if (!Body || !Body->IsValidBodyInstance())
		{
			continue;
		}
		FSandboxBodySample& Sample = OutSamples.AddDefaulted_GetRef();
		Sample.Bone = Mesh->GetBoneName(Body->InstanceBoneIndex);
		Sample.WorldTransform = Body->GetUnrealWorldTransform();
		Sample.LinearVelocity = Body->GetUnrealWorldVelocity();
		Sample.AngularVelocityRad = Body->GetUnrealWorldAngularVelocityInRadians();
		Sample.MassKg = Body->GetBodyMass();
	}
}

double FSandboxBodyProbe::MaxJointStretchCm(const USkeletalMeshComponent* Mesh, FName* OutWorstJoint)
{
	double MaxStretch = 0.0;
	if (!Mesh)
	{
		return MaxStretch;
	}

	for (const FConstraintInstance* Constraint : Mesh->Constraints)
	{
		// Only joints with all three linear axes locked are meant to hold their anchors together.
		// Physics assets may also carry free or limited constraints (the stock mannequin has one);
		// separation there is by design, not solver error.
		if (!Constraint ||
			Constraint->GetLinearXMotion() != LCM_Locked ||
			Constraint->GetLinearYMotion() != LCM_Locked ||
			Constraint->GetLinearZMotion() != LCM_Locked)
		{
			continue;
		}
		const FBodyInstance* Child = Mesh->GetBodyInstance(Constraint->GetChildBoneName());
		const FBodyInstance* Parent = Mesh->GetBodyInstance(Constraint->GetParentBoneName());
		if (!Child || !Parent || !Child->IsValidBodyInstance() || !Parent->IsValidBodyInstance())
		{
			continue;
		}

		// Each reference frame is stored in its body's local space; in a perfectly solved joint
		// both map to the same world position.
		const FVector ChildAnchor = (Constraint->GetRefFrame(EConstraintFrame::Frame1) * Child->GetUnrealWorldTransform()).GetLocation();
		const FVector ParentAnchor = (Constraint->GetRefFrame(EConstraintFrame::Frame2) * Parent->GetUnrealWorldTransform()).GetLocation();
		const double Stretch = FVector::Dist(ChildAnchor, ParentAnchor);
		if (Stretch > MaxStretch)
		{
			MaxStretch = Stretch;
			if (OutWorstJoint)
			{
				*OutWorstJoint = Constraint->JointName;
			}
		}
	}
	return MaxStretch;
}

double FSandboxBodyProbe::TranslationalKineticEnergyJ(const TArray<FSandboxBodySample>& Samples)
{
	double Energy = 0.0;
	for (const FSandboxBodySample& Sample : Samples)
	{
		const double SpeedMps = Sample.LinearVelocity.Size() * 0.01;
		Energy += 0.5 * Sample.MassKg * SpeedMps * SpeedMps;
	}
	return Energy;
}

double FSandboxBodyProbe::TotalMassKg(const TArray<FSandboxBodySample>& Samples)
{
	double Mass = 0.0;
	for (const FSandboxBodySample& Sample : Samples)
	{
		Mass += Sample.MassKg;
	}
	return Mass;
}
