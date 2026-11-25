#include "NaturalMeasures.h"

#include "Chaos/PBDCollisionConstraints.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "PBDRigidsSolver.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "SandboxBodyTuning.h"
#include "SandboxCore.h"
#include "SandboxMotionStats.h"
#include "SandboxPosePrior.h"

namespace
{
	/** The first frames are left out of what jumps: the spawn pose is still going over into the stance. */
	constexpr int32 SettleFrames = 5;
	/** A head that comes down slower than this has been laid on the floor, not struck it. */
	constexpr double HeadStruckMps = 0.5;
	/** A falling body that goes slower over the floor than this has no way ahead. */
	constexpr double FallWayMinCmS = 20.0;
	/** A muscle's target has changed once it is turned this far from what it was at the hit. */
	constexpr double TargetChangeDeg = 1.0;
	/** A muscle's stiffness has changed once it differs by this share. */
	constexpr double StrengthChange = 0.01;
	/** What the body does this long after a hit counts as the answer to it. */
	constexpr double ReactionWindowS = 1.5;
	/** Targets are said to have been moving before a hit by the fastest of them over this long. */
	constexpr double BeforeS = 0.1;
	/** A joint is outside its range from this far on. */
	constexpr double OutsideDeg = 0.5;
	/** A body is still while no part of it has gone further than StillCm in the last StillWindowS. */
	constexpr double StillWindowS = 1.0;
	constexpr double StillCm = 0.5;
	/** The targets stand still while none of them turns more than this in a frame. */
	constexpr double TargetStillDeg = 0.02;
	/** A push's displacement is taken over the last part of it. */
	constexpr double PushSettledS = 0.2;
	/** A pushed part has come back once it is within this share of where the push held it. */
	constexpr double ReturnShare = 0.2;
	/** A moving mean of 0.1 s over the sway before its speed is taken: contact impulses of single steps are not sway. */
	constexpr double SwaySmoothS = 0.1;
	/** The period of the sway is looked for at 15 points a second, from 1 s on, and has to be a clear one. */
	constexpr int32 SwayDecimation = 6;
	constexpr double SwayMinPeriodS = 1.0;
	constexpr double SwayMinCorrelation = 0.5;
	/** The pose is held against the database every third frame; the database has a pose every tenth of a second of a clip, none nearer than this to the one before. */
	constexpr int32 PriorEveryFrames = 3;
	constexpr double PriorStepS = 0.1;
	constexpr double PriorMinDeg = 1.0;
	/** The steady torque of a joint is its mean over the last second. */
	constexpr double RecentS = 1.0;

	TSharedPtr<FJsonValue> Number(double Value) { return MakeShared<FJsonValueNumber>(Value); }
	TSharedPtr<FJsonValue> Null() { return MakeShared<FJsonValueNull>(); }
	TSharedPtr<FJsonValue> Optional(bool bHave, double Value) { return bHave ? Number(Value) : Null(); }
	FString Text(FName Name) { return Name.IsNone() ? FString() : Name.ToString(); }

	const FSandboxBodySample* Find(const TArray<FSandboxBodySample>& Bodies, FName Bone)
	{
		return Bodies.FindByPredicate([Bone](const FSandboxBodySample& Body) { return Body.Bone == Bone; });
	}

	bool IsTrunk(FName Bone)
	{
		const ESandboxMuscleGroup Group = FSandboxBodyTuning::GroupOfBone(Bone);
		return Group == ESandboxMuscleGroup::Spine;
	}

	bool IsHead(FName Bone)
	{
		return FSandboxBodyTuning::GroupOfBone(Bone) == ESandboxMuscleGroup::Neck;
	}

	/** The hand and the forearm: what a person breaks a fall with. */
	bool IsHandOrForearm(FName Bone)
	{
		const FString Name = Bone.ToString();
		return Name.StartsWith(TEXT("hand")) || Name.StartsWith(TEXT("lowerarm"));
	}

	bool IsPelvis(FName Bone)
	{
		return Bone == FName(TEXT("pelvis"));
	}
}

void FNaturalMeasures::Begin(double InFixedDtS, const FVector& InForward)
{
	Dt = InFixedDtS;
	Forward = FVector2D(InForward.X, InForward.Y).GetSafeNormal();
}

void FNaturalMeasures::NotePush(int32 Frame, FName Bone, const FVector& ForceN, int32 Frames, const TArray<FSandboxBodySample>& Bodies)
{
	const FSandboxBodySample* Body = Find(Bodies, Bone);
	if (!Body)
	{
		return;
	}
	FPushed& Push = Pushes.AddDefaulted_GetRef();
	Push.Frame = Frame;
	Push.Frames = Frames;
	Push.Bone = Bone;
	Push.ForceN = ForceN;
	Push.Start = Body->CenterOfMass;
	Push.WholeStart = FSandboxBodyProbe::CenterOfMass(Bodies);
}

void FNaturalMeasures::NoteLimit(const USandboxBodyController* Controller, FName Joint, double ExcessDeg)
{
	// A lying body rests against its joints' ends, as a person does; the share is about a body in motion.
	if (Controller && Controller->GetState() == ESandboxBodyState::Lying)
	{
		return;
	}
	FLimit& Limit = Limits.FindOrAdd(Joint);
	++Limit.Frames;
	Limit.OutsideFrames += ExcessDeg > OutsideDeg ? 1 : 0;
}

void FNaturalMeasures::Sample(const FNaturalFrame& Frame)
{
	if (!Frame.Mesh || !Frame.Bodies)
	{
		return;
	}
	const ESandboxBodyState State = Frame.Controller ? Frame.Controller->GetState() : ESandboxBodyState::Standing;
	FramesInState = bHaveLast && State == LastState ? FramesInState + 1 : 1;
	double TargetStepDeg = -1.0;
	if (Frame.Controller)
	{
		SampleFall(Frame, State);
		if (Frame.View && Frame.View->Muscles.Num() > 0)
		{
			// Reactions compare with the targets of the frame before, which SampleMuscles then replaces.
			SampleReactions(Frame, State);
			SampleJumps(Frame);
			SampleMuscles(Frame, State, TargetStepDeg);
		}
		SampleSway(Frame, State);
		SamplePrior(Frame, State);
		const FSandboxAssistLoad& Load = Frame.Controller->GetAssistLoad();
		FAssistShare& Share = AssistByState.FindOrAdd(State);
		++Share.Frames;
		Share.ForceSum += Load.ForceShare;
		Share.ForceMax = FMath::Max(Share.ForceMax, Load.ForceShare);
		Share.TorqueSum += Load.TorqueShare;
		Share.TorqueMax = FMath::Max(Share.TorqueMax, Load.TorqueShare);
	}

	// The joints' relative turning speed, frame against frame.
	for (const FConstraintInstance* Constraint : Frame.Mesh->Constraints)
	{
		const FSandboxBodySample* Child = Constraint ? Find(*Frame.Bodies, Constraint->GetChildBoneName()) : nullptr;
		const FSandboxBodySample* Parent = Constraint ? Find(*Frame.Bodies, Constraint->GetParentBoneName()) : nullptr;
		if (!Child || !Parent)
		{
			continue;
		}
		const FVector Speed = Child->AngularVelocityRad - Parent->AngularVelocityRad;
		FVector* Last = LastJointSpeeds.Find(Constraint->JointName);
		if (Last && Frame.Frame >= SettleFrames)
		{
			const double Step = (Speed - *Last).Size();
			if (Step > JointSpeedStepMax)
			{
				JointSpeedStepMax = Step;
				JointSpeedStepJoint = Constraint->JointName;
				JointSpeedStepFrame = Frame.Frame;
			}
		}
		LastJointSpeeds.Add(Constraint->JointName, Speed);
	}

	SampleHead(Frame);
	SampleStillness(Frame, State, TargetStepDeg);
	SamplePushes(Frame);
	PressureMomentNsCm = FVector::ZeroVector;
	PressureNs = 0.0;
	LastBodies = *Frame.Bodies;
	LastState = State;
	bHaveLast = true;
}

