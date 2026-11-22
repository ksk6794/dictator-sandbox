#include "SandboxWorldSpring.h"

#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/ConstraintInstance.h"

namespace
{
	// 1 N = 1 kg*m/s^2 = 100 kg*cm/s^2. A linear spring needs no factor: 1 N/m = 1 kg/s^2 in both systems.
	constexpr double ForceToUnreal = 100.0;
	// 1 N*m = 1e4 kg*cm^2/s^2. Chaos multiplies angular drive stiffness and damping by 1.5
	// (p.Chaos.JointConstraint.AngularDriveStiffnessScale); the factor is divided out, as for the muscles.
	constexpr double TorqueToUnreal = 1.0e4;
	constexpr double ChaosAngularDriveScale = 1.5;

	// The smallest share of the cap an axis keeps when the force points elsewhere, so that damping across
	// the direction of pull is never switched off. It lets the force exceed the cap by a quarter of a percent.
	constexpr double MinAxisShare = 0.05;
	// Below this demand the direction of the force means nothing and the cap is shared evenly.
	constexpr double SmallDemandN = 1.0e-3;
}

FSandboxWorldSpring::FSandboxWorldSpring() = default;

FSandboxWorldSpring::~FSandboxWorldSpring()
{
	Detach();
}

void FSandboxWorldSpring::Attach(FBodyInstance* InBody, const FTransform& GripFrame, UObject* DebugOwner, bool bAngular)
{
	Detach();
	if (!InBody || !InBody->IsValidBodyInstance())
	{
		return;
	}
	Body = InBody;
	Grip = GripFrame;
	bAngularDrive = bAngular;
	TargetLocation = GetGripLocation();

	Constraint = MakeUnique<FConstraintInstance>();
	Constraint->SetLinearLimits(LCM_Free, LCM_Free, LCM_Free, 0.0f);
	Constraint->SetAngularSwing1Motion(ACM_Free);
	Constraint->SetAngularSwing2Motion(ACM_Free);
	Constraint->SetAngularTwistMotion(ACM_Free);
	Constraint->SetDisableCollision(false);
	// Frame 1 is on the body. Frame 2 is the world itself, at the origin and unrotated, and the solver
	// measures a drive's target in the frame of the second ("parent") body: so targets are world poses.
	Constraint->SetRefFrame(EConstraintFrame::Frame1, Grip);
	Constraint->SetRefFrame(EConstraintFrame::Frame2, FTransform::Identity);

	Constraint->SetLinearPositionDrive(true, true, true);
	Constraint->SetLinearVelocityDrive(true, true, true);
	Constraint->SetLinearDriveAccelerationMode(false);
	Constraint->SetLinearPositionTarget(TargetLocation);
	Constraint->SetLinearVelocityTarget(FVector::ZeroVector);
	if (bAngularDrive)
	{
		Constraint->SetAngularDriveMode(EAngularDriveMode::SLERP);
		Constraint->SetOrientationDriveSLERP(true);
		Constraint->SetAngularVelocityDriveSLERP(true);
		Constraint->SetAngularDriveAccelerationMode(false);
		Constraint->SetAngularOrientationTarget((Body->GetUnrealWorldTransform().GetRotation() * Grip.GetRotation()));
		Constraint->SetAngularVelocityTarget(FVector::ZeroVector);
	}
	Constraint->InitConstraint(Body, nullptr, 1.0f, DebugOwner);
}

void FSandboxWorldSpring::Detach()
{
	if (Constraint)
	{
		Constraint->TermConstraint();
		Constraint.Reset();
	}
	Body = nullptr;
	DemandN = 0.0;
}

void FSandboxWorldSpring::SetLinear(double InStiffnessNPerM, double InDampingS, double InMaxForceN)
{
	StiffnessNPerM = InStiffnessNPerM;
	DampingS = InDampingS;
	MaxForceN = InMaxForceN;
	// The drive itself is written by the next SetTarget, with the caps of that moment.
}

void FSandboxWorldSpring::SetAngular(double StiffnessNmPerRad, double InDampingS, double MaxTorqueNm)
{
	if (Constraint && bAngularDrive)
	{
		const double Spring = StiffnessNmPerRad * TorqueToUnreal / ChaosAngularDriveScale;
		Constraint->SetAngularDriveParams(static_cast<float>(Spring), static_cast<float>(Spring * InDampingS), static_cast<float>(MaxTorqueNm * TorqueToUnreal));
	}
}

FVector FSandboxWorldSpring::GetGripLocation() const
{
	return Body ? Body->GetUnrealWorldTransform().TransformPositionNoScale(Grip.GetLocation()) : FVector::ZeroVector;
}

double FSandboxWorldSpring::GetGapCm() const
{
	return Body ? FVector::Dist(GetGripLocation(), TargetLocation) : 0.0;
}

FVector FSandboxWorldSpring::AxisCapsN(const FVector& DemandN, double MaxForceN)
{
	// The engine caps each axis separately. Caps in proportion to the demand's direction make a box whose
	// far corner lies on the sphere of the cap, in the direction the force points.
	const double Size = DemandN.Size();
	FVector Share(1.0 / FMath::Sqrt(3.0));
	if (Size > SmallDemandN)
	{
		Share = (DemandN / Size).GetAbs().ComponentMax(FVector(MinAxisShare));
	}
	return Share * MaxForceN;
}

void FSandboxWorldSpring::SetTarget(const FVector& Location, const FQuat& Rotation, const FVector& VelocityCmS)
{
	if (!Constraint || !Body)
	{
		return;
	}
	TargetLocation = Location;

	// The force the spring law asks for at this instant, in newtons. The solver works the real force out
	// itself during the step; this is only for its direction, and for the record.
	const FVector GripLocation = GetGripLocation();
	const FVector ErrorM = (Location - GripLocation) * 0.01;
	const FVector VelocityErrorMps = (VelocityCmS - Body->GetUnrealWorldVelocityAtPoint(GripLocation)) * 0.01;
	const FVector Demand = StiffnessNPerM * (ErrorM + DampingS * VelocityErrorMps);
	DemandN = Demand.Size();

	const FVector Caps = AxisCapsN(Demand, MaxForceN) * ForceToUnreal;
	Constraint->SetLinearDriveParams(FVector(StiffnessNPerM), FVector(StiffnessNPerM * DampingS), Caps);
	Constraint->SetLinearPositionTarget(Location);
	Constraint->SetLinearVelocityTarget(VelocityCmS);
	if (bAngularDrive)
	{
		Constraint->SetAngularOrientationTarget(Rotation);
	}
	// A body at rest falls asleep, and a new target for the spring does not wake it: the solver would
	// leave it where it is. A body that is being pulled is awake.
	Body->WakeInstance();
}
