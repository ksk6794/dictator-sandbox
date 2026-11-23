#include "SandboxBodyProbe.h"

#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
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
		Sample.CenterOfMass = Body->GetCOMPosition();
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
		const double Stretch = JointStretchCm(Mesh, *Constraint);
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

double FSandboxBodyProbe::JointStretchCm(const USkeletalMeshComponent* Mesh, const FConstraintInstance& Constraint)
{
	const FBodyInstance* Child = Mesh ? Mesh->GetBodyInstance(Constraint.GetChildBoneName()) : nullptr;
	const FBodyInstance* Parent = Mesh ? Mesh->GetBodyInstance(Constraint.GetParentBoneName()) : nullptr;
	if (!Child || !Parent || !Child->IsValidBodyInstance() || !Parent->IsValidBodyInstance())
	{
		return 0.0;
	}

	// Each reference frame is stored in its body's local space; in a perfectly solved joint
	// both map to the same world position.
	const FVector ChildAnchor = (Constraint.GetRefFrame(EConstraintFrame::Frame1) * Child->GetUnrealWorldTransform()).GetLocation();
	const FVector ParentAnchor = (Constraint.GetRefFrame(EConstraintFrame::Frame2) * Parent->GetUnrealWorldTransform()).GetLocation();
	return FVector::Dist(ChildAnchor, ParentAnchor);
}

double FSandboxBodyProbe::MaxJointAngularSpeedRadS(const USkeletalMeshComponent* Mesh, FName* OutFastestJoint)
{
	double MaxSpeed = 0.0;
	if (!Mesh)
	{
		return MaxSpeed;
	}

	for (const FConstraintInstance* Constraint : Mesh->Constraints)
	{
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

		const double Speed = (Child->GetUnrealWorldAngularVelocityInRadians() - Parent->GetUnrealWorldAngularVelocityInRadians()).Size();
		if (Speed > MaxSpeed)
		{
			MaxSpeed = Speed;
			if (OutFastestJoint)
			{
				*OutFastestJoint = Constraint->JointName;
			}
		}
	}
	return MaxSpeed;
}

FVector FSandboxBodyProbe::CenterOfMass(const TArray<FSandboxBodySample>& Samples)
{
	FVector Weighted = FVector::ZeroVector;
	double Mass = 0.0;
	for (const FSandboxBodySample& Sample : Samples)
	{
		Weighted += Sample.CenterOfMass * Sample.MassKg;
		Mass += Sample.MassKg;
	}
	return Mass > 0.0 ? Weighted / Mass : FVector::ZeroVector;
}

double FSandboxBodyProbe::SupportPolygon(const USkeletalMeshComponent* Mesh, TConstArrayView<FName> StandingBones, double SoleToleranceCm, TArray<FVector2D>& OutPolygon)
{
	OutPolygon.Reset();
	TArray<FVector2D> Soles;
	double LowestCm = TNumericLimits<double>::Max();

	for (const FName Bone : StandingBones)
	{
		const FBodyInstance* Body = Mesh ? Mesh->GetBodyInstance(Bone) : nullptr;
		const UBodySetup* Setup = Body && Body->IsValidBodyInstance() ? Body->GetBodySetup() : nullptr;
		if (!Setup)
		{
			continue;
		}

		// Candidate points of this body in world space: the corners of its boxes and the undersides of its
		// capsules' end spheres and of its spheres.
		const FTransform BodyTransform = Body->GetUnrealWorldTransform();
		const FVector Scale = Body->Scale3D;
		TArray<FVector, TInlineAllocator<32>> Points;
		const auto ToWorld = [&BodyTransform, &Scale](const FTransform& Element, const FVector& Local)
		{
			return BodyTransform.TransformPositionNoScale(Element.TransformPositionNoScale(Local) * Scale);
		};
		for (const FKBoxElem& Box : Setup->AggGeom.BoxElems)
		{
			const FTransform Element = Box.GetTransform();
			for (int32 Corner = 0; Corner < 8; ++Corner)
			{
				Points.Add(ToWorld(Element, FVector(
					(Corner & 1 ? 0.5 : -0.5) * Box.X, (Corner & 2 ? 0.5 : -0.5) * Box.Y, (Corner & 4 ? 0.5 : -0.5) * Box.Z)));
			}
		}
		for (const FKSphylElem& Capsule : Setup->AggGeom.SphylElems)
		{
			const FTransform Element = Capsule.GetTransform();
			const FVector Down(0.0, 0.0, Capsule.Radius * Scale.GetAbsMax());
			Points.Add(ToWorld(Element, FVector(0.0, 0.0, 0.5 * Capsule.Length)) - Down);
			Points.Add(ToWorld(Element, FVector(0.0, 0.0, -0.5 * Capsule.Length)) - Down);
		}
		for (const FKSphereElem& Sphere : Setup->AggGeom.SphereElems)
		{
			Points.Add(ToWorld(Sphere.GetTransform(), FVector::ZeroVector) - FVector(0.0, 0.0, Sphere.Radius * Scale.GetAbsMax()));
		}

		double BodyLowestCm = TNumericLimits<double>::Max();
		for (const FVector& Point : Points)
		{
			BodyLowestCm = FMath::Min(BodyLowestCm, Point.Z);
		}
		for (const FVector& Point : Points)
		{
			if (Point.Z <= BodyLowestCm + SoleToleranceCm)
			{
				Soles.Emplace(Point.X, Point.Y);
			}
		}
		LowestCm = FMath::Min(LowestCm, BodyLowestCm);
	}

	ConvexHull2D(Soles, OutPolygon);
	return OutPolygon.Num() > 0 ? LowestCm : 0.0;
}

