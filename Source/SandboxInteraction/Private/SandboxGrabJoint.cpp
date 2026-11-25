#include "SandboxGrabJoint.h"

#include "Components/PrimitiveComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/ConstraintInstance.h"

FSandboxGrabJoint::FSandboxGrabJoint() = default;

FSandboxGrabJoint::~FSandboxGrabJoint()
{
	Detach();
}

namespace
{
	// As for the hand's own spring (FSandboxWorldSpring): 1 N*m = 1e4 kg*cm^2/s^2, and Chaos multiplies an
	// angular drive's stiffness and damping by 1.5, which is divided out.
	constexpr double GrabTorqueToUnreal = 1.0e4;
	constexpr double GrabChaosAngularDriveScale = 1.5;
}

bool FSandboxGrabJoint::Attach(FBodyInstance* InHand, const FVector& WorldPoint, UPrimitiveComponent* InComponent, FName InBone, UObject* DebugOwner,
	double HoldNmPerRad, double HoldDampingS, double HoldMaxTorqueNm, const FVector* ThingPoint)
{
	Detach();
	FBodyInstance* Other = InComponent ? InComponent->GetBodyInstance(InBone) : nullptr;
	if (!InHand || !InHand->IsValidBodyInstance() || !Other || !Other->IsValidBodyInstance() || !Other->IsInstanceSimulatingPhysics() || Other == InHand)
	{
		return false;
	}
	Hand = InHand;
	Component = InComponent;
	Bone = InBone;
	HandPoint = Hand->GetUnrealWorldTransform().InverseTransformPositionNoScale(WorldPoint);
	OtherPoint = Other->GetUnrealWorldTransform().InverseTransformPositionNoScale(ThingPoint ? *ThingPoint : WorldPoint);

	Constraint = MakeUnique<FConstraintInstance>();
	Constraint->SetLinearLimits(LCM_Locked, LCM_Locked, LCM_Locked, 0.0f);
	Constraint->SetAngularSwing1Motion(ACM_Free);
	Constraint->SetAngularSwing2Motion(ACM_Free);
	Constraint->SetAngularTwistMotion(ACM_Free);
	// Whether the two collide is the hand's business (USandboxEffectorComponent::SetPassesBodies).
	Constraint->SetDisableCollision(false);
	Constraint->SetRefFrame(EConstraintFrame::Frame1, FTransform(HandPoint));
	bHeldInDirection = HoldNmPerRad > 0.0;
	if (bHeldInDirection)
	{
		// Held in its direction: the frame on the thing is turned so that the two frames agree as things are
		// now, and a spring with a torque cap drives the turn between them to nothing.
		const FQuat AsTaken = Other->GetUnrealWorldTransform().GetRotation().Inverse() * Hand->GetUnrealWorldTransform().GetRotation();
		Constraint->SetRefFrame(EConstraintFrame::Frame2, FTransform(AsTaken, OtherPoint));
		Constraint->SetAngularDriveMode(EAngularDriveMode::SLERP);
		Constraint->SetOrientationDriveSLERP(true);
		Constraint->SetAngularVelocityDriveSLERP(true);
		Constraint->SetAngularDriveAccelerationMode(false);
		const double Spring = HoldNmPerRad * GrabTorqueToUnreal / GrabChaosAngularDriveScale;
		Constraint->SetAngularDriveParams(static_cast<float>(Spring), static_cast<float>(Spring * HoldDampingS), static_cast<float>(HoldMaxTorqueNm * GrabTorqueToUnreal));
		Constraint->SetAngularOrientationTarget(FQuat::Identity);
		Constraint->SetAngularVelocityTarget(FVector::ZeroVector);
	}
	else
	{
		Constraint->SetRefFrame(EConstraintFrame::Frame2, FTransform(OtherPoint));
	}
	Constraint->InitConstraint(Hand, Other, 1.0f, DebugOwner);
	if (!Constraint->IsValidConstraintInstance())
	{
		Detach();
		return false;
	}
	// Both bodies may be at rest and asleep; a new joint does not wake them.
	Hand->WakeInstance();
	Other->WakeInstance();
	return true;
}

void FSandboxGrabJoint::SetHold(double HoldNmPerRad, double HoldDampingS, double HoldMaxTorqueNm)
{
	if (Constraint && bHeldInDirection)
	{
		const double Spring = HoldNmPerRad * GrabTorqueToUnreal / GrabChaosAngularDriveScale;
		Constraint->SetAngularDriveParams(static_cast<float>(Spring), static_cast<float>(Spring * HoldDampingS), static_cast<float>(HoldMaxTorqueNm * GrabTorqueToUnreal));
	}
}

void FSandboxGrabJoint::Detach()
{
	if (Constraint)
	{
		Constraint->TermConstraint();
		Constraint.Reset();
	}
	Hand = nullptr;
	Component.Reset();
	Bone = NAME_None;
}

bool FSandboxGrabJoint::IsHolding() const
{
	if (!Constraint || !Hand)
	{
		return false;
	}
	const UPrimitiveComponent* Held = Component.Get();
	const FBodyInstance* Other = Held ? Held->GetBodyInstance(Bone) : nullptr;
	return Other && Other->IsValidBodyInstance() && Other->IsInstanceSimulatingPhysics();
}

double FSandboxGrabJoint::GetForceN(double SubstepS) const
{
	if (!Constraint || SubstepS <= 0.0)
	{
		return 0.0;
	}
	FVector Linear, Angular;
	Constraint->GetConstraintForce(Linear, Angular);
	// kg*cm/s over one substep, to newtons.
	return Linear.Size() / SubstepS * 0.01;
}

double FSandboxGrabJoint::GetStretchCm() const
{
	const UPrimitiveComponent* Held = Component.Get();
	const FBodyInstance* Other = Held ? Held->GetBodyInstance(Bone) : nullptr;
	if (!Constraint || !Hand || !Hand->IsValidBodyInstance() || !Other || !Other->IsValidBodyInstance())
	{
		return 0.0;
	}
	return FVector::Dist(Hand->GetUnrealWorldTransform().TransformPositionNoScale(HandPoint), Other->GetUnrealWorldTransform().TransformPositionNoScale(OtherPoint));
}
