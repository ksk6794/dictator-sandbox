#include "SandboxBodyController.h"

#include "SandboxAnimInstance.h"
#include "SandboxBodyProbe.h"
#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PhysicsControlComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "Chaos/RigidParticles.h"
#include "Physics/Experimental/PhysInterface_Chaos.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "SandboxCore.h"
#include "SandboxParamStore.h"

namespace
{
	const FName MuscleSet(TEXT("Muscles"));
	const FName AssistSet(TEXT("Assist"));

	// Physics Control takes a "strength" and turns it into a spring constant of (2 pi strength)^2. With the
	// drives in force mode that constant is used as it is, in Unreal units, so a stiffness in physical units
	// is fed in through the inverse of that formula.
	float StrengthForSpring(double SpringUnrealUnits)
	{
		return static_cast<float>(FMath::Sqrt(FMath::Max(SpringUnrealUnits, 0.0)) / UE_DOUBLE_TWO_PI);
	}

	// 1 N*m = 1e4 kg*cm^2/s^2. Chaos then multiplies angular drive stiffness and damping by 1.5
	// (p.Chaos.JointConstraint.AngularDriveStiffnessScale and ...DampingScale); the factor is divided out
	// here so that the tuning values are the torques the solver really applies. Torque caps are not scaled.
	constexpr double TorqueToUnreal = 1.0e4;
	constexpr double ChaosAngularDriveScale = 1.5;
	// 1 N/m = 1 kg/s^2 in Unreal units as well (force in kg*cm/s^2 per cm), and Chaos does not scale it.
}

USandboxBodyController::USandboxBodyController()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void USandboxBodyController::OnUnregister()
{
	if (Mesh)
	{
		Mesh->OnComponentHit.RemoveDynamic(this, &USandboxBodyController::OnBodyHit);
	}
	ReleaseIslandRefresh();
	Super::OnUnregister();
}

const FSandboxAssistTuning& USandboxBodyController::AssistTuning(EAssistSlot Slot) const
{
	switch (Slot)
	{
	case EAssistSlot::Chest: return Tuning.Chest;
	case EAssistSlot::Head: return Tuning.Head;
	case EAssistSlot::Feet: return Tuning.Feet;
	default: return Tuning.Pelvis;
	}
}

bool USandboxBodyController::Start(USkeletalMeshComponent* InMesh, const FSandboxParamStore& InParams, FString& OutError)
{
	if (bStarted)
	{
		OutError = TEXT("The body controller is already started");
		return false;
	}
	if (!InMesh || InMesh->Bodies.Num() == 0)
	{
		OutError = TEXT("The body controller needs a skeletal mesh with physics bodies");
		return false;
	}
	FSandboxBodyTuning InTuning;
	if (!FSandboxBodyTuning::FromParams(InParams, FSandboxBodyTuning::StateName(ESandboxBodyState::Standing), InTuning, OutError))
	{
		return false;
	}

	const TPair<FName, EAssistSlot> AssistBones[] = {
		{ InTuning.Rig.Pelvis, EAssistSlot::Pelvis },
		{ InTuning.Rig.Chest, EAssistSlot::Chest },
		{ InTuning.Rig.Head, EAssistSlot::Head },
		{ InTuning.Rig.FootLeft, EAssistSlot::Feet },
		{ InTuning.Rig.FootRight, EAssistSlot::Feet } };
	for (const TPair<FName, EAssistSlot>& Entry : AssistBones)
	{
		const FBodyInstance* Body = InMesh->GetBodyInstance(Entry.Key);
		if (!Body || !Body->IsValidBodyInstance() || InMesh->GetBoneName(Body->InstanceBoneIndex) != Entry.Key)
		{
			OutError = FString::Printf(TEXT("Rig bone '%s' has no body in the physics asset"), *Entry.Key.ToString());
			return false;
		}
	}

	Mesh = InMesh;
	Tuning = InTuning;

	// The Physics Control component is ticked by hand from TickComponent, never by the engine.
	Control = NewObject<UPhysicsControlComponent>(GetOwner(), TEXT("PhysicsControl"));
	Control->PrimaryComponentTick.bStartWithTickEnabled = false;
	Control->SetupAttachment(Mesh);
	Control->RegisterComponent();
	Control->SetComponentTickEnabled(false);

	// Muscles: one control per rigid joint, in physics-asset order.
	for (const FConstraintInstance* Joint : Mesh->Constraints)
	{
		if (!Joint ||
			Joint->GetLinearXMotion() != LCM_Locked || Joint->GetLinearYMotion() != LCM_Locked || Joint->GetLinearZMotion() != LCM_Locked)
		{
			continue;
		}
		const FName Child = Joint->GetChildBoneName();
		const FName Parent = Joint->GetParentBoneName();
		if (!Mesh->GetBodyInstance(Child) || !Mesh->GetBodyInstance(Parent))
		{
			continue;
		}

		FMuscle Muscle;
		Muscle.Control = FName(*FString::Printf(TEXT("Muscle_%s"), *Child.ToString()));
		Muscle.ParentBone = Parent;
		Muscle.ChildBone = Child;
		Muscle.Group = FSandboxBodyTuning::GroupOfBone(Child);
		{
			const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
			const int32 ParentIndex = Skeleton.FindBoneIndex(Parent);
			const int32 ChildIndex = Skeleton.FindBoneIndex(Child);
			if (ParentIndex != INDEX_NONE && ChildIndex != INDEX_NONE)
			{
				Muscle.ReferenceRelative = FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, ParentIndex).GetRotation().Inverse() *
					FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, ChildIndex).GetRotation();
			}
		}
		if (!Control->CreateNamedControl(Muscle.Control, Mesh, Parent, Mesh, Child, FPhysicsControlData(), FPhysicsControlTarget(), MuscleSet))
		{
			OutError = FString::Printf(TEXT("Could not create the muscle control for joint '%s'"), *Child.ToString());
			return false;
		}
		Muscles.Add(Muscle);
	}

	// Balance assist: each body is held where it is now. The mesh has not been simulated yet, so "now" is
	// the pose it was spawned in.
	for (const TPair<FName, EAssistSlot>& Entry : AssistBones)
	{
		FAssist Assist;
		Assist.Control = FName(*FString::Printf(TEXT("Assist_%s"), *Entry.Key.ToString()));
		Assist.Bone = Entry.Key;
		Assist.Slot = Entry.Value;
		Assist.Target = Mesh->GetBodyInstance(Entry.Key)->GetUnrealWorldTransform();
		Assist.Target.SetScale3D(FVector::OneVector);
		if (Entry.Value == EAssistSlot::Feet)
		{
			Assist.Side = Entry.Key == InTuning.Rig.FootLeft ? 0 : 1;
			Assist.StartRotation = Assist.Target.GetRotation();
		}

		// The target is the pose of the whole body; the spring itself acts at the centre of mass.
		FPhysicsControlTarget Target;
		Target.TargetPosition = Assist.Target.GetLocation();
		Target.TargetOrientation = Assist.Target.Rotator();
		Target.bApplyControlPointToTarget = true;
		if (!Control->CreateNamedControl(Assist.Control, nullptr, NAME_None, Mesh, Assist.Bone, FPhysicsControlData(), Target, AssistSet))
		{
			OutError = FString::Printf(TEXT("Could not create the assist control for '%s'"), *Assist.Bone.ToString());
			return false;
		}
		Assists.Add(Assist);
	}

	// The anchors of pelvis, chest and head belong to the support, not to the place of spawn: they are kept in
	// the frame of the feet's anchors, and follow it when a foot is planted elsewhere.
	{
		FVector SupportLocation;
		FQuat SupportRotation;
		SupportFrame(SupportLocation, SupportRotation);
		for (FAssist& Assist : Assists)
		{
			if (Assist.Slot != EAssistSlot::Feet)
			{
				Assist.SupportLocation = SupportRotation.UnrotateVector(Assist.Target.GetLocation() - SupportLocation);
				Assist.SupportRotation = SupportRotation.Inverse() * Assist.Target.GetRotation();
			}
		}

		// Where the centre of mass stands over the feet: what the balance strategies bring it back to.
		TArray<FSandboxBodySample> Bodies;
		FSandboxBodyProbe::SampleBodies(Mesh, Bodies);
		const FVector CenterOfMass = FSandboxBodyProbe::CenterOfMass(Bodies);
		ComSupportLocation = SupportRotation.UnrotateVector(CenterOfMass - SupportLocation);

		// A lean about the ankles moves each body in proportion to its height.
		ComHeightCm = FMath::Max(CenterOfMass.Z - SupportLocation.Z, 1.0);
		for (FAssist& Assist : Assists)
		{
			if (Assist.Slot != EAssistSlot::Feet)
			{
				Assist.LeanFactor = (Assist.Target.GetLocation().Z - SupportLocation.Z) / ComHeightCm;
			}
		}
	}

	// The legs the strategies turn: each foot, and the thigh two bones above it.
	{
		const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
		const FName Feet[2] = { InTuning.Rig.FootLeft, InTuning.Rig.FootRight };
		for (int32 Side = 0; Side < 2; ++Side)
		{
			FootBoneName[Side] = Feet[Side];
			FootBoneIndex[Side] = Skeleton.FindBoneIndex(Feet[Side]);
			const int32 Calf = FootBoneIndex[Side] != INDEX_NONE ? Skeleton.GetParentIndex(FootBoneIndex[Side]) : INDEX_NONE;
			ThighBoneIndex[Side] = Calf != INDEX_NONE ? Skeleton.GetParentIndex(Calf) : INDEX_NONE;
			ThighBoneName[Side] = ThighBoneIndex[Side] != INDEX_NONE ? Skeleton.GetBoneName(ThighBoneIndex[Side]) : NAME_None;
			if (ThighBoneIndex[Side] == INDEX_NONE || !Mesh->GetBodyInstance(ThighBoneName[Side]))
			{
				OutError = FString::Printf(TEXT("Foot '%s' is not the end of a leg with a thigh body"), *Feet[Side].ToString());
				return false;
			}
		}
	}

	// Reference values for the assist-load estimate.
	double TotalMassKg = 0.0;
	for (const FBodyInstance* Body : Mesh->Bodies)
	{
		if (Body && Body->IsValidBodyInstance())
		{
			TotalMassKg += Body->GetBodyMass();
		}
	}
	const double GravityMps2 = GetWorld() ? FMath::Abs(GetWorld()->GetGravityZ()) * 0.01 : 9.8;
	WeightN = TotalMassKg * GravityMps2;
	StandingComHeightM = FMath::Max((Mesh->GetSkeletalCenterOfMass().Z - Mesh->GetComponentLocation().Z) * 0.01, 0.01);

	// The sense of balance: what the floor pushes on each body with is collected from the contact events.
	GravityCmS2 = GravityMps2 * 100.0;
	SupportImpulseNs.Init(FVector::ZeroVector, Mesh->Bodies.Num());
	SupportStepS = 0.0;
	LastSupportHeightCm = Mesh->GetComponentLocation().Z;
	Balance = FSandboxBalanceView();

	PushTuningToControls();

	// The target pose: joint angles from the tuning, evaluated by the mesh's animation instance.
	if (!PoseRig.Init(Mesh, OutError))
	{
		return false;
	}
	{
		TArray<FSandboxPoseBone> Bones;
		if (!PoseRig.Build(Tuning.Poses.Stance, Bones, OutError))
		{
			return false;
		}
	}
	ApplyPose();

	// A body under control never sleeps. A standing character is still enough for the solver to put its
	// island to sleep, and the next tick woke it again: one substep in six it was frozen, with no gravity,
	// no contacts and no contact events, so the feet seemed to carry five sixths of the weight. A body that
	// has to sense its balance and answer a touch at once is awake for as long as it is controlled.
	for (FBodyInstance* Body : Mesh->Bodies)
	{
		if (Body && Body->IsValidBodyInstance())
		{
			FPhysicsCommand::ExecuteWrite(Body->GetPhysicsActor(), [](const FPhysicsActorHandle& Actor)
			{
				Actor->GetGameThreadAPI().SetSleepType(Chaos::ESleepType::NeverSleep);
			});
		}
	}
	Mesh->WakeAllRigidBodies();

	// The body learns that it was hit from its own contacts: the physics scene reports every contact of
	// these bodies after the step, with its impulse. Nothing here knows what the other body is.
	// A body of a skeletal mesh asks for contact events only when its own flag and the component's are both
	// set (FBodyInstance::BuildBodyFilterData), and the call below sets the bodies' flags alone. Without
	// the component's flag the character would hear only of bodies that ask for events themselves, as the
	// hands do, and nothing of the floor or of a prop thrown at it.
	Mesh->BodyInstance.bNotifyRigidBodyCollision = true;
	Mesh->SetNotifyRigidBodyCollision(true);
	Mesh->OnComponentHit.AddUniqueDynamic(this, &USandboxBodyController::OnBodyHit);

	// Run after the mesh has evaluated its animation pose for this frame: that pose is the muscles' target.
	AddTickPrerequisiteComponent(Mesh);
	Params = &InParams;
	ParamsRevision = InParams.GetRevision();
	State = ESandboxBodyState::Standing;
	StateLog.Reset();
	StateLog.Add(FSandboxStateChange{ 0.0, State, TEXT("started") });
	bStarted = true;

	UE_LOG(LogSandbox, Display, TEXT("Body controller started: %d muscles, %d assist controls, %.1f kg, centre of mass %.2f m above the origin"),
		Muscles.Num(), Assists.Num(), TotalMassKg, StandingComHeightM);
	return true;
}

