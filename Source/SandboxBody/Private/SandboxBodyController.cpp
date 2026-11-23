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

	// TEMPORARY (part 4 diagnosis): per-tick trace of a step.
	TAutoConsoleVariable<int32> CVarStepTrace(TEXT("Sandbox.StepTrace"), 0, TEXT("Logs the body's step every tick (diagnosis)."));
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
			CalfBoneName[Side] = Calf != INDEX_NONE ? Skeleton.GetBoneName(Calf) : NAME_None;
			if (ThighBoneIndex[Side] == INDEX_NONE || !Mesh->GetBodyInstance(ThighBoneName[Side]))
			{
				OutError = FString::Printf(TEXT("Foot '%s' is not the end of a leg with a thigh body"), *Feet[Side].ToString());
				return false;
			}
			// How long the leg is, for a pelvis that has to stay within reach of a planted foot.
			const FVector Hip = FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, ThighBoneIndex[Side]).GetLocation();
			const FVector Knee = FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, Calf).GetLocation();
			const FVector Ankle = FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, FootBoneIndex[Side]).GetLocation();
			HipReferenceCs[Side] = Hip;
			LegLengthCm[Side] = FVector::Dist(Hip, Knee) + FVector::Dist(Knee, Ankle);
			// How high the bone stands over the floor when the foot is flat on it, as spawned.
			TArray<FVector2D> Sole;
			const double SoleZ = FSandboxBodyProbe::SupportPolygon(Mesh, TArray<FName>{ Feet[Side] }, InTuning.Balance.SoleToleranceCm, Sole);
			const FBodyInstance* FootBody = Mesh->GetBodyInstance(Feet[Side]);
			FootStandHeightCm[Side] = FootBody && Sole.Num() > 0 ? FootBody->GetUnrealWorldTransform().GetLocation().Z - SoleZ : 0.0;
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
	// A foot the pose places is never turned beyond its ankle's range.
	if (USandboxAnimInstance* Animation = Cast<USandboxAnimInstance>(Mesh->GetAnimInstance()))
	{
		for (int32 Side = 0; Side < 2; ++Side)
		{
			FSandboxJointLimits Limits;
			if (FootBoneIndex[Side] != INDEX_NONE && FSandboxPoseRig::ReadJointLimits(Mesh, FootBoneName[Side], Limits))
			{
				Animation->SetLimbEndLimits(FootBoneIndex[Side], Limits);
			}
		}
	}

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
	const bool bUpright = IsUpright();
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
		// Only while standing: in a flinch or a stagger the assist does not hold the pelvis over its anchor, and the
		// slow part wound up to its limit on an offset that was no posture's (5 cm after a push to the pelvis); back
		// to standing, it drove the body over the middle of its feet at 0.6 m/s and out the other side.
		if (Limits.AnkleIntegralS > 0.0)
		{
			if (State == ESandboxBodyState::Standing)
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
	// A flinch that turns into a stagger keeps its weakening: it fades by its own clock from the hit, so that the
	// change of state does not hand the hit muscles their full strength back in one step.
	if ((State != ESandboxBodyState::Flinch && State != ESandboxBodyState::Stagger) || Group != FlinchGroup || FlinchStartedS < 0.0)
	{
		return 1.0;
	}
	// Weak at the moment of the hit, back to full strength in a straight line.
	const double Recovered = Tuning.Reactions.FlinchRecoveryS > 0.0 ? FMath::Clamp((ElapsedS - FlinchStartedS) / Tuning.Reactions.FlinchRecoveryS, 0.0, 1.0) : 1.0;
	return FMath::Lerp(Tuning.Reactions.FlinchWeakening, 1.0, Recovered);
}

void USandboxBodyController::EnterState(ESandboxBodyState NewState, const FString& Reason)
{
	const ESandboxBodyState OldState = State;
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

	// Back from a stagger or a flinch (states without the assist's horizontal pull) the assist takes hold again,
	// at full strength. Its anchors of pelvis, chest and head start where those bodies are and glide to their
	// places over the support, so that the pull grows from nothing instead of arriving at once.
	if ((OldState == ESandboxBodyState::Stagger || OldState == ESandboxBodyState::Flinch) && NewState == ESandboxBodyState::Standing)
	{
		// The feet are anchored where they stand: without the assist's pull they may have turned or slid a little.
		// (After a flinch too: a foot that had slid 7 cm under a push to the pelvis was pulled back by its spring, and
		// the pelvis's anchor was taken from where the feet had been.)
		{
			for (FAssist& Assist : Assists)
			{
				if (Assist.Slot == EAssistSlot::Feet)
				{
					Assist.Target = FlatFootTransform(Assist);
					SetAssistTarget(Assist);
				}
			}
		}
		FVector SupportLocation;
		FQuat SupportRotation;
		SupportFrame(SupportLocation, SupportRotation);
		for (FAssist& Assist : Assists)
		{
			const FBodyInstance* Body = Assist.Slot != EAssistSlot::Feet ? Mesh->GetBodyInstance(Assist.Bone) : nullptr;
			if (!Body || !Body->IsValidBodyInstance())
			{
				continue;
			}
			FTransform Now = Body->GetUnrealWorldTransform();
			Now.SetScale3D(FVector::OneVector);
			// The lean is added on top of the anchor (AssistGoal); the glide starts from the body less the lean.
			Now.AddToTranslation(-FVector(Assist.AppliedLeanCm * Assist.LeanFactor, 0.0));
			Assist.FollowTo = FTransform((SupportRotation * Assist.SupportRotation).GetNormalized(), SupportLocation + SupportRotation.RotateVector(Assist.SupportLocation));
			// Over the floor from where the body is, but at the standing height: the legs' frames are lowered from
			// it as far as the feet need (UpdateLowering), and an anchor at the height of a pelvis that the legs
			// already hold lower would lower them twice, at once (the knees gave, both feet left the floor).
			Now.SetLocation(FVector(Now.GetLocation().X, Now.GetLocation().Y, Assist.FollowTo.GetLocation().Z));
			Assist.FollowFrom = Now;
			Assist.FollowElapsedS = 0.0;
			Assist.FollowS = GlideSeconds(Assist.FollowFrom.GetLocation(), Assist.FollowTo.GetLocation());
			Assist.Target = Now;
			SetAssistTarget(Assist);
			if (Assist.Slot == EAssistSlot::Pelvis)
			{
				UE_LOG(LogSandbox, Display, TEXT("Body controller: the pelvis's anchor glides %.1f cm back over the support in %.2f s"),
					FVector::Dist2D(Assist.FollowFrom.GetLocation(), Assist.FollowTo.GetLocation()), Assist.FollowS);
			}
		}
		// What is left of a weight shift is where the body is now, and so in the anchors already: kept on top of
		// them, it moved the legs' upright frame 5 cm off the body, the far leg was asked for more than its length
		// and kicked the body off the floor (4800 N).
		ShiftCm = FVector2D::ZeroVector;
		ShiftGoalCm = FVector2D::ZeroVector;
		bShiftHeld = false;
		// The legs go over to the posture over the anchors, which starts where the body is, from the frame they were
		// solved in until now. (From a frame worked out anew, every frame moved with the anchors' jump to the body:
		// 16 cm after a step to the side, and the legs struck the floor with 5000 N each.)
		for (int32 Side = 0; Side < 2; ++Side)
		{
			FFootPlacement& Foot = Placement[Side];
			if (Foot.Mode != EPlacement::Planted)
			{
				continue;
			}
			// The leg's point goes over to where the foot is now, as the frame does. Kept where the foot was set
			// down, a foot that had slid 15 cm on its edge after a step across was pulled back toward it by its leg,
			// which pushed the body off its support and into the next stagger; moved there at once, the leg's reach
			// jumped past its length and the knee straightened in one step.
			const FVector Was = Foot.GlideS > 0.0 && Foot.GlideElapsedS < Foot.GlideS
				? FMath::Lerp(Foot.GlideFrom, Foot.Point, FMath::SmoothStep(0.0, 1.0, Foot.GlideElapsedS / Foot.GlideS)) : Foot.Point;
			FVector Now = Was;
			for (const FAssist& Assist : Assists)
			{
				if (Assist.Slot == EAssistSlot::Feet && Assist.Side == Side)
				{
					Now = FVector(Assist.Target.GetLocation().X, Assist.Target.GetLocation().Y, Was.Z);
				}
			}
			PlaceFoot(Side, EPlacement::Planted, Now, EFrame::Anchor, Tuning.Balance.AnchorFollowS, nullptr, true);
			Foot.GlideFrom = Was;
			Foot.GlideElapsedS = 0.0;
			Foot.GlideS = Tuning.Balance.AnchorFollowS;
		}
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
	// The controller's own placements too, and a step under way ends where it is.
	for (int32 Side = 0; Side < 2; ++Side)
	{
		ReleaseFoot(Side, Tuning.Poses.BlendS);
	}
	Swing.bActive = false;
	Pending.bActive = false;
	bShiftHeld = false;
}

FTransform USandboxBodyController::FrameTransform(EFrame Frame, int32 Side, bool bLowered) const
{
	const FTransform Component = Mesh->GetComponentTransform();
	const FAssist* PelvisAssist = Assists.FindByPredicate([](const FAssist& Assist) { return Assist.Slot == EAssistSlot::Pelvis; });
	const FBodyInstance* Body = PelvisAssist ? Mesh->GetBodyInstance(PelvisAssist->Bone) : nullptr;
	if (Frame == EFrame::Actual || !Body || !Body->IsValidBodyInstance())
	{
		return Component;
	}
	// The component sits on the simulated pelvis: carry that relation over to a pelvis where it should be.
	FTransform PelvisNow = Body->GetUnrealWorldTransform();
	PelvisNow.SetScale3D(FVector::OneVector);
	const FTransform Relative = Component.GetRelativeTransform(PelvisNow);
	FTransform Pelvis = AssistGoal(*PelvisAssist);
	// A weight shift before a step moves the goal toward the stance foot.
	Pelvis.AddToTranslation(FVector(ShiftCm, 0.0));
	if (Frame == EFrame::Here || Frame == EFrame::Stance)
	{
		const FVector Goal = Pelvis.GetLocation();
		const FVector Now = PelvisNow.GetLocation();
		const double Follow = Frame == EFrame::Here ? 1.0 : Tuning.Balance.StanceFollow;
		// In a stagger the planted legs also brake the body: the pelvis is solved for behind where it would be by the
		// body's speed times balance.stagger_damping_s, so that the leg ahead pushes back while the body moves, and
		// only the pull toward the anchor is left once it stops. (Without it, a step that caught the capture point
		// 11 cm inside the new support did not stop the body: the weight stayed on the trailing leg, which pushed the
		// body on past the leading foot; a pull toward the anchor strong enough to stop it turned the body round and
		// sent it out the other side.)
		FVector2D Behind = FVector2D::ZeroVector;
		if (Frame == EFrame::Stance && State == ESandboxBodyState::Stagger)
		{
			Behind = FVector2D(Balance.Velocity.X, Balance.Velocity.Y) * Tuning.Balance.StaggerDampingS;
		}
		Pelvis.SetLocation(FVector(FMath::Lerp(Goal.X, Now.X, Follow) - Behind.X, FMath::Lerp(Goal.Y, Now.Y, Follow) - Behind.Y, Goal.Z));
	}
	FTransform Solved = Relative * Pelvis;

	// No higher than standing, but low enough for the planted legs to reach their feet (UpdateLowering).
	if (bLowered && LowerCm > 0.0)
	{
		Solved.AddToTranslation(FVector(0.0, 0.0, -LowerCm));
	}
	return Solved;
}

double USandboxBodyController::GlideSeconds(const FVector& From, const FVector& To) const
{
	const double FollowS = Tuning.Balance.AnchorFollowS;
	const FVector2D Way(To.X - From.X, To.Y - From.Y);
	const double DistanceCm = Way.Size();
	const double HeightCm = StandingComHeightM * 100.0;
	if (DistanceCm < KINDA_SMALL_NUMBER || Tuning.Balance.AnchorRoomShare <= 0.0 || HeightCm <= 0.0)
	{
		return FollowS;
	}
	// The support: both soles, each where it lies (a foot just set down may not be quite on the floor yet).
	TArray<FVector2D> Points;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		TArray<FVector2D> Sole;
		FSandboxBodyProbe::SupportPolygon(Mesh, TArray<FName>{ FootBoneName[Side] }, Tuning.Balance.SoleToleranceCm, Sole);
		Points.Append(Sole);
	}
	TArray<FVector2D> Support;
	FSandboxBodyProbe::ConvexHull2D(Points, Support);
	// Standing, where the assist pulls the body along, no less than what the ankles bring back: a glide's end that
	// lies outside the support as the soles are now (one foot raised on its edge) has no room at all, and was given
	// the shortest glide instead of the longest (20 cm in 0.5 s, the body carried over the middle of its feet and out
	// the other side). In a stagger the assist does not pull the body (its anchor only sets the legs' frames), and
	// slower glides there held the legs to the old stance: more falls in the push matrix, one solver blow-up.
	double RoomCm = FSandboxBodyProbe::DistanceToEdgeAlong(Support, FVector2D(To.X, To.Y), Way);
	if (State == ESandboxBodyState::Standing)
	{
		RoomCm = FMath::Max(RoomCm, Tuning.Balance.RecoverableCm);
	}
	// A smooth step peaks at 1.5 times its mean speed; the capture point runs that speed / omega ahead.
	const double MaxSpeedCmS = Tuning.Balance.AnchorRoomShare * RoomCm * FMath::Sqrt(GravityCmS2 / HeightCm);
	return MaxSpeedCmS > 0.0 ? FMath::Max(FollowS, 1.5 * DistanceCm / MaxSpeedCmS) : FollowS;
}