void FSandboxBodyProbe::ConvexHull2D(TConstArrayView<FVector2D> Points, TArray<FVector2D>& OutHull)
{
	// Andrew's monotone chain: sort by x then y, build the lower and the upper chain.
	TArray<FVector2D> Sorted(Points.GetData(), Points.Num());
	Sorted.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X || (A.X == B.X && A.Y < B.Y); });
	for (int32 Index = Sorted.Num() - 1; Index > 0; --Index)
	{
		if (Sorted[Index].Equals(Sorted[Index - 1], UE_DOUBLE_KINDA_SMALL_NUMBER))
		{
			Sorted.RemoveAt(Index);
		}
	}

	OutHull.Reset();
	if (Sorted.Num() < 3)
	{
		OutHull = MoveTemp(Sorted);
		return;
	}
	// A turn to the left (or straight on) at the last point means it is not on the hull.
	const auto Chain = [&OutHull](const FVector2D& Point, int32 Floor)
	{
		while (OutHull.Num() >= Floor + 2 &&
			FVector2D::CrossProduct(OutHull.Last() - OutHull.Last(1), Point - OutHull.Last(1)) <= UE_DOUBLE_SMALL_NUMBER)
		{
			OutHull.Pop(EAllowShrinking::No);
		}
		OutHull.Add(Point);
	};
	for (int32 Index = 0; Index < Sorted.Num(); ++Index)
	{
		Chain(Sorted[Index], 0);
	}
	const int32 LowerCount = OutHull.Num() - 1;
	for (int32 Index = Sorted.Num() - 2; Index >= 0; --Index)
	{
		Chain(Sorted[Index], LowerCount);
	}
	// The last point is the first one again.
	OutHull.Pop(EAllowShrinking::No);
}

double FSandboxBodyProbe::MarginInsidePolygon(TConstArrayView<FVector2D> Polygon, const FVector2D& Point)
{
	if (Polygon.Num() == 0)
	{
		return -TNumericLimits<double>::Max();
	}

	double NearestEdge = TNumericLimits<double>::Max();
	bool bInside = Polygon.Num() >= 3;
	for (int32 Index = 0; Index < Polygon.Num(); ++Index)
	{
		const FVector2D& From = Polygon[Index];
		const FVector2D& To = Polygon[(Index + 1) % Polygon.Num()];
		const FVector2D Edge = To - From;
		const double LengthSquared = Edge.SizeSquared();
		const double Along = LengthSquared > 0.0 ? FMath::Clamp(FVector2D::DotProduct(Point - From, Edge) / LengthSquared, 0.0, 1.0) : 0.0;
		NearestEdge = FMath::Min(NearestEdge, FVector2D::Distance(Point, From + Edge * Along));
		// Counter-clockwise: the inside is to the left of every edge.
		bInside &= FVector2D::CrossProduct(Edge, Point - From) >= 0.0;
	}
	return bInside ? NearestEdge : -NearestEdge;
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