void FNaturalMeasures::SampleFall(const FNaturalFrame& Frame, ESandboxBodyState State)
{
	const bool bDown = State == ESandboxBodyState::Falling || State == ESandboxBodyState::Lying;
	if (State == ESandboxBodyState::Falling && bHaveLast && LastState != ESandboxBodyState::Falling && LastState != ESandboxBodyState::Lying)
	{
		FFall& Fall = Falls.AddDefaulted_GetRef();
		Fall.StartFrame = Frame.Frame;
	}
	if (Falls.Num() == 0)
	{
		return;
	}
	FFall& Fall = Falls.Last();
	Fall.bOpen &= bDown;
	if (!Fall.bOpen)
	{
		return;
	}
	const TConstArrayView<FVector> Support = Frame.Controller->GetSupportImpulsesNs();
	bool bHeadTouches = false;
	for (const FBodyInstance* Body : Frame.Mesh->Bodies)
	{
		if (!Body || !Body->IsValidBodyInstance() || !Support.IsValidIndex(Body->InstanceBodyIndex) || Support[Body->InstanceBodyIndex].IsZero())
		{
			continue;
		}
		const FName Bone = Frame.Mesh->GetBoneName(Body->InstanceBoneIndex);
		if (IsHead(Bone))
		{
			// A head can brush the floor, leave it and strike it: every time it comes down counts.
			bHeadTouches = true;
			const FSandboxBodySample* Before = Find(LastBodies, Bone);
			if (Before && !Fall.bHeadTouching)
			{
				Fall.HeadDownMps = FMath::Max(Fall.HeadDownMps, -Before->LinearVelocity.Z * 0.01);
			}
		}
		if (Frame.Controller->IsFootBone(Bone) || Fall.Touches.ContainsByPredicate([Bone](const FTouch& Touch) { return Touch.Bone == Bone; }))
		{
			continue;
		}
		FTouch& Touch = Fall.Touches.AddDefaulted_GetRef();
		Touch.Bone = Bone;
		Touch.Frame = Frame.Frame;
		// The body as the step before left it: how it came to the floor, not what the floor made of it.
		if (const FSandboxBodySample* Before = Find(LastBodies, Bone))
		{
			Touch.SpeedMps = Before->LinearVelocity.Size() * 0.01;
			Touch.DownMps = -Before->LinearVelocity.Z * 0.01;
			if (const FSandboxBodySample* Chest = Find(LastBodies, Frame.Controller->GetTuning().Rig.Chest))
			{
				Touch.AheadCm = AheadCm(Before->CenterOfMass, Chest->CenterOfMass, FSandboxBodyProbe::CenterOfMassVelocity(LastBodies));
			}
		}
	}
	Fall.bHeadTouching = bHeadTouches;
}

void FNaturalMeasures::SampleReactions(const FNaturalFrame& Frame, ESandboxBodyState State)
{
	const TArray<FSandboxMuscleView>& Muscles = Frame.View->Muscles;
	const TArray<FSandboxBodyEvent>& Events = Frame.Controller->GetEvents();
	const int32 WindowFrames = FMath::RoundToInt32(ReactionWindowS / Dt);
	for (; SeenEvents < Events.Num(); ++SeenEvents)
	{
		const FSandboxBodyEvent& Event = Events[SeenEvents];
		if (Event.Type != ESandboxBodyEventType::Hit || LastTargets.Num() != Muscles.Num())
		{
			continue;
		}
		FReaction& Reaction = Reactions.AddDefaulted_GetRef();
		Reaction.Frame = Frame.Frame;
		Reaction.Bone = Event.Bone;
		Reaction.ImpulseNs = Event.ImpulseNs.Size();
		Reaction.Targets = LastTargets;
		Reaction.Stiffness = LastStiffness;
		for (const double Dps : RecentTargetDps)
		{
			Reaction.TargetSpeedBeforeDps = FMath::Max(Reaction.TargetSpeedBeforeDps, Dps);
		}
		// Steps of the controller's log before the hit. One that began in the hit's own frame is an answer to it.
		for (const FSandboxStepRecord& Step : Frame.Controller->GetStepLog())
		{
			Reaction.StepsBefore += Step.StartS < Event.TimeS - 0.5 * Dt ? 1 : 0;
		}
	}

	for (FReaction& Reaction : Reactions)
	{
		if (Frame.Frame - Reaction.Frame > WindowFrames)
		{
			continue;
		}
		if (Reaction.TargetFrame < 0 && Reaction.Targets.Num() == Muscles.Num())
		{
			double MostDeg = TargetChangeDeg;
			for (int32 Index = 0; Index < Muscles.Num(); ++Index)
			{
				const double TurnDeg = FMath::RadiansToDegrees(Reaction.Targets[Index].AngularDistance(Muscles[Index].TargetRelative));
				if (TurnDeg > MostDeg)
				{
					MostDeg = TurnDeg;
					Reaction.TargetFrame = Frame.Frame;
					Reaction.TargetJoint = Muscles[Index].ChildBone;
				}
			}
		}
		if (Reaction.StrengthFrame < 0 && Reaction.Stiffness.Num() == Muscles.Num())
		{
			for (int32 Index = 0; Index < Muscles.Num(); ++Index)
			{
				const double Larger = FMath::Max(Reaction.Stiffness[Index], Muscles[Index].StiffnessNmPerRad);
				if (Larger > 0.0 && FMath::Abs(Reaction.Stiffness[Index] - Muscles[Index].StiffnessNmPerRad) > StrengthChange * Larger)
				{
					Reaction.StrengthFrame = Frame.Frame;
					break;
				}
			}
		}
		if (Reaction.LandFrame < 0)
		{
			const TArray<FSandboxStepRecord>& Steps = Frame.Controller->GetStepLog();
			for (int32 Index = Reaction.StepsBefore; Index < Steps.Num(); ++Index)
			{
				if (Steps[Index].bRegroup)
				{
					continue;
				}
				if (Reaction.LiftFrame < 0)
				{
					Reaction.LiftFrame = Frame.Frame;
				}
				if (Steps[Index].LandS >= 0.0)
				{
					Reaction.LandFrame = Frame.Frame;
				}
				break;
			}
		}
		if (Reaction.FallFrame < 0 && State == ESandboxBodyState::Falling && LastState != ESandboxBodyState::Falling)
		{
			Reaction.FallFrame = Frame.Frame;
		}
	}
}