double USandboxBodyController::LoweringFor(const FTransform& Frame, int32 Leg, const FVector& Foot) const
{
	if (LegLengthCm[Leg] <= 0.0)
	{
		return 0.0;
	}
	const FVector Hip = Frame.TransformPosition(HipReferenceCs[Leg]);
	const double ReachCm = Tuning.Balance.LegExtensionMax * LegLengthCm[Leg];
	const double AcrossSquared = FMath::Square(Hip.X - Foot.X) + FMath::Square(Hip.Y - Foot.Y);
	const double DownCm = FMath::Sqrt(FMath::Max(ReachCm * ReachCm - AcrossSquared, 0.0));
	return FMath::Max((Hip.Z - Foot.Z) - DownCm, 0.0);
}

void USandboxBodyController::UpdateLowering(double DeltaTime)
{
	// A leg asked to reach further than it can goes straight and lifts its foot off the floor (in a wide stance), so
	// the pelvis is held as low as the planted leg that needs it most. The same height for both legs, or they would
	// fight over it. While a foot swings, the swinging leg's reach to its target comes in as the foot travels over:
	// the stance knee bends before the foot comes down, so that the foot reaches the floor and the height does not
	// change when it lands. The height follows with a lag either way: a pelvis that drops at once unloads the
	// stance foot, and one that rises at once (a foot lifted for the next step) throws the body up.
	double GoalCm = 0.0;
	for (int32 Leg = 0; Leg < 2; ++Leg)
	{
		if (Placement[Leg].Mode == EPlacement::Planted)
		{
			GoalCm = FMath::Max(GoalCm, LoweringFor(FrameTransform(Placement[Leg].Frame, Leg, false), Leg, Placement[Leg].Point));
		}
	}
	if (Swing.bActive)
	{
		GoalCm = FMath::Max(GoalCm, LoweringFor(FrameTransform(EFrame::Stance, Swing.Side, false), Swing.Side, Swing.Target) * Swing.Across);
	}
	if (Placement[0].Mode == EPlacement::None && Placement[1].Mode == EPlacement::None)
	{
		LowerCm = 0.0;
		return;
	}
	GoalCm = FMath::Min(GoalCm, Tuning.Balance.LowerMaxCm);
	const double Follow = Tuning.Balance.LowerS > 0.0 ? 1.0 - FMath::Exp(-DeltaTime / Tuning.Balance.LowerS) : 1.0;
	LowerCm += (GoalCm - LowerCm) * Follow;
}

void USandboxBodyController::PlaceFoot(int32 Side, EPlacement Mode, const FVector& Point, EFrame Frame, double BlendS, const EFrame* From, bool bFromLast)
{
	USandboxAnimInstance* Animation = Cast<USandboxAnimInstance>(Mesh->GetAnimInstance());
	if (!Animation || Side < 0 || Side > 1)
	{
		return;
	}
	FFootPlacement& Foot = Placement[Side];
	if (Foot.Mode == EPlacement::None)
	{
		// From the pose: the placement's weight eases in, in its frame from the start.
		Animation->SetLimbTarget(FootBoneIndex[Side], Point, 1.0, BlendS);
		Foot.FromFrame = Frame;
		Foot.FrameElapsedS = 0.0;
		Foot.FrameBlendS = 0.0;
		Foot.bFromLastFrame = false;
	}
	else if (Foot.Frame != Frame || From || bFromLast)
	{
		// From one frame to another: the frame itself goes over, so that the leg's target does not jump.
		Foot.FromFrame = From ? *From : Foot.Frame;
		Foot.FrameElapsedS = 0.0;
		Foot.FrameBlendS = BlendS;
		Foot.bFromLastFrame = bFromLast && Foot.bHasLastFrame;
		Foot.FromTransform = Foot.LastFrame;
	}
	// A foot that stays planted keeps settling (a new frame or point dropped what was left of it at once, and the
	// leg stamped the foot down by 4 cm in one frame).
	if (!(Foot.Mode == EPlacement::Planted && Mode == EPlacement::Planted))
	{
		Foot.SettleCm = 0.0;
		Foot.SettleElapsedS = 0.0;
		Foot.SettleS = 0.0;
		Foot.GlideS = 0.0;
	}
	Foot.Mode = Mode;
	Foot.Frame = Frame;
	Foot.Point = Point;
}

FVector USandboxBodyController::FlatFootPoint(int32 Side) const
{
	const FBodyInstance* Foot = Mesh->GetBodyInstance(FootBoneName[Side]);
	const FVector At = Foot && Foot->IsValidBodyInstance() ? Foot->GetUnrealWorldTransform().GetLocation() : Placement[Side].Point;
	return FVector(At.X, At.Y, Balance.SupportHeightCm + FootStandHeightCm[Side]);
}

FTransform USandboxBodyController::FlatFootTransform(const FAssist& Assist) const
{
	const FBodyInstance* Foot = Mesh->GetBodyInstance(Assist.Bone);
	if (!Foot || !Foot->IsValidBodyInstance() || Assist.Side < 0)
	{
		return Assist.Target;
	}
	const FTransform At = Foot->GetUnrealWorldTransform();
	FQuat Tilt, Yaw;
	(At.GetRotation() * Assist.StartRotation.Inverse()).ToSwingTwist(FVector::UpVector, Tilt, Yaw);
	// The floor is where the sole's lowest point is, whichever way the foot is tilted.
	TArray<FVector2D> Sole;
	const double SoleZ = FSandboxBodyProbe::SupportPolygon(Mesh, TArray<FName>{ Assist.Bone }, Tuning.Balance.SoleToleranceCm, Sole);
	const FVector Location = At.GetLocation();
	const double Z = Sole.Num() > 0 ? SoleZ + FootStandHeightCm[Assist.Side] : Location.Z;
	return FTransform((Yaw * Assist.StartRotation).GetNormalized(), FVector(Location.X, Location.Y, Z));
}

void USandboxBodyController::ReleaseFoot(int32 Side, double BlendS)
{
	FFootPlacement& Foot = Placement[Side];
	if (Foot.Mode == EPlacement::None)
	{
		return;
	}
	if (USandboxAnimInstance* Animation = Mesh ? Cast<USandboxAnimInstance>(Mesh->GetAnimInstance()) : nullptr)
	{
		// Let go from the leg as it is now, held in the frame of the body as it is now: a point fixed in the
		// world would have the leg reach for it while the body goes down, and feed the fall.
		const FBodyInstance* Body = Mesh->GetBodyInstance(FootBoneName[Side]);
		const FVector Point = Body && Body->IsValidBodyInstance() ? Body->GetUnrealWorldTransform().GetLocation() : Foot.Point;
		const FTransform Frame = Mesh->GetComponentTransform();
		Animation->SetLimbTarget(FootBoneIndex[Side], Point, 0.0, BlendS);
		Animation->MoveLimbTarget(FootBoneIndex[Side], Point, &Frame);
	}
	Foot = FFootPlacement();
}

