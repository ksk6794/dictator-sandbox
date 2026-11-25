#include "SandboxPropComponent.h"

#include "PhysicsEngine/BodySetup.h"

void USandboxPropComponent::SetBoxes(const TArray<TPair<FVector, FVector>>& InBoxes)
{
	Setup = NewObject<UBodySetup>(this);
	Setup->CollisionTraceFlag = CTF_UseSimpleAsComplex;
	Setup->bNeverNeedsCookedCollisionData = true;
	LocalBox = FBox(ForceInit);
	for (const TPair<FVector, FVector>& Each : InBoxes)
	{
		FKBoxElem Box(Each.Value.X, Each.Value.Y, Each.Value.Z);
		Box.Center = Each.Key;
		Setup->AggGeom.BoxElems.Add(Box);
		LocalBox += FBox(Each.Key - Each.Value * 0.5, Each.Key + Each.Value * 0.5);
	}
	Setup->InvalidatePhysicsData();
	Setup->CreatePhysicsMeshes();
}

FBoxSphereBounds USandboxPropComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	return FBoxSphereBounds(LocalBox.IsValid ? LocalBox : FBox(FVector(-1.0), FVector(1.0))).TransformBy(LocalToWorld);
}