bool USandboxBodyController::ApplyTuning(const FSandboxBodyTuning& InTuning, FString& OutError)
{
	if (!bStarted)
	{
		OutError = TEXT("The body controller is not started");
		return false;
	}
	if (!InTuning.Validate(OutError))
	{
		return false;
	}
	const FSandboxBodyRig Rig = Tuning.Rig;
	const FSandboxSolverTuning Solver = Tuning.Solver;
	Tuning = InTuning;
	Tuning.Rig = Rig;
	PushTuningToControls();
	ApplyPose();
	if (Solver.PositionIterations != Tuning.Solver.PositionIterations || Solver.VelocityIterations != Tuning.Solver.VelocityIterations ||
		Solver.ProjectionIterations != Tuning.Solver.ProjectionIterations)
	{
		RefreshSolverIsland();
	}
	return true;
}

void USandboxBodyController::ApplyPose()
{
	USandboxAnimInstance* Animation = Mesh ? Cast<USandboxAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
	if (!Animation)
	{
		// A mesh without the sandbox animation instance holds its reference pose; that is all it can do.
		return;
	}
	TArray<FSandboxPoseBone> Bones;
	FString Error;
	if (!PoseRig.Build(Tuning.Poses.Stance, Bones, Error))
	{
		UE_LOG(LogSandbox, Error, TEXT("Body controller keeps its pose: %s"), *Error);
		return;
	}
	Animation->SetTargetPose(Bones, Tuning.Poses.BlendS);
}

