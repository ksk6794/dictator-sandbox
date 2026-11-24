#include "SandboxBodyTuning.h"

#include "SandboxParamStore.h"

namespace
{
	// States of the body that may carry tuning values of their own (docs/PRD.md, "States").
	const TCHAR* const StateNames[] = { TEXT("standing"), TEXT("flinch"), TEXT("stagger"), TEXT("falling"), TEXT("lying"), TEXT("get_up") };

	// Upper bounds that catch a slipped decimal point or a wrong unit, not physical limits: a human hip
	// reaches a few thousand N*m/rad only when fully braced.
	constexpr double MaxAngularStiffness = 20000.0;
	constexpr double MaxLinearStiffness = 200000.0;
	constexpr double MaxDampingS = 2.0;
	constexpr double MaxTorque = 5000.0;

	bool CheckRange(double Value, double Max, const TCHAR* Group, const TCHAR* Field, FString& OutError)
	{
		if (FMath::IsFinite(Value) && Value >= 0.0 && Value <= Max)
		{
			return true;
		}
		OutError = FString::Printf(TEXT("%s.%s = %g is outside 0..%g"), Group, Field, Value, Max);
		return false;
	}

	/** Calls Visitor(name, value) for every tunable number. TuningType is the tuning, const or not. */
	template <typename TuningType, typename VisitorType>
	void VisitValues(TuningType& Tuning, VisitorType&& Visitor)
	{
		const auto Muscle = [&Visitor](const TCHAR* Group, auto& Values)
		{
			Visitor(FString::Printf(TEXT("muscles.%s.stiffness_nm_per_rad"), Group), Values.StiffnessNmPerRad);
			Visitor(FString::Printf(TEXT("muscles.%s.damping_s"), Group), Values.DampingS);
			Visitor(FString::Printf(TEXT("muscles.%s.max_torque_nm"), Group), Values.MaxTorqueNm);
			Visitor(FString::Printf(TEXT("muscles.%s.pose_velocity_share"), Group), Values.PoseVelocityShare);
		};
		const auto Assist = [&Visitor](const TCHAR* Group, auto& Values)
		{
			Visitor(FString::Printf(TEXT("assist.%s.horizontal_n_per_m"), Group), Values.HorizontalNPerM);
			Visitor(FString::Printf(TEXT("assist.%s.vertical_n_per_m"), Group), Values.VerticalNPerM);
			Visitor(FString::Printf(TEXT("assist.%s.angular_nm_per_rad"), Group), Values.AngularNmPerRad);
			Visitor(FString::Printf(TEXT("assist.%s.damping_s"), Group), Values.DampingS);
			Visitor(FString::Printf(TEXT("assist.%s.max_torque_nm"), Group), Values.MaxTorqueNm);
			Visitor(FString::Printf(TEXT("assist.%s.max_force_n"), Group), Values.MaxForceN);
		};
		Muscle(TEXT("spine"), Tuning.Spine);
		Muscle(TEXT("neck"), Tuning.Neck);
		Muscle(TEXT("arms"), Tuning.Arms);
		Muscle(TEXT("legs"), Tuning.Legs);
		Assist(TEXT("pelvis"), Tuning.Pelvis);
		Assist(TEXT("chest"), Tuning.Chest);
		Assist(TEXT("head"), Tuning.Head);
		Assist(TEXT("feet"), Tuning.Feet);
		Visitor(FString(TEXT("solver.position_iterations")), Tuning.Solver.PositionIterations);
		Visitor(FString(TEXT("solver.velocity_iterations")), Tuning.Solver.VelocityIterations);
		Visitor(FString(TEXT("solver.projection_iterations")), Tuning.Solver.ProjectionIterations);
		Visitor(FString(TEXT("thresholds.flinch_impulse_ns")), Tuning.Thresholds.FlinchImpulseNs);
		Visitor(FString(TEXT("thresholds.flinch_window_s")), Tuning.Thresholds.FlinchWindowS);
		Visitor(FString(TEXT("thresholds.fall_pelvis_drop_cm")), Tuning.Thresholds.FallPelvisDropCm);
		Visitor(FString(TEXT("thresholds.fall_tilt_deg")), Tuning.Thresholds.FallTiltDeg);
		Visitor(FString(TEXT("thresholds.rest_speed_mps")), Tuning.Thresholds.RestSpeedMps);
		Visitor(FString(TEXT("thresholds.rest_angular_speed_rads")), Tuning.Thresholds.RestAngularSpeedRadS);
		Visitor(FString(TEXT("thresholds.rest_time_s")), Tuning.Thresholds.RestTimeS);
		Visitor(FString(TEXT("thresholds.held_time_s")), Tuning.Thresholds.HeldTimeS);
		Visitor(FString(TEXT("thresholds.held_descent_cm")), Tuning.Thresholds.HeldDescentCm);
		Visitor(FString(TEXT("thresholds.impact_let_go_s")), Tuning.Thresholds.ImpactLetGoS);
		Visitor(FString(TEXT("balance.foot_contact_min_n")), Tuning.Balance.FootContactMinN);
		Visitor(FString(TEXT("balance.sole_tolerance_cm")), Tuning.Balance.SoleToleranceCm);
		Visitor(FString(TEXT("balance.recoverable_cm")), Tuning.Balance.RecoverableCm);
		Visitor(FString(TEXT("balance.step_reach_cm")), Tuning.Balance.StepReachCm);
		Visitor(FString(TEXT("balance.step_time_s")), Tuning.Balance.StepTimeS);
		Visitor(FString(TEXT("balance.decision_s")), Tuning.Balance.DecisionS);
		Visitor(FString(TEXT("balance.anchor_follow_s")), Tuning.Balance.AnchorFollowS);
		Visitor(FString(TEXT("balance.replant_air_s")), Tuning.Balance.ReplantAirS);
		Visitor(FString(TEXT("balance.lift_grace_s")), Tuning.Balance.LiftGraceS);
		Visitor(FString(TEXT("balance.ankle_gain")), Tuning.Balance.AnkleGain);
		Visitor(FString(TEXT("balance.ankle_side_gain")), Tuning.Balance.AnkleSideGain);
		Visitor(FString(TEXT("balance.ankle_integral_s")), Tuning.Balance.AnkleIntegralS);
		Visitor(FString(TEXT("balance.ankle_integral_max_cm")), Tuning.Balance.AnkleIntegralMaxCm);
		Visitor(FString(TEXT("balance.ankle_max_deg")), Tuning.Balance.AnkleMaxDeg);
		Visitor(FString(TEXT("balance.hip_deg_per_cm")), Tuning.Balance.HipDegPerCm);
		Visitor(FString(TEXT("balance.hip_max_deg")), Tuning.Balance.HipMaxDeg);
		Visitor(FString(TEXT("balance.response_s")), Tuning.Balance.ResponseS);
		Visitor(FString(TEXT("balance.lean_max_cm")), Tuning.Balance.LeanMaxCm);
		Visitor(FString(TEXT("balance.recover_s")), Tuning.Balance.RecoverS);
		Visitor(FString(TEXT("balance.recover_speed_mps")), Tuning.Balance.RecoverSpeedMps);
		Visitor(FString(TEXT("balance.step_margin_cm")), Tuning.Balance.StepMarginCm);
		Visitor(FString(TEXT("balance.step_margin_goal_cm")), Tuning.Balance.StepMarginGoalCm);
		Visitor(FString(TEXT("balance.step_lift_cm")), Tuning.Balance.StepLiftCm);
		Visitor(FString(TEXT("balance.step_press_cm")), Tuning.Balance.StepPressCm);
		Visitor(FString(TEXT("balance.step_settle_s")), Tuning.Balance.StepSettleS);
		Visitor(FString(TEXT("balance.step_over_share")), Tuning.Balance.StepOverShare);
		Visitor(FString(TEXT("balance.step_descent_share")), Tuning.Balance.StepDescentShare);
		Visitor(FString(TEXT("balance.swing_stiffness_scale")), Tuning.Balance.SwingStiffnessScale);
		Visitor(FString(TEXT("balance.swing_damping_scale")), Tuning.Balance.SwingDampingScale);
		Visitor(FString(TEXT("balance.swing_pose_velocity_share")), Tuning.Balance.SwingPoseVelocityShare);
		Visitor(FString(TEXT("balance.swing_foot_n_per_m")), Tuning.Balance.SwingFootNPerM);
		Visitor(FString(TEXT("balance.step_width_min_cm")), Tuning.Balance.StepWidthMinCm);
		Visitor(FString(TEXT("balance.cross_offset_cm")), Tuning.Balance.CrossOffsetCm);
		Visitor(FString(TEXT("balance.step_ease_s")), Tuning.Balance.StepEaseS);
		Visitor(FString(TEXT("balance.stance_follow")), Tuning.Balance.StanceFollow);
		Visitor(FString(TEXT("balance.leg_extension_max")), Tuning.Balance.LegExtensionMax);
		Visitor(FString(TEXT("balance.step_shift_share")), Tuning.Balance.StepShiftShare);
		Visitor(FString(TEXT("balance.step_shift_ready_cm")), Tuning.Balance.StepShiftReadyCm);
		Visitor(FString(TEXT("balance.step_shift_s")), Tuning.Balance.StepShiftS);
		Visitor(FString(TEXT("balance.step_shift_max_s")), Tuning.Balance.StepShiftMaxS);
		Visitor(FString(TEXT("balance.step_asked_time_s")), Tuning.Balance.StepAskedTimeS);
		Visitor(FString(TEXT("balance.anchor_room_share")), Tuning.Balance.AnchorRoomShare);
		Visitor(FString(TEXT("balance.lower_s")), Tuning.Balance.LowerS);
		Visitor(FString(TEXT("balance.lower_max_cm")), Tuning.Balance.LowerMaxCm);
		Visitor(FString(TEXT("balance.cross_past_cm")), Tuning.Balance.CrossPastCm);
		Visitor(FString(TEXT("balance.stagger_damping_s")), Tuning.Balance.StaggerDampingS);
		Visitor(FString(TEXT("balance.regroup_wait_s")), Tuning.Balance.RegroupWaitS);
		Visitor(FString(TEXT("balance.regroup_tolerance_cm")), Tuning.Balance.RegroupToleranceCm);
		Visitor(FString(TEXT("balance.regroup_max_steps")), Tuning.Balance.RegroupMaxSteps);
		Visitor(FString(TEXT("balance.regroup_inside_share")), Tuning.Balance.RegroupInsideShare);
		Visitor(FString(TEXT("balance.regroup_lead_gain")), Tuning.Balance.RegroupLeadGain);
		Visitor(FString(TEXT("balance.regroup_shift_gain_per_s")), Tuning.Balance.RegroupShiftGainPerS);
		Visitor(FString(TEXT("balance.regroup_lead_max_cm")), Tuning.Balance.RegroupLeadMaxCm);
		Visitor(FString(TEXT("balance.regroup_margin_cm")), Tuning.Balance.RegroupMarginCm);
		Visitor(FString(TEXT("balance.regroup_shift_max_s")), Tuning.Balance.RegroupShiftMaxS);
		Visitor(FString(TEXT("balance.arm_dead_cm")), Tuning.Balance.ArmDeadCm);
		Visitor(FString(TEXT("balance.arm_full_cm")), Tuning.Balance.ArmFullCm);
		Visitor(FString(TEXT("balance.arm_response_s")), Tuning.Balance.ArmResponseS);
		Visitor(FString(TEXT("reactions.flinch_weakening")), Tuning.Reactions.FlinchWeakening);
		Visitor(FString(TEXT("reactions.flinch_recovery_s")), Tuning.Reactions.FlinchRecoveryS);
		Visitor(FString(TEXT("reactions.flinch_pose_full_ns")), Tuning.Reactions.FlinchPoseFullNs);
		Visitor(FString(TEXT("reactions.flinch_give_deg")), Tuning.Reactions.FlinchGiveDeg);
		Visitor(FString(TEXT("reactions.pressure_s")), Tuning.Reactions.PressureS);
		Visitor(FString(TEXT("reactions.held_push_s")), Tuning.Reactions.HeldPushS);
		Visitor(FString(TEXT("poses.blend_s")), Tuning.Poses.BlendS);
		const auto Pose = [&Visitor](const TCHAR* Name, auto& Values)
		{
			for (int32 Index = 0; Index < SandboxPoseJointCount; ++Index)
			{
				Visitor(FString::Printf(TEXT("poses.%s.%s.swing1_deg"), Name, SandboxPoseJoints[Index]), Values.Joints[Index].Swing1Deg);
				Visitor(FString::Printf(TEXT("poses.%s.%s.swing2_deg"), Name, SandboxPoseJoints[Index]), Values.Joints[Index].Swing2Deg);
				Visitor(FString::Printf(TEXT("poses.%s.%s.twist_deg"), Name, SandboxPoseJoints[Index]), Values.Joints[Index].TwistDeg);
			}
		};
		Pose(TEXT("stance"), Tuning.Poses.Stance);
		Pose(TEXT("flinch"), Tuning.Poses.Flinch);
		Visitor(FString(TEXT("poses.fall_blend_s")), Tuning.Poses.FallBlendS);
		Pose(TEXT("fall_back"), Tuning.Poses.FallBack);
		Pose(TEXT("fall_forward"), Tuning.Poses.FallForward);
		Pose(TEXT("fall_side"), Tuning.Poses.FallSide);
		Pose(TEXT("balance_back"), Tuning.Poses.BalanceBack);
		Pose(TEXT("balance_forward"), Tuning.Poses.BalanceForward);
		Pose(TEXT("balance_side"), Tuning.Poses.BalanceSide);
		Visitor(FString(TEXT("poses.idle.weight")), Tuning.Poses.IdleWeight);
		Visitor(FString(TEXT("poses.idle.sway_weight")), Tuning.Poses.IdleSwayWeight);
		Visitor(FString(TEXT("getup.rest_s")), Tuning.GetUp.RestS);
		Visitor(FString(TEXT("getup.ease_s")), Tuning.GetUp.EaseS);
		Visitor(FString(TEXT("getup.rate")), Tuning.GetUp.Rate);
		Visitor(FString(TEXT("getup.end_s")), Tuning.GetUp.EndS);
		Visitor(FString(TEXT("getup.lag_cm")), Tuning.GetUp.LagCm);
		Visitor(FString(TEXT("getup.side_lag_cm")), Tuning.GetUp.SideLagCm);
		Visitor(FString(TEXT("getup.foot_lag_cm")), Tuning.GetUp.FootLagCm);
		Visitor(FString(TEXT("getup.foot_wait_s")), Tuning.GetUp.FootWaitS);
		Visitor(FString(TEXT("getup.frame_follow_s")), Tuning.GetUp.FrameFollowS);
		Visitor(FString(TEXT("getup.stall_s")), Tuning.GetUp.StallS);
		Visitor(FString(TEXT("getup.lost_cm")), Tuning.GetUp.LostCm);
		Visitor(FString(TEXT("getup.upright_deg")), Tuning.GetUp.UprightDeg);
	}