void USandboxBodyController::UpdatePlacements(double DeltaTime)
{
	USandboxAnimInstance* Animation = Cast<USandboxAnimInstance>(Mesh->GetAnimInstance());
	if (!Animation)
	{
		return;
	}
	for (int32 Side = 0; Side < 2; ++Side)
	{
		FFootPlacement& Foot = Placement[Side];
		if (Foot.Mode == EPlacement::None)
		{
			continue;
		}
		Foot.FrameElapsedS += DeltaTime;
		Foot.SettleElapsedS += DeltaTime;
		Foot.GlideElapsedS += DeltaTime;
		const FVector Point = Foot.SettledPoint();
		const double Weight = Foot.FrameBlendS <= 0.0 || Foot.FrameElapsedS >= Foot.FrameBlendS ? 1.0
			: FMath::SmoothStep(0.0, 1.0, Foot.FrameElapsedS / Foot.FrameBlendS);
		// A foot in the air is kept level, as it would be under an upright pelvis, so that it lands flat.
		const FQuat Level = FrameTransform(EFrame::Here, -1).GetRotation();
		const FQuat* EndFrame = Foot.Mode == EPlacement::Swing ? &Level : nullptr;
		// Held to the ankle's range in the air, let go of it as the foot settles on the floor.
		const double EndLimitWeight = Foot.Mode == EPlacement::Swing ? 1.0 : Foot.SettleLeft();
		if (Weight >= 1.0 && Foot.Frame == EFrame::Actual)
		{
			// The component as the animation sees it when it evaluates: the most recent there is.
			Animation->MoveLimbTarget(FootBoneIndex[Side], Point, nullptr, EndFrame, EndLimitWeight, Tuning.Balance.LegExtensionMax);
			Foot.LastFrame = Mesh->GetComponentTransform();
			Foot.bHasLastFrame = true;
			continue;
		}
		FTransform Frame = FrameTransform(Foot.Frame, Side);
		if (Weight < 1.0)
		{
			const FTransform From = Foot.bFromLastFrame ? Foot.FromTransform : FrameTransform(Foot.FromFrame, Side);
			Frame = FTransform(FQuat::Slerp(From.GetRotation(), Frame.GetRotation(), Weight).GetNormalized(),
				FMath::Lerp(From.GetLocation(), Frame.GetLocation(), Weight));
		}
		Animation->MoveLimbTarget(FootBoneIndex[Side], Point, &Frame, EndFrame, EndLimitWeight, Tuning.Balance.LegExtensionMax);
		Foot.LastFrame = Frame;
		Foot.bHasLastFrame = true;
	}
}

void USandboxBodyController::PlannedSupport(TArray<FVector2D>& OutPolygon) const
{
	OutPolygon.Reset();
	if (!Swing.bActive)
	{
		return;
	}
	TArray<FVector2D> Points;
	FSandboxBodyProbe::SupportPolygon(Mesh, TArray<FName>{ FootBoneName[1 - Swing.Side] }, Tuning.Balance.SoleToleranceCm, Points);
	Points.Append(Swing.TargetSole);
	FSandboxBodyProbe::ConvexHull2D(Points, OutPolygon);
}

bool USandboxBodyController::PlanStep(int32 Side, double TimeS, FSandboxStepRecord& OutPlan, TArray<FVector2D>& OutTargetSole,
	const TArray<FVector2D>* LiftSole, const FVector* LiftFrom, int32 KeepCross, const FVector2D* KeepAim) const
{
	const int32 Other = 1 - Side;
	const FBodyInstance* Foot = Mesh->GetBodyInstance(FootBoneName[Side]);
	const FBodyInstance* Stance = Mesh->GetBodyInstance(FootBoneName[Other]);
	if (!Foot || !Stance || !Foot->IsValidBodyInstance() || !Stance->IsValidBodyInstance() || !Balance.bValid)
	{
		return false;
	}
	const double Tolerance = Tuning.Balance.SoleToleranceCm;
	TArray<FVector2D> SwingSole, StanceSole;
	if (LiftSole)
	{
		SwingSole = *LiftSole;
	}
	else
	{
		FSandboxBodyProbe::SupportPolygon(Mesh, TArray<FName>{ FootBoneName[Side] }, Tolerance, SwingSole);
	}
	FSandboxBodyProbe::SupportPolygon(Mesh, TArray<FName>{ FootBoneName[Other] }, Tolerance, StanceSole);
	if (SwingSole.Num() == 0 || StanceSole.Num() == 0)
	{
		return false;
	}
	const FSandboxBalanceTuning& Limits = Tuning.Balance;

	// The capture point the step has to catch, and where it will be when the foot lands.
	const FVector2D Point = Balance.CapturePoint + (WeightN > 0.0 ? SustainedMomentNCm / WeightN : FVector2D::ZeroVector);
	const FVector2D Pivot = FSandboxBodyProbe::NearestPointOfPolygon(StanceSole, Point);
	const FVector2D Landing = CaptureAtLanding(Other, TimeS);
	FVector2D Way = (Landing - Pivot).GetSafeNormal();
	if (Way.IsNearlyZero())
	{
		Way = (Point - FVector2D(Balance.CenterOfMass.X, Balance.CenterOfMass.Y)).GetSafeNormal();
	}
	const FVector2D Wanted = Landing + Way * Limits.StepMarginCm;

	// The sole's middle goes there; the bone moves with it.
	FVector2D SoleMiddle = FVector2D::ZeroVector;
	for (const FVector2D& Corner : SwingSole)
	{
		SoleMiddle += Corner;
	}
	SoleMiddle /= SwingSole.Num();

	// A foot that steps to its own side of the other keeps a stance's width from it. One whose target lies past the
	// other foot's line crosses over (the owner's decision): it goes in front of the other foot, or behind it when
	// the step goes back, and lands at least balance.cross_offset_cm ahead or behind, since calves and feet collide.
	FVector SupportLocation;
	FQuat SupportRotation;
	SupportFrame(SupportLocation, SupportRotation);
	const FVector Left3 = SupportRotation.RotateVector(FVector(1.0, 0.0, 0.0));
	const FVector2D Outward = FVector2D(Left3.X, Left3.Y) * (Side == 0 ? 1.0 : -1.0);
	const FVector2D Forward = CrossForward(Other);
	const FVector From = LiftFrom ? *LiftFrom : Foot->GetUnrealWorldTransform().GetLocation();
	const FVector StanceBone = Stance->GetUnrealWorldTransform().GetLocation();
	const FVector2D FromToStance = FVector2D(From.X, From.Y) - FVector2D(StanceBone.X, StanceBone.Y);
	// In front or behind: the side that needs the smaller correction, in front when the two are near alike (a push
	// straight to the side). Chosen for the step as cut to the reach: chosen before, a foot far behind the other
	// was sent 60 cm forward to land in front of it.
	const auto Choose = [&Limits](double AheadCm)
	{
		const double FrontCm = FMath::Max(Limits.CrossOffsetCm - AheadCm, 0.0);
		const double BehindCm = FMath::Max(Limits.CrossOffsetCm + AheadCm, 0.0);
		return FrontCm <= BehindCm + 0.5 * Limits.CrossOffsetCm ? 1 : -1;
	};
	// The limits on a move: the reach, the stance's width on its own side, ahead of or behind the other foot on the
	// far side. Which side of the other foot the step lands on is decided once (Cross, -1 to 1, or anything else to
	// decide it here), when the foot lifts; a step aimed anew keeps it, or its swing's path would jump from one side
	// of the other foot to the other.
	const auto Constrain = [&](FVector2D Move, int32 InCross, FSandboxStepRecord& Flags)
	{
		Flags.bWidthLimited = false;
		Flags.bReachLimited = Move.Size() > Limits.StepReachCm;
		if (Flags.bReachLimited)
		{
			Move = Move.GetSafeNormal() * Limits.StepReachCm;
		}
		double Apart = FVector2D::DotProduct(FromToStance + Move, Outward);
		const bool bKept = InCross >= -1 && InCross <= 1;
		const bool bAcross = bKept ? InCross != 0 : Apart < 0.0;
		int32 Cross = 0;
		if (!bAcross)
		{
			if (Apart < Limits.StepWidthMinCm)
			{
				Flags.bWidthLimited = true;
				Move += Outward * (Limits.StepWidthMinCm - Apart);
			}
		}
		else
		{
			// Kept on the far side it was aimed at.
			if (Apart > 0.0)
			{
				Move -= Outward * Apart;
				Apart = 0.0;
			}
			// And no further past the other foot than balance.cross_past_cm: a leg reaches across the body only so far,
			// and the step across is the first of two (the other foot, unloaded by it, steps out next). Sent to catch the
			// capture point alone, it was cut to the reach in three of four long chains of steps and landed short.
			if (-Apart > Limits.CrossPastCm)
			{
				Move += Outward * (-Apart - Limits.CrossPastCm);
				Apart = -Limits.CrossPastCm;
				Flags.bReachLimited = true;
			}
			Cross = bKept ? InCross : Choose(FVector2D::DotProduct(FromToStance + Move, Forward));
			// Ahead of (or behind) the other foot by the offset, while it is not a stance's width to the side of it.
			const double Ahead = FVector2D::DotProduct(FromToStance + Move, Forward) * Cross;
			if (-Apart < Limits.StepWidthMinCm && Ahead < Limits.CrossOffsetCm)
			{
				Move += Forward * (Cross * (Limits.CrossOffsetCm - Ahead));
				Flags.bWidthLimited = true;
			}
		}
		Flags.Cross = Cross;
		// The swing goes round the other foot when it starts on one side of it and ends on the other: over to the far
		// side on the side it lands, back from the far side on the side the foot is.
		const double FromApart = FVector2D::DotProduct(FromToStance, Outward);
		Flags.Pass = 0;
		if ((FromApart < 0.0) != bAcross)
		{
			Flags.Pass = bAcross ? Cross : Choose(FVector2D::DotProduct(FromToStance, Forward));
		}
		// Within reach whatever the corrections asked for.
		if (Move.Size() > Limits.StepReachCm)
		{
			Move = Move.GetSafeNormal() * Limits.StepReachCm;
			Flags.bReachLimited = true;
		}
		return Move;
	};
	// How far inside the support a move would put the capture point at landing: the least over the way from where
	// it is now to where it is predicted. The prediction (running away from the stance sole's nearest point) is the
	// best the stance foot can do and can be far off in direction when the capture point is near that sole; scored
	// on the prediction alone, a capture point 5 cm ahead of both feet sent the stepping foot 8 cm back.
	const FVector2D Midway = 0.5 * (Point + Landing);
	const auto MarginOf = [&](const FVector2D& Move)
	{
		TArray<FVector2D> Points = StanceSole;
		for (const FVector2D& Corner : SwingSole)
		{
			Points.Add(Corner + Move);
		}
		TArray<FVector2D> Hull;
		FSandboxBodyProbe::ConvexHull2D(Points, Hull);
		if (Hull.Num() == 0)
		{
			return -1.0e6;
		}
		return FMath::Min3(FSandboxBodyProbe::MarginInsidePolygon(Hull, Landing), FSandboxBodyProbe::MarginInsidePolygon(Hull, Midway),
			FSandboxBodyProbe::MarginInsidePolygon(Hull, Point));
	};

	// The aimed point, and around it a few others along the capture point's way and across it: the one that leaves
	// the capture point at least balance.step_margin_goal_cm inside the new support, as near the aimed point as
	// can be. (Aimed at the point alone, a step to the side and back put the feet in a line along the way, the
	// capture point landed on the edge of their narrow hull, and the body stepped ten times before it stood.) A step
	// aimed anew keeps the offset it was given at the lift (KeepAim), so that its target does not hop between them.
	const FVector2D Across(-Way.Y, Way.X);
	FSandboxStepRecord Nominal;
	FVector2D Move = Constrain(Wanted - SoleMiddle, KeepCross, Nominal);
	const int32 Cross = Nominal.Cross;
	FVector2D BestAim = FVector2D::ZeroVector;
	if (KeepAim)
	{
		BestAim = *KeepAim;
		Move = Constrain(Wanted + Way * BestAim.X + Across * BestAim.Y - SoleMiddle, Cross, Nominal);
	}
	else if (Limits.StepMarginGoalCm > 0.0)
	{
		double BestScore = -1.0e9;
		static const double Offsets[] = { 0.0, -5.0, 5.0, -10.0, 10.0, -15.0, 15.0 };
		for (const double AlongCm : { 0.0, 5.0, 10.0, 15.0, -5.0 })
		{
			for (const double AcrossCm : Offsets)
			{
				FSandboxStepRecord Flags;
				const FVector2D Candidate = Constrain(Wanted + Way * AlongCm + Across * AcrossCm - SoleMiddle, Cross, Flags);
				// Of two offsets alike, the one toward the other foot: a rule that mirror-image pushes mirror.
				const double Inward = FVector2D::DotProduct(Across, Outward) > 0.0 ? -AcrossCm : AcrossCm;
				const double Score = FMath::Min(MarginOf(Candidate), Limits.StepMarginGoalCm) - 0.05 * (FMath::Abs(AlongCm) + FMath::Abs(AcrossCm))
					+ 0.001 * Inward;
				if (Score > BestScore + KINDA_SMALL_NUMBER)
				{
					BestScore = Score;
					BestAim = FVector2D(AlongCm, AcrossCm);
					Move = Candidate;
					Nominal = Flags;
				}
			}
		}
	}
	OutPlan.bWidthLimited = Nominal.bWidthLimited;
	OutPlan.bReachLimited = Nominal.bReachLimited;
	OutPlan.Cross = Nominal.Cross;
	OutPlan.Pass = Nominal.Pass;
	OutPlan.AimOffsetCm = BestAim;

	// Down at the height the foot's bone has standing flat on the floor the body stands on. (The height of the
	// foot's anchor was that of a foot planted on its toe, 4 cm up, and the step never reached the floor.)
	const double PlantedZ = Balance.SupportHeightCm + FootStandHeightCm[Side];
	OutPlan.Side = Side;
	OutPlan.Foot = FootBoneName[Side];
	OutPlan.From = From;
	OutPlan.Target = FVector(From.X + Move.X, From.Y + Move.Y, PlantedZ);
	OutPlan.CapturePoint = Point;
	OutPlan.CaptureAtLanding = Landing;

	// What the step would give: the stance sole and this one at its target, and the capture point in it.
	OutTargetSole = SwingSole;
	for (FVector2D& Corner : OutTargetSole)
	{
		Corner += Move;
	}
	FVector2D Unused;
	OutPlan.PlannedMarginCm = LandingMarginCm(Side, OutTargetSole, TimeS, Unused);
	return true;
}