void USandboxBodyController::UpdatePoseTurns()
{
	USandboxAnimInstance* Animation = Cast<USandboxAnimInstance>(Mesh->GetAnimInstance());
	if (!Animation)
	{
		return;
	}
	TArray<FSandboxPoseTurn> Turns;
	// The flinch's pose offset: as strong as the hit, up to reactions.flinch_pose_full_ns, and gone by the time
	// the muscles are back to full strength. It fades by its own clock, not with the state: a hit that knocks
	// the body over ends the flinch at once, and an offset dropped in one step had the muscles snap the body
	// back (13.8 J put into a falling body). Once the body lies, nothing is left of it.
	if (FlinchStartedS >= 0.0 && State != ESandboxBodyState::Lying)
	{
		const FSandboxReactionTuning& Reactions = Tuning.Reactions;
		const double Strength = FMath::Clamp(FlinchImpulseNs.Size() / Reactions.FlinchPoseFullNs, 0.0, 1.0);
		const double Left = Reactions.FlinchRecoveryS > 0.0 ? FMath::Clamp(1.0 - (ElapsedS - FlinchStartedS) / Reactions.FlinchRecoveryS, 0.0, 1.0) : 0.0;
		const double Weight = Strength * Left;
		PoseRig.BuildTurns(Tuning.Poses.Flinch, Weight, Turns);

		// The struck body gives way: its joint turns so that the body moves along the hit, about the axis
		// across the line from the joint to the body's centre of mass and the hit.
		const int32 StruckIndex = PoseRig.MeshBoneIndexOf(FlinchBone);
		const FBodyInstance* Struck = Mesh->GetBodyInstance(FlinchBone);
		if (Weight > 0.0 && StruckIndex != INDEX_NONE && Struck && Struck->IsValidBodyInstance())
		{
			const FTransform Bone = Struck->GetUnrealWorldTransform();
			const FVector Axis = FVector::CrossProduct(Struck->GetCOMPosition() - Bone.GetLocation(), FlinchImpulseNs).GetSafeNormal();
			if (!Axis.IsNearlyZero())
			{
				const FQuat Give(Bone.GetRotation().UnrotateVector(Axis), FMath::DegreesToRadians(Reactions.FlinchGiveDeg * Weight));
				FSandboxPoseTurn* Existing = Turns.FindByPredicate([StruckIndex](const FSandboxPoseTurn& Turn) { return Turn.MeshBoneIndex == StruckIndex; });
				if (Existing)
				{
					Existing->Turn = (Existing->Turn * Give).GetNormalized();
				}
				else
				{
					Turns.Add(FSandboxPoseTurn{ StruckIndex, Give });
				}
			}
		}
	}
	AddStrategyTurns(Turns);

	// Nothing to hand over while there is no offset now and there was none before.
	if (Turns.Num() > 0 || bPoseTurnsSet)
	{
		Animation->SetTurns(Turns);
		bPoseTurnsSet = Turns.Num() > 0;
	}
}

void USandboxBodyController::UpdateStrategies(double DeltaTime)
{
	const FSandboxBalanceTuning& Limits = Tuning.Balance;
	const bool bUpright = State == ESandboxBodyState::Standing || State == ESandboxBodyState::Flinch;
	FVector2D LeanGoal = FVector2D::ZeroVector;
	FVector2D HipGoal = FVector2D::ZeroVector;
	StrategyErrorCm = FVector2D::ZeroVector;
	const int32 Feet = Balance.bValid ? Balance.StandingFeet.Num() : 0;

	// Leaning into a held push: the centre of mass goes F h / (m g) toward the force, so that the centre of
	// pressure can stay in the middle of the feet. It follows the sustained force with the strategies' lag,
	// and lets go of the lean the same way.
	FVector2D LeanGoal2 = FVector2D::ZeroVector;
	if (bUpright && WeightN > 0.0)
	{
		LeanGoal2 = -SustainedMomentNCm / WeightN;
		if (LeanGoal2.Size() > Limits.LeanMaxCm)
		{
			LeanGoal2 = LeanGoal2.GetSafeNormal() * Limits.LeanMaxCm;
		}
	}
	{
		const double LeanFollow = Limits.ResponseS > 0.0 ? 1.0 - FMath::Exp(-DeltaTime / Limits.ResponseS) : 1.0;
		LeanCm += (LeanGoal2 - LeanCm) * LeanFollow;
		if (LeanGoal2.IsZero() && LeanCm.Size() < 1.0e-6)
		{
			LeanCm = FVector2D::ZeroVector;
		}
	}
	// The assist leans with the body rather than holding it upright against the push.
	for (FAssist& Assist : Assists)
	{
		if (Assist.Slot != EAssistSlot::Feet && Assist.AppliedLeanCm != LeanCm)
		{
			Assist.AppliedLeanCm = LeanCm;
			SetAssistTarget(Assist);
		}
	}

	if (bUpright && Feet > 0)
	{
		FVector SupportLocation;
		FQuat SupportRotation;
		SupportFrame(SupportLocation, SupportRotation);
		SupportForward = SupportRotation.RotateVector(FVector(0.0, 1.0, 0.0));
		SupportSide = SupportRotation.RotateVector(FVector(1.0, 0.0, 0.0));
		const FVector Reference = SupportLocation + SupportRotation.RotateVector(ComSupportLocation);
		StrategyErrorCm = Balance.CapturePoint - (FVector2D(Reference.X, Reference.Y) + LeanCm);
		// The error in the support's own axes: forward, sideways.
		const FVector2D Error(
			StrategyErrorCm.X * SupportForward.X + StrategyErrorCm.Y * SupportForward.Y,
			StrategyErrorCm.X * SupportSide.X + StrategyErrorCm.Y * SupportSide.Y);

		// Ankles: the centre of pressure moves toward the capture point by the gain, plus a slow part that takes
		// up a steady error. Pressing the toes down by a torque tau moves it by tau / weight; the legs' muscles
		// give that torque when their target turns by tau / stiffness.
		if (Limits.AnkleIntegralS > 0.0)
		{
			// The slow part keeps the pelvis over the place it stands on, which is where the assist's anchor
			// holds it: whatever steady offset the posture settles into under its weight is taken up by the
			// ankles instead of by the assist.
			FVector2D PelvisErrorCm = FVector2D::ZeroVector;
			if (const FAssist* Pelvis = Assists.FindByPredicate([](const FAssist& Assist) { return Assist.Slot == EAssistSlot::Pelvis; }))
			{
				const FBodyInstance* Body = Mesh->GetBodyInstance(Pelvis->Bone);
				if (Body && Body->IsValidBodyInstance())
				{
					const FVector Offset = Body->GetCOMPosition() - AssistGoal(*Pelvis).TransformPositionNoScale(Body->GetMassSpaceLocal().GetLocation());
					PelvisErrorCm = FVector2D(Offset.X * SupportForward.X + Offset.Y * SupportForward.Y, Offset.X * SupportSide.X + Offset.Y * SupportSide.Y);
				}
			}
			AnkleIntegralCm += PelvisErrorCm * (DeltaTime / Limits.AnkleIntegralS);
			if (AnkleIntegralCm.Size() > Limits.AnkleIntegralMaxCm)
			{
				AnkleIntegralCm = AnkleIntegralCm.GetSafeNormal() * Limits.AnkleIntegralMaxCm;
			}
		}
		else
		{
			AnkleIntegralCm = FVector2D::ZeroVector;
		}
		const double StiffnessNmPerRad = Tuning.Legs.StiffnessNmPerRad * MuscleScale(ESandboxMuscleGroup::Legs) * Feet;
		if (StiffnessNmPerRad > 0.0)
		{
			const FVector2D ShiftM = (FVector2D(Error.X * Limits.AnkleGain, Error.Y * Limits.AnkleSideGain) + AnkleIntegralCm) * 0.01;
			// Leaning into a held push the body tilts about its ankles as a whole: their targets turn by the
			// lean's angle, so that the muscles hold the lean instead of pulling the body back upright (the
			// push's torque and the lean's weight cancel at the ankles). A lean forward turns the body away from
			// a centre of pressure behind it, hence the sign.
			const FVector2D Lean(LeanCm.X * SupportForward.X + LeanCm.Y * SupportForward.Y, LeanCm.X * SupportSide.X + LeanCm.Y * SupportSide.Y);
			const FVector2D TiltRad = -Lean / ComHeightCm;
			const double MaxRad = FMath::DegreesToRadians(Limits.AnkleMaxDeg);
			LeanGoal.X = FMath::Clamp(ShiftM.X * WeightN / StiffnessNmPerRad + TiltRad.X, -MaxRad, MaxRad);
			LeanGoal.Y = FMath::Clamp(ShiftM.Y * WeightN / StiffnessNmPerRad + TiltRad.Y, -MaxRad, MaxRad);
		}

		// Hips: once the capture point is outside the support, where the ankles cannot help, the trunk bends
		// toward it, in proportion to how far out it is. Forward and back only: bending sideways is not how a
		// person catches a fall to the side.
		const double OutsideCm = FMath::Max(-LastDecisionMarginCm, 0.0);
		if (OutsideCm > 0.0 && Limits.HipDegPerCm > 0.0 && Error.X != 0.0)
		{
			const double BendRad = FMath::DegreesToRadians(FMath::Min(OutsideCm * Limits.HipDegPerCm, Limits.HipMaxDeg));
			HipGoal = FVector2D(FMath::Sign(Error.X) * BendRad, 0.0);
		}
	}
	else
	{
		AnkleIntegralCm = FVector2D::ZeroVector;
	}

	// Follow, never jump: a first-order lag toward the goal, also on the way out when the body falls.
	const double Follow = Limits.ResponseS > 0.0 ? 1.0 - FMath::Exp(-DeltaTime / Limits.ResponseS) : 1.0;
	AnkleLeanRad += (LeanGoal - AnkleLeanRad) * Follow;
	HipBendRad += (HipGoal - HipBendRad) * Follow;
	// A turn too small to matter is no turn: a body without strategies carries none, and its run is unchanged.
	constexpr double NegligibleRad = 1.0e-7;
	if (LeanGoal.IsZero() && AnkleLeanRad.Size() < NegligibleRad)
	{
		AnkleLeanRad = FVector2D::ZeroVector;
	}
	if (HipGoal.IsZero() && HipBendRad.Size() < NegligibleRad)
	{
		HipBendRad = FVector2D::ZeroVector;
	}
	bStrategyActive = !AnkleLeanRad.IsZero() || !HipBendRad.IsZero();
}

