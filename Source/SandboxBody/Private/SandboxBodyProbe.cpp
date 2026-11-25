#include "SandboxBodyProbe.h"

#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "PhysicsEngine/PhysicsAsset.h"

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
		Sample.InertiaKgCm2 = Body->GetBodyInertiaTensor();
		Sample.InertiaRotation = Body->GetMassSpaceToWorldSpace().GetRotation();
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
		// Candidate points of this foot in world space, of the body itself and of the toes that hang on it: the corners
		// of its boxes and the undersides of its capsules' end spheres and of its spheres.
		TArray<FVector, TInlineAllocator<32>> Points;
		TArray<FName, TInlineAllocator<2>> Parts = { Bone };
		for (const FConstraintInstance* Joint : Mesh ? Mesh->Constraints : TArray<FConstraintInstance*>())
		{
			if (Joint && Joint->GetParentBoneName() == Bone)
			{
				Parts.Add(Joint->GetChildBoneName());
			}
		}
		for (const FName Part : Parts)
		{
		const FBodyInstance* Body = Mesh ? Mesh->GetBodyInstance(Part) : nullptr;
		const UBodySetup* Setup = Body && Body->IsValidBodyInstance() ? Body->GetBodySetup() : nullptr;
		if (!Setup)
		{
			continue;
		}
		const FTransform BodyTransform = Body->GetUnrealWorldTransform();
		const FVector Scale = Body->Scale3D;
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

		}
		if (Points.Num() == 0)
		{
			continue;
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

FVector2D FSandboxBodyProbe::NearestPointOfPolygon(TConstArrayView<FVector2D> Polygon, const FVector2D& Point)
{
	if (Polygon.Num() == 0 || (Polygon.Num() >= 3 && MarginInsidePolygon(Polygon, Point) >= 0.0))
	{
		return Point;
	}
	FVector2D Nearest = Polygon[0];
	double NearestDistance = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < Polygon.Num(); ++Index)
	{
		const FVector2D& From = Polygon[Index];
		const FVector2D& To = Polygon[(Index + 1) % Polygon.Num()];
		const FVector2D Edge = To - From;
		const double LengthSquared = Edge.SizeSquared();
		const double Along = LengthSquared > 0.0 ? FMath::Clamp(FVector2D::DotProduct(Point - From, Edge) / LengthSquared, 0.0, 1.0) : 0.0;
		const FVector2D OnEdge = From + Edge * Along;
		const double Distance = FVector2D::Distance(Point, OnEdge);
		if (Distance < NearestDistance)
		{
			NearestDistance = Distance;
			Nearest = OnEdge;
		}
	}
	return Nearest;
}