	const TCHAR* const IdleAnimationKey = TEXT("poses.idle.animation");

	/** Calls Visitor(name, text) for every clip of the get-up. */
	template <typename TuningType, typename VisitorType>
	void VisitClips(TuningType& GetUp, VisitorType&& Visitor)
	{
		Visitor(TEXT("getup.clip_back"), GetUp.ClipBack);
		Visitor(TEXT("getup.clip_front"), GetUp.ClipFront);
		Visitor(TEXT("getup.clip_left"), GetUp.ClipLeft);
		Visitor(TEXT("getup.clip_right"), GetUp.ClipRight);
	}
}

FSandboxPose FSandboxPoseTuning::Mirrored(const FSandboxPose& Pose)
{
	FSandboxPose Result;
	for (int32 Index = 0; Index < SandboxPoseJointCount; ++Index)
	{
		const FString Name(SandboxPoseJoints[Index]);
		const bool bLeft = Name.EndsWith(TEXT("_l"));
		const bool bRight = Name.EndsWith(TEXT("_r"));
		if (!bLeft && !bRight)
		{
			// A joint of the trunk: bending forward and back stays, bending sideways and twisting turn round.
			Result.Joints[Index] = Pose.Joints[Index];
			Result.Joints[Index].Swing2Deg = -Pose.Joints[Index].Swing2Deg;
			Result.Joints[Index].TwistDeg = -Pose.Joints[Index].TwistDeg;
			continue;
		}
		// The same angles on the joint of the other side give the mirror-image motion.
		const FString Other = Name.LeftChop(1) + (bLeft ? TEXT("r") : TEXT("l"));
		for (int32 From = 0; From < SandboxPoseJointCount; ++From)
		{
			if (Other == SandboxPoseJoints[From])
			{
				Result.Joints[Index] = Pose.Joints[From];
			}
		}
	}
	return Result;
}