void USandboxBodyController::AddStrategyTurns(TArray<FSandboxPoseTurn>& Turns) const
{
	if (!bStrategyActive)
	{
		return;
	}
	// The turns are rotations of the world as the bodies are now, carried into each bone's own space.
	const auto Rotation = [](const FVector& Toward, double Angle)
	{
		// Turns the up direction toward Toward by Angle (radians).
		const FVector Axis = FVector::CrossProduct(FVector::UpVector, Toward).GetSafeNormal();
		return Axis.IsNearlyZero() || Angle == 0.0 ? FQuat::Identity : FQuat(Axis, Angle);
	};
	const auto AddTurn = [&Turns](int32 BoneIndex, const FQuat& Turn)
	{
		if (BoneIndex == INDEX_NONE || Turn.Equals(FQuat::Identity, 0.0))
		{
			return;
		}
		FSandboxPoseTurn* Existing = Turns.FindByPredicate([BoneIndex](const FSandboxPoseTurn& Each) { return Each.MeshBoneIndex == BoneIndex; });
		if (Existing)
		{
			Existing->Turn = (Existing->Turn * Turn).GetNormalized();
		}
		else
		{
			Turns.Add(FSandboxPoseTurn{ BoneIndex, Turn.GetNormalized() });
		}
	};

	// The ankles: the body above the feet leans away from where the centre of pressure is to go, forward
	// as one piece, sideways as a parallelogram (the legs lean, the pelvis stays level).
	const FVector LeanForward = SupportForward * AnkleLeanRad.X;
	const FVector LeanSide = SupportSide * AnkleLeanRad.Y;
	const FQuat Forward = Rotation(-LeanForward.GetSafeNormal(), LeanForward.Size());
	const FQuat Sideways = Rotation(-LeanSide.GetSafeNormal(), LeanSide.Size());
	const FQuat Legs = Sideways * Forward;
	// The hips: the trunk bends toward the capture point.
	const FVector Bend = SupportForward * HipBendRad.X + SupportSide * HipBendRad.Y;
	const FQuat Trunk = Rotation(Bend.GetSafeNormal(), Bend.Size());

	// Both legs turn alike. (Turning each in proportion to the load it carries was tried: the less loaded leg
	// turned less, the body rolled onto the other, and under a held push it rocked from foot to foot.)
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FBodyInstance* Foot = Mesh->GetBodyInstance(FootBoneName[Side]);
		const FBodyInstance* Thigh = Mesh->GetBodyInstance(ThighBoneName[Side]);
		if (!Foot || !Thigh || !Foot->IsValidBodyInstance() || !Thigh->IsValidBodyInstance())
		{
			continue;
		}
		// The foot stays on the floor while everything above it turns by Legs: relative to its calf it turns back.
		const FQuat FootWorld = Foot->GetUnrealWorldTransform().GetRotation();
		AddTurn(FootBoneIndex[Side], FootWorld.Inverse() * Legs.Inverse() * FootWorld);
		// The thigh stays while the trunk bends: relative to the pelvis it turns back.
		const FQuat ThighWorld = Thigh->GetUnrealWorldTransform().GetRotation();
		AddTurn(ThighBoneIndex[Side], ThighWorld.Inverse() * Trunk.Inverse() * ThighWorld);
	}
}

void USandboxBodyController::RefreshSolverIsland()
{
	// Chaos works out a simulation island's iteration counts from its bodies only when something joins or
	// leaves the island (FPBDIslandManager, "this won't handle updates to per-body iterations after being
	// added to an island"). A standing character's island never changes, so new counts would be ignored.
	// A constraint to the world that leaves every axis free is such a change and applies no force; it is
	// taken away again on the next tick, which is a second change and leaves nothing behind.
	ReleaseIslandRefresh();
	FBodyInstance* Body = Mesh->GetBodyInstance(Tuning.Rig.Pelvis);
	if (!Body || !Body->IsValidBodyInstance())
	{
		return;
	}
	IslandRefresh = MakeUnique<FConstraintInstance>();
	IslandRefresh->SetLinearLimits(LCM_Free, LCM_Free, LCM_Free, 0.0f);
	IslandRefresh->SetAngularSwing1Motion(ACM_Free);
	IslandRefresh->SetAngularSwing2Motion(ACM_Free);
	IslandRefresh->SetAngularTwistMotion(ACM_Free);
	IslandRefresh->SetRefFrame(EConstraintFrame::Frame1, FTransform::Identity);
	IslandRefresh->SetRefFrame(EConstraintFrame::Frame2, Body->GetUnrealWorldTransform());
	IslandRefresh->InitConstraint(Body, nullptr, 1.0f, this);
}

void USandboxBodyController::ReleaseIslandRefresh()
{
	if (IslandRefresh)
	{
		IslandRefresh->TermConstraint();
		IslandRefresh.Reset();
	}
}

bool USandboxBodyController::ReadStateTuning(FSandboxBodyTuning& OutTuning, FString& OutError) const
{
	return FSandboxBodyTuning::FromParams(*Params, FSandboxBodyTuning::StateName(State), OutTuning, OutError);
}

double USandboxBodyController::MuscleScale(ESandboxMuscleGroup Group) const
{
	if (State != ESandboxBodyState::Flinch || Group != FlinchGroup)
	{
		return 1.0;
	}
	// Weak at the moment of the hit, back to full strength in a straight line.
	const double Recovered = Tuning.Reactions.FlinchRecoveryS > 0.0 ? FMath::Clamp((ElapsedS - StateEnteredS) / Tuning.Reactions.FlinchRecoveryS, 0.0, 1.0) : 1.0;
	return FMath::Lerp(Tuning.Reactions.FlinchWeakening, 1.0, Recovered);
}

void USandboxBodyController::EnterState(ESandboxBodyState NewState, const FString& Reason)
{
	State = NewState;
	StateEnteredS = ElapsedS;
	RestS = 0.0;
	StateLog.Add(FSandboxStateChange{ ElapsedS, NewState, Reason });
	UE_LOG(LogSandbox, Display, TEXT("Body controller: %s at %.3f s (%s)"), FSandboxBodyTuning::StateName(NewState), ElapsedS, *Reason);

	// Each state has its own values. The store has checked every state that sets any, so this reads.
	FSandboxBodyTuning NewTuning;
	FString Error;
	if (!ReadStateTuning(NewTuning, Error) || !ApplyTuning(NewTuning, Error))
	{
		UE_LOG(LogSandbox, Error, TEXT("Body controller keeps the previous state's tuning: %s"), *Error);
	}

	// A falling body has no use for a foot held at a point of the floor: its legs would keep reaching for it
	// as the body goes down, and feed it.
	if (NewState == ESandboxBodyState::Falling)
	{
		ReleaseFootTargets();
	}
}

void USandboxBodyController::ApplyImpulse(FName Bone, const FVector& ImpulseNs)
{
	FBodyInstance* Body = bStarted ? Mesh->GetBodyInstance(Bone) : nullptr;
	if (!Body || !Body->IsValidBodyInstance())
	{
		return;
	}
	// 1 N*s = 1 kg*m/s = 100 kg*cm/s in Unreal units.
	Body->AddImpulse(ImpulseNs * 100.0, /*bVelChange*/ false);
	FeelImpulse(Bone, ImpulseNs, Body->GetCOMPosition());
}