void FNaturalMeasures::SampleJumps(const FNaturalFrame& Frame)
{
	// The command that is judged is LastMuscles', of the frame before: what comes after it has to be seen too.
	const TArray<FSandboxMuscleView>& Muscles = Frame.View->Muscles;
	const int32 Judged = Frame.Frame - 1;
	if (Judged >= SettleFrames && LastMuscles.Num() == Muscles.Num() && OlderMuscles[0].Num() == Muscles.Num() && OlderMuscles[1].Num() == Muscles.Num())
	{
		for (int32 Index = 0; Index < Muscles.Num(); ++Index)
		{
			const FSandboxBodySample* Parent = Find(OlderBodies, Muscles[Index].ParentBone);
			const FSandboxBodySample* Child = Find(OlderBodies, Muscles[Index].ChildBone);
			if (!Parent || !Child)
			{
				continue;
			}
			const FSandboxMuscleView* const Views[] = { &OlderMuscles[0][Index], &OlderMuscles[1][Index], &LastMuscles[Index], &Muscles[Index] };
			FSandboxDriveCommand Commands[4];
			for (int32 Each = 0; Each < 4; ++Each)
			{
				Commands[Each] = FSandboxDriveCommand{ Views[Each]->TargetRelative, Views[Each]->StiffnessNmPerRad, Views[Each]->DampingNmsPerRad, Views[Each]->MaxTorqueNm };
			}
			double TargetDeg = 0.0;
			const double JumpNm = FSandboxMotionStats::CommandJumpNm(Commands, Parent->WorldTransform.GetRotation(), Child->WorldTransform.GetRotation(),
				Parent->AngularVelocityRad, Child->AngularVelocityRad, TargetDeg);
			if (JumpNm > CommandJumpMaxNm)
			{
				CommandJumpMaxNm = JumpNm;
				CommandJumpJoint = Muscles[Index].ChildBone;
				CommandJumpFrame = Judged;
			}
			if (TargetDeg > TargetJumpMaxDeg)
			{
				TargetJumpMaxDeg = TargetDeg;
				TargetJumpJoint = Muscles[Index].ChildBone;
				TargetJumpFrame = Judged;
			}
		}
	}
	OlderMuscles[0] = MoveTemp(OlderMuscles[1]);
	OlderMuscles[1] = LastMuscles;
	// The bodies as this frame's step found them: it is this frame's command that is judged next.
	OlderBodies = LastBodies;
}

void FNaturalMeasures::SampleMuscles(const FNaturalFrame& Frame, ESandboxBodyState State, double& OutTargetStepDeg)
{
	const TArray<FSandboxMuscleView>& Muscles = Frame.View->Muscles;
	const bool bHaveTargets = LastTargets.Num() == Muscles.Num();
	if (MuscleJoints.Num() != Muscles.Num())
	{
		MuscleJoints.Reset();
		Efforts.Reset();
		Efforts.SetNum(Muscles.Num());
		for (const FSandboxMuscleView& Muscle : Muscles)
		{
			MuscleJoints.Add(Muscle.ChildBone);
		}
	}

	const bool bUpright = USandboxBodyController::IsUprightState(State);
	UprightFrames += bUpright ? 1 : 0;
	bool bAnyAtCap = false;
	double PowerW = 0.0;
	double MostStepDeg = 0.0;
	TArray<FQuat> Targets;
	TArray<double> Stiffness;
	for (int32 Index = 0; Index < Muscles.Num(); ++Index)
	{
		const FSandboxMuscleView& Muscle = Muscles[Index];
		Targets.Add(Muscle.TargetRelative);
		Stiffness.Add(Muscle.StiffnessNmPerRad);

		// Jumps of the target and of the strength.
		FVector TargetSpeed = FVector::ZeroVector;
		if (bHaveTargets)
		{
			const double StepDeg = FMath::RadiansToDegrees(LastTargets[Index].AngularDistance(Muscle.TargetRelative));
			MostStepDeg = FMath::Max(MostStepDeg, StepDeg);
			TargetSpeed = FSandboxMotionStats::TurnSpeed(LastTargets[Index], Muscle.TargetRelative, Dt);
			const double Larger = FMath::Max(LastStiffness[Index], Muscle.StiffnessNmPerRad);
			const double StrengthStep = Larger > 0.0 ? FMath::Abs(LastStiffness[Index] - Muscle.StiffnessNmPerRad) / Larger : 0.0;
			if (Frame.Frame >= SettleFrames && StepDeg > TargetStepMaxDeg)
			{
				TargetStepMaxDeg = StepDeg;
				TargetStepJoint = Muscle.ChildBone;
				TargetStepFrame = Frame.Frame;
			}
			if (Frame.Frame >= SettleFrames && StrengthStep > StrengthStepMax)
			{
				StrengthStepMax = StrengthStep;
				StrengthStepJoint = Muscle.ChildBone;
				StrengthStepFrame = Frame.Frame;
			}

			// The same as a torque: the old command and the new one, on the body as the step found it. A target that
			// jumps while its muscle has no strength asks for nothing.
			const FSandboxBodySample* ParentBefore = Find(LastBodies, Muscle.ParentBone);
			const FSandboxBodySample* ChildBefore = Find(LastBodies, Muscle.ChildBone);
			if (Frame.Frame >= SettleFrames && ParentBefore && ChildBefore && LastMuscles.IsValidIndex(Index))
			{
				const auto Command = [ParentBefore, ChildBefore](const FSandboxMuscleView& Asked)
				{
					bool bUnused = false;
					return FSandboxMotionStats::DriveTorqueNm(ParentBefore->WorldTransform.GetRotation(), ChildBefore->WorldTransform.GetRotation(), Asked.TargetRelative,
						ParentBefore->AngularVelocityRad, ChildBefore->AngularVelocityRad, FVector::ZeroVector, Asked.StiffnessNmPerRad, Asked.DampingNmsPerRad, Asked.MaxTorqueNm, bUnused);
				};
				const double CommandStepNm = (Command(Muscle) - Command(LastMuscles[Index])).Size();
				if (CommandStepNm > CommandStepMaxNm)
				{
					CommandStepMaxNm = CommandStepNm;
					CommandStepJoint = Muscle.ChildBone;
					CommandStepFrame = Frame.Frame;
				}
			}
		}

		// The muscle's torque, from the spring law with the state this step ended in.
		const FSandboxBodySample* Parent = Find(*Frame.Bodies, Muscle.ParentBone);
		const FSandboxBodySample* Child = Find(*Frame.Bodies, Muscle.ChildBone);
		if (!Parent || !Child)
		{
			continue;
		}
		bool bAtCap = false;
		const FVector TorqueNm = FSandboxMotionStats::DriveTorqueNm(Parent->WorldTransform.GetRotation(), Child->WorldTransform.GetRotation(), Muscle.TargetRelative,
			Parent->AngularVelocityRad, Child->AngularVelocityRad, Parent->WorldTransform.GetRotation().RotateVector(TargetSpeed) * Muscle.PoseVelocityShare,
			Muscle.StiffnessNmPerRad, Muscle.DampingNmsPerRad, Muscle.MaxTorqueNm, bAtCap);
		const double MusclePowerW = TorqueNm | (Child->AngularVelocityRad - Parent->AngularVelocityRad);
		FEffort& Effort = Efforts[Index];
		Effort.TorqueNm.Add(static_cast<float>(TorqueNm.Size()));
		Effort.Recent.Add(static_cast<float>(TorqueNm.Size()));
		if (Effort.Recent.Num() > FMath::RoundToInt32(RecentS / Dt))
		{
			Effort.Recent.RemoveAt(0);
		}
		Effort.CapNm = FMath::Max(Effort.CapNm, Muscle.MaxTorqueNm);
		Effort.CapFrames += bAtCap ? 1 : 0;
		Effort.UprightFrames += bUpright ? 1 : 0;
		Effort.UprightCapFrames += bUpright && bAtCap ? 1 : 0;
		Effort.PowerMaxW = FMath::Max(Effort.PowerMaxW, MusclePowerW);
		Effort.WorkJ += FMath::Max(MusclePowerW, 0.0) * Dt;
		PowerW += FMath::Max(MusclePowerW, 0.0);
		bAnyAtCap |= bAtCap;
	}
	AnyCapUprightFrames += bUpright && bAnyAtCap ? 1 : 0;
	PositiveWorkJ += PowerW * Dt;
	if (PowerW > PowerPeakW)
	{
		PowerPeakW = PowerW;
		PowerPeakFrame = Frame.Frame;
	}
	if (bHaveTargets)
	{
		OutTargetStepDeg = MostStepDeg;
		RecentTargetDps.Add(MostStepDeg / Dt);
		if (RecentTargetDps.Num() > FMath::RoundToInt32(BeforeS / Dt))
		{
			RecentTargetDps.RemoveAt(0);
		}
	}
	LastTargets = MoveTemp(Targets);
	LastStiffness = MoveTemp(Stiffness);
	LastMuscles = Muscles;
}