FVector2D USandboxBodyController::CrossForward(int32 StanceSide) const
{
	// The foot's own turn, not the support's: after steps across, the two feet can point apart, and the calves to be
	// passed stand along the stance foot.
	const FAssist* FootAssist = Assists.FindByPredicate([StanceSide](const FAssist& Assist) { return Assist.Slot == EAssistSlot::Feet && Assist.Side == StanceSide; });
	const FBodyInstance* Foot = Mesh->GetBodyInstance(FootBoneName[StanceSide]);
	FQuat Yaw = FQuat::Identity;
	if (FootAssist && Foot && Foot->IsValidBodyInstance())
	{
		FQuat Tilt;
		(Foot->GetUnrealWorldTransform().GetRotation() * FootAssist->StartRotation.Inverse()).ToSwingTwist(FVector::UpVector, Tilt, Yaw);
	}
	// The character faces +y at Start.
	const FVector Forward3 = Yaw.RotateVector(FVector(0.0, 1.0, 0.0));
	return FVector2D(Forward3.X, Forward3.Y).GetSafeNormal();
}

FVector2D USandboxBodyController::CaptureAtLanding(int32 StanceSide, double TimeS) const
{
	// Moved by a held push as the decision moves it; it runs away from the stance sole (the centre of pressure as
	// far toward it as the foot reaches) by e^(omega t).
	const FVector2D Point = Balance.CapturePoint + (WeightN > 0.0 ? SustainedMomentNCm / WeightN : FVector2D::ZeroVector);
	TArray<FVector2D> StanceSole;
	FSandboxBodyProbe::SupportPolygon(Mesh, TArray<FName>{ FootBoneName[StanceSide] }, Tuning.Balance.SoleToleranceCm, StanceSole);
	if (StanceSole.Num() == 0 || !Balance.bValid)
	{
		return Point;
	}
	const FVector2D Pivot = FSandboxBodyProbe::NearestPointOfPolygon(StanceSole, Point);
	const double HeightCm = FMath::Max(Balance.CenterOfMass.Z - Balance.SupportHeightCm, 1.0);
	const double Growth = FMath::Exp(FMath::Sqrt(GravityCmS2 / HeightCm) * TimeS);
	return Pivot + (Point - Pivot) * Growth;
}

double USandboxBodyController::LandingMarginCm(int32 Side, const TArray<FVector2D>& TargetSole, double TimeS, FVector2D& OutLanding) const
{
	const int32 Other = 1 - Side;
	TArray<FVector2D> Points;
	FSandboxBodyProbe::SupportPolygon(Mesh, TArray<FName>{ FootBoneName[Other] }, Tuning.Balance.SoleToleranceCm, Points);
	Points.Append(TargetSole);
	TArray<FVector2D> Planned;
	FSandboxBodyProbe::ConvexHull2D(Points, Planned);
	OutLanding = CaptureAtLanding(Other, TimeS);
	return Planned.Num() > 0 ? FSandboxBodyProbe::MarginInsidePolygon(Planned, OutLanding) : 0.0;
}

void USandboxBodyController::UpdateShift(double DeltaTime)
{
	ShiftGoalCm = FVector2D::ZeroVector;
	if (Pending.bActive)
	{
		// From the middle of the two soles toward the middle of the stance sole: sideways when the feet stand side
		// by side, forward or back too when one stands ahead of the other.
		TArray<FVector2D> Soles[2];
		for (int32 Each = 0; Each < 2; ++Each)
		{
			FSandboxBodyProbe::SupportPolygon(Mesh, TArray<FName>{ FootBoneName[Each] }, Tuning.Balance.SoleToleranceCm, Soles[Each]);
		}
		if (Soles[0].Num() > 0 && Soles[1].Num() > 0)
		{
			const auto Middle = [](const TArray<FVector2D>& Sole)
			{
				FVector2D Sum = FVector2D::ZeroVector;
				for (const FVector2D& Corner : Sole)
				{
					Sum += Corner;
				}
				return Sum / Sole.Num();
			};
			const int32 Stance = 1 - Pending.Plan.Side;
			ShiftGoalCm = (Middle(Soles[Stance]) - Middle(Soles[Pending.Plan.Side])) * (0.5 * Tuning.Balance.StepShiftShare);
		}
	}
	else if (Swing.bActive && bShiftHeld)
	{
		// The weight stays on the stance foot while the other swings, and what of the shift lies along the step
		// goes over to the new foot as it comes down. (Let go along the step from the start, the body went with it,
		// and a prediction of where over 0.4 s of swing, grown by e^(omega t) = 3.7, put it 10 to 15 cm past the
		// new feet. Held whole, a step to the side landed 51 cm from the stance foot with the pelvis still over
		// it: the new leg could not reach its foot and pushed the body away.) A step forward or back from feet
		// side by side has no shift along it.
		const FVector2D Along = FVector2D(Swing.Target.X - Swing.From.X, Swing.Target.Y - Swing.From.Y).GetSafeNormal();
		const FVector2D AlongCm = Along * FVector2D::DotProduct(ShiftAtLiftCm, Along);
		ShiftGoalCm = ShiftAtLiftCm - AlongCm * Swing.Descent;
	}
	// Out of the shift as the anchors glide to the new support (balance.anchor_follow_s): let go sooner, the
	// pelvis's goal went back toward the old support first and the legs threw the body sideways.
	const double LagS = ShiftGoalCm.IsZero() ? Tuning.Balance.AnchorFollowS : Tuning.Balance.StepShiftS;
	const double Follow = LagS > 0.0 ? 1.0 - FMath::Exp(-DeltaTime / LagS) : 1.0;
	ShiftCm += (ShiftGoalCm - ShiftCm) * Follow;
	if (ShiftGoalCm.IsZero() && ShiftCm.Size() < 1.0e-4)
	{
		ShiftCm = FVector2D::ZeroVector;
	}
}