bool USandboxBodyController::SetFootTarget(FName Foot, const FVector& WorldLocation, double Weight, FString& OutError)
{
	USandboxAnimInstance* Animation = bStarted ? Cast<USandboxAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
	if (!Animation)
	{
		OutError = TEXT("The body is not started, or its mesh has no sandbox animation instance");
		return false;
	}
	const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
	const int32 End = Skeleton.FindBoneIndex(Foot);
	const int32 Middle = End != INDEX_NONE ? Skeleton.GetParentIndex(End) : INDEX_NONE;
	const int32 Root = Middle != INDEX_NONE ? Skeleton.GetParentIndex(Middle) : INDEX_NONE;
	if (Root == INDEX_NONE || !Mesh->GetBodyInstance(Foot) || !Mesh->GetBodyInstance(Skeleton.GetBoneName(Middle)) ||
		!Mesh->GetBodyInstance(Skeleton.GetBoneName(Root)))
	{
		OutError = FString::Printf(TEXT("'%s' is not the end of two segments with bodies"), *Foot.ToString());
		return false;
	}
	Animation->SetLimbTarget(End, WorldLocation, Weight, Tuning.Poses.BlendS);
	PlacedFeet.Remove(End);
	if (Weight > 0.0)
	{
		PlacedFeet.Add(End, WorldLocation);
	}
	return true;
}

void USandboxBodyController::ReleaseFootTargets()
{
	USandboxAnimInstance* Animation = Mesh ? Cast<USandboxAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
	if (Animation)
	{
		// Eased out like any placement: a leg let go in one step would snap back.
		for (const TPair<int32, FVector>& Placed : PlacedFeet)
		{
			Animation->SetLimbTarget(Placed.Key, Placed.Value, 0.0, Tuning.Poses.BlendS);
		}
	}
	PlacedFeet.Reset();
}

void USandboxBodyController::OnBodyHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	// An arm resting against the character's own side is neither a hit nor support.
	if (!bStarted || !OtherComponent || OtherComponent == Mesh)
	{
		return;
	}
	// One report per pair of bodies and substep, in kg*cm/s. The impulse vector's sign follows the order
	// the solver holds the pair in; the normal is always given from this body's side, so the two together
	// give impulses that add up properly when one hand touches several of the character's bodies.
	const FVector ImpulseNs = Hit.ImpactNormal * (NormalImpulse.Size() * 0.01);

	// A hit is a contact with another body that moves under physics: a hand, a thrown prop. The floor
	// under the feet pushes back with the character's whole weight in every step and is no hit: it is what
	// the body stands on, and is kept for the sense of balance.
	if (!OtherComponent->IsSimulatingPhysics(Hit.BoneName))
	{
		const FBodyInstance* Body = Mesh->GetBodyInstance(Hit.MyBoneName);
		if (Body && SupportImpulseNs.IsValidIndex(Body->InstanceBodyIndex))
		{
			SupportImpulseNs[Body->InstanceBodyIndex] += ImpulseNs;
		}
		return;
	}
	ContactImpulseNs += ImpulseNs.Size();
	++ContactReports;
	FeelImpulse(Hit.MyBoneName, ImpulseNs, Hit.ImpactPoint);
}

void USandboxBodyController::FeelImpulse(FName Bone, const FVector& ImpulseNs, const FVector& PointCm)
{
	FrameImpulseNs += ImpulseNs;
	FrameMomentNsCm += FVector2D(ImpulseNs.X, ImpulseNs.Y) * (PointCm.Z - LastSupportHeightCm);
	if (ImpulseNs.Size() > FrameBoneImpulseNs)
	{
		FrameBoneImpulseNs = ImpulseNs.Size();
		FrameBone = Bone;
	}
}

void USandboxBodyController::FeelFrame(double DeltaTime)
{
	const FSandboxReactionTuning& Reactions = Tuning.Reactions;
	const FVector Felt = FrameImpulseNs;

	// The hit: what this frame brought beyond the pressure felt so far. A blow is all hit; a hand that keeps
	// pressing is hit only while its force rises, and its pressure alone makes nobody flinch.
	const FVector HitNs = Felt - PressureN * DeltaTime;
	if (!Felt.IsZero())
	{
		// What is felt is the hit as a whole: hits that follow each other closely are added up.
		if (ElapsedS - HitStartedS > Tuning.Thresholds.FlinchWindowS)
		{
			HitImpulseNs = FVector::ZeroVector;
			HitStartedS = ElapsedS;
		}
		HitImpulseNs += HitNs;
		// A pressure that falls away is no hit, however fast: only what pushes along with this frame counts.
		if (State == ESandboxBodyState::Standing && HitImpulseNs.Size() >= Tuning.Thresholds.FlinchImpulseNs && (HitImpulseNs | Felt) > 0.0)
		{
			FlinchGroup = FSandboxBodyTuning::GroupOfBone(FrameBone);
			FlinchBone = FrameBone;
			FlinchImpulseNs = HitImpulseNs;
			FlinchStartedS = ElapsedS;
			EnterState(ESandboxBodyState::Flinch, FString::Printf(TEXT("hit of %.1f N*s on %s"), HitImpulseNs.Size(), *FrameBone.ToString()));
			HitImpulseNs = FVector::ZeroVector;
		}
	}

	// The pressure follows the felt force.
	if (DeltaTime > 0.0)
	{
		const double Follow = Reactions.PressureS > 0.0 ? 1.0 - FMath::Exp(-DeltaTime / Reactions.PressureS) : 1.0;
		PressureN += (Felt / DeltaTime - PressureN) * Follow;
		if (PressureN.Size() < 1.0e-6)
		{
			PressureN = FVector::ZeroVector;
		}

		// The sustained force: the least of the last frames, so that only a push felt in all of them counts.
		const int32 Frames = FMath::Max(FMath::CeilToInt32(Reactions.HeldPushS / DeltaTime), 1);
		if (RecentForceN.Num() != Frames)
		{
			RecentForceN.Init(FVector::ZeroVector, Frames);
			RecentMomentNCm.Init(FVector2D::ZeroVector, Frames);
			RecentNext = 0;
		}
		RecentForceN[RecentNext] = Felt / DeltaTime;
		RecentMomentNCm[RecentNext] = FrameMomentNsCm / DeltaTime;
		RecentNext = (RecentNext + 1) % Frames;
		int32 Least = 0;
		for (int32 Index = 1; Index < Frames; ++Index)
		{
			if (RecentForceN[Index].SizeSquared() < RecentForceN[Least].SizeSquared())
			{
				Least = Index;
			}
		}
		SustainedN = RecentForceN[Least];
		SustainedMomentNCm = RecentMomentNCm[Least];
	}

	FrameImpulseNs = FVector::ZeroVector;
	FrameMomentNsCm = FVector2D::ZeroVector;
	FrameBone = NAME_None;
	FrameBoneImpulseNs = 0.0;
}