void FNaturalMeasures::SampleHead(const FNaturalFrame& Frame)
{
	const FName HeadBone = Frame.Controller ? Frame.Controller->GetTuning().Rig.Head : FName(TEXT("neck_01"));
	const FSandboxBodySample* Head = Find(*Frame.Bodies, HeadBone);
	if (!Head)
	{
		return;
	}
	// The velocity of the head's centre of mass, a mean of three frames; its differences are the acceleration and the jerk.
	HeadVelocities.Add(Head->LinearVelocity * 0.01);
	if (HeadVelocities.Num() < 4)
	{
		return;
	}
	if (HeadVelocities.Num() > 4)
	{
		HeadVelocities.RemoveAt(0);
	}
	const FVector Now = (HeadVelocities[1] + HeadVelocities[2] + HeadVelocities[3]) / 3.0;
	const FVector Before = (HeadVelocities[0] + HeadVelocities[1] + HeadVelocities[2]) / 3.0;
	const FVector Acceleration = (Now - Before) / Dt;
	if (Frame.Frame >= SettleFrames)
	{
		if (Acceleration.Size() > HeadAccelMax)
		{
			HeadAccelMax = Acceleration.Size();
			HeadAccelFrame = Frame.Frame;
		}
		if (bHaveHeadAcceleration && Frame.bSettled)
		{
			HeadJerkSquares += ((Acceleration - LastHeadAcceleration) / Dt).SizeSquared();
			++HeadJerkFrames;
		}
	}
	LastHeadAcceleration = Acceleration;
	bHaveHeadAcceleration = true;
}

void FNaturalMeasures::SampleStillness(const FNaturalFrame& Frame, ESandboxBodyState State, double TargetStepDeg)
{
	// Still: no part of the body has gone further than StillCm over the last StillWindowS. Judged by how far, not by
	// how fast: what breathes is slow, and at the end of each breath not moving at all.
	const int32 WindowFrames = FMath::RoundToInt32(StillWindowS / Dt);
	if (Places.Num() > WindowFrames)
	{
		Places.RemoveAt(0);
	}
	TArray<FVector>& Now = Places.AddDefaulted_GetRef();
	for (const FSandboxBodySample& Body : *Frame.Bodies)
	{
		Now.Add(Body.CenterOfMass);
	}
	const bool bWhole = Places.Num() == WindowFrames + 1;
	double TravelCm = 0.0;
	for (int32 Body = 0; bWhole && Body < Now.Num(); ++Body)
	{
		FBox Box(ForceInit);
		for (const TArray<FVector>& Each : Places)
		{
			if (Each.IsValidIndex(Body))
			{
				Box += Each[Body];
			}
		}
		TravelCm = FMath::Max(TravelCm, Box.GetSize().Size());
	}
	const bool bStill = bWhole && TravelCm <= StillCm;

	// The window that shows it is still has been still too.
	StillFrames = bStill ? (StillFrames > 0 ? StillFrames + 1 : WindowFrames) : 0;
	if (StillFrames > LongestStillFrames)
	{
		LongestStillFrames = StillFrames;
		LongestStillStart = Frame.Frame + 1 - StillFrames;
		LongestStillState = State;
	}
	FStillShare& Share = StillByState.FindOrAdd(State);
	++Share.Frames;
	Share.Still += bStill ? 1 : 0;
	if (bWhole)
	{
		Share.TravelCm.Add(static_cast<float>(TravelCm));
	}
	// The window may have begun in the state before: what counts is this one's part of it.
	Share.Run = bStill ? FMath::Min(Share.Run > 0 ? Share.Run + 1 : WindowFrames, FramesInState) : 0;
	Share.Longest = FMath::Max(Share.Longest, Share.Run);

	// The targets: what the body asks of itself. Below 0: not known in this frame.
	if (TargetStepDeg >= 0.0)
	{
		Share.TargetRun = TargetStepDeg <= TargetStillDeg && LastState == State ? Share.TargetRun + 1 : 0;
		Share.TargetLongest = FMath::Max(Share.TargetLongest, Share.TargetRun);
	}
	for (TPair<ESandboxBodyState, FStillShare>& Other : StillByState)
	{
		if (Other.Key != State)
		{
			Other.Value.Run = 0;
			Other.Value.TargetRun = 0;
		}
	}
}

void FNaturalMeasures::SamplePushes(const FNaturalFrame& Frame)
{
	for (FPushed& Push : Pushes)
	{
		const FSandboxBodySample* Body = Find(*Frame.Bodies, Push.Bone);
		if (!Body || Frame.Frame < Push.Frame)
		{
			continue;
		}
		const FVector Way = Body->CenterOfMass - Push.Start;
		const FVector Along = Push.ForceN.GetSafeNormal();
		const int32 End = Push.Frame + Push.Frames;
		if (Frame.Frame < End)
		{
			Push.MaxCm = FMath::Max(Push.MaxCm, Way.Size());
			Push.AlongCm = FMath::Max(Push.AlongCm, Way | Along);
			Push.AgainstCm = FMath::Max(Push.AgainstCm, -(Way | Along));
			if (Frame.Frame >= End - FMath::RoundToInt32(PushSettledS / Dt))
			{
				Push.SettledSumCm += Way | Along;
				Push.WholeSumCm += (FSandboxBodyProbe::CenterOfMass(*Frame.Bodies) - Push.WholeStart) | Along;
				++Push.SettledFrames;
			}
		}
		else if (Push.ReturnFrame < 0 && Push.SettledFrames > 0 && FMath::Abs(Way | Along) <= ReturnShare * FMath::Abs(Push.SettledSumCm / Push.SettledFrames))
		{
			Push.ReturnFrame = Frame.Frame;
		}
	}
}

void FNaturalMeasures::SampleSway(const FNaturalFrame& Frame, ESandboxBodyState State)
{
	if (!Frame.bSettled || State != ESandboxBodyState::Standing)
	{
		EndSwayStretch();
		return;
	}
	const FVector Center = PressureNs > 0.0 ? PressureMomentNsCm / PressureNs : FVector::ZeroVector;
	const double UpwardN = PressureNs / Dt;
	if (UpwardN <= 0.0)
	{
		return;
	}
	const double WeightN = FSandboxBodyProbe::TotalMassKg(*Frame.Bodies) * FMath::Abs(Frame.Mesh->GetWorld()->GetGravityZ()) * 0.01;
	FloorShareSum += WeightN > 0.0 ? UpwardN / WeightN : 0.0;
	const auto InBody = [this](const FVector& Point) { return FVector2D(Point.X * Forward.X + Point.Y * Forward.Y, Point.X * Forward.Y - Point.Y * Forward.X); };
	SwayCom.Add(InBody(FSandboxBodyProbe::CenterOfMass(*Frame.Bodies)));
	SwayCop.Add(InBody(Center));
}