void USandboxBodyController::StartSwing(const FSandboxStepRecord& Plan, const TArray<FVector2D>& TargetSole, double DurationS, bool bReaim)
{
	const FSandboxBalanceTuning& Limits = Tuning.Balance;
	const int32 Side = Plan.Side;
	const int32 Other = 1 - Side;
	FSandboxStepRecord& Record = StepLog.Add_GetRef(Plan);
	Record.Number = ++StaggerSteps;
	Record.StartS = ElapsedS;
	Swing.bActive = true;
	Swing.Side = Side;
	Swing.ElapsedS = 0.0;
	Swing.From = Record.From;
	Swing.Target = Record.Target;
	Swing.Point = Record.From;
	Swing.VelocityCmS = FVector::ZeroVector;
	Swing.Across = 0.0;
	Swing.Descent = 0.0;
	Swing.DurationS = DurationS;
	Swing.TargetSole = TargetSole;
	// The sole at the lift, which the aim moves: the target sole less the plan's move.
	const FVector2D Move(Record.Target.X - Record.From.X, Record.Target.Y - Record.From.Y);
	Swing.LiftSole = TargetSole;
	for (FVector2D& Corner : Swing.LiftSole)
	{
		Corner -= Move;
	}
	Swing.bReaim = bReaim;
	Swing.Cross = Record.Cross;
	Swing.Pass = Record.Pass;
	if (Swing.Pass != 0)
	{
		// The swing passes the other foot's bone this far ahead of it (or behind).
		const FVector StanceAt = Mesh->GetBodyInstance(FootBoneName[Other])->GetUnrealWorldTransform().GetLocation();
		Swing.PassPoint = FVector2D(StanceAt.X, StanceAt.Y) + CrossForward(Other) * (Swing.Pass * Limits.CrossOffsetCm);
	}
	PlaceFoot(Side, EPlacement::Swing, Swing.Point, EFrame::Actual, Limits.StepEaseS);
	// The other leg carries the body while the foot is in the air.
	const FVector StancePoint = Placement[Other].Mode != EPlacement::None ? Placement[Other].Point : FlatFootPoint(Other);
	PlaceFoot(Other, EPlacement::Planted, StancePoint, EFrame::Stance, Limits.StepEaseS);
	PushTuningToControls();
	UE_LOG(LogSandbox, Display, TEXT("Body controller: step %d with %s at %.3f s, %.1f cm toward (%.1f, %.1f)%s%s"),
		Record.Number, *Record.Foot.ToString(), ElapsedS, (Record.Target - Record.From).Size2D(), Record.Target.X, Record.Target.Y,
		Record.bReachLimited ? TEXT(", cut to the reach") : TEXT(""),
		Record.Pass > 0 ? TEXT(", round the other foot in front") : Record.Pass < 0 ? TEXT(", round the other foot behind")
			: Record.Cross != 0 ? TEXT(", on the far side") : Record.bWidthLimited ? TEXT(", kept to its side") : TEXT(""));
}

bool USandboxBodyController::RequestStep(FName Foot, const FVector& WorldTarget, FString& OutError)
{
	const int32 Side = Foot == FootBoneName[0] ? 0 : Foot == FootBoneName[1] ? 1 : -1;
	if (!bStarted || !IsUpright() || IsStepUnderWay() || Side < 0 || !Cast<USandboxAnimInstance>(Mesh->GetAnimInstance()))
	{
		OutError = Side < 0 ? FString::Printf(TEXT("'%s' is not a foot of the rig"), *Foot.ToString())
			: TEXT("the body is not upright under control, or a step is under way");
		return false;
	}
	FSandboxStepRecord Plan;
	TArray<FVector2D> Sole;
	if (!PlanStep(Side, Tuning.Balance.StepTimeS, Plan, Sole))
	{
		OutError = TEXT("the feet have no soles to step with");
		return false;
	}
	// The plan's own target is replaced by the one asked for; its sole moves with it.
	const FVector2D Shift(WorldTarget.X - Plan.Target.X, WorldTarget.Y - Plan.Target.Y);
	for (FVector2D& Corner : Sole)
	{
		Corner += Shift;
	}
	Plan.Target = FVector(WorldTarget.X, WorldTarget.Y, Plan.Target.Z);
	Plan.bReachLimited = false;
	Plan.bWidthLimited = false;
	Plan.Cross = 0;
	Plan.Pass = 0;
	// A step is a step, asked for or not: the body is on one leg while it lasts, and stands again only once its
	// balance is back (the stagger's own way out).
	if (State != ESandboxBodyState::Stagger)
	{
		RecoveredS = 0.0;
		StaggerSteps = 0;
		EnterState(ESandboxBodyState::Stagger, FString::Printf(TEXT("a step with %s was asked for"), *Foot.ToString()));
	}
	// First the weight goes over onto the other foot (UpdateStep lifts this one when the step would hold): both
	// legs are solved over the pelvis's goal, which moves toward the stance foot.
	Pending.bActive = true;
	Pending.Plan = Plan;
	Pending.TargetSole = Sole;
	Pending.ElapsedS = 0.0;
	const FSandboxBalanceTuning& Limits = Tuning.Balance;
	for (int32 Each = 0; Each < 2; ++Each)
	{
		const FVector Point = Placement[Each].Mode != EPlacement::None ? Placement[Each].Point : FlatFootPoint(Each);
		PlaceFoot(Each, EPlacement::Planted, Point, EFrame::Stance, Limits.StepEaseS);
	}
	return true;
}