void USandboxBodyController::UpdateState(float DeltaTime)
{
	if (State == ESandboxBodyState::Standing || State == ESandboxBodyState::Flinch)
	{
		// The capture point decides. Inside the support, or outside by no more than the ankles and the hips
		// can bring back, the body stands. Further out it needs a step; it cannot step yet (M3 part 4), so the
		// assist holds it and the need is reported. Beyond a step's reach balance is lost. Each holds only
		// after decision_s, so that a foot that loses its load for one frame decides nothing.
		const FSandboxBalanceTuning& Limits = Tuning.Balance;
		LastDecisionMarginCm = DecisionMarginCm();
		// Where the capture point will be when a step started now has landed: it runs away from the support's
		// edge by e^(omega t) (linear inverted pendulum).
		const double HeightCm = FMath::Max(Balance.CenterOfMass.Z - Balance.SupportHeightCm, 1.0);
		const double Growth = FMath::Exp(FMath::Sqrt(GravityCmS2 / HeightCm) * Limits.StepTimeS);
		const double AtLandingCm = -LastDecisionMarginCm * Growth;
		// A step being needed has to last a moment. Balance beyond a step's reach is lost at once: the capture
		// point is the whole body's momentum, which a blow gives it in one step, and waiting would only let
		// the assist take back what the body could not.
		StepNeededS = LastDecisionMarginCm < -Limits.RecoverableCm ? StepNeededS + DeltaTime : 0.0;
		LostS = AtLandingCm > Limits.StepReachCm ? LostS + DeltaTime : 0.0;
		bStepNeeded = StepNeededS >= Limits.DecisionS - KINDA_SMALL_NUMBER;
		if (LostS > 0.0)
		{
			bStepNeeded = false;
			EnterState(ESandboxBodyState::Falling, FString::Printf(TEXT("capture point %.1f cm outside the support, %.0f cm by the time a step lands: beyond a step"),
				-LastDecisionMarginCm, AtLandingCm));
		}
	}
	else
	{
		bStepNeeded = false;
		StepNeededS = 0.0;
		LostS = 0.0;
	}

	if (State == ESandboxBodyState::Standing || State == ESandboxBodyState::Flinch)
	{
		// Whatever the capture point says, the posture itself can give way: the legs fold (the pelvis sinks) or
		// the body bends over too far to stand.
		const FAssist* Pelvis = Assists.FindByPredicate([](const FAssist& Assist) { return Assist.Slot == EAssistSlot::Pelvis; });
		const FBodyInstance* Body = Pelvis ? Mesh->GetBodyInstance(Pelvis->Bone) : nullptr;
		if (Body && Body->IsValidBodyInstance())
		{
			const FTransform Now = Body->GetUnrealWorldTransform();
			const double DropCm = Pelvis->Target.GetLocation().Z - Now.GetLocation().Z;
			// How far the direction that pointed up when standing has turned away from up.
			const FVector Up = Now.GetRotation().RotateVector(Pelvis->Target.GetRotation().UnrotateVector(FVector::UpVector));
			const double TiltDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Up.Z, -1.0, 1.0)));

			const FSandboxThresholdTuning& Limits = Tuning.Thresholds;
			if (DropCm > Limits.FallPelvisDropCm)
			{
				EnterState(ESandboxBodyState::Falling, FString::Printf(TEXT("pelvis %.1f cm below its standing height"), DropCm));
			}
			else if (TiltDeg > Limits.FallTiltDeg)
			{
				EnterState(ESandboxBodyState::Falling, FString::Printf(TEXT("pelvis tilted by %.1f degrees"), TiltDeg));
			}
		}
	}

	if (State == ESandboxBodyState::Flinch)
	{
		if (ElapsedS - StateEnteredS >= Tuning.Reactions.FlinchRecoveryS)
		{
			EnterState(ESandboxBodyState::Standing, TEXT("recovered from the hit"));
		}
		else
		{
			// The hit muscles regain strength every tick.
			PushTuningToControls();
		}
	}
	else if (State == ESandboxBodyState::Falling)
	{
		// The speed of the body as a whole: the root mean square over its bodies, weighted by mass. A hand
		// that trembles against the floor does not keep an 80 kg character from being at rest; a moving
		// trunk does.
		double MassKg = 0.0;
		double LinearSquared = 0.0;
		double AngularSquared = 0.0;
		for (const FBodyInstance* Body : Mesh->Bodies)
		{
			if (Body && Body->IsValidBodyInstance())
			{
				const double BodyMassKg = Body->GetBodyMass();
				MassKg += BodyMassKg;
				LinearSquared += BodyMassKg * (Body->GetUnrealWorldVelocity() * 0.01).SizeSquared();
				AngularSquared += BodyMassKg * Body->GetUnrealWorldAngularVelocityInRadians().SizeSquared();
			}
		}
		const bool bAtRest = MassKg > 0.0 &&
			FMath::Sqrt(LinearSquared / MassKg) <= Tuning.Thresholds.RestSpeedMps &&
			FMath::Sqrt(AngularSquared / MassKg) <= Tuning.Thresholds.RestAngularSpeedRadS;
		RestS = bAtRest ? RestS + DeltaTime : 0.0;
		if (bAtRest && RestS >= Tuning.Thresholds.RestTimeS)
		{
			EnterState(ESandboxBodyState::Lying, FString::Printf(TEXT("at rest for %.2f s"), RestS));
		}
	}
}

void USandboxBodyController::FollowParams()
{
	ParamsRevision = Params->GetRevision();

	// The store has already put a new set through the body's own checks, so a failure here is not expected.
	FSandboxBodyTuning NewTuning;
	FString Error;
	if (!ReadStateTuning(NewTuning, Error))
	{
		UE_LOG(LogSandbox, Error, TEXT("Body controller keeps its tuning: %s"), *Error);
		return;
	}
	if (!(NewTuning.Rig == Tuning.Rig))
	{
		UE_LOG(LogSandbox, Warning, TEXT("Body controller: the rig bones changed in the tuning files; that takes effect when the body is next started"));
	}
	if (NewTuning.HasSameValues(Tuning))
	{
		return;
	}
	if (!ApplyTuning(NewTuning, Error))
	{
		UE_LOG(LogSandbox, Error, TEXT("Body controller keeps its tuning: %s"), *Error);
		return;
	}
	++TuningChangeCount;
	UE_LOG(LogSandbox, Display, TEXT("Body controller: new tuning values applied (change %d)"), TuningChangeCount);
}

void USandboxBodyController::PushTuningToControls()
{
	for (FBodyInstance* Body : Mesh->Bodies)
	{
		if (Body && Body->IsValidBodyInstance())
		{
			Body->SetPositionSolverIterationCount(static_cast<uint8>(Tuning.Solver.PositionIterations));
			Body->SetVelocitySolverIterationCount(static_cast<uint8>(Tuning.Solver.VelocityIterations));
			Body->SetProjectionSolverIterationCount(static_cast<uint8>(Tuning.Solver.ProjectionIterations));
			Body->SetOverrideIterationCounts(true);
		}
	}

	for (const FMuscle& Muscle : Muscles)
	{
		const FSandboxMuscleTuning& Group = Tuning.Muscle(Muscle.Group);
		const double Spring = Group.StiffnessNmPerRad * MuscleScale(Muscle.Group) * TorqueToUnreal / ChaosAngularDriveScale;
		FPhysicsControlData Data;
		// Force mode: the spring is a real torque per radian. In the plugin's default (acceleration) mode
		// it would be scaled by the inertia of the two jointed bodies alone, which leaves an ankle or a hip
		// far too weak for the weight of everything above it.
		Data.bUseAccelerationDriveMode = false;
		Data.LinearStrength = 0.0f; // the joint itself holds the bodies together
		Data.AngularStrength = StrengthForSpring(Spring);
		Data.AngularDampingRatio = 0.0f;
		Data.AngularExtraDamping = static_cast<float>(Spring * Group.DampingS);
		Data.MaxTorque = static_cast<float>(Group.MaxTorqueNm * TorqueToUnreal);
		Data.bUseSkeletalAnimation = true;
		// The two bodies are jointed and must never collide, whatever this extra constraint says.
		Data.bDisableCollision = true;
		Control->SetControlData(Muscle.Control, Data);
	}

	for (const FAssist& Assist : Assists)
	{
		const FSandboxAssistTuning& Slot = AssistTuning(Assist.Slot);
		const double AngularSpring = Slot.AngularNmPerRad * TorqueToUnreal / ChaosAngularDriveScale;
		FPhysicsControlData Data;
		Data.bUseAccelerationDriveMode = false;
		// Linear strength and damping are 1 times a per-axis multiplier, so that the horizontal and the
		// vertical pull can differ.
		Data.LinearStrength = 1.0f;
		Data.LinearDampingRatio = 0.0f;
		Data.LinearExtraDamping = 1.0f;
		Data.AngularStrength = StrengthForSpring(AngularSpring);
		Data.AngularDampingRatio = 0.0f;
		Data.AngularExtraDamping = static_cast<float>(AngularSpring * Slot.DampingS);
		Data.bUseSkeletalAnimation = false; // the target is fixed in the world, not taken from the mesh
		Control->SetControlData(Assist.Control, Data);

		FPhysicsControlMultiplier Multiplier;
		Multiplier.LinearStrengthMultiplier = FVector(
			StrengthForSpring(Slot.HorizontalNPerM), StrengthForSpring(Slot.HorizontalNPerM), StrengthForSpring(Slot.VerticalNPerM));
		Multiplier.LinearExtraDampingMultiplier = FVector(
			Slot.HorizontalNPerM * Slot.DampingS, Slot.HorizontalNPerM * Slot.DampingS, Slot.VerticalNPerM * Slot.DampingS);
		Control->SetControlMultiplier(Assist.Control, Multiplier);
	}
}