void FNaturalMeasures::EndSwayStretch()
{
	if (SwayCom.Num() > BestCom.Num())
	{
		BestCom = MoveTemp(SwayCom);
		BestCop = MoveTemp(SwayCop);
		BestFloorShareSum = FloorShareSum;
	}
	SwayCom.Reset();
	SwayCop.Reset();
	FloorShareSum = 0.0;
}

namespace
{
	/** The databases this process has read, by file and by the joints they were read for. */
	TMap<FString, TSharedPtr<const FSandboxPosePrior>>& PriorCache()
	{
		static TMap<FString, TSharedPtr<const FSandboxPosePrior>> Databases;
		return Databases;
	}

	FString PriorKey(const FString& File, const TArray<TPair<FName, FName>>& Joints)
	{
		FString Key = File;
		for (const TPair<FName, FName>& Joint : Joints)
		{
			Key += TEXT("|") + Joint.Key.ToString();
		}
		return Key;
	}
}

bool FNaturalMeasures::BuildPrior(USandboxBodyController& Controller, const USkeletalMeshComponent* Mesh, const TArray<FString>& ClipPaths, const FString& File, FString& OutError)
{
	TArray<TPair<FName, FName>> Joints;
	Controller.GetMuscleJoints(Joints);
	FSandboxPosePrior Database;
	if (!Database.Init(Mesh, Controller.GetTuning().Rig.Pelvis, Joints, OutError))
	{
		return false;
	}
	for (const FString& Path : ClipPaths)
	{
		const FSandboxClipPose* Clip = Controller.ReadClip(Path, OutError);
		if (!Clip)
		{
			return false;
		}
		Database.AddClip(Path, *Clip, PriorStepS, PriorMinDeg);
	}
	if (!Database.Check(/*Every*/ 37, OutError) || !Database.Save(File, OutError))
	{
		return false;
	}
	PriorCache().Remove(PriorKey(File, Joints));
	UE_LOG(LogSandbox, Display, TEXT("Pose database: %d poses of %d clips, stamp %08x, written to %s"), Database.Num(), Database.NumClips(), Database.GetStamp(), *File);
	return true;
}

bool FNaturalMeasures::UsePrior(const USandboxBodyController& Controller, const USkeletalMeshComponent* Mesh, const FString& File, FString& OutError)
{
	// Read once a process: the runs of a queue ask for the same one.
	TArray<TPair<FName, FName>> Joints;
	Controller.GetMuscleJoints(Joints);
	const FString Key = PriorKey(File, Joints);
	if (const TSharedPtr<const FSandboxPosePrior>* Known = PriorCache().Find(Key))
	{
		Prior = *Known;
		return true;
	}
	const TSharedRef<FSandboxPosePrior> Database = MakeShared<FSandboxPosePrior>();
	if (!Database->Init(Mesh, Controller.GetTuning().Rig.Pelvis, Joints, OutError) || !Database->Load(File, OutError))
	{
		return false;
	}
	PriorCache().Add(Key, Database);
	Prior = Database;
	return true;
}

void FNaturalMeasures::SamplePrior(const FNaturalFrame& Frame, ESandboxBodyState State)
{
	FSandboxPriorPose Pose;
	if (!Prior || !Frame.bSettled || Frame.Frame % PriorEveryFrames != 0 || !Prior->PoseOf(*Frame.Bodies, Pose))
	{
		return;
	}
	const FSandboxPriorMatch Match = Prior->FindNearest(Pose);
	PriorByState.FindOrAdd(State).Add(Match.DistanceDeg);
	if (Match.DistanceDeg > PriorWorstDeg)
	{
		PriorWorstDeg = Match.DistanceDeg;
		PriorWorstFrame = Frame.Frame;
		PriorWorstState = State;
		PriorWorstClip = Match.Clip;
		PriorWorstClipS = Match.TimeS;
	}
}

void FNaturalMeasures::AddPressure(const USkeletalMeshComponent* Mesh, double SubstepS)
{
	const UWorld* World = Mesh ? Mesh->GetWorld() : nullptr;
	FPhysScene* Scene = World ? World->GetPhysicsScene() : nullptr;
	Chaos::FPhysicsSolver* Solver = Scene ? Scene->GetSolver() : nullptr;
	if (!Solver || !Solver->GetEvolution() || SubstepS <= 0.0)
	{
		return;
	}
	TSet<const Chaos::FGeometryParticleHandle*, DefaultKeyFuncs<const Chaos::FGeometryParticleHandle*>, TInlineSetAllocator<32>> Own;
	for (const FBodyInstance* Body : Mesh->Bodies)
	{
		if (Body && Body->GetPhysicsActor())
		{
			Own.Add(Body->GetPhysicsActor()->GetHandle_LowLevel());
		}
	}
	for (const Chaos::FPBDCollisionConstraintHandle* Handle : Solver->GetEvolution()->GetCollisionConstraints().GetConstraintHandles())
	{
		const Chaos::FPBDCollisionConstraint& Contact = Handle->GetContact();
		const Chaos::FGeometryParticleHandle* First = Contact.GetParticle0();
		const Chaos::FGeometryParticleHandle* Second = Contact.GetParticle1();
		const bool bFirst = Own.Contains(First);
		if (!Contact.IsEnabled() || bFirst == Own.Contains(Second))
		{
			continue;
		}
		const Chaos::FConstGenericParticleHandle Other(bFirst ? Second : First);
		if (Other->IsDynamic())
		{
			continue;
		}
		for (int32 Index = 0; Index < Contact.NumManifoldPoints(); ++Index)
		{
			if (!Contact.IsManifoldPointActive(Index))
			{
				continue;
			}
			const Chaos::FManifoldPointResult& Result = Contact.GetManifoldPointResult(Index);
			// kg*cm/s to N*s. Upward on the body, whichever of the two the solver counts the impulse for.
			const FVector ImpulseNs = (FVector(Result.NetImpulse) + FVector(Result.NetPushOut) / SubstepS) * 0.01;
			const double Upward = FMath::Abs(ImpulseNs.Z);
			const FVector Point = Contact.GetShapeWorldTransform0().TransformPositionNoScale(FVector(Contact.GetManifoldPoint(Index).ContactPoint.ShapeContactPoints[0]));
			PressureMomentNsCm += Point * Upward;
			PressureNs += Upward;
		}
	}
}

TOptional<double> FNaturalMeasures::AheadCm(const FVector& Part, const FVector& Chest, const FVector& VelocityCmS)
{
	const FVector2D Way(VelocityCmS.X, VelocityCmS.Y);
	if (Way.Size() < FallWayMinCmS)
	{
		return TOptional<double>();
	}
	return FVector2D(Part.X - Chest.X, Part.Y - Chest.Y) | Way.GetSafeNormal();
}