const TCHAR* const SandboxPoseJoints[SandboxPoseJointCount] = {
	TEXT("spine_02"), TEXT("spine_04"), TEXT("neck_01"),
	TEXT("clavicle_l"), TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l"),
	TEXT("clavicle_r"), TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r"),
	TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"),
	TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r") };

void FSandboxBodyTuning::ListValues(TArray<TPair<FString, double>>& OutValues) const
{
	OutValues.Reset();
	VisitValues(*this, [&OutValues](const FString& Key, const double& Value) { OutValues.Emplace(Key, Value); });
}

bool FSandboxBodyTuning::SetValue(const FString& Key, double Value, FString& OutError)
{
	bool bFound = false;
	VisitValues(*this, [&](const FString& Candidate, double& Target)
	{
		if (Candidate == Key)
		{
			Target = Value;
			bFound = true;
		}
	});
	if (!bFound)
	{
		OutError = FString::Printf(TEXT("'%s' is not a tuning value"), *Key);
	}
	return bFound;
}

namespace
{
	/** Calls Visitor(name, bone) for every bone of the rig. */
	template <typename RigType, typename VisitorType>
	void VisitRig(RigType& Rig, VisitorType&& Visitor)
	{
		Visitor(TEXT("rig.pelvis"), Rig.Pelvis);
		Visitor(TEXT("rig.chest"), Rig.Chest);
		Visitor(TEXT("rig.head"), Rig.Head);
		Visitor(TEXT("rig.foot_left"), Rig.FootLeft);
		Visitor(TEXT("rig.foot_right"), Rig.FootRight);
	}
}

void FSandboxBodyTuning::DeclareParams(FSandboxParamStore& Params)
{
	FSandboxBodyTuning Names;
	VisitValues(Names, [&Params](const FString& Key, double&) { Params.DeclareNumber(Key); });
	VisitRig(Names.Rig, [&Params](const TCHAR* Key, FName&) { Params.DeclareString(Key); });
	Params.DeclareString(IdleAnimationKey);
	VisitClips(Names.GetUp, [&Params](const TCHAR* Key, FString&) { Params.DeclareString(Key); });
	for (const TCHAR* State : StateNames)
	{
		Params.DeclareState(State);
	}

	// The base values, and every state that sets something, must give a tuning the body can use.
	Params.AddValidator([](const FSandboxParamStore& Store, FString& OutError)
	{
		FSandboxBodyTuning Tuning;
		if (!FromParams(Store, FString(), Tuning, OutError))
		{
			return false;
		}
		TArray<FString> States;
		Store.GetStatesInUse(States);
		for (const FString& State : States)
		{
			if (!FromParams(Store, State, Tuning, OutError))
			{
				OutError = FString::Printf(TEXT("state '%s': %s"), *State, *OutError);
				return false;
			}
		}
		return true;
	});
}

bool FSandboxBodyTuning::FromParams(const FSandboxParamStore& Params, const FString& State, FSandboxBodyTuning& OutTuning, FString& OutError)
{
	OutTuning = FSandboxBodyTuning();
	FString Missing;
	VisitValues(OutTuning, [&](const FString& Key, double& Target)
	{
		if (!Params.FindNumber(Key, State, Target) && Missing.IsEmpty())
		{
			Missing = Key;
		}
	});
	VisitRig(OutTuning.Rig, [&](const TCHAR* Key, FName& Bone)
	{
		FString Name;
		if (Params.FindString(Key, Name))
		{
			Bone = FName(*Name);
		}
		else if (Missing.IsEmpty())
		{
			Missing = Key;
		}
	});
	if (!Params.FindString(IdleAnimationKey, OutTuning.Poses.IdleAnimation) && Missing.IsEmpty())
	{
		Missing = IdleAnimationKey;
	}
	VisitClips(OutTuning.GetUp, [&](const TCHAR* Key, FString& Path)
	{
		if (!Params.FindString(Key, Path) && Missing.IsEmpty())
		{
			Missing = Key;
		}
	});
	if (!Missing.IsEmpty())
	{
		OutError = FString::Printf(TEXT("'%s' is missing from the tuning files"), *Missing);
		return false;
	}
	return OutTuning.Validate(OutError);
}

bool FSandboxBodyTuning::HasSameValues(const FSandboxBodyTuning& Other) const
{
	TArray<TPair<FString, double>> Mine, Theirs;
	ListValues(Mine);
	Other.ListValues(Theirs);
	return Mine == Theirs;
}

const FSandboxMuscleTuning& FSandboxBodyTuning::Muscle(ESandboxMuscleGroup Group) const
{
	switch (Group)
	{
	case ESandboxMuscleGroup::Neck: return Neck;
	case ESandboxMuscleGroup::Arms: return Arms;
	case ESandboxMuscleGroup::Legs: return Legs;
	default: return Spine;
	}
}

bool FSandboxBodyTuning::Validate(FString& OutError) const
{
	const TPair<const TCHAR*, const FSandboxMuscleTuning*> Muscles[] = {
		{ TEXT("muscles.spine"), &Spine }, { TEXT("muscles.neck"), &Neck }, { TEXT("muscles.arms"), &Arms }, { TEXT("muscles.legs"), &Legs } };
	for (const TPair<const TCHAR*, const FSandboxMuscleTuning*>& Entry : Muscles)
	{
		if (!CheckRange(Entry.Value->StiffnessNmPerRad, MaxAngularStiffness, Entry.Key, TEXT("stiffness_nm_per_rad"), OutError) ||
			!CheckRange(Entry.Value->DampingS, MaxDampingS, Entry.Key, TEXT("damping_s"), OutError) ||
			!CheckRange(Entry.Value->MaxTorqueNm, MaxTorque, Entry.Key, TEXT("max_torque_nm"), OutError) ||
			!CheckRange(Entry.Value->PoseVelocityShare, 1.0, Entry.Key, TEXT("pose_velocity_share"), OutError))
		{
			return false;
		}
	}

	const TPair<const TCHAR*, const FSandboxAssistTuning*> Assists[] = {
		{ TEXT("assist.pelvis"), &Pelvis }, { TEXT("assist.chest"), &Chest }, { TEXT("assist.head"), &Head }, { TEXT("assist.feet"), &Feet } };
	for (const TPair<const TCHAR*, const FSandboxAssistTuning*>& Entry : Assists)
	{
		if (!CheckRange(Entry.Value->HorizontalNPerM, MaxLinearStiffness, Entry.Key, TEXT("horizontal_n_per_m"), OutError) ||
			!CheckRange(Entry.Value->VerticalNPerM, MaxLinearStiffness, Entry.Key, TEXT("vertical_n_per_m"), OutError) ||
			!CheckRange(Entry.Value->AngularNmPerRad, MaxAngularStiffness, Entry.Key, TEXT("angular_nm_per_rad"), OutError) ||
			!CheckRange(Entry.Value->DampingS, MaxDampingS, Entry.Key, TEXT("damping_s"), OutError) ||
			!CheckRange(Entry.Value->MaxTorqueNm, MaxTorque, Entry.Key, TEXT("max_torque_nm"), OutError) ||
			!CheckRange(Entry.Value->MaxForceN, 100000.0, Entry.Key, TEXT("max_force_n"), OutError))
		{
			return false;
		}
	}
	if (!CheckRange(GetUp.RestS, 600.0, TEXT("getup"), TEXT("rest_s"), OutError) ||
		!CheckRange(GetUp.EaseS, 10.0, TEXT("getup"), TEXT("ease_s"), OutError) ||
		!CheckRange(GetUp.Rate, 4.0, TEXT("getup"), TEXT("rate"), OutError) ||
		!CheckRange(GetUp.EndS, 60.0, TEXT("getup"), TEXT("end_s"), OutError) ||
		!CheckRange(GetUp.LagCm, 500.0, TEXT("getup"), TEXT("lag_cm"), OutError) ||
		!CheckRange(GetUp.SideLagCm, 500.0, TEXT("getup"), TEXT("side_lag_cm"), OutError) ||
		!CheckRange(GetUp.FootLagCm, 500.0, TEXT("getup"), TEXT("foot_lag_cm"), OutError) ||
		!CheckRange(GetUp.FootWaitS, 60.0, TEXT("getup"), TEXT("foot_wait_s"), OutError) ||
		!CheckRange(GetUp.FrameFollowS, 60.0, TEXT("getup"), TEXT("frame_follow_s"), OutError) ||
		!CheckRange(GetUp.StallS, 60.0, TEXT("getup"), TEXT("stall_s"), OutError) ||
		!CheckRange(GetUp.LostCm, 1000.0, TEXT("getup"), TEXT("lost_cm"), OutError) ||
		!CheckRange(GetUp.UprightDeg, 180.0, TEXT("getup"), TEXT("upright_deg"), OutError))
	{
		return false;
	}

	// Iteration counts are whole numbers that fit the engine's per-body setting (one byte).
	const TPair<const TCHAR*, double> Iterations[] = {
		{ TEXT("position_iterations"), Solver.PositionIterations }, { TEXT("velocity_iterations"), Solver.VelocityIterations },
		{ TEXT("projection_iterations"), Solver.ProjectionIterations } };
	for (const TPair<const TCHAR*, double>& Entry : Iterations)
	{
		if (!CheckRange(Entry.Value, 255.0, TEXT("solver"), Entry.Key, OutError))
		{
			return false;
		}
		if (Entry.Value != FMath::RoundToDouble(Entry.Value))
		{
			OutError = FString::Printf(TEXT("solver.%s = %g must be a whole number"), Entry.Key, Entry.Value);
			return false;
		}
	}
	if (Solver.PositionIterations < 1.0)
	{
		OutError = TEXT("solver.position_iterations must be at least 1");
		return false;
	}

	// Thresholds. A fall threshold of zero would have the body fall where it stands.
	if (!CheckRange(Thresholds.FlinchImpulseNs, 1000.0, TEXT("thresholds"), TEXT("flinch_impulse_ns"), OutError) ||
		!CheckRange(Thresholds.FlinchWindowS, 5.0, TEXT("thresholds"), TEXT("flinch_window_s"), OutError) ||
		!CheckRange(Thresholds.FallPelvisDropCm, 500.0, TEXT("thresholds"), TEXT("fall_pelvis_drop_cm"), OutError) ||
		!CheckRange(Thresholds.FallTiltDeg, 180.0, TEXT("thresholds"), TEXT("fall_tilt_deg"), OutError) ||
		!CheckRange(Thresholds.RestSpeedMps, 10.0, TEXT("thresholds"), TEXT("rest_speed_mps"), OutError) ||
		!CheckRange(Thresholds.RestAngularSpeedRadS, 100.0, TEXT("thresholds"), TEXT("rest_angular_speed_rads"), OutError) ||
		!CheckRange(Thresholds.RestTimeS, 60.0, TEXT("thresholds"), TEXT("rest_time_s"), OutError) ||
		!CheckRange(Thresholds.HeldTimeS, 60.0, TEXT("thresholds"), TEXT("held_time_s"), OutError) ||
		!CheckRange(Thresholds.HeldDescentCm, 100.0, TEXT("thresholds"), TEXT("held_descent_cm"), OutError) ||
		!CheckRange(Thresholds.ImpactLetGoS, 10.0, TEXT("thresholds"), TEXT("impact_let_go_s"), OutError) ||
		!CheckRange(Balance.FootContactMinN, 5000.0, TEXT("balance"), TEXT("foot_contact_min_n"), OutError) ||
		!CheckRange(Balance.SoleToleranceCm, 20.0, TEXT("balance"), TEXT("sole_tolerance_cm"), OutError) ||
		!CheckRange(Balance.RecoverableCm, 100.0, TEXT("balance"), TEXT("recoverable_cm"), OutError) ||
		!CheckRange(Balance.StepReachCm, 300.0, TEXT("balance"), TEXT("step_reach_cm"), OutError) ||
		!CheckRange(Balance.StepTimeS, 2.0, TEXT("balance"), TEXT("step_time_s"), OutError) ||
		!CheckRange(Balance.DecisionS, 2.0, TEXT("balance"), TEXT("decision_s"), OutError) ||
		!CheckRange(Balance.AnchorFollowS, 10.0, TEXT("balance"), TEXT("anchor_follow_s"), OutError) ||
		!CheckRange(Balance.ReplantAirS, 2.0, TEXT("balance"), TEXT("replant_air_s"), OutError) ||
		!CheckRange(Balance.LiftGraceS, 2.0, TEXT("balance"), TEXT("lift_grace_s"), OutError) ||
		!CheckRange(Balance.AnkleGain, 20.0, TEXT("balance"), TEXT("ankle_gain"), OutError) ||
		!CheckRange(Balance.AnkleSideGain, 20.0, TEXT("balance"), TEXT("ankle_side_gain"), OutError) ||
		!CheckRange(Balance.AnkleIntegralS, 60.0, TEXT("balance"), TEXT("ankle_integral_s"), OutError) ||
		!CheckRange(Balance.AnkleIntegralMaxCm, 30.0, TEXT("balance"), TEXT("ankle_integral_max_cm"), OutError) ||
		!CheckRange(Balance.AnkleMaxDeg, 30.0, TEXT("balance"), TEXT("ankle_max_deg"), OutError) ||
		!CheckRange(Balance.HipDegPerCm, 10.0, TEXT("balance"), TEXT("hip_deg_per_cm"), OutError) ||
		!CheckRange(Balance.HipMaxDeg, 60.0, TEXT("balance"), TEXT("hip_max_deg"), OutError) ||
		!CheckRange(Balance.ResponseS, 2.0, TEXT("balance"), TEXT("response_s"), OutError) ||
		!CheckRange(Balance.LeanMaxCm, 50.0, TEXT("balance"), TEXT("lean_max_cm"), OutError) ||
		!CheckRange(Balance.RecoverS, 10.0, TEXT("balance"), TEXT("recover_s"), OutError) ||
		!CheckRange(Balance.RecoverSpeedMps, 5.0, TEXT("balance"), TEXT("recover_speed_mps"), OutError) ||
		!CheckRange(Balance.StepMarginCm, 50.0, TEXT("balance"), TEXT("step_margin_cm"), OutError) ||
		!CheckRange(Balance.StepMarginGoalCm, 50.0, TEXT("balance"), TEXT("step_margin_goal_cm"), OutError) ||
		!CheckRange(Balance.StepLiftCm, 50.0, TEXT("balance"), TEXT("step_lift_cm"), OutError) ||
		!CheckRange(Balance.StepPressCm, 20.0, TEXT("balance"), TEXT("step_press_cm"), OutError) ||
		!CheckRange(Balance.StepSettleS, 2.0, TEXT("balance"), TEXT("step_settle_s"), OutError) ||
		!CheckRange(Balance.StepOverShare, 1.0, TEXT("balance"), TEXT("step_over_share"), OutError) ||
		!CheckRange(Balance.StepDescentShare, 1.0, TEXT("balance"), TEXT("step_descent_share"), OutError) ||
		!CheckRange(Balance.SwingStiffnessScale, 10.0, TEXT("balance"), TEXT("swing_stiffness_scale"), OutError) ||
		!CheckRange(Balance.SwingDampingScale, 10.0, TEXT("balance"), TEXT("swing_damping_scale"), OutError) ||
		!CheckRange(Balance.SwingPoseVelocityShare, 1.0, TEXT("balance"), TEXT("swing_pose_velocity_share"), OutError) ||
		!CheckRange(Balance.SwingFootNPerM, MaxLinearStiffness, TEXT("balance"), TEXT("swing_foot_n_per_m"), OutError) ||
		!CheckRange(Balance.StepWidthMinCm, 100.0, TEXT("balance"), TEXT("step_width_min_cm"), OutError) ||
		!CheckRange(Balance.CrossOffsetCm, 100.0, TEXT("balance"), TEXT("cross_offset_cm"), OutError) ||
		!CheckRange(Balance.StepEaseS, 2.0, TEXT("balance"), TEXT("step_ease_s"), OutError) ||
		!CheckRange(Balance.StanceFollow, 1.0, TEXT("balance"), TEXT("stance_follow"), OutError) ||
		!CheckRange(Balance.LegExtensionMax, 1.0, TEXT("balance"), TEXT("leg_extension_max"), OutError) ||
		!CheckRange(Balance.StepShiftShare, 1.0, TEXT("balance"), TEXT("step_shift_share"), OutError) ||
		!CheckRange(Balance.StepShiftReadyCm, 50.0, TEXT("balance"), TEXT("step_shift_ready_cm"), OutError) ||
		!CheckRange(Balance.StepShiftS, 2.0, TEXT("balance"), TEXT("step_shift_s"), OutError) ||
		!CheckRange(Balance.StepShiftMaxS, 5.0, TEXT("balance"), TEXT("step_shift_max_s"), OutError) ||
		!CheckRange(Balance.StepAskedTimeS, 2.0, TEXT("balance"), TEXT("step_asked_time_s"), OutError) ||
		!CheckRange(Balance.AnchorRoomShare, 1.0, TEXT("balance"), TEXT("anchor_room_share"), OutError) ||
		!CheckRange(Balance.LowerS, 2.0, TEXT("balance"), TEXT("lower_s"), OutError) ||
		!CheckRange(Balance.LowerMaxCm, 60.0, TEXT("balance"), TEXT("lower_max_cm"), OutError) ||
		!CheckRange(Balance.CrossPastCm, 300.0, TEXT("balance"), TEXT("cross_past_cm"), OutError) ||
		!CheckRange(Balance.StaggerDampingS, 2.0, TEXT("balance"), TEXT("stagger_damping_s"), OutError) ||
		!CheckRange(Balance.RegroupWaitS, 10.0, TEXT("balance"), TEXT("regroup_wait_s"), OutError) ||
		!CheckRange(Balance.RegroupToleranceCm, 100.0, TEXT("balance"), TEXT("regroup_tolerance_cm"), OutError) ||
		!CheckRange(Balance.RegroupMaxSteps, 10.0, TEXT("balance"), TEXT("regroup_max_steps"), OutError) ||
		!CheckRange(Balance.RegroupInsideShare, 0.5, TEXT("balance"), TEXT("regroup_inside_share"), OutError) ||
		!CheckRange(Balance.RegroupLeadGain, 10.0, TEXT("balance"), TEXT("regroup_lead_gain"), OutError) ||
		!CheckRange(Balance.RegroupShiftGainPerS, 20.0, TEXT("balance"), TEXT("regroup_shift_gain_per_s"), OutError) ||
		!CheckRange(Balance.RegroupLeadMaxCm, 100.0, TEXT("balance"), TEXT("regroup_lead_max_cm"), OutError) ||
		!CheckRange(Balance.RegroupMarginCm, 50.0, TEXT("balance"), TEXT("regroup_margin_cm"), OutError) ||
		!CheckRange(Balance.RegroupShiftMaxS, 10.0, TEXT("balance"), TEXT("regroup_shift_max_s"), OutError) ||
		!CheckRange(Balance.ArmDeadCm, 100.0, TEXT("balance"), TEXT("arm_dead_cm"), OutError) ||
		!CheckRange(Balance.ArmFullCm, 300.0, TEXT("balance"), TEXT("arm_full_cm"), OutError) ||
		!CheckRange(Balance.ArmResponseS, 2.0, TEXT("balance"), TEXT("arm_response_s"), OutError) ||
		!CheckRange(Reactions.PressureS, 5.0, TEXT("reactions"), TEXT("pressure_s"), OutError) ||
		!CheckRange(Reactions.HeldPushS, 5.0, TEXT("reactions"), TEXT("held_push_s"), OutError) ||
		!CheckRange(Reactions.FlinchWeakening, 1.0, TEXT("reactions"), TEXT("flinch_weakening"), OutError) ||
		!CheckRange(Reactions.FlinchRecoveryS, 10.0, TEXT("reactions"), TEXT("flinch_recovery_s"), OutError))
	{
		return false;
	}
	if (!CheckRange(Poses.BlendS, 10.0, TEXT("poses"), TEXT("blend_s"), OutError) ||
		!CheckRange(Poses.FallBlendS, 10.0, TEXT("poses"), TEXT("fall_blend_s"), OutError) ||
		!CheckRange(Poses.IdleWeight, 5.0, TEXT("poses.idle"), TEXT("weight"), OutError) ||
		!CheckRange(Poses.IdleSwayWeight, 5.0, TEXT("poses.idle"), TEXT("sway_weight"), OutError))
	{
		return false;
	}
	const TPair<const TCHAR*, const FSandboxPose*> PoseList[] = { { TEXT("stance"), &Poses.Stance }, { TEXT("flinch"), &Poses.Flinch },
		{ TEXT("fall_back"), &Poses.FallBack }, { TEXT("fall_forward"), &Poses.FallForward }, { TEXT("fall_side"), &Poses.FallSide },
		{ TEXT("balance_back"), &Poses.BalanceBack }, { TEXT("balance_forward"), &Poses.BalanceForward }, { TEXT("balance_side"), &Poses.BalanceSide } };
	for (const TPair<const TCHAR*, const FSandboxPose*>& Entry : PoseList)
	{
		for (int32 Index = 0; Index < SandboxPoseJointCount; ++Index)
		{
			const FSandboxPoseAngles& Angles = Entry.Value->Joints[Index];
			for (const double Angle : { Angles.Swing1Deg, Angles.Swing2Deg, Angles.TwistDeg })
			{
				if (!FMath::IsFinite(Angle) || FMath::Abs(Angle) > 180.0)
				{
					OutError = FString::Printf(TEXT("poses.%s.%s: %g degrees is outside -180..180"), Entry.Key, SandboxPoseJoints[Index], Angle);
					return false;
				}
			}
		}
	}
	if (!CheckRange(Reactions.FlinchPoseFullNs, 1000.0, TEXT("reactions"), TEXT("flinch_pose_full_ns"), OutError) ||
		!CheckRange(Reactions.FlinchGiveDeg, 90.0, TEXT("reactions"), TEXT("flinch_give_deg"), OutError))
	{
		return false;
	}
	if (Reactions.FlinchPoseFullNs <= 0.0)
	{
		OutError = TEXT("reactions.flinch_pose_full_ns must be above 0");
		return false;
	}
	if (Thresholds.FallPelvisDropCm <= 0.0 || Thresholds.FallTiltDeg <= 0.0)
	{
		OutError = TEXT("thresholds: fall_pelvis_drop_cm and fall_tilt_deg must be above 0");
		return false;
	}
	if (Balance.StepReachCm <= Balance.RecoverableCm)
	{
		OutError = TEXT("balance.step_reach_cm must be larger than balance.recoverable_cm");
		return false;
	}

	const FName RigBones[] = { Rig.Pelvis, Rig.Chest, Rig.Head, Rig.FootLeft, Rig.FootRight };
	for (const FName Bone : RigBones)
	{
		if (Bone.IsNone())
		{
			OutError = TEXT("rig: every assist bone must be named");
			return false;
		}
	}
	return true;
}

ESandboxMuscleGroup FSandboxBodyTuning::GroupOfBone(FName ChildBone)
{
	const FString Name = ChildBone.ToString().ToLower();
	const auto StartsWithAny = [&Name](std::initializer_list<const TCHAR*> Prefixes)
	{
		for (const TCHAR* Prefix : Prefixes)
		{
			if (Name.StartsWith(Prefix))
			{
				return true;
			}
		}
		return false;
	};

	if (StartsWithAny({ TEXT("neck"), TEXT("head") }))
	{
		return ESandboxMuscleGroup::Neck;
	}
	// The collarbones belong to the trunk: the shoulder girdle is held by the muscles of the back, whatever the arm
	// does. (With the arms' soft muscles each collarbone sagged 10 degrees under its arm and laid the arm against the body.)
	if (StartsWithAny({ TEXT("upperarm"), TEXT("lowerarm"), TEXT("hand") }))
	{
		return ESandboxMuscleGroup::Arms;
	}
	if (StartsWithAny({ TEXT("thigh"), TEXT("calf"), TEXT("foot"), TEXT("ball") }))
	{
		return ESandboxMuscleGroup::Legs;
	}
	return ESandboxMuscleGroup::Spine;
}

const TCHAR* FSandboxBodyTuning::StateName(ESandboxBodyState State)
{
	switch (State)
	{
	case ESandboxBodyState::Flinch: return TEXT("flinch");
	case ESandboxBodyState::Stagger: return TEXT("stagger");
	case ESandboxBodyState::Falling: return TEXT("falling");
	case ESandboxBodyState::Lying: return TEXT("lying");
	case ESandboxBodyState::GetUp: return TEXT("get_up");
	default: return TEXT("standing");
	}
}

const TCHAR* FSandboxBodyTuning::GroupName(ESandboxMuscleGroup Group)
{
	switch (Group)
	{
	case ESandboxMuscleGroup::Neck: return TEXT("neck");
	case ESandboxMuscleGroup::Arms: return TEXT("arms");
	case ESandboxMuscleGroup::Legs: return TEXT("legs");
	default: return TEXT("spine");
	}
}