void USandboxBodyController::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bStarted || !Control || !Mesh || TickType != LEVELTICK_All)
	{
		return;
	}
	// A body spawned in the middle of a frame (the session's reset) is ticked in that same frame, before
	// its mesh has a pose: there are no bone transforms yet for the muscles' targets to be taken from, and
	// Physics Control would warn about every one of them. The controller starts with the mesh's first pose,
	// which is also when it starts for a body spawned before the world's first tick.
	if (Mesh->GetEditableComponentSpaceTransforms().Num() == 0)
	{
		return;
	}

	ReleaseIslandRefresh();

	// One integer compared per tick: nothing else happens here while the tuning stays as it is.
	if (Params && Params->GetRevision() != ParamsRevision)
	{
		FollowParams();
	}

	// What the body senses of its balance at the start of this step: the bodies as the last step left them,
	// and the floor's push on the feet during it. The impulses collected since are then cleared, and the
	// events of the coming step fill them again.
	MeasureBalance(Balance);
	if (Balance.IsSupported())
	{
		LastSupportHeightCm = Balance.SupportHeightCm;
		LastSupportPolygon = Balance.SupportPolygon;
	}
	FollowSupport(DeltaTime);
	// What the body felt since the last tick: a hit may make it flinch, a held push makes it lean.
	FeelFrame(DeltaTime);
	for (FVector& Impulse : SupportImpulseNs)
	{
		Impulse = FVector::ZeroVector;
	}
	SupportStepS = DeltaTime;

	Control->UpdateTargetCaches(DeltaTime);
	UpdateState(DeltaTime);
	UpdateStrategies(DeltaTime);
	UpdatePoseTurns();
	UpdateAssistLoad();
	Control->UpdateControls(DeltaTime);

	// Advanced last: during a tick this is the time at which the coming physics step begins.
	ElapsedS += DeltaTime;
}

double USandboxBodyController::DecisionMarginCm() const
{
	if (!Balance.bValid)
	{
		return 0.0;
	}
	// The support a decision is made on: every foot the floor touches, whatever its load, and those it touched
	// until a moment ago. A foot that carries less than the standing threshold (balance.foot_contact_min_n)
	// is still under the body and takes weight the moment the body sways onto it; a foot knocked off the floor
	// comes back down by itself, where it is, within balance.lift_grace_s. Only a foot that has been off the
	// floor longer no longer counts. (AirS is kept by FollowSupport, which runs first.)
	TArray<FName, TInlineAllocator<2>> Planted;
	for (const FAssist& Assist : Assists)
	{
		if (Assist.Slot == EAssistSlot::Feet && Assist.AirS < Tuning.Balance.LiftGraceS - KINDA_SMALL_NUMBER)
		{
			Planted.Add(Assist.Bone);
		}
	}
	// Under a held push the centre of pressure has to be F h / (m g) further along the force than the capture
	// point alone says: that is the point that must stay inside the support.
	const FVector2D Shift = WeightN > 0.0 ? SustainedMomentNCm / WeightN : FVector2D::ZeroVector;
	const FVector2D Point = Balance.CapturePoint + Shift;
	if (Shift.IsZero() && Planted.Num() == Balance.StandingFeet.Num() && Balance.IsSupported())
	{
		return Balance.CaptureMarginCm;
	}
	if (Planted.Num() > 0)
	{
		TArray<FVector2D> Polygon;
		FSandboxBodyProbe::SupportPolygon(Mesh, Planted, Tuning.Balance.SoleToleranceCm, Polygon);
		if (Polygon.Num() > 0)
		{
			return FSandboxBodyProbe::MarginInsidePolygon(Polygon, Point);
		}
	}
	// No foot stands this instant (both lifted by a blow, or one frame without load): judged against where
	// the body stood last.
	return LastSupportPolygon.Num() > 0 ? FSandboxBodyProbe::MarginInsidePolygon(LastSupportPolygon, Point) : 0.0;
}

void USandboxBodyController::SupportFrame(FVector& OutLocation, FQuat& OutRotation) const
{
	FVector Sum = FVector::ZeroVector;
	double YawSum = 0.0;
	int32 Feet = 0;
	for (const FAssist& Assist : Assists)
	{
		if (Assist.Slot != EAssistSlot::Feet)
		{
			continue;
		}
		Sum += Assist.Target.GetLocation();
		// The foot's turn about the vertical since Start.
		FQuat Swing, Twist;
		(Assist.Target.GetRotation() * Assist.StartRotation.Inverse()).ToSwingTwist(FVector::UpVector, Swing, Twist);
		YawSum += Twist.GetTwistAngle(FVector::UpVector);
		++Feet;
	}
	OutLocation = Feet > 0 ? Sum / Feet : FVector::ZeroVector;
	OutRotation = Feet > 0 ? FQuat(FVector::UpVector, YawSum / Feet) : FQuat::Identity;
}

FTransform USandboxBodyController::AssistGoal(const FAssist& Assist) const
{
	if (Assist.AppliedLeanCm.IsZero())
	{
		return Assist.Target;
	}
	FTransform Goal = Assist.Target;
	Goal.AddToTranslation(FVector(Assist.AppliedLeanCm * Assist.LeanFactor, 0.0));
	return Goal;
}

void USandboxBodyController::SetAssistTarget(const FAssist& Assist)
{
	const FTransform Goal = AssistGoal(Assist);
	FPhysicsControlTarget Target;
	Target.TargetPosition = Goal.GetLocation();
	Target.TargetOrientation = Goal.Rotator();
	Target.bApplyControlPointToTarget = true;
	Control->SetControlTarget(Assist.Control, Target);
}

void USandboxBodyController::FollowSupport(double DeltaTime)
{
	// Only an upright body plants its feet; a falling or lying one has no assist to move.
	const bool bUpright = State == ESandboxBodyState::Standing || State == ESandboxBodyState::Flinch;
	bool bSupportMoved = false;
	for (FAssist& Assist : Assists)
	{
		if (Assist.Slot != EAssistSlot::Feet || !Balance.bValid)
		{
			continue;
		}
		// In the air is untouched by the floor: a foot that carries less than the standing threshold but still
		// rests on the floor is not in the air, and takes weight again the moment the body sways onto it.
		const bool bStanding = Assist.Side >= 0 && Balance.FootForceN[Assist.Side] > 0.0;
		if (!bStanding)
		{
			Assist.AirS += DeltaTime;
		}
		else
		{
			// Landed after a real lift, not a frame without load: the foot is planted where it is now.
			if (!Assist.bStanding && Assist.AirS >= Tuning.Balance.ReplantAirS - KINDA_SMALL_NUMBER && bUpright)
			{
				const FBodyInstance* Foot = Mesh->GetBodyInstance(Assist.Bone);
				if (Foot && Foot->IsValidBodyInstance())
				{
					Assist.Target = Foot->GetUnrealWorldTransform();
					Assist.Target.SetScale3D(FVector::OneVector);
					SetAssistTarget(Assist);
					bSupportMoved = true;
					++ReplantCount;
				}
			}
			Assist.AirS = 0.0;
		}
		Assist.bStanding = bStanding;
	}

	if (bSupportMoved)
	{
		FVector SupportLocation;
		FQuat SupportRotation;
		SupportFrame(SupportLocation, SupportRotation);
		for (FAssist& Assist : Assists)
		{
			if (Assist.Slot == EAssistSlot::Feet)
			{
				continue;
			}
			Assist.FollowFrom = Assist.Target;
			Assist.FollowTo = FTransform((SupportRotation * Assist.SupportRotation).GetNormalized(), SupportLocation + SupportRotation.RotateVector(Assist.SupportLocation));
			Assist.FollowElapsedS = 0.0;
		}
	}

	// The anchors on their way to a new support, eased in and out like a pose.
	for (FAssist& Assist : Assists)
	{
		if (Assist.FollowElapsedS < 0.0)
		{
			continue;
		}
		Assist.FollowElapsedS += DeltaTime;
		const double FollowS = Tuning.Balance.AnchorFollowS;
		if (FollowS <= 0.0 || Assist.FollowElapsedS >= FollowS)
		{
			Assist.Target = Assist.FollowTo;
			Assist.FollowElapsedS = -1.0;
		}
		else
		{
			const double Weight = FMath::SmoothStep(0.0, 1.0, Assist.FollowElapsedS / FollowS);
			Assist.Target = FTransform(
				FQuat::Slerp(Assist.FollowFrom.GetRotation(), Assist.FollowTo.GetRotation(), Weight).GetNormalized(),
				FMath::Lerp(Assist.FollowFrom.GetLocation(), Assist.FollowTo.GetLocation(), Weight));
		}
		SetAssistTarget(Assist);
	}
}