double FSandboxBodyProbe::DistanceToEdgeAlong(TConstArrayView<FVector2D> Polygon, const FVector2D& Point, const FVector2D& Direction)
{
	const FVector2D Way = Direction.GetSafeNormal();
	if (Polygon.Num() < 3 || Way.IsNearlyZero() || MarginInsidePolygon(Polygon, Point) < 0.0)
	{
		return 0.0;
	}
	// The nearest edge the ray leaves through: for a convex polygon, the least distance over the edges it
	// crosses going outward.
	double Nearest = TNumericLimits<double>::Max();
	for (int32 Index = 0; Index < Polygon.Num(); ++Index)
	{
		const FVector2D& From = Polygon[Index];
		const FVector2D Edge = Polygon[(Index + 1) % Polygon.Num()] - From;
		// Counter-clockwise: the outward normal is the edge turned clockwise.
		const FVector2D Outward(Edge.Y, -Edge.X);
		const double Toward = FVector2D::DotProduct(Way, Outward);
		if (Toward <= 0.0)
		{
			continue;
		}
		const double Distance = FVector2D::DotProduct(From - Point, Outward) / Toward;
		Nearest = FMath::Min(Nearest, FMath::Max(Distance, 0.0));
	}
	return Nearest == TNumericLimits<double>::Max() ? 0.0 : Nearest;
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

FVector FSandboxBodyProbe::CenterOfMassVelocity(const TArray<FSandboxBodySample>& Samples)
{
	FVector Momentum = FVector::ZeroVector;
	double Mass = 0.0;
	for (const FSandboxBodySample& Sample : Samples)
	{
		Momentum += Sample.LinearVelocity * Sample.MassKg;
		Mass += Sample.MassKg;
	}
	return Mass > 0.0 ? Momentum / Mass : FVector::ZeroVector;
}

FVector2D FSandboxBodyProbe::CapturePoint(const FVector& CenterOfMassCm, const FVector& VelocityCmS, double HeightCm, double GravityCmS2)
{
	// The time constant of the pendulum; a body at or below its support has none.
	const double TimeS = GravityCmS2 > 0.0 ? FMath::Sqrt(FMath::Max(HeightCm, 0.0) / GravityCmS2) : 0.0;
	return FVector2D(CenterOfMassCm.X + VelocityCmS.X * TimeS, CenterOfMassCm.Y + VelocityCmS.Y * TimeS);
}

void FSandboxBodyProbe::MassWeightedSpeeds(const TArray<FSandboxBodySample>& Samples, double& OutLinearMps, double& OutAngularRadS)
{
	double MassKg = 0.0;
	double LinearSquared = 0.0;
	double AngularSquared = 0.0;
	for (const FSandboxBodySample& Sample : Samples)
	{
		MassKg += Sample.MassKg;
		LinearSquared += Sample.MassKg * (Sample.LinearVelocity * 0.01).SizeSquared();
		AngularSquared += Sample.MassKg * Sample.AngularVelocityRad.SizeSquared();
	}
	OutLinearMps = MassKg > 0.0 ? FMath::Sqrt(LinearSquared / MassKg) : 0.0;
	OutAngularRadS = MassKg > 0.0 ? FMath::Sqrt(AngularSquared / MassKg) : 0.0;
}

double FSandboxBodyProbe::KineticEnergyJ(const TArray<FSandboxBodySample>& Samples)
{
	double Energy = 0.0;
	for (const FSandboxBodySample& Sample : Samples)
	{
		const double SpeedMps = Sample.LinearVelocity.Size() * 0.01;
		Energy += 0.5 * Sample.MassKg * SpeedMps * SpeedMps;
		// Rotation, about the principal axes of the body. 1 kg*cm^2 = 1e-4 kg*m^2.
		const FVector Spin = Sample.InertiaRotation.UnrotateVector(Sample.AngularVelocityRad);
		Energy += 0.5 * 1.0e-4 * (Sample.InertiaKgCm2.X * Spin.X * Spin.X + Sample.InertiaKgCm2.Y * Spin.Y * Spin.Y + Sample.InertiaKgCm2.Z * Spin.Z * Spin.Z);
	}
	return Energy;
}

double FSandboxBodyProbe::PotentialEnergyJ(const TArray<FSandboxBodySample>& Samples, double GravityCmS2, double FloorZCm)
{
	double Energy = 0.0;
	for (const FSandboxBodySample& Sample : Samples)
	{
		// kg * (cm/s^2 to m/s^2) * (cm to m).
		Energy += Sample.MassKg * (GravityCmS2 * 0.01) * ((Sample.CenterOfMass.Z - FloorZCm) * 0.01);
	}
	return Energy;
}

namespace
{
	/** One collision shape of a body as a scene query takes it: a simple shape and where it is in the world. */
	struct FProbeShape
	{
		FCollisionShape Shape;
		FVector Location = FVector::ZeroVector;
		FQuat Rotation = FQuat::Identity;
	};

	/** The boxes, capsules and spheres of a body in world space. Returns how many shapes of other kinds it has. */
	int32 CollectShapes(const FBodyInstance& Body, TArray<FProbeShape, TInlineAllocator<4>>& OutShapes)
	{
		OutShapes.Reset();
		const UBodySetup* Setup = Body.GetBodySetup();
		if (!Setup)
		{
			return 0;
		}
		const FTransform BodyTransform = Body.GetUnrealWorldTransform();
		const FVector Scale = Body.Scale3D;
		const double Uniform = Scale.GetAbsMax();
		const auto Place = [&BodyTransform, &Scale](const FTransform& Element, FProbeShape& Out)
		{
			Out.Location = BodyTransform.TransformPositionNoScale(Element.GetLocation() * Scale);
			Out.Rotation = (BodyTransform.GetRotation() * Element.GetRotation()).GetNormalized();
		};
		for (const FKBoxElem& Box : Setup->AggGeom.BoxElems)
		{
			FProbeShape& Out = OutShapes.AddDefaulted_GetRef();
			Out.Shape = FCollisionShape::MakeBox(FVector(Box.X, Box.Y, Box.Z) * 0.5 * Scale.GetAbs());
			Place(Box.GetTransform(), Out);
		}
		for (const FKSphylElem& Capsule : Setup->AggGeom.SphylElems)
		{
			// The engine's capsule shape is given by its radius and its half height including the end caps.
			FProbeShape& Out = OutShapes.AddDefaulted_GetRef();
			Out.Shape = FCollisionShape::MakeCapsule(static_cast<float>(Capsule.Radius * Uniform), static_cast<float>((0.5 * Capsule.Length + Capsule.Radius) * Uniform));
			Place(Capsule.GetTransform(), Out);
		}
		for (const FKSphereElem& Sphere : Setup->AggGeom.SphereElems)
		{
			FProbeShape& Out = OutShapes.AddDefaulted_GetRef();
			Out.Shape = FCollisionShape::MakeSphere(static_cast<float>(Sphere.Radius * Uniform));
			Place(Sphere.GetTransform(), Out);
		}
		return Setup->AggGeom.GetElementCount() - OutShapes.Num();
	}
}

void FSandboxBodyProbe::MeasurePenetration(const USkeletalMeshComponent* Mesh, TConstArrayView<const AActor*> Ignored, FSandboxPenetration& OutPenetration)
{
	OutPenetration = FSandboxPenetration();
	const UWorld* World = Mesh ? Mesh->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	struct FProbeBody
	{
		const FBodyInstance* Body = nullptr;
		FName Bone;
		FBox Bounds;
		TArray<FProbeShape, TInlineAllocator<4>> Shapes;
	};
	TArray<FProbeBody, TInlineAllocator<24>> Bodies;
	for (const FBodyInstance* Body : Mesh->Bodies)
	{
		if (Body && Body->IsValidBodyInstance())
		{
			FProbeBody& Entry = Bodies.AddDefaulted_GetRef();
			Entry.Body = Body;
			Entry.Bone = Mesh->GetBoneName(Body->InstanceBoneIndex);
			Entry.Bounds = Body->GetBodyBounds();
			OutPenetration.UnmeasuredShapes += CollectShapes(*Body, Entry.Shapes);
		}
	}

	// Against the world: everything that blocks, except the character itself and what the caller leaves out.
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SandboxBodyPenetration), /*bTraceComplex*/ false);
	Params.AddIgnoredActor(Mesh->GetOwner());
	for (const AActor* Actor : Ignored)
	{
		if (Actor)
		{
			Params.AddIgnoredActor(Actor);
		}
	}
	TArray<FOverlapResult> Overlaps;
	for (const FProbeBody& Entry : Bodies)
	{
		for (const FProbeShape& Shape : Entry.Shapes)
		{
			Overlaps.Reset();
			World->OverlapMultiByObjectType(Overlaps, Shape.Location, Shape.Rotation, Objects, Shape.Shape, Params);
			for (const FOverlapResult& Overlap : Overlaps)
			{
				UPrimitiveComponent* Component = Overlap.GetComponent();
				FMTDResult Separation;
				if (Component && Component->ComputePenetration(Separation, Shape.Shape, Shape.Location, Shape.Rotation) && Separation.Distance > OutPenetration.WorldCm)
				{
					OutPenetration.WorldCm = Separation.Distance;
					OutPenetration.WorldBone = Entry.Bone;
					OutPenetration.WorldInto = Component->GetOwner() ? Component->GetOwner()->GetFName() : Component->GetFName();
				}
			}
		}
	}

	// Against itself: pairs of bodies the physics asset lets collide. Jointed bodies never do.
	const UPhysicsAsset* Asset = Mesh->GetPhysicsAsset();
	TSet<TPair<FName, FName>> Jointed;
	for (const FConstraintInstance* Constraint : Mesh->Constraints)
	{
		if (Constraint)
		{
			Jointed.Add(TPair<FName, FName>(Constraint->GetChildBoneName(), Constraint->GetParentBoneName()));
			Jointed.Add(TPair<FName, FName>(Constraint->GetParentBoneName(), Constraint->GetChildBoneName()));
		}
	}
	for (int32 IndexA = 0; IndexA < Bodies.Num(); ++IndexA)
	{
		for (int32 IndexB = IndexA + 1; IndexB < Bodies.Num(); ++IndexB)
		{
			const FProbeBody& A = Bodies[IndexA];
			const FProbeBody& B = Bodies[IndexB];
			if (Jointed.Contains(TPair<FName, FName>(A.Bone, B.Bone)) || (Asset && !Asset->IsCollisionEnabled(A.Body->InstanceBodyIndex, B.Body->InstanceBodyIndex)))
			{
				continue;
			}
			++OutPenetration.SelfPairs;
			if (!A.Bounds.Intersect(B.Bounds))
			{
				continue;
			}
			for (const FProbeShape& Shape : A.Shapes)
			{
				FMTDResult Separation;
				if (B.Body->OverlapTest(Shape.Location, Shape.Rotation, Shape.Shape, &Separation) && Separation.Distance > OutPenetration.SelfCm)
				{
					OutPenetration.SelfCm = Separation.Distance;
					OutPenetration.SelfBoneA = A.Bone;
					OutPenetration.SelfBoneB = B.Bone;
				}
			}
		}
	}
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