void FNaturalMeasures::WriteOneFall(const FFall& Fall, FJsonObject& Json) const
{
	TArray<TSharedPtr<FJsonValue>> TouchesJson;
	const FTouch* First = nullptr;
	const FTouch* Head = nullptr;
	const FTouch* Pelvis = nullptr;
	const FTouch* Trunk = nullptr;
	const FTouch* Hand = nullptr;
	const FTouch* HandAhead = nullptr;
	for (const FTouch& Touch : Fall.Touches)
	{
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("bone"), Text(Touch.Bone));
		Entry->SetNumberField(TEXT("t"), Touch.Frame * Dt);
		Entry->SetNumberField(TEXT("speed_mps"), Touch.SpeedMps);
		Entry->SetNumberField(TEXT("down_mps"), Touch.DownMps);
		Entry->SetField(TEXT("ahead_cm"), Optional(Touch.AheadCm.IsSet(), Touch.AheadCm.Get(0.0)));
		TouchesJson.Add(MakeShared<FJsonValueObject>(Entry));
		First = First ? First : &Touch;
		Head = Head || !IsHead(Touch.Bone) ? Head : &Touch;
		Pelvis = Pelvis || !IsPelvis(Touch.Bone) ? Pelvis : &Touch;
		Trunk = Trunk || !IsTrunk(Touch.Bone) ? Trunk : &Touch;
		Hand = Hand || !IsHandOrForearm(Touch.Bone) ? Hand : &Touch;
		HandAhead = HandAhead || !IsHandOrForearm(Touch.Bone) || Touch.AheadCm.Get(0.0) <= 0.0 ? HandAhead : &Touch;
	}
	Json.SetArrayField(TEXT("touches"), TouchesJson);
	Json.SetStringField(TEXT("first_touch_bone"), First ? Text(First->Bone) : FString());
	Json.SetField(TEXT("time_to_first_touch_s"), Optional(First != nullptr, First ? (First->Frame - Fall.StartFrame) * Dt : 0.0));
	Json.SetField(TEXT("time_to_trunk_s"), Optional(Trunk != nullptr, Trunk ? (Trunk->Frame - Fall.StartFrame) * Dt : 0.0));
	Json.SetField(TEXT("time_to_hand_s"), Optional(Hand != nullptr, Hand ? (Hand->Frame - Fall.StartFrame) * Dt : 0.0));
	Json.SetBoolField(TEXT("head_touched"), Head != nullptr);
	Json.SetBoolField(TEXT("head_struck"), Head && Fall.HeadDownMps >= HeadStruckMps);
	Json.SetField(TEXT("head_impact_speed_mps"), Optional(Head != nullptr, Fall.HeadDownMps));
	Json.SetField(TEXT("pelvis_impact_speed_mps"), Optional(Pelvis != nullptr, Pelvis ? Pelvis->DownMps : 0.0));
	Json.SetField(TEXT("hand_ahead_cm"), Optional(Hand && Hand->AheadCm.IsSet(), Hand ? Hand->AheadCm.Get(0.0) : 0.0));
	Json.SetField(TEXT("hand_impact_speed_mps"), Optional(Hand != nullptr, Hand ? Hand->DownMps : 0.0));
	// A hand or a forearm in an earlier step than any body of the trunk; unknown if neither touched.
	if (Hand || Trunk)
	{
		Json.SetBoolField(TEXT("hands_first"), Hand && (!Trunk || Hand->Frame < Trunk->Frame));
		// The owner's rule for C14 (25 November 2026): a hand that lands beside the hips has not caught the fall.
		Json.SetBoolField(TEXT("hands_ahead_first"), HandAhead && (!Trunk || HandAhead->Frame < Trunk->Frame));
	}
	else
	{
		Json.SetField(TEXT("hands_first"), Null());
		Json.SetField(TEXT("hands_ahead_first"), Null());
	}
}

void FNaturalMeasures::WriteFall(FJsonObject& Fall) const
{
	// The first fall of the run beside what the runner says of it; every fall in a list.
	if (Falls.Num() > 0)
	{
		WriteOneFall(Falls[0], Fall);
	}
	TArray<TSharedPtr<FJsonValue>> List;
	for (const FFall& Each : Falls)
	{
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("started_t"), Each.StartFrame * Dt);
		WriteOneFall(Each, *Entry);
		List.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Fall.SetArrayField(TEXT("falls"), List);
}

void FNaturalMeasures::WriteControl(FJsonObject& Control)
{
	// What the world pulls on the body with, state by state, over the whole run.
	const TSharedRef<FJsonObject> AssistJson = MakeShared<FJsonObject>();
	for (const TPair<ESandboxBodyState, FAssistShare>& Share : AssistByState)
	{
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("frames"), Share.Value.Frames);
		Entry->SetNumberField(TEXT("force_share_mean"), Share.Value.ForceSum / FMath::Max(Share.Value.Frames, 1));
		Entry->SetNumberField(TEXT("force_share_max"), Share.Value.ForceMax);
		Entry->SetNumberField(TEXT("torque_share_mean"), Share.Value.TorqueSum / FMath::Max(Share.Value.Frames, 1));
		Entry->SetNumberField(TEXT("torque_share_max"), Share.Value.TorqueMax);
		AssistJson->SetObjectField(FSandboxBodyTuning::StateName(Share.Key), Entry);
	}
	Control.SetObjectField(TEXT("assist_by_state"), AssistJson);

	TArray<TSharedPtr<FJsonValue>> ReactionsJson;
	for (const FReaction& Reaction : Reactions)
	{
		const auto After = [&Reaction, this](int32 Frame) { return Optional(Frame >= 0, (Frame - Reaction.Frame) * Dt); };
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("hit_t"), Reaction.Frame * Dt);
		Entry->SetStringField(TEXT("bone"), Text(Reaction.Bone));
		Entry->SetNumberField(TEXT("impulse_ns"), Reaction.ImpulseNs);
		Entry->SetNumberField(TEXT("target_speed_before_dps"), Reaction.TargetSpeedBeforeDps);
		Entry->SetField(TEXT("first_target_change_s"), After(Reaction.TargetFrame));
		Entry->SetStringField(TEXT("first_target_joint"), Text(Reaction.TargetJoint));
		Entry->SetField(TEXT("first_strength_change_s"), After(Reaction.StrengthFrame));
		Entry->SetField(TEXT("step_lift_s"), After(Reaction.LiftFrame));
		Entry->SetField(TEXT("step_land_s"), After(Reaction.LandFrame));
		Entry->SetField(TEXT("fall_start_s"), After(Reaction.FallFrame));
		ReactionsJson.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Control.SetArrayField(TEXT("reactions"), ReactionsJson);

	const TSharedRef<FJsonObject> EffortJson = MakeShared<FJsonObject>();
	const TSharedRef<FJsonObject> JointsJson = MakeShared<FJsonObject>();
	double WorstShare = 0.0;
	FName WorstJoint;
	for (int32 Index = 0; Index < Efforts.Num(); ++Index)
	{
		const FEffort& Effort = Efforts[Index];
		TArray<double> Torques;
		Torques.Append(Effort.TorqueNm);
		TArray<double> Recent;
		Recent.Append(Effort.Recent);
		const double UprightShare = Effort.UprightFrames > 0 ? static_cast<double>(Effort.UprightCapFrames) / Effort.UprightFrames : 0.0;
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("torque_max_nm"), FSandboxMotionStats::Percentile(Torques, 1.0));
		Entry->SetNumberField(TEXT("torque_p95_nm"), FSandboxMotionStats::Percentile(Torques, 0.95));
		Entry->SetNumberField(TEXT("torque_final_nm"), FSandboxMotionStats::Mean(Recent));
		Entry->SetNumberField(TEXT("cap_nm"), Effort.CapNm);
		Entry->SetNumberField(TEXT("at_cap_frames"), Effort.CapFrames);
		Entry->SetNumberField(TEXT("at_cap_upright_share"), UprightShare);
		Entry->SetNumberField(TEXT("power_max_w"), Effort.PowerMaxW);
		Entry->SetNumberField(TEXT("work_j"), Effort.WorkJ);
		JointsJson->SetObjectField(Text(MuscleJoints[Index]), Entry);
		if (UprightShare > WorstShare)
		{
			WorstShare = UprightShare;
			WorstJoint = MuscleJoints[Index];
		}
	}
	EffortJson->SetObjectField(TEXT("joints"), JointsJson);
	// Of the frames the body was upright in: those in which some muscle was at its cap, and the share of the joint that was there longest.
	EffortJson->SetNumberField(TEXT("at_cap_share"), UprightFrames > 0 ? static_cast<double>(AnyCapUprightFrames) / UprightFrames : 0.0);
	EffortJson->SetNumberField(TEXT("at_cap_joint_share_max"), WorstShare);
	EffortJson->SetStringField(TEXT("at_cap_joint"), Text(WorstJoint));
	EffortJson->SetNumberField(TEXT("positive_work_j"), PositiveWorkJ);
	EffortJson->SetNumberField(TEXT("power_peak_w"), PowerPeakW);
	EffortJson->SetNumberField(TEXT("power_peak_t"), PowerPeakFrame * Dt);
	Control.SetObjectField(TEXT("effort"), EffortJson);

	TArray<TSharedPtr<FJsonValue>> PushesJson;
	for (const FPushed& Push : Pushes)
	{
		const double SettledCm = Push.SettledFrames > 0 ? Push.SettledSumCm / Push.SettledFrames : 0.0;
		const double WholeCm = Push.SettledFrames > 0 ? Push.WholeSumCm / Push.SettledFrames : 0.0;
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("t"), Push.Frame * Dt);
		Entry->SetNumberField(TEXT("duration_s"), Push.Frames * Dt);
		Entry->SetStringField(TEXT("bone"), Text(Push.Bone));
		Entry->SetNumberField(TEXT("force_n"), Push.ForceN.Size());
		Entry->SetNumberField(TEXT("displacement_cm"), Push.MaxCm);
		// How far the push takes the part before the body answers it, and how far the body goes against the push.
		Entry->SetNumberField(TEXT("peak_along_cm"), Push.AlongCm);
		Entry->SetNumberField(TEXT("against_cm"), Push.AgainstCm);
		Entry->SetField(TEXT("peak_stiffness_n_per_cm"), Optional(Push.AlongCm > 0.01, Push.ForceN.Size() / FMath::Max(Push.AlongCm, 0.01)));
		Entry->SetNumberField(TEXT("settled_displacement_cm"), SettledCm);
		Entry->SetNumberField(TEXT("whole_body_cm"), WholeCm);
		Entry->SetField(TEXT("return_s"), Optional(Push.ReturnFrame >= 0, (Push.ReturnFrame - Push.Frame - Push.Frames) * Dt));
		// How stiff the body is to this push, and how stiff the part is against the body as a whole. Unknown where nothing moved.
		Entry->SetField(TEXT("stiffness_n_per_cm"), Optional(SettledCm > 0.01, Push.ForceN.Size() / FMath::Max(SettledCm, 0.01)));
		Entry->SetField(TEXT("local_stiffness_n_per_cm"), Optional(SettledCm - WholeCm > 0.01, Push.ForceN.Size() / FMath::Max(SettledCm - WholeCm, 0.01)));
		PushesJson.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Control.SetArrayField(TEXT("pushes"), PushesJson);
}