void USandboxBodyController::MeasureBalance(FSandboxBalanceView& OutView) const
{
	OutView = FSandboxBalanceView();
	// Before the first step under the controller no contact has been reported: nothing is known yet.
	if (!bStarted || !Mesh || SupportStepS <= 0.0)
	{
		return;
	}

	TArray<FSandboxBodySample> Bodies;
	FSandboxBodyProbe::SampleBodies(Mesh, Bodies);
	OutView.CenterOfMass = FSandboxBodyProbe::CenterOfMass(Bodies);
	OutView.Velocity = FSandboxBodyProbe::CenterOfMassVelocity(Bodies);

	// A foot stands when the floor pushed up on it hard enough over the last step.
	const FName Feet[2] = { Tuning.Rig.FootLeft, Tuning.Rig.FootRight };
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const FBodyInstance* Foot = Mesh->GetBodyInstance(Feet[Side]);
		if (Foot && SupportImpulseNs.IsValidIndex(Foot->InstanceBodyIndex))
		{
			OutView.FootForceN[Side] = SupportImpulseNs[Foot->InstanceBodyIndex].Z / SupportStepS;
			if (OutView.FootForceN[Side] >= Tuning.Balance.FootContactMinN)
			{
				OutView.StandingFeet.Add(Feet[Side]);
			}
		}
	}

	const double SoleHeightCm = FSandboxBodyProbe::SupportPolygon(Mesh, OutView.StandingFeet, Tuning.Balance.SoleToleranceCm, OutView.SupportPolygon);
	OutView.SupportHeightCm = OutView.IsSupported() ? SoleHeightCm : LastSupportHeightCm;
	OutView.CapturePoint = FSandboxBodyProbe::CapturePoint(OutView.CenterOfMass, OutView.Velocity, OutView.CenterOfMass.Z - OutView.SupportHeightCm, GravityCmS2);
	if (OutView.IsSupported())
	{
		OutView.CenterOfMassMarginCm = FSandboxBodyProbe::MarginInsidePolygon(OutView.SupportPolygon, FVector2D(OutView.CenterOfMass.X, OutView.CenterOfMass.Y));
		OutView.CaptureMarginCm = FSandboxBodyProbe::MarginInsidePolygon(OutView.SupportPolygon, OutView.CapturePoint);
	}
	OutView.bValid = true;
}

void USandboxBodyController::GetControlView(FSandboxBodyControlView& OutView) const
{
	OutView.Muscles.Reset();
	OutView.Assists.Reset();
	// Physics Control has cached a target pose only once it has been updated.
	if (!bStarted || !Control || !Mesh || ElapsedS <= 0.0)
	{
		return;
	}

	for (const FMuscle& Muscle : Muscles)
	{
		const FBodyInstance* Parent = Mesh->GetBodyInstance(Muscle.ParentBone);
		const FBodyInstance* Child = Mesh->GetBodyInstance(Muscle.ChildBone);
		if (!Parent || !Child || !Parent->IsValidBodyInstance() || !Child->IsValidBodyInstance())
		{
			continue;
		}
		// The muscle holds the child's orientation relative to the parent as it is in the cached target
		// pose. Carried over to where the parent body really is, that is where the child should point.
		const FQuat CachedParent = Control->GetCachedBoneTransform(Mesh, Muscle.ParentBone).GetRotation();
		const FQuat CachedChild = Control->GetCachedBoneTransform(Mesh, Muscle.ChildBone).GetRotation();
		const FQuat Relative = CachedParent.Inverse() * CachedChild;

		FSandboxMuscleView& View = OutView.Muscles.AddDefaulted_GetRef();
		View.ParentBone = Muscle.ParentBone;
		View.ChildBone = Muscle.ChildBone;
		View.Group = Muscle.Group;
		View.Strength = MuscleScale(Muscle.Group);
		View.bDriven = Tuning.Muscle(Muscle.Group).StiffnessNmPerRad > 0.0;
		View.TargetRotation = (Parent->GetUnrealWorldTransform().GetRotation() * Relative).GetNormalized();
		View.ErrorRad = View.TargetRotation.AngularDistance(Child->GetUnrealWorldTransform().GetRotation());
		View.TargetOffsetRad = Relative.AngularDistance(Muscle.ReferenceRelative);
	}

	for (const FAssist& Assist : Assists)
	{
		const FSandboxAssistTuning& Slot = AssistTuning(Assist.Slot);
		FSandboxAssistView& View = OutView.Assists.AddDefaulted_GetRef();
		View.Bone = Assist.Bone;
		View.Target = AssistGoal(Assist);
		View.bActive = Slot.HorizontalNPerM > 0.0 || Slot.VerticalNPerM > 0.0 || Slot.AngularNmPerRad > 0.0;
	}
}

void USandboxBodyController::UpdateAssistLoad()
{
	// The solver does not report drive forces, so the load is rebuilt from the spring law: stiffness times
	// error minus damping times velocity, for the state at the start of this step. The solver integrates
	// the springs implicitly, so this is a close estimate rather than a reading.
	double ForceN = 0.0;
	double TorqueNm = 0.0;
	double HorizontalN[FSandboxAssistLoad::SlotCount] = { 0.0, 0.0, 0.0, 0.0 };
	double VerticalN[FSandboxAssistLoad::SlotCount] = { 0.0, 0.0, 0.0, 0.0 };
	for (const FAssist& Assist : Assists)
	{
		const FBodyInstance* Body = Mesh->GetBodyInstance(Assist.Bone);
		if (!Body || !Body->IsValidBodyInstance())
		{
			continue;
		}
		const FSandboxAssistTuning& Slot = AssistTuning(Assist.Slot);

		// Centimetres to metres.
		const FVector TargetCom = AssistGoal(Assist).TransformPositionNoScale(Body->GetMassSpaceLocal().GetLocation());
		const FVector ErrorM = (TargetCom - Body->GetCOMPosition()) * 0.01;
		const FVector VelocityMps = Body->GetUnrealWorldVelocity() * 0.01;
		const FVector Stiffness(Slot.HorizontalNPerM, Slot.HorizontalNPerM, Slot.VerticalNPerM);
		const FVector CommandN = Stiffness * (ErrorM - VelocityMps * Slot.DampingS);
		ForceN += CommandN.Size();
		const int32 SlotIndex = static_cast<int32>(Assist.Slot);
		HorizontalN[SlotIndex] += CommandN.Size2D();
		VerticalN[SlotIndex] += FMath::Abs(CommandN.Z);

		const FQuat Delta = (Assist.Target.GetRotation() * Body->GetUnrealWorldTransform().GetRotation().Inverse()).GetShortestArcWith(FQuat::Identity);
		const FVector ErrorRad = Delta.ToRotationVector();
		TorqueNm += Slot.AngularNmPerRad * (ErrorRad - Body->GetUnrealWorldAngularVelocityInRadians() * Slot.DampingS).Size();
	}

	AssistLoad.ForceShare = WeightN > 0.0 ? ForceN / WeightN : 0.0;
	AssistLoad.TorqueShare = WeightN > 0.0 ? TorqueNm / (WeightN * StandingComHeightM) : 0.0;
	for (int32 SlotIndex = 0; SlotIndex < FSandboxAssistLoad::SlotCount; ++SlotIndex)
	{
		AssistLoad.HorizontalShare[SlotIndex] = WeightN > 0.0 ? HorizontalN[SlotIndex] / WeightN : 0.0;
		AssistLoad.VerticalShare[SlotIndex] = WeightN > 0.0 ? VerticalN[SlotIndex] / WeightN : 0.0;
	}
}

const TCHAR* FSandboxAssistLoad::SlotName(int32 Slot)
{
	switch (Slot)
	{
	case 1: return TEXT("chest");
	case 2: return TEXT("head");
	case 3: return TEXT("feet");
	default: return TEXT("pelvis");
	}
}
