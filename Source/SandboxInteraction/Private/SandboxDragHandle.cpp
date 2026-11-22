#include "SandboxDragHandle.h"

#include "Components/PrimitiveComponent.h"
#include "PhysicsEngine/BodyInstance.h"

bool FSandboxDragHandle::Grab(UPrimitiveComponent* InComponent, FName InBone, const FVector& WorldPoint, const FSandboxSpringTuning& Tuning, UObject* DebugOwner)
{
	Release();
	FBodyInstance* Body = InComponent ? InComponent->GetBodyInstance(InBone) : nullptr;
	if (!Body || !Body->IsValidBodyInstance() || !Body->IsInstanceSimulatingPhysics())
	{
		return false;
	}
	Component = InComponent;
	Bone = InBone;
	// The spring holds the body at the clicked point, so pulling there also turns it, as a hand would.
	const FTransform Grip(Body->GetUnrealWorldTransform().InverseTransformPositionNoScale(WorldPoint));
	Spring.Attach(Body, Grip, DebugOwner, /*bAngular*/ false);
	SetTuning(Tuning);
	Spring.SetTarget(WorldPoint, FQuat::Identity, FVector::ZeroVector);
	return true;
}

void FSandboxDragHandle::Release()
{
	Spring.Detach();
	Component.Reset();
	Bone = NAME_None;
}

void FSandboxDragHandle::SetTuning(const FSandboxSpringTuning& Tuning)
{
	Spring.SetLinear(Tuning.StiffnessNPerM, Tuning.DampingS, Tuning.MaxForceN);
}

void FSandboxDragHandle::SetTarget(const FVector& WorldPoint, const FVector& VelocityCmS)
{
	if (!Component.IsValid())
	{
		Release();
		return;
	}
	Spring.SetTarget(WorldPoint, FQuat::Identity, VelocityCmS);
}