void FNaturalMeasures::WritePose(FJsonObject& Pose) const
{
	Pose.SetNumberField(TEXT("target_step_max_deg"), TargetStepMaxDeg);
	Pose.SetStringField(TEXT("target_step_joint"), Text(TargetStepJoint));
	Pose.SetField(TEXT("target_step_t"), Optional(TargetStepFrame >= 0, TargetStepFrame * Dt));
	Pose.SetNumberField(TEXT("command_step_max_nm"), CommandStepMaxNm);
	Pose.SetStringField(TEXT("command_step_joint"), Text(CommandStepJoint));
	Pose.SetField(TEXT("command_step_t"), Optional(CommandStepFrame >= 0, CommandStepFrame * Dt));
	Pose.SetNumberField(TEXT("command_jump_max_nm"), CommandJumpMaxNm);
	Pose.SetStringField(TEXT("command_jump_joint"), Text(CommandJumpJoint));
	Pose.SetField(TEXT("command_jump_t"), Optional(CommandJumpFrame >= 0, CommandJumpFrame * Dt));
	Pose.SetNumberField(TEXT("target_jump_max_deg"), TargetJumpMaxDeg);
	Pose.SetStringField(TEXT("target_jump_joint"), Text(TargetJumpJoint));
	Pose.SetField(TEXT("target_jump_t"), Optional(TargetJumpFrame >= 0, TargetJumpFrame * Dt));
	Pose.SetNumberField(TEXT("strength_step_max"), StrengthStepMax);
	Pose.SetStringField(TEXT("strength_step_joint"), Text(StrengthStepJoint));
	Pose.SetField(TEXT("strength_step_t"), Optional(StrengthStepFrame >= 0, StrengthStepFrame * Dt));
}