void USandboxBodyController::UpdateStep(double DeltaTime)
{
	const FSandboxBalanceTuning& Limits = Tuning.Balance;
	if (!IsUpright())
	{
		// A body that falls has let go of its feet already (EnterState).
		Swing.bActive = false;
		return;
	}
	// Legs placed from outside (a scenario's foot_target) are not the controller's to step with.
	if (PlacedFeet.Num() > 0 || !Cast<USandboxAnimInstance>(Mesh->GetAnimInstance()))
	{
		return;
	}

	if (Pending.bActive)
	{
		// The foot lifts once the weight is over the stance foot (the capture point within
		// balance.step_shift_ready_cm of its sole: it can carry the body alone while the other swings), or when
		// the shift has taken long enough.
		Pending.ElapsedS += DeltaTime;
		FVector2D Landing;
		const double MarginCm = LandingMarginCm(Pending.Plan.Side, Pending.TargetSole, Limits.StepAskedTimeS, Landing);
		TArray<FVector2D> StanceSole;
		FSandboxBodyProbe::SupportPolygon(Mesh, TArray<FName>{ FootBoneName[1 - Pending.Plan.Side] }, Limits.SoleToleranceCm, StanceSole);
		const FVector2D Point = Balance.CapturePoint + (WeightN > 0.0 ? SustainedMomentNCm / WeightN : FVector2D::ZeroVector);
		const bool bOver = StanceSole.Num() > 0 && FSandboxBodyProbe::MarginInsidePolygon(StanceSole, Point) >= -Limits.StepShiftReadyCm;
		if (bOver || Pending.ElapsedS >= Limits.StepShiftMaxS - KINDA_SMALL_NUMBER)
		{
			Pending.bActive = false;
			Pending.Plan.CapturePoint = Balance.CapturePoint + (WeightN > 0.0 ? SustainedMomentNCm / WeightN : FVector2D::ZeroVector);
			Pending.Plan.CaptureAtLanding = Landing;
			Pending.Plan.PlannedMarginCm = MarginCm;
			Pending.Plan.From = Mesh->GetBodyInstance(FootBoneName[Pending.Plan.Side])->GetUnrealWorldTransform().GetLocation();
			UE_LOG(LogSandbox, Display, TEXT("Body controller: weight shifted for %.3f s%s"), Pending.ElapsedS,
				bOver ? TEXT("") : TEXT(", not yet over the stance foot; the foot lifts anyway"));
			bShiftHeld = true;
			ShiftAtLiftCm = ShiftCm;
			StartSwing(Pending.Plan, Pending.TargetSole, Limits.StepAskedTimeS, false);
		}
		else
		{
			return;
		}
	}

	if (!Swing.bActive)
	{
		// Recovery steps are the stagger's; a standing body steps only when asked to (RequestStep).
		if (State != ESandboxBodyState::Stagger || !bStepNeeded)
		{
			return;
		}
		FSandboxStepRecord Plans[2];
		TArray<FVector2D> Soles[2];
		// Only a foot whose other foot is on the floor can step: the body has to stand on something meanwhile.
		// (A blow that lifts one foot leaves that foot as the only one that can.)
		bool bPlanned[2] = { false, false };
		for (int32 Each = 0; Each < 2; ++Each)
		{
			bPlanned[Each] = Balance.FootForceN[1 - Each] > 0.0 && PlanStep(Each, Limits.StepTimeS, Plans[Each], Soles[Each]);
		}
		if (!bPlanned[0] && !bPlanned[1])
		{
			return;
		}
		int32 Side = 0;
		if (StaggerSteps > 0 && StepLog.Num() > 0 && bPlanned[1 - StepLog.Last().Side])
		{
			// Step after step, the feet take turns.
			Side = 1 - StepLog.Last().Side;
		}
		else if (bPlanned[0] && bPlanned[1])
		{
			// The first step is made by the foot that catches the capture point better, and of two that catch it
			// alike, by the one that has less far to go. Mirror-image pushes have to choose mirror-image feet, so
			// near-equal figures count as equal, and then the foot on the side the capture point runs to steps, or,
			// straight ahead or back, the one that carries less.
			constexpr double SameCm = 1.0;
			const double Way[2] = { (Plans[0].Target - Plans[0].From).Size2D(), (Plans[1].Target - Plans[1].From).Size2D() };
			// A step across the other foot is not held back: with the weight already on the near foot, lifting it
			// leaves the body on the far one, from which the capture point runs away much faster (40 cm out instead
			// of 15 after a push to the pelvis's side: to 81 cm by the landing instead of 51).
			if (FMath::Abs(Plans[0].PlannedMarginCm - Plans[1].PlannedMarginCm) > SameCm)
			{
				Side = Plans[0].PlannedMarginCm > Plans[1].PlannedMarginCm ? 0 : 1;
			}
			else if (FMath::Abs(Way[0] - Way[1]) > SameCm)
			{
				Side = Way[0] < Way[1] ? 0 : 1;
			}
			else
			{
				FVector SupportLocation;
				FQuat SupportRotation;
				SupportFrame(SupportLocation, SupportRotation);
				const FVector Left3 = SupportRotation.RotateVector(FVector(1.0, 0.0, 0.0));
				const FVector2D Runs = Plans[0].CaptureAtLanding - FVector2D(Balance.CenterOfMass.X, Balance.CenterOfMass.Y);
				const double Sideways = FVector2D::DotProduct(Runs.GetSafeNormal(), FVector2D(Left3.X, Left3.Y));
				constexpr double StraightCos = 0.1;
				Side = FMath::Abs(Sideways) > StraightCos ? (Sideways > 0.0 ? 0 : 1) : (Balance.FootForceN[0] <= Balance.FootForceN[1] ? 0 : 1);
			}
		}
		else
		{
			Side = bPlanned[0] ? 0 : 1;
		}
		const int32 Other = 1 - Side;
		UE_LOG(LogSandbox, Display, TEXT("Body controller: feet to step with: left %s (margin %.1f cm%s), right %s (margin %.1f cm%s)"),
			bPlanned[0] ? TEXT("can") : TEXT("cannot"), Plans[0].PlannedMarginCm, Plans[0].Cross != 0 ? TEXT(", across") : TEXT(""),
			bPlanned[1] ? TEXT("can") : TEXT("cannot"), Plans[1].PlannedMarginCm, Plans[1].Cross != 0 ? TEXT(", across") : TEXT(""));
		// The other foot has to be on the floor to carry the body, and after a step it has to have taken weight.
		const double OtherMinN = StaggerSteps > 0 ? Limits.FootContactMinN : 0.0;
		if (!bPlanned[Side] || Balance.FootForceN[Other] <= OtherMinN)
		{
			return;
		}

		StartSwing(Plans[Side], Soles[Side], Limits.StepTimeS, true);
	}

	// The swing: the foot's point travels to the target, eased in and out, over an arc, and ends a little below
	// the floor, so that the leg sets the foot down on it whatever the pelvis has done meanwhile.
	Swing.ElapsedS += DeltaTime;
	const double DurationS = FMath::Max(Swing.DurationS, DeltaTime);
	const double Progress = FMath::Clamp(Swing.ElapsedS / DurationS, 0.0, 1.0);
	// Over the target by StepOverShare of the swing: up in the first half of that, then across at the top, then
	// down to below the floor in the rest.
	const double Over = FMath::Clamp(Limits.StepOverShare, KINDA_SMALL_NUMBER, 1.0 - KINDA_SMALL_NUMBER);
	// A recovery step is aimed anew, over the time it has left, until its foot starts down. Aimed once at the lift,
	// the capture point was taken to run away from the stance foot, which a blow had lifted off the floor for the
	// first 0.15 s: the step went 17 cm to the side of where the capture point went.
	if (Swing.bReaim && Progress < Over)
	{
		FSandboxStepRecord Aim;
		TArray<FVector2D> AimSole;
		const FVector2D KeepAim = StepLog.Last().AimOffsetCm;
		if (PlanStep(Swing.Side, FMath::Max(DurationS - Swing.ElapsedS, 0.0), Aim, AimSole, &Swing.LiftSole, &Swing.From, Swing.Cross, &KeepAim))
		{
			Swing.Target = Aim.Target;
			Swing.TargetSole = AimSole;
			FSandboxStepRecord& Record = StepLog.Last();
			Record.Target = Aim.Target;
			Record.bReachLimited = Aim.bReachLimited;
			Record.bWidthLimited = Aim.bWidthLimited;
			Record.Cross = Aim.Cross;
		}
	}
	const double Across =FMath::SmoothStep(0.0, 1.0, FMath::Min(Progress / Over, 1.0));
	const double Descent = Progress > Over ? FMath::SmoothStep(0.0, 1.0, (Progress - Over) / (1.0 - Over)) : 0.0;
	// The foot leaves the floor at rest and speeds up. (Raised along a sine, it started at its fastest, 1.8 m/s, and the
	// leg drove its toe into the floor as it lifted: up to 3800 N, the stance foot thrown off the floor.)
	const double Rise = FMath::SmoothStep(0.0, 1.0, FMath::Min(Progress / (0.5 * Over), 1.0));
	// The swing leg stiffens over balance.step_ease_s (PushTuningToControls), not at once.
	if (Swing.ElapsedS <= Limits.StepEaseS + DeltaTime)
	{
		PushTuningToControls();
	}
	const FVector LastPoint = Swing.Point;
	Swing.Across = Across;
	Swing.Descent = Descent;
	Swing.Point = FMath::Lerp(Swing.From, Swing.Target, Across) +
		FVector(0.0, 0.0, Limits.StepLiftCm * Rise * (1.0 - Descent) - Limits.StepPressCm * Descent);
	if (Swing.Pass != 0)
	{
		// Across the other foot: over the floor along a quadratic curve through the pass point (halfway), so that
		// the foot goes round the other foot instead of through it.
		const FVector2D From2(Swing.From.X, Swing.From.Y);
		const FVector2D Target2(Swing.Target.X, Swing.Target.Y);
		const FVector2D Control2 = 2.0 * Swing.PassPoint - 0.5 * (From2 + Target2);
		const FVector2D Curve = From2 * FMath::Square(1.0 - Across) + Control2 * (2.0 * Across * (1.0 - Across)) + Target2 * FMath::Square(Across);
		Swing.Point.X = Curve.X;
		Swing.Point.Y = Curve.Y;
	}
	Swing.VelocityCmS = DeltaTime > 0.0 ? (Swing.Point - LastPoint) / DeltaTime : FVector::ZeroVector;
	Placement[Swing.Side].Point = Swing.Point;
	// The assist's foot-planting control carries the foot along: its target is the swing's point, moving.
	for (const FAssist& Assist : Assists)
	{
		if (IsSwingFoot(Assist))
		{
			FPhysicsControlTarget Target;
			Target.TargetPosition = Swing.Point;
			Target.TargetVelocity = Swing.VelocityCmS;
			Target.TargetOrientation = Assist.Target.Rotator();
			Target.bApplyControlPointToTarget = true;
			Control->SetControlTarget(Assist.Control, Target);
		}
	}

	// Down once the foot touches the floor on its way down over the target, or when the swing has run twice its
	// time. A toe that brushes the floor on the way across is not a landing. Waiting for the swing's end instead,
	// a foot that touched early with its toe (a step back) or its edge (a step to the side) was driven on into the
	// floor by the stiffened swing leg: 2900 N, both feet off the floor, the foot thrown off again.
	const bool bTouched = Progress >= Over && Balance.FootForceN[Swing.Side] > 0.0;
	if (bTouched || Swing.ElapsedS >= 2.0 * DurationS)
	{
		Swing.bActive = false;
		bShiftHeld = false;
		FSandboxStepRecord& Record = StepLog.Last();
		const FBodyInstance* Landed = Mesh->GetBodyInstance(FootBoneName[Swing.Side]);
		Record.LandS = ElapsedS;
		Record.bTouched = bTouched;
		Record.Landed = Landed ? Landed->GetUnrealWorldTransform().GetLocation() : Swing.Target;
		Record.CaptureAtTouch = Balance.CapturePoint + (WeightN > 0.0 ? SustainedMomentNCm / WeightN : FVector2D::ZeroVector);
		Record.MarginAtTouchCm = LastDecisionMarginCm;
		// Both feet stand where they are now, and both legs are solved as the stance leg was: the pelvis upright
		// at its height, balance.stance_follow of the way from its anchor (which glides to the new support from
		// now on) to where it is. Fully at the anchor, a leg reaching back toward a foot behind a body that is
		// ahead of its anchor is asked for too short a reach and lifts that foot. Over AnchorFollowS, so that no
		// leg's target jumps.
		// A body that is standing (a step asked for) goes straight over to the posture over its anchors.
		const int32 Other = 1 - Swing.Side;
		const EFrame Planted = State == ESandboxBodyState::Stagger ? EFrame::Stance : EFrame::Anchor;
		// Where the other foot stands now: it may have turned or slid under the body while this one swung.
		const FBodyInstance* OtherFoot = Mesh->GetBodyInstance(FootBoneName[Other]);
		const FVector OtherAt = OtherFoot ? OtherFoot->GetUnrealWorldTransform().GetLocation() : Placement[Other].Point;
		PlaceFoot(Swing.Side, EPlacement::Planted, FVector(Record.Landed.X, Record.Landed.Y, Swing.Target.Z), Planted, Limits.AnchorFollowS);
		// From the height the bone touched at down to the planted one: the sole comes down onto the floor.
		Placement[Swing.Side].SettleCm = FMath::Max(Record.Landed.Z - Swing.Target.Z, 0.0);
		Placement[Swing.Side].SettleS = Limits.StepSettleS;
		PlaceFoot(Other, EPlacement::Planted, FVector(OtherAt.X, OtherAt.Y, FlatFootPoint(Other).Z), Planted, Limits.AnchorFollowS);
		// The assist's control of the landed foot was led along the swing, with the swing's velocity: it is planted
		// where the foot is now (FollowSupport may have done so as the foot touched, before the swing's last
		// guidance in the same tick overwrote it).
		for (FAssist& Assist : Assists)
		{
			if (Assist.Slot == EAssistSlot::Feet && Assist.Side == Swing.Side && Landed && Landed->IsValidBodyInstance())
			{
				Assist.Target = FlatFootTransform(Assist);
				SetAssistTarget(Assist);
			}
		}
		PushTuningToControls();
		UE_LOG(LogSandbox, Display, TEXT("Body controller: step %d landed at %.3f s, %.1f cm from its target%s"),
			Record.Number, ElapsedS, FVector::Dist2D(Record.Landed, Record.Target), bTouched ? TEXT("") : TEXT(" (never touched the floor)"));
	}
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
			bFlinchWeakeningOver = false;
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
	if (IsUpright())
	{
		// The capture point decides. Inside the support, or outside by no more than the ankles and the hips
		// can bring back, the body stands. Further out it needs a step, and staggers. Beyond a step's reach
		// balance is lost. A step being needed holds only after decision_s, so that a foot that loses its load
		// for one frame decides nothing.
		const FSandboxBalanceTuning& Limits = Tuning.Balance;
		LastDecisionMarginCm = DecisionMarginCm();
		// Where the capture point will be when a step started now has landed: it runs away from the support's
		// edge by e^(omega t) (linear inverted pendulum).
		// While a step is under way, the margin is to the support the step will give, and the capture point runs
		// away only for the rest of the swing.
		const double HeightCm = FMath::Max(Balance.CenterOfMass.Z - Balance.SupportHeightCm, 1.0);
		const double StepLeftS = Swing.bActive ? FMath::Max(Swing.DurationS - Swing.ElapsedS, 0.0) : Limits.StepTimeS;
		const double Growth = FMath::Exp(FMath::Sqrt(GravityCmS2 / HeightCm) * StepLeftS);
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
		else if (bStepNeeded && State != ESandboxBodyState::Stagger)
		{
			RecoveredS = 0.0;
			StaggerSteps = 0;
			EnterState(ESandboxBodyState::Stagger, FString::Printf(TEXT("capture point %.1f cm outside the support: a step is needed"), -LastDecisionMarginCm));
		}
	}
	else
	{
		bStepNeeded = false;
		StepNeededS = 0.0;
		LostS = 0.0;
	}

	if (IsUpright())
	{
		// Whatever the capture point says, the posture itself can give way: the legs fold (the pelvis sinks) or
		// the body bends over too far to stand.
		const FAssist* Pelvis = Assists.FindByPredicate([](const FAssist& Assist) { return Assist.Slot == EAssistSlot::Pelvis; });
		const FBodyInstance* Body = Pelvis ? Mesh->GetBodyInstance(Pelvis->Bone) : nullptr;
		if (Body && Body->IsValidBodyInstance())
		{
			const FTransform Now = Body->GetUnrealWorldTransform();
			// Below the height the legs' posture holds it at: in a wide stance after a step that is lower than
			// standing by LowerCm, on purpose (a lunge of 50 cm held the pelvis 20 cm down and was taken for a fall).
			const double DropCm = Pelvis->Target.GetLocation().Z - LowerCm - Now.GetLocation().Z;
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
			bFlinchWeakeningOver = true;
			EnterState(ESandboxBodyState::Standing, TEXT("recovered from the hit"));
		}
		else
		{
			// The hit muscles regain strength every tick.
			PushTuningToControls();
		}
	}
	else if (State == ESandboxBodyState::Stagger)
	{
		// A flinch that turned into a stagger goes on weakening the hit muscles, by its own clock, until they are
		// back to full strength (the last push of them is at full strength).
		if (!bFlinchWeakeningOver)
		{
			PushTuningToControls();
			bFlinchWeakeningOver = ElapsedS - FlinchStartedS >= Tuning.Reactions.FlinchRecoveryS;
		}
		// Balance is back once the centre of mass is over the support and the capture point within what the
		// ankles and hips bring back, for a while.
		// And both feet are on the floor: the assist anchors them where they are when the body stands again, and a
		// foot that had bounced off the floor after its step was anchored 3 cm up in the air, tilted, and the
		// pelvis's anchor with it.
		// And the body has come to rest over its feet (balance.recover_speed_mps): the assist's springs take hold
		// again at full strength, and a body still on its way was thrown by them into the next stagger.
		const bool bFeetDown = Balance.FootForceN[0] > 0.0 && Balance.FootForceN[1] > 0.0;
		const bool bSlow = Balance.Velocity.Size2D() <= Tuning.Balance.RecoverSpeedMps * 100.0;
		const bool bRecovered = Balance.bValid && Balance.IsSupported() && Balance.CenterOfMassMarginCm > 0.0 &&
			LastDecisionMarginCm >= -Tuning.Balance.RecoverableCm && !bStepNeeded && !IsStepUnderWay() && bFeetDown && bSlow;
		RecoveredS = bRecovered ? RecoveredS + DeltaTime : 0.0;
		if (RecoveredS >= Tuning.Balance.RecoverS - KINDA_SMALL_NUMBER && bFlinchWeakeningOver)
		{
			EnterState(ESandboxBodyState::Standing, FString::Printf(TEXT("centre of mass over the support for %.2f s"), RecoveredS));
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
		// The leg of a step under way is stiffer, so that the foot keeps up with its point.
		const bool bSwingLeg = Swing.bActive &&
			(Muscle.ChildBone == ThighBoneName[Swing.Side] || Muscle.ChildBone == CalfBoneName[Swing.Side] || Muscle.ChildBone == FootBoneName[Swing.Side]);
		// Eased in from the lift over balance.step_ease_s: doubled at once, the error the leg had while it carried weight
		// was pushed twice as hard into the floor.
		const double SwingEase = Tuning.Balance.StepEaseS > 0.0 ? FMath::SmoothStep(0.0, 1.0, FMath::Min(Swing.ElapsedS / Tuning.Balance.StepEaseS, 1.0)) : 1.0;
		const double SwingScale = bSwingLeg ? FMath::Lerp(1.0, FMath::Max(Tuning.Balance.SwingStiffnessScale, 1.0), SwingEase) : 1.0;
		const double SwingDampingScale = bSwingLeg ? FMath::Lerp(1.0, Tuning.Balance.SwingDampingScale, SwingEase) : 1.0;
		const double Spring = Group.StiffnessNmPerRad * MuscleScale(Muscle.Group) * SwingScale * TorqueToUnreal / ChaosAngularDriveScale;
		FPhysicsControlData Data;
		// Force mode: the spring is a real torque per radian. In the plugin's default (acceleration) mode
		// it would be scaled by the inertia of the two jointed bodies alone, which leaves an ankle or a hip
		// far too weak for the weight of everything above it.
		Data.bUseAccelerationDriveMode = false;
		Data.LinearStrength = 0.0f; // the joint itself holds the bodies together
		Data.AngularStrength = StrengthForSpring(Spring);
		Data.AngularDampingRatio = 0.0f;
		Data.AngularExtraDamping = static_cast<float>(Spring * Group.DampingS * SwingDampingScale);
		Data.MaxTorque = static_cast<float>(Group.MaxTorqueNm * TorqueToUnreal);
		Data.bUseSkeletalAnimation = true;
		// The two bodies are jointed and must never collide, whatever this extra constraint says.
		Data.bDisableCollision = true;
		Control->SetControlData(Muscle.Control, Data);
	}

	for (const FAssist& Assist : Assists)
	{
		FSandboxAssistTuning Slot = AssistTuning(Assist.Slot);
		if (IsSwingFoot(Assist))
		{
			// The foot of a step under way is guided along its swing (UpdateStep moves the target).
			Slot.HorizontalNPerM = Tuning.Balance.SwingFootNPerM;
			Slot.VerticalNPerM = Tuning.Balance.SwingFootNPerM;
		}
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
		Data.MaxTorque = static_cast<float>(Slot.MaxTorqueNm * TorqueToUnreal);
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
	UpdateStep(DeltaTime);
	UpdateShift(DeltaTime);
	UpdateLowering(DeltaTime);
	UpdateStrategies(DeltaTime);
	UpdatePlacements(DeltaTime);
	UpdatePoseTurns();
	UpdateAssistLoad();
	Control->UpdateControls(DeltaTime);

	if (CVarStepTrace.GetValueOnGameThread() > 1 || (CVarStepTrace.GetValueOnGameThread() > 0 && (Placement[0].Mode != EPlacement::None || Placement[1].Mode != EPlacement::None)))
	{
		const USandboxAnimInstance* Animation = Cast<USandboxAnimInstance>(Mesh->GetAnimInstance());
		const FBodyInstance* PelvisBody = Mesh->GetBodyInstance(TEXT("pelvis"));
		const FVector P = PelvisBody ? PelvisBody->GetUnrealWorldTransform().GetLocation() : FVector::ZeroVector;
		const FVector V = PelvisBody ? PelvisBody->GetUnrealWorldVelocity() : FVector::ZeroVector;
		const FAssist* PelvisAssist = Assists.FindByPredicate([](const FAssist& Assist) { return Assist.Slot == EAssistSlot::Pelvis; });
		const FQuat Turn = PelvisBody && PelvisAssist ? PelvisBody->GetUnrealWorldTransform().GetRotation() * PelvisAssist->Target.GetRotation().Inverse() : FQuat::Identity;
		const FVector Goal = PelvisAssist ? AssistGoal(*PelvisAssist).GetLocation() : FVector::ZeroVector;
		const FRotator R = Turn.Rotator();
		FString Line = FString::Printf(TEXT("TRACE t=%.3f %s pelvis (%.1f %.1f %.1f) rot p%.0f y%.0f r%.0f v (%.0f %.0f %.0f) com (%.1f %.1f %.1f) cp (%.1f %.1f)"),
			ElapsedS, FSandboxBodyTuning::StateName(State), P.X, P.Y, P.Z, R.Pitch, R.Yaw, R.Roll, V.X, V.Y, V.Z,
			Balance.CenterOfMass.X, Balance.CenterOfMass.Y, Balance.CenterOfMass.Z, Balance.CapturePoint.X, Balance.CapturePoint.Y);
		if (PelvisBody)
		{
			const FTransform PelvisT = PelvisBody->GetUnrealWorldTransform();
			const FBodyInstance* Calf = Mesh->GetBodyInstance(CalfBoneName[0]);
			const FBodyInstance* Thigh = Mesh->GetBodyInstance(ThighBoneName[0]);
			const FBodyInstance* FootL = Mesh->GetBodyInstance(FootBoneName[0]);
			const FTransform Comp = Mesh->GetComponentTransform();
			if (Calf && Thigh && FootL)
			{
				// In the component's frame: hip, knee, ankle of the left leg.
				const FVector H = Comp.InverseTransformPosition(Thigh->GetUnrealWorldTransform().GetLocation());
				const FVector K = Comp.InverseTransformPosition(Calf->GetUnrealWorldTransform().GetLocation());
				const FVector A = Comp.InverseTransformPosition(FootL->GetUnrealWorldTransform().GetLocation());
				const FVector Pd = Comp.InverseTransformPosition(PelvisT.GetLocation());
				const FVector Tg = Comp.InverseTransformPosition(Placement[0].Point);
				Line += FString::Printf(TEXT(" CS pel(%.1f %.1f %.1f) hip(%.1f %.1f %.1f) knee(%.1f %.1f %.1f) ank(%.1f %.1f %.1f) tgt(%.1f %.1f %.1f)"),
					Pd.X, Pd.Y, Pd.Z, H.X, H.Y, H.Z, K.X, K.Y, K.Z, A.X, A.Y, A.Z, Tg.X, Tg.Y, Tg.Z);
				// The animation's pose of the left ankle (what the muscles are asked for), component space.
				const TArray<FTransform>& Local = Mesh->GetBoneSpaceTransforms();
				const FReferenceSkeleton& Skeleton = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
				if (FootBoneIndex[0] != INDEX_NONE && Local.IsValidIndex(FootBoneIndex[0]))
				{
					FTransform Acc = Local[FootBoneIndex[0]];
					for (int32 Parent = Skeleton.GetParentIndex(FootBoneIndex[0]); Parent != INDEX_NONE && Local.IsValidIndex(Parent); Parent = Skeleton.GetParentIndex(Parent))
					{
						Acc = Acc * Local[Parent];
					}
					const FVector AnimAnkle = Acc.GetLocation();
					Line += FString::Printf(TEXT(" anim(%.1f %.1f %.1f)"), AnimAnkle.X, AnimAnkle.Y, AnimAnkle.Z);
				}
				// Twist about the bone (deg) of each left-leg bone relative to its parent, from the reference pose:
				// the animation's (a) and the bodies' (b).
				{
					const auto AnimCs = [&](int32 Bone)
					{
						FTransform T = Local[Bone];
						for (int32 Parent = Skeleton.GetParentIndex(Bone); Parent != INDEX_NONE && Local.IsValidIndex(Parent); Parent = Skeleton.GetParentIndex(Parent))
						{
							T = T * Local[Parent];
						}
						return T.GetRotation();
					};
					const auto BodyCs = [&](int32 Bone)
					{
						const FBodyInstance* B = Mesh->GetBodyInstance(Skeleton.GetBoneName(Bone));
						return B ? (Comp.GetRotation().Inverse() * B->GetUnrealWorldTransform().GetRotation()) : FQuat::Identity;
					};
					const int32 Ball = Skeleton.FindBoneIndex(TEXT("ball_l"));
					const int32 Chain[4] = { ThighBoneIndex[0], Skeleton.GetParentIndex(FootBoneIndex[0]), FootBoneIndex[0], Ball };
					for (int32 Link = 0; Link < 3; ++Link)
					{
						const int32 Bone = Chain[Link];
						const int32 Next = Chain[Link + 1];
						const int32 Parent = Skeleton.GetParentIndex(Bone);
						if (Bone == INDEX_NONE || Next == INDEX_NONE || Parent == INDEX_NONE || !Local.IsValidIndex(Bone))
						{
							continue;
						}
						const FTransform RefParent = FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, Parent);
						const FTransform RefBone = FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, Bone);
						const FTransform RefNext = FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, Next);
						const FQuat RefRel = RefParent.GetRotation().Inverse() * RefBone.GetRotation();
						const FVector Axis = RefParent.GetRotation().Inverse().RotateVector((RefNext.GetLocation() - RefBone.GetLocation()).GetSafeNormal());
						double Out[2];
						const FQuat Rel[2] = { AnimCs(Parent).Inverse() * AnimCs(Bone), BodyCs(Parent).Inverse() * BodyCs(Bone) };
						for (int32 Which = 0; Which < 2; ++Which)
						{
							FQuat SwingPart, TwistPart;
							(Rel[Which] * RefRel.Inverse()).GetNormalized().ToSwingTwist(Axis, SwingPart, TwistPart);
							Out[Which] = FMath::RadiansToDegrees(TwistPart.GetTwistAngle(Axis));
						}
						Line += FString::Printf(TEXT(" T%s(a%.0f b%.0f)"), *Skeleton.GetBoneName(Bone).ToString().Left(2), Out[0], Out[1]);
					}
				}
				// The left leg's joints: swing1, swing2, twist (deg, from the middle of the range) and how far into the
				// swing cone (1 at its edge) and the twist range.
				for (const FName Child : { ThighBoneName[0], CalfBoneName[0], FootBoneName[0] })
				{
					for (FConstraintInstance* Joint : Mesh->Constraints)
					{
						if (Joint && Joint->ConstraintBone1 == Child)
						{
							const double S1 = FMath::RadiansToDegrees(Joint->GetCurrentSwing1());
							const double S2 = FMath::RadiansToDegrees(Joint->GetCurrentSwing2());
							const double Tw = FMath::RadiansToDegrees(Joint->GetCurrentTwist());
							const double L1 = Joint->GetAngularSwing1Limit();
							const double L2 = Joint->GetAngularSwing2Limit();
							const double Lt = Joint->GetAngularTwistLimit();
							const double Cone = (L1 > 0.0 ? FMath::Square(S1 / L1) : 0.0) + (L2 > 0.0 ? FMath::Square(S2 / L2) : 0.0);
							Line += FString::Printf(TEXT(" J%s(%.0f/%.0f %.0f/%.0f %.0f/%.0f c%.2f)"), *Child.ToString().Left(2),
								S1, L1, S2, L2, Tw, Lt, Cone);
						}
					}
				}
			}
		}
		Line += FString::Printf(TEXT(" goal (%.1f %.1f %.1f) low %.1f shift (%.1f %.1f) %s"), Goal.X, Goal.Y, Goal.Z, LowerCm, ShiftCm.X, ShiftCm.Y, Pending.bActive ? TEXT("PEND") : Swing.bActive ? TEXT("SWING") : TEXT("-"));
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FBodyInstance* FootBody = Mesh->GetBodyInstance(FootBoneName[Side]);
			const FVector F = FootBody ? FootBody->GetUnrealWorldTransform().GetLocation() : FVector::ZeroVector;
			const FVector FV = FootBody ? FootBody->GetUnrealWorldVelocity() : FVector::ZeroVector;
			const FTransform Frame = Placement[Side].Mode != EPlacement::None ? FrameTransform(Placement[Side].Frame, Side) : Mesh->GetComponentTransform();
			const FVector Hip = Frame.TransformPosition(HipReferenceCs[Side]);
			const FVector Pt = Placement[Side].Point;
			// The foot's turn from its planted orientation (the assist's target for it).
			const FQuat FootRot = FootBody ? FootBody->GetUnrealWorldTransform().GetRotation() : FQuat::Identity;
			const FAssist* FootAssist = Assists.FindByPredicate([Side](const FAssist& Assist) { return Assist.Slot == EAssistSlot::Feet && Assist.Side == Side; });
			const FRotator FootTurn = FootAssist ? (FootRot * FootAssist->Target.GetRotation().Inverse()).Rotator() : FRotator::ZeroRotator;
			Line += FString::Printf(TEXT(" | %s m%d f%d w%.2f N%.0f foot (%.1f %.1f %.1f) v (%.0f %.0f %.0f) ang p%.0f y%.0f r%.0f pt (%.1f %.1f %.1f) frameZ %.1f hip-pt %.1f/%.1f"),
				Side == 0 ? TEXT("L") : TEXT("R"), static_cast<int32>(Placement[Side].Mode), static_cast<int32>(Placement[Side].Frame),
				Animation ? Animation->GetLimbWeight(FootBoneIndex[Side]) : -1.0, Balance.FootForceN[Side], F.X, F.Y, F.Z, FV.X, FV.Y, FV.Z,
				FootTurn.Pitch, FootTurn.Yaw, FootTurn.Roll,
				Pt.X, Pt.Y, Pt.Z, Frame.GetLocation().Z, FVector::Dist(Hip, Pt), LegLengthCm[Side]);
		}
		UE_LOG(LogSandbox, Display, TEXT("%s"), *Line);
	}

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
		// A foot the pose holds where it was set down after a step is support too: if the body leans off it and
		// it loses the floor for a moment, the leg still puts it there.
		const bool bHeld = Assist.Side >= 0 && Placement[Assist.Side].Mode == EPlacement::Planted;
		if (Assist.Slot == EAssistSlot::Feet && (Assist.AirS < Tuning.Balance.LiftGraceS - KINDA_SMALL_NUMBER || bHeld))
		{
			Planted.Add(Assist.Bone);
		}
	}
	// Under a held push the centre of pressure has to be F h / (m g) further along the force than the capture
	// point alone says: that is the point that must stay inside the support.
	const FVector2D Shift = WeightN > 0.0 ? SustainedMomentNCm / WeightN : FVector2D::ZeroVector;
	const FVector2D Point = Balance.CapturePoint + Shift;
	// While a foot swings, the support is the one the step will give: the stance sole and the swinging sole at
	// its target.
	if (Swing.bActive)
	{
		TArray<FVector2D> Planned;
		PlannedSupport(Planned);
		if (Planned.Num() > 0)
		{
			return FSandboxBodyProbe::MarginInsidePolygon(Planned, Point);
		}
	}
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
		FQuat Tilt, Twist;
		(Assist.Target.GetRotation() * Assist.StartRotation.Inverse()).ToSwingTwist(FVector::UpVector, Tilt, Twist);
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
	const bool bUpright = IsUpright();
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
					// Flat on the floor: a foot that came down on its toe was planted 4 cm up and tilted, and the
					// next step of it was aimed that high and never reached the floor.
					Assist.Target = FlatFootTransform(Assist);
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
			Assist.FollowS = GlideSeconds(Assist.FollowFrom.GetLocation(), Assist.FollowTo.GetLocation());
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
		const double FollowS = Assist.FollowS;
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
		FSandboxAssistTuning Slot = AssistTuning(Assist.Slot);
		FTransform Goal = AssistGoal(Assist);
		FVector GoalVelocityMps = FVector::ZeroVector;
		if (IsSwingFoot(Assist))
		{
			Slot.HorizontalNPerM = Tuning.Balance.SwingFootNPerM;
			Slot.VerticalNPerM = Tuning.Balance.SwingFootNPerM;
			Goal.SetLocation(Swing.Point);
			GoalVelocityMps = Swing.VelocityCmS * 0.01;
		}

		// Centimetres to metres.
		const FVector TargetCom = Goal.TransformPositionNoScale(Body->GetMassSpaceLocal().GetLocation());
		const FVector ErrorM = (TargetCom - Body->GetCOMPosition()) * 0.01;
		const FVector VelocityMps = Body->GetUnrealWorldVelocity() * 0.01 - GoalVelocityMps;
		const FVector Stiffness(Slot.HorizontalNPerM, Slot.HorizontalNPerM, Slot.VerticalNPerM);
		const FVector CommandN = Stiffness * (ErrorM - VelocityMps * Slot.DampingS);
		ForceN += CommandN.Size();
		const int32 SlotIndex = static_cast<int32>(Assist.Slot);
		HorizontalN[SlotIndex] += CommandN.Size2D();
		VerticalN[SlotIndex] += FMath::Abs(CommandN.Z);

		const FQuat Delta = (Assist.Target.GetRotation() * Body->GetUnrealWorldTransform().GetRotation().Inverse()).GetShortestArcWith(FQuat::Identity);
		const FVector ErrorRad = Delta.ToRotationVector();
		const double SlotTorqueNm = Slot.AngularNmPerRad * (ErrorRad - Body->GetUnrealWorldAngularVelocityInRadians() * Slot.DampingS).Size();
		TorqueNm += Slot.MaxTorqueNm > 0.0 ? FMath::Min(SlotTorqueNm, Slot.MaxTorqueNm) : SlotTorqueNm;
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