void FNaturalMeasures::WriteMetrics(FJsonObject& Metrics)
{
	EndSwayStretch();
	// Time beyond the joints' ranges, over every joint and frame in which the body was not lying.
	int32 Frames = 0;
	int32 Outside = 0;
	double WorstShare = 0.0;
	FName WorstJoint;
	for (const TPair<FName, FLimit>& Limit : Limits)
	{
		Frames += Limit.Value.Frames;
		Outside += Limit.Value.OutsideFrames;
		const double Share = Limit.Value.Frames > 0 ? static_cast<double>(Limit.Value.OutsideFrames) / Limit.Value.Frames : 0.0;
		if (Share > WorstShare)
		{
			WorstShare = Share;
			WorstJoint = Limit.Key;
		}
	}
	Metrics.SetNumberField(TEXT("limit_excess_share"), Frames > 0 ? static_cast<double>(Outside) / Frames : 0.0);
	Metrics.SetNumberField(TEXT("limit_excess_joint_share"), WorstShare);
	Metrics.SetStringField(TEXT("limit_excess_joint"), Text(WorstJoint));

	Metrics.SetNumberField(TEXT("joint_speed_step_max_rads"), JointSpeedStepMax);
	Metrics.SetStringField(TEXT("joint_speed_step_joint"), Text(JointSpeedStepJoint));
	Metrics.SetField(TEXT("joint_speed_step_t"), Optional(JointSpeedStepFrame >= 0, JointSpeedStepFrame * Dt));

	Metrics.SetField(TEXT("head_accel_max_mps2"), Optional(bHaveHeadAcceleration, HeadAccelMax));
	Metrics.SetField(TEXT("head_accel_max_t"), Optional(HeadAccelFrame >= 0, HeadAccelFrame * Dt));
	Metrics.SetField(TEXT("head_jerk_rms_mps3"), Optional(HeadJerkFrames > 0, FMath::Sqrt(HeadJerkSquares / FMath::Max(HeadJerkFrames, 1))));

	const TSharedRef<FJsonObject> Still = MakeShared<FJsonObject>();
	Still->SetNumberField(TEXT("longest_still_s"), LongestStillFrames * Dt);
	Still->SetField(TEXT("longest_still_t"), Optional(LongestStillFrames > 0, LongestStillStart * Dt));
	Still->SetStringField(TEXT("longest_still_state"), LongestStillFrames > 0 ? FSandboxBodyTuning::StateName(LongestStillState) : TEXT(""));
	const TSharedRef<FJsonObject> ShareJson = MakeShared<FJsonObject>();
	const TSharedRef<FJsonObject> LongestJson = MakeShared<FJsonObject>();
	const TSharedRef<FJsonObject> TargetJson = MakeShared<FJsonObject>();
	const TSharedRef<FJsonObject> TravelJson = MakeShared<FJsonObject>();
	double TargetLongest = 0.0;
	for (const TPair<ESandboxBodyState, FStillShare>& Share : StillByState)
	{
		const FString Name = FSandboxBodyTuning::StateName(Share.Key);
		ShareJson->SetNumberField(Name, Share.Value.Frames > 0 ? static_cast<double>(Share.Value.Still) / Share.Value.Frames : 0.0);
		LongestJson->SetNumberField(Name, Share.Value.Longest * Dt);
		TargetJson->SetNumberField(Name, Share.Value.TargetLongest * Dt);
		TArray<double> Travel;
		Travel.Append(Share.Value.TravelCm);
		const TSharedRef<FJsonObject> Each = MakeShared<FJsonObject>();
		Each->SetNumberField(TEXT("p5"), FSandboxMotionStats::Percentile(Travel, 0.05));
		Each->SetNumberField(TEXT("p50"), FSandboxMotionStats::Percentile(Travel, 0.5));
		Each->SetNumberField(TEXT("p95"), FSandboxMotionStats::Percentile(Travel, 0.95));
		TravelJson->SetObjectField(Name, Each);
		if (Share.Key != ESandboxBodyState::Standing && Share.Key != ESandboxBodyState::Lying)
		{
			TargetLongest = FMath::Max(TargetLongest, Share.Value.TargetLongest * Dt);
		}
	}
	Still->SetObjectField(TEXT("still_share_by_state"), ShareJson);
	Still->SetObjectField(TEXT("longest_still_by_state_s"), LongestJson);
	// What "still" is judged by: how far the part that went furthest went within a second.
	Still->SetObjectField(TEXT("travel_by_state_cm"), TravelJson);
	// What the body asks of itself, unchanged, while it neither stands nor lies; and by state.
	Still->SetNumberField(TEXT("target_still_longest_s"), TargetLongest);
	Still->SetObjectField(TEXT("target_still_longest_by_state_s"), TargetJson);
	Metrics.SetObjectField(TEXT("stillness"), Still);

	if (Prior && PriorByState.Num() > 0)
	{
		const TSharedRef<FJsonObject> PriorJson = MakeShared<FJsonObject>();
		const TSharedRef<FJsonObject> States = MakeShared<FJsonObject>();
		TArray<double> All;
		for (const TPair<ESandboxBodyState, TArray<double>>& Each : PriorByState)
		{
			All.Append(Each.Value);
			const TSharedRef<FJsonObject> StateJson = MakeShared<FJsonObject>();
			StateJson->SetNumberField(TEXT("mean_deg"), FSandboxMotionStats::Mean(Each.Value));
			StateJson->SetNumberField(TEXT("p95_deg"), FSandboxMotionStats::Percentile(Each.Value, 0.95));
			StateJson->SetNumberField(TEXT("worst_deg"), FSandboxMotionStats::Percentile(Each.Value, 1.0));
			StateJson->SetNumberField(TEXT("samples"), Each.Value.Num());
			States->SetObjectField(FSandboxBodyTuning::StateName(Each.Key), StateJson);
		}
		PriorJson->SetNumberField(TEXT("mean_deg"), FSandboxMotionStats::Mean(All));
		PriorJson->SetNumberField(TEXT("p95_deg"), FSandboxMotionStats::Percentile(All, 0.95));
		PriorJson->SetNumberField(TEXT("worst_deg"), PriorWorstDeg);
		PriorJson->SetNumberField(TEXT("worst_t"), (PriorWorstFrame + 1) * Dt);
		PriorJson->SetStringField(TEXT("worst_state"), FSandboxBodyTuning::StateName(PriorWorstState));
		PriorJson->SetStringField(TEXT("worst_nearest"), FString::Printf(TEXT("%s at %.1f s"), *PriorWorstClip, PriorWorstClipS));
		PriorJson->SetObjectField(TEXT("by_state"), States);
		// Which database: figures from two different ones are not to be compared.
		PriorJson->SetStringField(TEXT("stamp"), FString::Printf(TEXT("%08x"), Prior->GetStamp()));
		PriorJson->SetNumberField(TEXT("poses"), Prior->Num());
		Metrics.SetObjectField(TEXT("pose_prior"), PriorJson);
	}

	if (BestCom.Num() > 1)
	{
		const auto Along = [](const TArray<FVector2D>& Points, bool bAcross)
		{
			TArray<double> Values;
			for (const FVector2D& Point : Points)
			{
				Values.Add(bAcross ? Point.Y : Point.X);
			}
			return Values;
		};
		const int32 Half = FMath::RoundToInt32(0.5 * SwaySmoothS / Dt);
		const TArray<FVector2D> SmoothCom = FSandboxMotionStats::MovingMean(BestCom, Half);
		const TArray<FVector2D> SmoothCop = FSandboxMotionStats::MovingMean(BestCop, Half);
		TArray<FVector2D> Sparse;
		for (int32 Index = 0; Index < SmoothCom.Num(); Index += SwayDecimation)
		{
			Sparse.Add(SmoothCom[Index]);
		}
		double Correlation = 0.0;
		const double PeriodS = FSandboxMotionStats::Period(Sparse, SwayDecimation * Dt, SwayMinPeriodS, SwayMinCorrelation, &Correlation);
		const TSharedRef<FJsonObject> Sway = MakeShared<FJsonObject>();
		Sway->SetNumberField(TEXT("seconds"), BestCom.Num() * Dt);
		Sway->SetNumberField(TEXT("com_rms_ap_cm"), FSandboxMotionStats::RmsDeviation(Along(SmoothCom, false)));
		Sway->SetNumberField(TEXT("com_rms_ml_cm"), FSandboxMotionStats::RmsDeviation(Along(SmoothCom, true)));
		Sway->SetNumberField(TEXT("com_speed_mean_cmps"), FSandboxMotionStats::PathSpeed(SmoothCom, Dt));
		Sway->SetNumberField(TEXT("cop_rms_ap_cm"), FSandboxMotionStats::RmsDeviation(Along(SmoothCop, false)));
		Sway->SetNumberField(TEXT("cop_rms_ml_cm"), FSandboxMotionStats::RmsDeviation(Along(SmoothCop, true)));
		Sway->SetNumberField(TEXT("cop_speed_mean_cmps"), FSandboxMotionStats::PathSpeed(SmoothCop, Dt));
		Sway->SetNumberField(TEXT("cop_speed_raw_cmps"), FSandboxMotionStats::PathSpeed(BestCop, Dt));
		// Where the floor pushes less where the centre of mass is, on average: 0 for a body nothing else holds.
		Sway->SetNumberField(TEXT("cop_offset_ap_cm"), FSandboxMotionStats::Mean(Along(BestCop, false)) - FSandboxMotionStats::Mean(Along(BestCom, false)));
		Sway->SetNumberField(TEXT("cop_offset_ml_cm"), FSandboxMotionStats::Mean(Along(BestCop, true)) - FSandboxMotionStats::Mean(Along(BestCom, true)));
		Sway->SetNumberField(TEXT("floor_share_mean"), BestFloorShareSum / BestCom.Num());
		Sway->SetField(TEXT("period_s"), Optional(PeriodS > 0.0, PeriodS));
		Sway->SetNumberField(TEXT("period_correlation"), Correlation);
		Metrics.SetObjectField(TEXT("sway"), Sway);
	}
}

void FNaturalMeasures::WriteJoint(FName Joint, FJsonObject& Entry) const
{
	if (const FLimit* Limit = Limits.Find(Joint))
	{
		Entry.SetNumberField(TEXT("limit_excess_share"), Limit->Frames > 0 ? static_cast<double>(Limit->OutsideFrames) / Limit->Frames : 0.0);
	}
}
