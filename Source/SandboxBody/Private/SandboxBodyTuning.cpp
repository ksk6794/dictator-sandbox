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
			Visitor(FString::Printf(TEXT("muscles.%s.weight_share"), Group), Values.WeightShare);
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
		Muscle(TEXT("toes"), Tuning.Toes);
		Visitor(FString(TEXT("muscles.weight_follow_s")), Tuning.Weight.FollowS);
		Visitor(FString(TEXT("muscles.weight_legs_by")), Tuning.Weight.LegsBy);
		Visitor(FString(TEXT("balance.ankle_as_torque")), Tuning.Weight.AnkleAsTorque);
		for (int32 Kind = 0; Kind < SandboxJointKindCount; ++Kind)
		{
			Visitor(FString::Printf(TEXT("muscles.joints.%s.stiffness_scale"), SandboxJointKinds[Kind]), Tuning.Joints[Kind].StiffnessScale);
			Visitor(FString::Printf(TEXT("muscles.joints.%s.damping_scale"), SandboxJointKinds[Kind]), Tuning.Joints[Kind].DampingScale);
			Visitor(FString::Printf(TEXT("muscles.joints.%s.max_torque_scale"), SandboxJointKinds[Kind]), Tuning.Joints[Kind].MaxTorqueScale);
		}
		{
			static const TCHAR* const Levels[SandboxToneCount] = { TEXT("limp"), TEXT("relaxed"), TEXT("ready"), TEXT("braced") };
			static const TCHAR* const Groups[5] = { TEXT("spine"), TEXT("neck"), TEXT("arms"), TEXT("legs"), TEXT("toes") };
			for (int32 Level = 0; Level < SandboxToneCount; ++Level)
			{
				for (int32 Group = 0; Group < 5; ++Group)
				{
					Visitor(FString::Printf(TEXT("tone.%s.%s.stiffness"), Levels[Level], Groups[Group]), Tuning.Tone.Stiffness[Level][Group]);
					Visitor(FString::Printf(TEXT("tone.%s.%s.damping"), Levels[Level], Groups[Group]), Tuning.Tone.Damping[Level][Group]);
				}
			}
			for (int32 Group = 0; Group < 5; ++Group)
			{
				Visitor(FString::Printf(TEXT("tone.standing.%s"), Groups[Group]), Tuning.Tone.Standing[Group]);
			}
			Visitor(FString(TEXT("tone.rise_s")), Tuning.Tone.RiseS);
			Visitor(FString(TEXT("tone.brace_from_ns")), Tuning.Tone.BraceFromNs);
			Visitor(FString(TEXT("tone.brace_delay_s")), Tuning.Tone.BraceDelayS);
			Visitor(FString(TEXT("tone.brace_hold_s")), Tuning.Tone.BraceHoldS);
			Visitor(FString(TEXT("tone.fall_s")), Tuning.Tone.FallS);
		}
		Visitor(FString(TEXT("perception.enabled")), Tuning.Perception.Enabled);
		Visitor(FString(TEXT("perception.view_half_angle_deg")), Tuning.Perception.ViewHalfAngleDeg);
		Visitor(FString(TEXT("perception.notice_s")), Tuning.Perception.NoticeS);
		Visitor(FString(TEXT("perception.threat_speed_mps")), Tuning.Perception.ThreatSpeedMps);
		Visitor(FString(TEXT("perception.threat_time_s")), Tuning.Perception.ThreatTimeS);
		Visitor(FString(TEXT("perception.threat_near_cm")), Tuning.Perception.ThreatNearCm);
		Visitor(FString(TEXT("perception.head_radius_cm")), Tuning.Perception.HeadRadiusCm);
		Visitor(FString(TEXT("perception.trunk_radius_cm")), Tuning.Perception.TrunkRadiusCm);
		Visitor(FString(TEXT("perception.thing_radius_cm")), Tuning.Perception.ThingRadiusCm);
		Visitor(FString(TEXT("perception.gaze")), Tuning.Perception.Gaze);
		Visitor(FString(TEXT("perception.gaze_head_share")), Tuning.Perception.GazeHeadShare);
		Visitor(FString(TEXT("perception.gaze_speed_dps")), Tuning.Perception.GazeSpeedDps);
		Visitor(FString(TEXT("perception.gaze_max_yaw_deg")), Tuning.Perception.GazeMaxYawDeg);
		Visitor(FString(TEXT("perception.gaze_max_pitch_deg")), Tuning.Perception.GazeMaxPitchDeg);
		Visitor(FString(TEXT("perception.gaze_dwell_s")), Tuning.Perception.GazeDwellS);
		Visitor(FString(TEXT("perception.gaze_move_mps")), Tuning.Perception.GazeMoveMps);
		Visitor(FString(TEXT("perception.gaze_rise_s")), Tuning.Perception.GazeRiseS);
		Visitor(FString(TEXT("perception.anticipate")), Tuning.Perception.Anticipate);
		Visitor(FString(TEXT("perception.react_s")), Tuning.Perception.ReactS);
		Visitor(FString(TEXT("perception.anticipate_hold_s")), Tuning.Perception.AnticipateHoldS);
		Visitor(FString(TEXT("perception.anticipate_pose_s")), Tuning.Perception.AnticipatePoseS);
		Visitor(FString(TEXT("perception.guard")), Tuning.Perception.Guard);
		Visitor(FString(TEXT("perception.guard_time_s")), Tuning.Perception.GuardTimeS);
		Visitor(FString(TEXT("perception.guard_reach_cm")), Tuning.Perception.GuardReachCm);
		Visitor(FString(TEXT("perception.guard_hold_s")), Tuning.Perception.GuardHoldS);
		Visitor(FString(TEXT("perception.guard_blend_s")), Tuning.Perception.GuardBlendS);
		// R10 (docs/recon/r10_plan.md): fear (step 4), staying down and lowering (step 5).
		Visitor(FString(TEXT("behaviour.fear")), Tuning.Behaviour.Fear);
		Visitor(FString(TEXT("behaviour.fear_per_ns")), Tuning.Behaviour.FearPerNs);
		Visitor(FString(TEXT("behaviour.fear_per_threat")), Tuning.Behaviour.FearPerThreat);
		Visitor(FString(TEXT("behaviour.fear_fade_s")), Tuning.Behaviour.FearFadeS);
		Visitor(FString(TEXT("behaviour.stay_down")), Tuning.Behaviour.StayDown);
		Visitor(FString(TEXT("behaviour.fear_stay_down")), Tuning.Behaviour.FearStayDown);
		Visitor(FString(TEXT("behaviour.near_cm")), Tuning.Behaviour.NearCm);
		Visitor(FString(TEXT("behaviour.near_margin_cm")), Tuning.Behaviour.NearMarginCm);
		Visitor(FString(TEXT("behaviour.lower_rate")), Tuning.Behaviour.LowerRate);
		Visitor(FString(TEXT("behaviour.lower_end_cm")), Tuning.Behaviour.LowerEndCm);
		Visitor(FString(TEXT("behaviour.lower_turn_s")), Tuning.Behaviour.LowerTurnS);
		Visitor(FString(TEXT("behaviour.lower_max_mps")), Tuning.Behaviour.LowerMaxMps);
		Visitor(FString(TEXT("behaviour.lower_release_s")), Tuning.Behaviour.LowerReleaseS);
		Visitor(FString(TEXT("behaviour.commands")), Tuning.Behaviour.Commands);
		Visitor(FString(TEXT("behaviour.command_tries")), Tuning.Behaviour.CommandTries);
		Visitor(FString(TEXT("behaviour.command_react_s")), Tuning.Behaviour.CommandReactS);
		Visitor(FString(TEXT("behaviour.command_cover_react")), Tuning.Behaviour.CommandCoverReact);
		Visitor(FString(TEXT("behaviour.protect")), Tuning.Behaviour.Protect);
		Visitor(FString(TEXT("behaviour.protect_from_head_ns")), Tuning.Behaviour.ProtectFromHeadNs);
		Visitor(FString(TEXT("behaviour.protect_from_ns")), Tuning.Behaviour.ProtectFromNs);
		Visitor(FString(TEXT("behaviour.protect_hold_s")), Tuning.Behaviour.ProtectHoldS);
		Visitor(FString(TEXT("behaviour.protect_blend_s")), Tuning.Behaviour.ProtectBlendS);
		Visitor(FString(TEXT("behaviour.protect_held_leg")), Tuning.Behaviour.ProtectHeldLeg);
		Visitor(FString(TEXT("behaviour.protect_leg_wait_s")), Tuning.Behaviour.ProtectLegWaitS);
		Visitor(FString(TEXT("behaviour.protect_spine_nm_per_rad")), Tuning.Behaviour.ProtectSpineNmPerRad);
		Visitor(FString(TEXT("behaviour.protect_neck_nm_per_rad")), Tuning.Behaviour.ProtectNeckNmPerRad);
		Visitor(FString(TEXT("behaviour.protect_arms_nm_per_rad")), Tuning.Behaviour.ProtectArmsNmPerRad);
		Visitor(FString(TEXT("behaviour.protect_legs_nm_per_rad")), Tuning.Behaviour.ProtectLegsNmPerRad);
		Visitor(FString(TEXT("behaviour.protect_damping_s")), Tuning.Behaviour.ProtectDampingS);
		Visitor(FString(TEXT("behaviour.give_hand")), Tuning.Behaviour.GiveHand);
		Visitor(FString(TEXT("behaviour.offer_s")), Tuning.Behaviour.OfferS);
		Visitor(FString(TEXT("behaviour.offer_reach_cm")), Tuning.Behaviour.OfferReachCm);
		Visitor(FString(TEXT("behaviour.offer_open_grip")), Tuning.Behaviour.OfferOpenGrip);
		Visitor(FString(TEXT("behaviour.offer_blend_s")), Tuning.Behaviour.OfferBlendS);
		Visitor(FString(TEXT("behaviour.offer_arms_nm_per_rad")), Tuning.Behaviour.OfferArmsNmPerRad);
		Visitor(FString(TEXT("behaviour.offer_damping_s")), Tuning.Behaviour.OfferDampingS);
		Visitor(FString(TEXT("behaviour.offer_along_deg")), Tuning.Behaviour.OfferAlongDeg);
		Visitor(FString(TEXT("behaviour.struggle")), Tuning.Behaviour.Struggle);
		Visitor(FString(TEXT("behaviour.struggle_chance")), Tuning.Behaviour.StruggleChance);
		Visitor(FString(TEXT("behaviour.struggle_every_s")), Tuning.Behaviour.StruggleEveryS);
		Visitor(FString(TEXT("behaviour.struggle_attempt_s")), Tuning.Behaviour.StruggleAttemptS);
		Visitor(FString(TEXT("behaviour.struggle_vigour_min")), Tuning.Behaviour.StruggleVigourMin);
		Visitor(FString(TEXT("behaviour.struggle_pull_in_cm")), Tuning.Behaviour.StrugglePullInCm);
		Visitor(FString(TEXT("behaviour.struggle_leg_in_cm")), Tuning.Behaviour.StruggleLegInCm);
		Visitor(FString(TEXT("behaviour.struggle_arms_nm_per_rad")), Tuning.Behaviour.StruggleArmsNmPerRad);
		Visitor(FString(TEXT("behaviour.struggle_legs_nm_per_rad")), Tuning.Behaviour.StruggleLegsNmPerRad);
		Visitor(FString(TEXT("behaviour.struggle_blend_s")), Tuning.Behaviour.StruggleBlendS);
		Visitor(FString(TEXT("behaviour.struggle_from_n")), Tuning.Behaviour.StruggleFromN);
		Visitor(FString(TEXT("behaviour.struggle_max_torque_nm")), Tuning.Behaviour.StruggleMaxTorqueNm);
		Visitor(FString(TEXT("fall.aim")), Tuning.FallAim.Aim);
		Visitor(FString(TEXT("fall.reach_ahead_cm")), Tuning.FallAim.ReachAheadCm);
		Visitor(FString(TEXT("fall.hand_height_cm")), Tuning.FallAim.HandHeightCm);
		Visitor(FString(TEXT("fall.reach_blend_s")), Tuning.FallAim.ReachBlendS);
		Visitor(FString(TEXT("fall.way_follow_s")), Tuning.FallAim.WayFollowS);
		Visitor(FString(TEXT("fall.arm_reach_stiffness")), Tuning.FallAim.ReachStiffness);
		Visitor(FString(TEXT("fall.arm_reach_damping")), Tuning.FallAim.ReachDamping);
		Visitor(FString(TEXT("fall.reach_back")), Tuning.FallAim.ReachBack);
		Visitor(FString(TEXT("fall.arm_yield_stiffness")), Tuning.FallAim.YieldStiffness);
		Visitor(FString(TEXT("fall.arm_yield_damping")), Tuning.FallAim.YieldDamping);
		Visitor(FString(TEXT("fall.blend")), Tuning.FallAim.Blend);
		Visitor(FString(TEXT("fall.blend_share")), Tuning.FallAim.BlendShare);
		Visitor(FString(TEXT("fall.blend_follow_s")), Tuning.FallAim.BlendFollowS);
		Visitor(FString(TEXT("fall.blend_clip_s")), Tuning.FallAim.BlendClipS);
		Visitor(FString(TEXT("fall.arm_curve")), Tuning.FallAim.ArmCurve);
		Visitor(FString(TEXT("fall.arm_brace_from_s")), Tuning.FallAim.ArmBraceFromS);
		Visitor(FString(TEXT("fall.arm_far_stiffness")), Tuning.FallAim.ArmFarStiffness);
		Visitor(FString(TEXT("fall.arm_far_damping")), Tuning.FallAim.ArmFarDamping);
		Visitor(FString(TEXT("fall.arm_brace_stiffness")), Tuning.FallAim.ArmBraceStiffness);
		Visitor(FString(TEXT("fall.arm_brace_damping")), Tuning.FallAim.ArmBraceDamping);
		Visitor(FString(TEXT("fall.arm_touch_stiffness")), Tuning.FallAim.ArmTouchStiffness);
		Visitor(FString(TEXT("fall.arm_touch_damping")), Tuning.FallAim.ArmTouchDamping);
		Visitor(FString(TEXT("fall.catch")), Tuning.FallAim.Catch);
		Visitor(FString(TEXT("fall.catch_tilt_deg")), Tuning.FallAim.CatchTiltDeg);
		Visitor(FString(TEXT("fall.catch_ahead")), Tuning.FallAim.CatchAhead);
		Visitor(FString(TEXT("fall.catch_reach")), Tuning.FallAim.CatchReach);
		Visitor(FString(TEXT("fall.catch_follow_s")), Tuning.FallAim.CatchFollowS);
		Visitor(FString(TEXT("fall.catch_hold_s")), Tuning.FallAim.CatchHoldS);
		Visitor(FString(TEXT("fall.catch_blend_s")), Tuning.FallAim.CatchBlendS);
		Visitor(FString(TEXT("fall.catch_back")), Tuning.FallAim.CatchBack);
		Visitor(FString(TEXT("fall.catch_max_torque_scale")), Tuning.FallAim.CatchMaxTorqueScale);
		Visitor(FString(TEXT("fall.catch_turn_deg")), Tuning.FallAim.CatchTurnDeg);
		Visitor(FString(TEXT("fall.catch_joint_blend")), Tuning.FallAim.CatchJointBlend);
		Visitor(FString(TEXT("fall.catch_let_go_at_trunk")), Tuning.FallAim.CatchLetGoAtTrunk);
		Visitor(FString(TEXT("fall.head")), Tuning.FallAim.Head);
		Visitor(FString(TEXT("fall.head_stiffness")), Tuning.FallAim.HeadStiffness);
		Visitor(FString(TEXT("fall.head_damping")), Tuning.FallAim.HeadDamping);
		Visitor(FString(TEXT("fall.head_follow_s")), Tuning.FallAim.HeadFollowS);
		Visitor(FString(TEXT("fall.head_back_deg")), Tuning.FallAim.HeadBackDeg);
		Visitor(FString(TEXT("fall.head_forward_deg")), Tuning.FallAim.HeadForwardDeg);
		Visitor(FString(TEXT("fall.head_side_deg")), Tuning.FallAim.HeadSideDeg);
		Visitor(FString(TEXT("fall.curl")), Tuning.FallAim.Curl);
		Visitor(FString(TEXT("fall.curl_below_s")), Tuning.FallAim.CurlBelowS);
		Visitor(FString(TEXT("fall.curl_from_ns")), Tuning.FallAim.CurlFromNs);
		Visitor(FString(TEXT("fall.sit")), Tuning.FallAim.Sit);
		Visitor(FString(TEXT("fall.sit_from")), Tuning.FallAim.SitFrom);
		Visitor(FString(TEXT("fall.sit_blend_s")), Tuning.FallAim.SitBlendS);
		Visitor(FString(TEXT("fall.sit_leg_stiffness")), Tuning.FallAim.SitLegStiffness);
		Visitor(FString(TEXT("fall.turn")), Tuning.FallAim.Turn);
		Visitor(FString(TEXT("fall.turn_min_s")), Tuning.FallAim.TurnMinS);
		Visitor(FString(TEXT("fall.turn_blend_s")), Tuning.FallAim.TurnBlendS);
		Visitor(FString(TEXT("fall.turn_side_min")), Tuning.FallAim.TurnSideMin);
		Visitor(FString(TEXT("fall.palms")), Tuning.FallAim.Palms);
		Visitor(FString(TEXT("fall.palm_share")), Tuning.FallAim.PalmShare);
		Visitor(FString(TEXT("assist.internal_share")), Tuning.Weight.InternalShare);
		Assist(TEXT("pelvis"), Tuning.Pelvis);
		Assist(TEXT("chest"), Tuning.Chest);
		Assist(TEXT("head"), Tuning.Head);
		Assist(TEXT("feet"), Tuning.Feet);
		Visitor(FString(TEXT("solver.position_iterations")), Tuning.Solver.PositionIterations);
		Visitor(FString(TEXT("solver.velocity_iterations")), Tuning.Solver.VelocityIterations);
		Visitor(FString(TEXT("solver.projection_iterations")), Tuning.Solver.ProjectionIterations);
		Visitor(FString(TEXT("surface.friction")), Tuning.Surface.Friction);
		Visitor(FString(TEXT("surface.feet_friction")), Tuning.Surface.FeetFriction);
		Visitor(FString(TEXT("surface.restitution")), Tuning.Surface.Restitution);
		Visitor(FString(TEXT("surface.friction_combine")), Tuning.Surface.FrictionCombine);
		Visitor(FString(TEXT("surface.restitution_combine")), Tuning.Surface.RestitutionCombine);
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
		Visitor(FString(TEXT("balance.rest_step_s")), Tuning.Balance.RestStepS);
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
		Visitor(FString(TEXT("balance.step_width_min_cm")), Tuning.Balance.StepWidthMinCm);
		Visitor(FString(TEXT("balance.cross_offset_cm")), Tuning.Balance.CrossOffsetCm);
		Visitor(FString(TEXT("balance.step_ease_s")), Tuning.Balance.StepEaseS);
		Visitor(FString(TEXT("balance.lift_smooth")), Tuning.Balance.LiftSmooth);
		Visitor(FString(TEXT("balance.settle_smooth")), Tuning.Balance.SettleSmooth);
		Visitor(FString(TEXT("balance.release_smooth")), Tuning.Balance.ReleaseSmooth);
		Visitor(FString(TEXT("balance.land_ease_s")), Tuning.Balance.LandEaseS);
		Visitor(FString(TEXT("balance.fall_ease_s")), Tuning.Balance.FallEaseS);
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
		Visitor(FString(TEXT("reactions.double_over_ns")), Tuning.Reactions.DoubleOverNs);
		Visitor(FString(TEXT("reactions.double_over_s")), Tuning.Reactions.DoubleOverS);
		Visitor(FString(TEXT("reactions.double_over_in_s")), Tuning.Reactions.DoubleOverInS);
		Visitor(FString(TEXT("reactions.double_over_out_s")), Tuning.Reactions.DoubleOverOutS);
		Visitor(FString(TEXT("reactions.double_over_from")), Tuning.Reactions.DoubleOverFrom);
		Visitor(FString(TEXT("reactions.double_over_to")), Tuning.Reactions.DoubleOverTo);
		Visitor(FString(TEXT("reactions.knockdown_head_ns")), Tuning.Reactions.KnockdownHeadNs);
		Visitor(FString(TEXT("reactions.reflex_delay_s")), Tuning.Reactions.ReflexDelayS);
		Visitor(FString(TEXT("reactions.pressure_s")), Tuning.Reactions.PressureS);
		Visitor(FString(TEXT("reactions.held_push_s")), Tuning.Reactions.HeldPushS);
		Visitor(FString(TEXT("reactions.thing_support")), Tuning.Reactions.ThingSupport);
		Visitor(FString(TEXT("reactions.hold_felt")), Tuning.Reactions.HoldFelt);
		Visitor(FString(TEXT("reactions.thing_support_max_up_mps")), Tuning.Reactions.ThingSupportMaxUpMps);
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
		Pose(TEXT("double_over"), Tuning.Poses.DoubleOver);
		Visitor(FString(TEXT("poses.fall_blend_s")), Tuning.Poses.FallBlendS);
		Pose(TEXT("fall_back"), Tuning.Poses.FallBack);
		Pose(TEXT("fall_forward"), Tuning.Poses.FallForward);
		Pose(TEXT("fall_side"), Tuning.Poses.FallSide);
		Pose(TEXT("balance_back"), Tuning.Poses.BalanceBack);
		Pose(TEXT("balance_forward"), Tuning.Poses.BalanceForward);
		Pose(TEXT("balance_side"), Tuning.Poses.BalanceSide);
		Pose(TEXT("roll_to_back"), Tuning.Poses.RollToBack);
		Pose(TEXT("roll_to_front"), Tuning.Poses.RollToFront);
		Pose(TEXT("fall_curl"), Tuning.Poses.FallCurl);
		Pose(TEXT("fall_sit"), Tuning.Poses.FallSit);
		Pose(TEXT("fall_turn"), Tuning.Poses.FallTurn);
		Pose(TEXT("protect_head"), Tuning.Poses.ProtectHead);
		Pose(TEXT("protect_trunk"), Tuning.Poses.ProtectTrunk);
		Pose(TEXT("protect_groin"), Tuning.Poses.ProtectGroin);
		Pose(TEXT("protect_leg"), Tuning.Poses.ProtectLeg);
		Visitor(FString(TEXT("poses.fingers.weight")), Tuning.Poses.Fingers.Weight);
		Visitor(FString(TEXT("poses.fingers.blend_s")), Tuning.Poses.Fingers.BlendS);
		const auto Fingers = [&Visitor](const TCHAR* Name, auto& Values)
		{
			static const TCHAR* const FingerNames[5] = { TEXT("thumb"), TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky") };
			for (int32 Finger = 0; Finger < 5; ++Finger)
			{
				Visitor(FString::Printf(TEXT("poses.fingers.%s.%s_deg"), Name, FingerNames[Finger]), Values.CurlDeg[Finger]);
			}
			Visitor(FString::Printf(TEXT("poses.fingers.%s.spread_deg"), Name), Values.SpreadDeg);
		};
		Fingers(TEXT("rest"), Tuning.Poses.Fingers.Rest);
		Fingers(TEXT("fist"), Tuning.Poses.Fingers.Fist);
		Fingers(TEXT("splayed"), Tuning.Poses.Fingers.Splayed);
		Fingers(TEXT("flat"), Tuning.Poses.Fingers.Flat);
		Visitor(FString(TEXT("poses.idle.weight")), Tuning.Poses.IdleWeight);
		Visitor(FString(TEXT("poses.idle.sway_weight")), Tuning.Poses.IdleSwayWeight);
		Visitor(FString(TEXT("getup.rest_s")), Tuning.GetUp.RestS);
		Visitor(FString(TEXT("getup.ease_s")), Tuning.GetUp.EaseS);
		Visitor(FString(TEXT("getup.rate")), Tuning.GetUp.Rate);
		Visitor(FString(TEXT("getup.end_s")), Tuning.GetUp.EndS);
		Visitor(FString(TEXT("getup.hold_force_n")), Tuning.GetUp.HoldForceN);
		Visitor(FString(TEXT("getup.stall_s")), Tuning.GetUp.StallS);
		Visitor(FString(TEXT("getup.wait_s")), Tuning.GetUp.WaitS);
		Visitor(FString(TEXT("getup.lost_cm")), Tuning.GetUp.LostCm);
		Visitor(FString(TEXT("getup.upright_deg")), Tuning.GetUp.UprightDeg);
		Visitor(FString(TEXT("getup.rest_per_ns_s")), Tuning.GetUp.RestPerNsS);
		Visitor(FString(TEXT("getup.rest_max_s")), Tuning.GetUp.RestMaxS);
		Visitor(FString(TEXT("getup.carry_n_per_m_per_kg")), Tuning.GetUp.CarryNPerMPerKg);
		Visitor(FString(TEXT("getup.carry_nm_per_rad_per_kg")), Tuning.GetUp.CarryNmPerRadPerKg);
		Visitor(FString(TEXT("getup.carry_damping_s")), Tuning.GetUp.CarryDampingS);
		Visitor(FString(TEXT("getup.carry_max_weights")), Tuning.GetUp.CarryMaxWeights);
		Visitor(FString(TEXT("getup.carry_max_up_weights")), Tuning.GetUp.CarryMaxUpWeights);
		Visitor(FString(TEXT("getup.carry_max_nm_per_kg")), Tuning.GetUp.CarryMaxNmPerKg);
		Visitor(FString(TEXT("getup.carry_release_s")), Tuning.GetUp.CarryReleaseS);
		Visitor(FString(TEXT("getup.carry_yield_ns")), Tuning.GetUp.CarryYieldNs);
		Visitor(FString(TEXT("getup.hold_by_body")), Tuning.GetUp.HoldByBody);
		Visitor(FString(TEXT("getup.hold_chain")), Tuning.GetUp.HoldChain);
		Visitor(FString(TEXT("getup.hold_chain_falloff")), Tuning.GetUp.HoldChainFalloff);
		Visitor(FString(TEXT("getup.hold_chain_full_n")), Tuning.GetUp.HoldChainFullN);
		Visitor(FString(TEXT("getup.hold_chain_share")), Tuning.GetUp.HoldChainShare);
		Visitor(FString(TEXT("getup.hold_rest_full_n")), Tuning.GetUp.HoldRestFullN);
		Visitor(FString(TEXT("getup.hold_slow_from_n")), Tuning.GetUp.HoldSlowFromN);
		Visitor(FString(TEXT("getup.hold_sum")), Tuning.GetUp.HoldSum);
		Visitor(FString(TEXT("getup.hold_press_pace")), Tuning.GetUp.HoldPressPace);
		Visitor(FString(TEXT("getup.hit_by_impulse")), Tuning.GetUp.HitByImpulse);
		Visitor(FString(TEXT("getup.hit_back_s")), Tuning.GetUp.HitBackS);
		Visitor(FString(TEXT("getup.hit_overflow")), Tuning.GetUp.HitOverflow);
		Visitor(FString(TEXT("getup.start_blend_s")), Tuning.GetUp.StartBlendS);
		Visitor(FString(TEXT("getup.match_window_s")), Tuning.GetUp.MatchWindowS);
		Visitor(FString(TEXT("getup.match_pelvis")), Tuning.GetUp.MatchPelvis);
		Visitor(FString(TEXT("getup.match_trunk")), Tuning.GetUp.MatchTrunk);
		Visitor(FString(TEXT("getup.match_hands")), Tuning.GetUp.MatchHands);
		Visitor(FString(TEXT("getup.match_elbows")), Tuning.GetUp.MatchElbows);
		Visitor(FString(TEXT("getup.match_feet")), Tuning.GetUp.MatchFeet);
		Visitor(FString(TEXT("getup.match_knees")), Tuning.GetUp.MatchKnees);
		Visitor(FString(TEXT("getup.match_up_cm_per_deg")), Tuning.GetUp.MatchUpCmPerDeg);
		Visitor(FString(TEXT("getup.match_given_up_cm")), Tuning.GetUp.MatchGivenUpCm);
		Visitor(FString(TEXT("getup.match_late_cm_per_s")), Tuning.GetUp.MatchLateCmPerS);
		Visitor(FString(TEXT("getup.roll")), Tuning.GetUp.Roll);
		Visitor(FString(TEXT("getup.roll_side_deg")), Tuning.GetUp.RollSideDeg);
		Visitor(FString(TEXT("getup.roll_done_deg")), Tuning.GetUp.RollDoneDeg);
		Visitor(FString(TEXT("getup.roll_s")), Tuning.GetUp.RollS);
		Visitor(FString(TEXT("getup.roll_way")), Tuning.GetUp.RollWay);
		Visitor(FString(TEXT("getup.roll_blend_s")), Tuning.GetUp.RollBlendS);
		Visitor(FString(TEXT("getup.roll_push")), Tuning.GetUp.RollPush);
		Visitor(FString(TEXT("getup.fit_surface")), Tuning.GetUp.FitSurface);
		Visitor(FString(TEXT("getup.fit_floor")), Tuning.GetUp.FitFloor);
		Visitor(FString(TEXT("getup.fit_ease_s")), Tuning.GetUp.FitEaseS);
		Visitor(FString(TEXT("getup.fit_free_z")), Tuning.GetUp.FitFreeZ);
		Visitor(FString(TEXT("getup.drag")), Tuning.GetUp.Drag);
		Visitor(FString(TEXT("getup.drag_force_n")), Tuning.GetUp.DragForceN);
		Visitor(FString(TEXT("getup.drag_s")), Tuning.GetUp.DragS);
		Visitor(FString(TEXT("getup.drag_mps")), Tuning.GetUp.DragMps);
		Visitor(FString(TEXT("getup.drag_free_s")), Tuning.GetUp.DragFreeS);
		Visitor(FString(TEXT("getup.arm_answer")), Tuning.GetUp.ArmAnswer);
		Visitor(FString(TEXT("getup.arm_yield_scale")), Tuning.GetUp.ArmYieldScale);
		Visitor(FString(TEXT("getup.arm_reach_cm")), Tuning.GetUp.ArmReachCm);
		Visitor(FString(TEXT("getup.arm_pull_scale")), Tuning.GetUp.ArmPullScale);
		Visitor(FString(TEXT("getup.arm_pull_in_cm")), Tuning.GetUp.ArmPullInCm);
		Visitor(FString(TEXT("getup.arm_follow_s")), Tuning.GetUp.ArmFollowS);
		Visitor(FString(TEXT("motion.contact_cm")), Tuning.Motion.ContactCm);
		Visitor(FString(TEXT("motion.contact_mps")), Tuning.Motion.ContactMps);
	}

	const TCHAR* const IdleAnimationKey = TEXT("poses.idle.animation");

	/** Calls Visitor(name, text) for every clip of the fall's blend, and the motion library's list (the get-up's clips are its). */
	template <typename TuningType, typename VisitorType>
	void VisitClips(TuningType& Tuning, VisitorType&& Visitor)
	{
		Visitor(TEXT("motion.library"), Tuning.Motion.Library);
		Visitor(TEXT("fall.clip_reach_forward"), Tuning.FallAim.ClipReachForward);
		Visitor(TEXT("fall.clip_reach_back"), Tuning.FallAim.ClipReachBack);
		Visitor(TEXT("fall.clip_reach_left"), Tuning.FallAim.ClipReachLeft);
		Visitor(TEXT("fall.clip_reach_right"), Tuning.FallAim.ClipReachRight);
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

const TCHAR* const SandboxJointKinds[SandboxJointKindCount] = {
	TEXT("spine_02"), TEXT("spine_04"), TEXT("neck_01"), TEXT("head"), TEXT("clavicle"), TEXT("upperarm"), TEXT("lowerarm"), TEXT("hand"),
	TEXT("thigh"), TEXT("calf"), TEXT("foot"), TEXT("ball") };

const FSandboxJointScale* FSandboxBodyTuning::JointScale(FName ChildBone) const
{
	FString Kind = ChildBone.ToString();
	if (Kind.EndsWith(TEXT("_l")) || Kind.EndsWith(TEXT("_r")))
	{
		Kind = Kind.LeftChop(2);
	}
	for (int32 Index = 0; Index < SandboxJointKindCount; ++Index)
	{
		if (Kind == SandboxJointKinds[Index])
		{
			return &Joints[Index];
		}
	}
	return nullptr;
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
		Visitor(TEXT("rig.head_if_own"), Rig.HeadIfOwn);
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
	VisitClips(Names, [&Params](const TCHAR* Key, FString&) { Params.DeclareString(Key); });
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
	VisitClips(OutTuning, [&](const TCHAR* Key, FString& Path)
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
	case ESandboxMuscleGroup::Toes: return Toes;
	default: return Spine;
	}
}

bool FSandboxBodyTuning::Validate(FString& OutError) const
{
	const TPair<const TCHAR*, const FSandboxMuscleTuning*> Muscles[] = {
		{ TEXT("muscles.spine"), &Spine }, { TEXT("muscles.neck"), &Neck }, { TEXT("muscles.arms"), &Arms }, { TEXT("muscles.legs"), &Legs }, { TEXT("muscles.toes"), &Toes } };
	for (const TPair<const TCHAR*, const FSandboxMuscleTuning*>& Entry : Muscles)
	{
		if (!CheckRange(Entry.Value->StiffnessNmPerRad, MaxAngularStiffness, Entry.Key, TEXT("stiffness_nm_per_rad"), OutError) ||
			!CheckRange(Entry.Value->DampingS, MaxDampingS, Entry.Key, TEXT("damping_s"), OutError) ||
			!CheckRange(Entry.Value->MaxTorqueNm, MaxTorque, Entry.Key, TEXT("max_torque_nm"), OutError) ||
			!CheckRange(Entry.Value->PoseVelocityShare, 1.0, Entry.Key, TEXT("pose_velocity_share"), OutError) ||
			!CheckRange(Entry.Value->WeightShare, 1.5, Entry.Key, TEXT("weight_share"), OutError))
		{
			return false;
		}
	}

	for (int32 Kind = 0; Kind < SandboxJointKindCount; ++Kind)
	{
		if (!CheckRange(Joints[Kind].StiffnessScale, 20.0, SandboxJointKinds[Kind], TEXT("stiffness_scale"), OutError) ||
			!CheckRange(Joints[Kind].DampingScale, 20.0, SandboxJointKinds[Kind], TEXT("damping_scale"), OutError) ||
			!CheckRange(Joints[Kind].MaxTorqueScale, 20.0, SandboxJointKinds[Kind], TEXT("max_torque_scale"), OutError))
		{
			return false;
		}
	}
	for (int32 Level = 0; Level < SandboxToneCount; ++Level)
	{
		for (int32 Group = 0; Group < 5; ++Group)
		{
			if (!CheckRange(Tone.Stiffness[Level][Group], 20.0, TEXT("tone"), TEXT("stiffness"), OutError) || !CheckRange(Tone.Damping[Level][Group], 20.0, TEXT("tone"), TEXT("damping"), OutError))
			{
				return false;
			}
		}
	}
	for (int32 Group = 0; Group < 5; ++Group)
	{
		if (!CheckRange(Tone.Standing[Group], 3.0, TEXT("tone.standing"), TEXT("level"), OutError) || Tone.Standing[Group] != FMath::RoundToDouble(Tone.Standing[Group]))
		{
			OutError = TEXT("tone.standing.<group> must be a level: 0 limp, 1 relaxed, 2 ready, 3 braced");
			return false;
		}
	}
	if (!CheckRange(Tone.RiseS, 10.0, TEXT("tone"), TEXT("rise_s"), OutError) || !CheckRange(Tone.FallS, 10.0, TEXT("tone"), TEXT("fall_s"), OutError) ||
		!CheckRange(Tone.BraceFromNs, 1000.0, TEXT("tone"), TEXT("brace_from_ns"), OutError) || !CheckRange(Tone.BraceDelayS, 2.0, TEXT("tone"), TEXT("brace_delay_s"), OutError) ||
		!CheckRange(Tone.BraceHoldS, 30.0, TEXT("tone"), TEXT("brace_hold_s"), OutError))
	{
		return false;
	}
	if (!CheckRange(Perception.Enabled, 1.0, TEXT("perception"), TEXT("enabled"), OutError) || !CheckRange(Perception.ViewHalfAngleDeg, 180.0, TEXT("perception"), TEXT("view_half_angle_deg"), OutError) ||
		!CheckRange(Perception.NoticeS, 10.0, TEXT("perception"), TEXT("notice_s"), OutError) || !CheckRange(Perception.ThreatSpeedMps, 50.0, TEXT("perception"), TEXT("threat_speed_mps"), OutError) ||
		!CheckRange(Perception.ThreatTimeS, 10.0, TEXT("perception"), TEXT("threat_time_s"), OutError) || !CheckRange(Perception.ThreatNearCm, 200.0, TEXT("perception"), TEXT("threat_near_cm"), OutError) ||
		!CheckRange(Perception.HeadRadiusCm, 50.0, TEXT("perception"), TEXT("head_radius_cm"), OutError) || !CheckRange(Perception.TrunkRadiusCm, 100.0, TEXT("perception"), TEXT("trunk_radius_cm"), OutError) ||
		!CheckRange(Perception.ThingRadiusCm, 100.0, TEXT("perception"), TEXT("thing_radius_cm"), OutError) ||
		!CheckRange(Perception.Gaze, 1.0, TEXT("perception"), TEXT("gaze"), OutError) || !CheckRange(Perception.GazeHeadShare, 1.0, TEXT("perception"), TEXT("gaze_head_share"), OutError) ||
		!CheckRange(Perception.GazeSpeedDps, 2000.0, TEXT("perception"), TEXT("gaze_speed_dps"), OutError) || !CheckRange(Perception.GazeMaxYawDeg, 90.0, TEXT("perception"), TEXT("gaze_max_yaw_deg"), OutError) ||
		!CheckRange(Perception.GazeMaxPitchDeg, 90.0, TEXT("perception"), TEXT("gaze_max_pitch_deg"), OutError) || !CheckRange(Perception.GazeDwellS, 10.0, TEXT("perception"), TEXT("gaze_dwell_s"), OutError) ||
		!CheckRange(Perception.GazeMoveMps, 20.0, TEXT("perception"), TEXT("gaze_move_mps"), OutError) ||
		!CheckRange(Perception.GazeRiseS, 2.0, TEXT("perception"), TEXT("gaze_rise_s"), OutError) ||
		!CheckRange(Perception.Anticipate, 1.0, TEXT("perception"), TEXT("anticipate"), OutError) || !CheckRange(Perception.ReactS, 2.0, TEXT("perception"), TEXT("react_s"), OutError) ||
		!CheckRange(Perception.AnticipateHoldS, 10.0, TEXT("perception"), TEXT("anticipate_hold_s"), OutError) || !CheckRange(Perception.AnticipatePoseS, 2.0, TEXT("perception"), TEXT("anticipate_pose_s"), OutError))
	{
		return false;
	}
	if (!CheckRange(Perception.Guard, 1.0, TEXT("perception"), TEXT("guard"), OutError) || !CheckRange(Perception.GuardTimeS, 5.0, TEXT("perception"), TEXT("guard_time_s"), OutError) ||
		!CheckRange(Perception.GuardReachCm, 80.0, TEXT("perception"), TEXT("guard_reach_cm"), OutError) || !CheckRange(Perception.GuardHoldS, 10.0, TEXT("perception"), TEXT("guard_hold_s"), OutError) ||
		!CheckRange(Perception.GuardBlendS, 2.0, TEXT("perception"), TEXT("guard_blend_s"), OutError))
	{
		return false;
	}
	if (Perception.Guard > 0.5 && Perception.GuardBlendS <= 0.0)
	{
		OutError = TEXT("perception.guard_blend_s must be above 0 while perception.guard is on");
		return false;
	}
	if (Perception.Anticipate > 0.5 && Perception.AnticipatePoseS <= 0.0)
	{
		// At 0 the whole of the flinch's pose would arrive in one frame.
		OutError = TEXT("perception.anticipate_pose_s must be above 0 while perception.anticipate is on");
		return false;
	}
	// R10: fear and staying down.
	if (!CheckRange(Behaviour.Fear, 1.0, TEXT("behaviour"), TEXT("fear"), OutError) || !CheckRange(Behaviour.FearPerNs, 1.0, TEXT("behaviour"), TEXT("fear_per_ns"), OutError) ||
		!CheckRange(Behaviour.FearPerThreat, 1.0, TEXT("behaviour"), TEXT("fear_per_threat"), OutError) || !CheckRange(Behaviour.FearFadeS, 600.0, TEXT("behaviour"), TEXT("fear_fade_s"), OutError) ||
		!CheckRange(Behaviour.StayDown, 1.0, TEXT("behaviour"), TEXT("stay_down"), OutError) || !CheckRange(Behaviour.FearStayDown, 1.0, TEXT("behaviour"), TEXT("fear_stay_down"), OutError) ||
		!CheckRange(Behaviour.NearCm, 1000.0, TEXT("behaviour"), TEXT("near_cm"), OutError) || !CheckRange(Behaviour.NearMarginCm, 500.0, TEXT("behaviour"), TEXT("near_margin_cm"), OutError) ||
		!CheckRange(Behaviour.LowerRate, 2.0, TEXT("behaviour"), TEXT("lower_rate"), OutError) || !CheckRange(Behaviour.ProtectBlendS, 5.0, TEXT("behaviour"), TEXT("protect_blend_s"), OutError) ||
		!CheckRange(Behaviour.LowerEndCm, 100.0, TEXT("behaviour"), TEXT("lower_end_cm"), OutError) ||
		!CheckRange(Behaviour.LowerTurnS, 2.0, TEXT("behaviour"), TEXT("lower_turn_s"), OutError) ||
		!CheckRange(Behaviour.LowerMaxMps, 5.0, TEXT("behaviour"), TEXT("lower_max_mps"), OutError) ||
		!CheckRange(Behaviour.LowerReleaseS, 5.0, TEXT("behaviour"), TEXT("lower_release_s"), OutError) ||
		!CheckRange(Behaviour.Commands, 1.0, TEXT("behaviour"), TEXT("commands"), OutError) ||
		!CheckRange(Behaviour.CommandTries, 100.0, TEXT("behaviour"), TEXT("command_tries"), OutError) ||
		!CheckRange(Behaviour.CommandReactS, 5.0, TEXT("behaviour"), TEXT("command_react_s"), OutError) ||
		!CheckRange(Behaviour.CommandCoverReact, 1.0, TEXT("behaviour"), TEXT("command_cover_react"), OutError) ||
		!CheckRange(Behaviour.Protect, 1.0, TEXT("behaviour"), TEXT("protect"), OutError) ||
		!CheckRange(Behaviour.ProtectFromHeadNs, 500.0, TEXT("behaviour"), TEXT("protect_from_head_ns"), OutError) ||
		!CheckRange(Behaviour.ProtectFromNs, 1000.0, TEXT("behaviour"), TEXT("protect_from_ns"), OutError) ||
		!CheckRange(Behaviour.ProtectHoldS, 30.0, TEXT("behaviour"), TEXT("protect_hold_s"), OutError) ||
		!CheckRange(Behaviour.ProtectHeldLeg, 1.0, TEXT("behaviour"), TEXT("protect_held_leg"), OutError) ||
		!CheckRange(Behaviour.ProtectLegWaitS, 1.0, TEXT("behaviour"), TEXT("protect_leg_wait_s"), OutError) ||
		!CheckRange(Behaviour.ProtectSpineNmPerRad, 3000.0, TEXT("behaviour"), TEXT("protect_spine_nm_per_rad"), OutError) ||
		!CheckRange(Behaviour.ProtectNeckNmPerRad, 3000.0, TEXT("behaviour"), TEXT("protect_neck_nm_per_rad"), OutError) ||
		!CheckRange(Behaviour.ProtectArmsNmPerRad, 3000.0, TEXT("behaviour"), TEXT("protect_arms_nm_per_rad"), OutError) ||
		!CheckRange(Behaviour.ProtectLegsNmPerRad, 3000.0, TEXT("behaviour"), TEXT("protect_legs_nm_per_rad"), OutError) ||
		!CheckRange(Behaviour.ProtectDampingS, 2.0, TEXT("behaviour"), TEXT("protect_damping_s"), OutError) ||
		!CheckRange(Behaviour.GiveHand, 1.0, TEXT("behaviour"), TEXT("give_hand"), OutError) ||
		!CheckRange(Behaviour.OfferS, 60.0, TEXT("behaviour"), TEXT("offer_s"), OutError) ||
		!CheckRange(Behaviour.OfferReachCm, 300.0, TEXT("behaviour"), TEXT("offer_reach_cm"), OutError) ||
		!CheckRange(Behaviour.OfferOpenGrip, 1.0, TEXT("behaviour"), TEXT("offer_open_grip"), OutError) ||
		!CheckRange(Behaviour.OfferBlendS, 5.0, TEXT("behaviour"), TEXT("offer_blend_s"), OutError) ||
		!CheckRange(Behaviour.OfferArmsNmPerRad, 3000.0, TEXT("behaviour"), TEXT("offer_arms_nm_per_rad"), OutError) ||
		!CheckRange(Behaviour.OfferDampingS, 2.0, TEXT("behaviour"), TEXT("offer_damping_s"), OutError) ||
		!CheckRange(Behaviour.OfferAlongDeg, 90.0, TEXT("behaviour"), TEXT("offer_along_deg"), OutError) ||
		!CheckRange(Behaviour.Struggle, 1.0, TEXT("behaviour"), TEXT("struggle"), OutError) ||
		!CheckRange(Behaviour.StruggleChance, 1.0, TEXT("behaviour"), TEXT("struggle_chance"), OutError) ||
		!CheckRange(Behaviour.StruggleEveryS, 30.0, TEXT("behaviour"), TEXT("struggle_every_s"), OutError) ||
		!CheckRange(Behaviour.StruggleAttemptS, 10.0, TEXT("behaviour"), TEXT("struggle_attempt_s"), OutError) ||
		!CheckRange(Behaviour.StruggleVigourMin, 1.0, TEXT("behaviour"), TEXT("struggle_vigour_min"), OutError) ||
		!CheckRange(Behaviour.StrugglePullInCm, 100.0, TEXT("behaviour"), TEXT("struggle_pull_in_cm"), OutError) ||
		!CheckRange(Behaviour.StruggleLegInCm, 150.0, TEXT("behaviour"), TEXT("struggle_leg_in_cm"), OutError) ||
		!CheckRange(Behaviour.StruggleArmsNmPerRad, 5000.0, TEXT("behaviour"), TEXT("struggle_arms_nm_per_rad"), OutError) ||
		!CheckRange(Behaviour.StruggleLegsNmPerRad, 5000.0, TEXT("behaviour"), TEXT("struggle_legs_nm_per_rad"), OutError) ||
		!CheckRange(Behaviour.StruggleBlendS, 2.0, TEXT("behaviour"), TEXT("struggle_blend_s"), OutError) ||
		!CheckRange(Behaviour.StruggleFromN, 5000.0, TEXT("behaviour"), TEXT("struggle_from_n"), OutError) ||
		!CheckRange(Behaviour.StruggleMaxTorqueNm, 5000.0, TEXT("behaviour"), TEXT("struggle_max_torque_nm"), OutError))
	{
		return false;
	}
	if (Behaviour.StayDown > 0.5 && (Behaviour.Fear <= 0.5 || Behaviour.FearFadeS <= 0.0 || Behaviour.ProtectBlendS < 0.02))
	{
		// Without fear nothing is ever afraid; a pose that comes in faster than 0.02 s is a jump of the target (C25).
		OutError = TEXT("behaviour.stay_down needs behaviour.fear on, fear_fade_s above 0 and protect_blend_s of at least 0.02 s");
		return false;
	}
	if (Behaviour.Protect > 0.5 && (Behaviour.ProtectBlendS < 0.02 || Behaviour.ProtectFromHeadNs <= 0.0 || Behaviour.ProtectFromNs <= 0.0))
	{
		OutError = TEXT("behaviour.protect needs protect_blend_s of at least 0.02 s and protect_from_head_ns and protect_from_ns above 0");
		return false;
	}
	if (Behaviour.Struggle > 0.5 && (Behaviour.StruggleEveryS <= 0.0 || Behaviour.StruggleAttemptS <= 0.0 || Behaviour.StruggleBlendS < 0.02))
	{
		// At 0 a drag's run ended in the tick it began and was drawn again every tick (the independent reading of the window's diff).
		OutError = TEXT("behaviour.struggle needs struggle_every_s and struggle_attempt_s above 0 and struggle_blend_s of at least 0.02 s");
		return false;
	}
	if (!CheckRange(FallAim.Aim, 1.0, TEXT("fall"), TEXT("aim"), OutError) || !CheckRange(FallAim.ReachAheadCm, 200.0, TEXT("fall"), TEXT("reach_ahead_cm"), OutError) ||
		!CheckRange(FallAim.HandHeightCm, 50.0, TEXT("fall"), TEXT("hand_height_cm"), OutError) || !CheckRange(FallAim.ReachBlendS, 2.0, TEXT("fall"), TEXT("reach_blend_s"), OutError) ||
		!CheckRange(FallAim.WayFollowS, 2.0, TEXT("fall"), TEXT("way_follow_s"), OutError) || !CheckRange(FallAim.ReachStiffness, 20.0, TEXT("fall"), TEXT("arm_reach_stiffness"), OutError) ||
		!CheckRange(FallAim.ReachDamping, 20.0, TEXT("fall"), TEXT("arm_reach_damping"), OutError) || !CheckRange(FallAim.YieldStiffness, 20.0, TEXT("fall"), TEXT("arm_yield_stiffness"), OutError) ||
		!CheckRange(FallAim.YieldDamping, 20.0, TEXT("fall"), TEXT("arm_yield_damping"), OutError) ||
		!CheckRange(FallAim.ReachBack, 1.0, TEXT("fall"), TEXT("reach_back"), OutError) ||
		!CheckRange(FallAim.Blend, 1.0, TEXT("fall"), TEXT("blend"), OutError) || !CheckRange(FallAim.BlendShare, 1.0, TEXT("fall"), TEXT("blend_share"), OutError) ||
		!CheckRange(FallAim.BlendFollowS, 2.0, TEXT("fall"), TEXT("blend_follow_s"), OutError) || !CheckRange(FallAim.BlendClipS, 10.0, TEXT("fall"), TEXT("blend_clip_s"), OutError) ||
		!CheckRange(FallAim.ArmCurve, 1.0, TEXT("fall"), TEXT("arm_curve"), OutError) || !CheckRange(FallAim.ArmBraceFromS, 2.0, TEXT("fall"), TEXT("arm_brace_from_s"), OutError) ||
		!CheckRange(FallAim.ArmFarStiffness, 20.0, TEXT("fall"), TEXT("arm_far_stiffness"), OutError) || !CheckRange(FallAim.ArmFarDamping, 20.0, TEXT("fall"), TEXT("arm_far_damping"), OutError) ||
		!CheckRange(FallAim.ArmBraceStiffness, 20.0, TEXT("fall"), TEXT("arm_brace_stiffness"), OutError) || !CheckRange(FallAim.ArmBraceDamping, 20.0, TEXT("fall"), TEXT("arm_brace_damping"), OutError) ||
		!CheckRange(FallAim.ArmTouchStiffness, 20.0, TEXT("fall"), TEXT("arm_touch_stiffness"), OutError) || !CheckRange(FallAim.ArmTouchDamping, 20.0, TEXT("fall"), TEXT("arm_touch_damping"), OutError) ||
		!CheckRange(FallAim.Catch, 1.0, TEXT("fall"), TEXT("catch"), OutError) || !CheckRange(FallAim.CatchTiltDeg, 120.0, TEXT("fall"), TEXT("catch_tilt_deg"), OutError) ||
		!CheckRange(FallAim.CatchAhead, 5.0, TEXT("fall"), TEXT("catch_ahead"), OutError) || !CheckRange(FallAim.CatchReach, 1.0, TEXT("fall"), TEXT("catch_reach"), OutError) ||
		!CheckRange(FallAim.CatchFollowS, 2.0, TEXT("fall"), TEXT("catch_follow_s"), OutError) || !CheckRange(FallAim.CatchHoldS, 2.0, TEXT("fall"), TEXT("catch_hold_s"), OutError) ||
		!CheckRange(FallAim.CatchBlendS, 2.0, TEXT("fall"), TEXT("catch_blend_s"), OutError) || !CheckRange(FallAim.CatchBack, 1.0, TEXT("fall"), TEXT("catch_back"), OutError) ||
		!CheckRange(FallAim.CatchMaxTorqueScale, 5.0, TEXT("fall"), TEXT("catch_max_torque_scale"), OutError) ||
		!CheckRange(FallAim.CatchTurnDeg, 180.0, TEXT("fall"), TEXT("catch_turn_deg"), OutError) ||
		!CheckRange(FallAim.CatchJointBlend, 1.0, TEXT("fall"), TEXT("catch_joint_blend"), OutError) ||
		!CheckRange(FallAim.CatchLetGoAtTrunk, 1.0, TEXT("fall"), TEXT("catch_let_go_at_trunk"), OutError) ||
		!CheckRange(FallAim.Head, 1.0, TEXT("fall"), TEXT("head"), OutError) || !CheckRange(FallAim.HeadStiffness, 20.0, TEXT("fall"), TEXT("head_stiffness"), OutError) ||
		!CheckRange(FallAim.HeadDamping, 20.0, TEXT("fall"), TEXT("head_damping"), OutError) || !CheckRange(FallAim.HeadFollowS, 2.0, TEXT("fall"), TEXT("head_follow_s"), OutError) ||
		!CheckRange(FallAim.HeadBackDeg, 90.0, TEXT("fall"), TEXT("head_back_deg"), OutError) || !CheckRange(FallAim.HeadForwardDeg, 90.0, TEXT("fall"), TEXT("head_forward_deg"), OutError) ||
		!CheckRange(FallAim.HeadSideDeg, 90.0, TEXT("fall"), TEXT("head_side_deg"), OutError) ||
		!CheckRange(FallAim.Curl, 1.0, TEXT("fall"), TEXT("curl"), OutError) || !CheckRange(FallAim.CurlBelowS, 2.0, TEXT("fall"), TEXT("curl_below_s"), OutError) ||
		!CheckRange(FallAim.Sit, 1.0, TEXT("fall"), TEXT("sit"), OutError) || !CheckRange(FallAim.SitFrom, 1.0, TEXT("fall"), TEXT("sit_from"), OutError) ||
		!CheckRange(FallAim.SitBlendS, 2.0, TEXT("fall"), TEXT("sit_blend_s"), OutError) ||
		!CheckRange(FallAim.Turn, 1.0, TEXT("fall"), TEXT("turn"), OutError) || !CheckRange(FallAim.TurnMinS, 2.0, TEXT("fall"), TEXT("turn_min_s"), OutError) ||
		!CheckRange(FallAim.TurnBlendS, 2.0, TEXT("fall"), TEXT("turn_blend_s"), OutError) ||
		!CheckRange(FallAim.TurnSideMin, 1.0, TEXT("fall"), TEXT("turn_side_min"), OutError) ||
		!CheckRange(FallAim.CurlFromNs, 1000.0, TEXT("fall"), TEXT("curl_from_ns"), OutError) ||
		!CheckRange(FallAim.SitLegStiffness, 20.0, TEXT("fall"), TEXT("sit_leg_stiffness"), OutError) ||
		!CheckRange(FallAim.Palms, 1.0, TEXT("fall"), TEXT("palms"), OutError) || !CheckRange(FallAim.PalmShare, 1.0, TEXT("fall"), TEXT("palm_share"), OutError))
	{
		return false;
	}
	// R3's lower bounds: a hand reaching less than half its arm comes near its shoulder (the elbow's target turns over), and a blend of
	// no time is a step.
	if (FallAim.CatchReach < 0.5)
	{
		OutError = FString::Printf(TEXT("fall.catch_reach is %g; it is 0.5 to 1 (a hand nearer its shoulder turns the elbow over)"), FallAim.CatchReach);
		return false;
	}
	for (const TPair<const TCHAR*, double>& Blend : { TPair<const TCHAR*, double>(TEXT("catch_blend_s"), FallAim.CatchBlendS),
		TPair<const TCHAR*, double>(TEXT("sit_blend_s"), FallAim.SitBlendS), TPair<const TCHAR*, double>(TEXT("turn_blend_s"), FallAim.TurnBlendS),
		TPair<const TCHAR*, double>(TEXT("catch_follow_s"), FallAim.CatchFollowS), TPair<const TCHAR*, double>(TEXT("head_follow_s"), FallAim.HeadFollowS) })
	{
		if (Blend.Value < 0.02)
		{
			OutError = FString::Printf(TEXT("fall.%s is %g; it is 0.02 s or more (a blend or a follow of no time is a step)"), Blend.Key, Blend.Value);
			return false;
		}
	}
	// A way straight back has no side: with no lower bound it turned the body to its right.
	if (FallAim.TurnSideMin < 0.01)
	{
		OutError = FString::Printf(TEXT("fall.turn_side_min is %g; it is 0.01 to 1 (a way with no side has nothing to turn to)"), FallAim.TurnSideMin);
		return false;
	}
	// The palm's turn is palm_share of a quarter turn about the forearm, and the wrist's range is 60 degrees.
	if (FallAim.PalmShare > 0.67)
	{
		OutError = FString::Printf(TEXT("fall.palm_share is %g; it is 0 to 0.67 (a quarter turn's share beyond the wrist's 60 degrees)"), FallAim.PalmShare);
		return false;
	}
	if (!CheckRange(Weight.FollowS, 5.0, TEXT("muscles"), TEXT("weight_follow_s"), OutError) ||
		!CheckRange(Weight.LegsBy, 1.0, TEXT("muscles"), TEXT("weight_legs_by"), OutError) ||
		!CheckRange(Weight.AnkleAsTorque, 1.0, TEXT("balance"), TEXT("ankle_as_torque"), OutError) ||
		!CheckRange(Weight.InternalShare, 1.0, TEXT("assist"), TEXT("internal_share"), OutError))
	{
		return false;
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
		!CheckRange(GetUp.HoldForceN, 100000.0, TEXT("getup"), TEXT("hold_force_n"), OutError) ||
		!CheckRange(GetUp.StallS, 60.0, TEXT("getup"), TEXT("stall_s"), OutError) ||
		!CheckRange(GetUp.WaitS, 60.0, TEXT("getup"), TEXT("wait_s"), OutError) ||
		!CheckRange(GetUp.LostCm, 1000.0, TEXT("getup"), TEXT("lost_cm"), OutError) ||
		!CheckRange(GetUp.UprightDeg, 180.0, TEXT("getup"), TEXT("upright_deg"), OutError) ||
		!CheckRange(GetUp.RestPerNsS, 0.1, TEXT("getup"), TEXT("rest_per_ns_s"), OutError) ||
		!CheckRange(GetUp.RestMaxS, 60.0, TEXT("getup"), TEXT("rest_max_s"), OutError) ||
		!CheckRange(GetUp.CarryNPerMPerKg, 10000.0, TEXT("getup"), TEXT("carry_n_per_m_per_kg"), OutError) ||
		!CheckRange(GetUp.CarryNmPerRadPerKg, 1000.0, TEXT("getup"), TEXT("carry_nm_per_rad_per_kg"), OutError) ||
		!CheckRange(GetUp.CarryDampingS, 5.0, TEXT("getup"), TEXT("carry_damping_s"), OutError) ||
		!CheckRange(GetUp.CarryMaxWeights, 100.0, TEXT("getup"), TEXT("carry_max_weights"), OutError) ||
		!CheckRange(GetUp.CarryMaxUpWeights, 100.0, TEXT("getup"), TEXT("carry_max_up_weights"), OutError) ||
		!CheckRange(GetUp.CarryMaxNmPerKg, 1000.0, TEXT("getup"), TEXT("carry_max_nm_per_kg"), OutError) ||
		!CheckRange(GetUp.CarryReleaseS, 5.0, TEXT("getup"), TEXT("carry_release_s"), OutError) ||
		!CheckRange(GetUp.CarryYieldNs, 10000.0, TEXT("getup"), TEXT("carry_yield_ns"), OutError) ||
		!CheckRange(GetUp.HoldByBody, 1.0, TEXT("getup"), TEXT("hold_by_body"), OutError) ||
		!CheckRange(GetUp.HoldChain, 3.0, TEXT("getup"), TEXT("hold_chain"), OutError) ||
		!CheckRange(GetUp.HoldRestFullN, 100000.0, TEXT("getup"), TEXT("hold_rest_full_n"), OutError) ||
		!CheckRange(GetUp.HoldChainFalloff, 1.0, TEXT("getup"), TEXT("hold_chain_falloff"), OutError) ||
		!CheckRange(GetUp.HoldChainFullN, 10000.0, TEXT("getup"), TEXT("hold_chain_full_n"), OutError) ||
		!CheckRange(GetUp.HoldChainShare, 1.0, TEXT("getup"), TEXT("hold_chain_share"), OutError) ||
		!CheckRange(GetUp.HoldSlowFromN, 100000.0, TEXT("getup"), TEXT("hold_slow_from_n"), OutError) ||
		!CheckRange(GetUp.HoldSum, 1.0, TEXT("getup"), TEXT("hold_sum"), OutError) ||
		!CheckRange(GetUp.HoldPressPace, 1.0, TEXT("getup"), TEXT("hold_press_pace"), OutError) ||
		!CheckRange(GetUp.HitByImpulse, 1.0, TEXT("getup"), TEXT("hit_by_impulse"), OutError) ||
		!CheckRange(GetUp.HitBackS, 10.0, TEXT("getup"), TEXT("hit_back_s"), OutError) ||
		!CheckRange(GetUp.HitOverflow, 1.0, TEXT("getup"), TEXT("hit_overflow"), OutError) ||
		!CheckRange(GetUp.StartBlendS, 10.0, TEXT("getup"), TEXT("start_blend_s"), OutError) ||
		!CheckRange(GetUp.MatchWindowS, 10.0, TEXT("getup"), TEXT("match_window_s"), OutError) ||
		!CheckRange(GetUp.MatchPelvis, 100.0, TEXT("getup"), TEXT("match_pelvis"), OutError) ||
		!CheckRange(GetUp.MatchTrunk, 100.0, TEXT("getup"), TEXT("match_trunk"), OutError) ||
		!CheckRange(GetUp.MatchHands, 100.0, TEXT("getup"), TEXT("match_hands"), OutError) ||
		!CheckRange(GetUp.MatchElbows, 100.0, TEXT("getup"), TEXT("match_elbows"), OutError) ||
		!CheckRange(GetUp.MatchFeet, 100.0, TEXT("getup"), TEXT("match_feet"), OutError) ||
		!CheckRange(GetUp.MatchKnees, 100.0, TEXT("getup"), TEXT("match_knees"), OutError) ||
		!CheckRange(GetUp.MatchUpCmPerDeg, 100.0, TEXT("getup"), TEXT("match_up_cm_per_deg"), OutError) ||
		!CheckRange(GetUp.MatchGivenUpCm, 1000.0, TEXT("getup"), TEXT("match_given_up_cm"), OutError) ||
		!CheckRange(GetUp.MatchLateCmPerS, 1000.0, TEXT("getup"), TEXT("match_late_cm_per_s"), OutError) ||
		!CheckRange(GetUp.Roll, 1.0, TEXT("getup"), TEXT("roll"), OutError) ||
		!CheckRange(GetUp.RollSideDeg, 90.0, TEXT("getup"), TEXT("roll_side_deg"), OutError) ||
		!CheckRange(GetUp.RollDoneDeg, 90.0, TEXT("getup"), TEXT("roll_done_deg"), OutError) ||
		!CheckRange(GetUp.RollS, 10.0, TEXT("getup"), TEXT("roll_s"), OutError) ||
		!CheckRange(GetUp.RollWay, 2.0, TEXT("getup"), TEXT("roll_way"), OutError) ||
		!CheckRange(GetUp.RollBlendS, 10.0, TEXT("getup"), TEXT("roll_blend_s"), OutError) ||
		!CheckRange(GetUp.RollPush, 3.0, TEXT("getup"), TEXT("roll_push"), OutError) ||
		!CheckRange(GetUp.FitSurface, 1.0, TEXT("getup"), TEXT("fit_surface"), OutError) ||
		!CheckRange(GetUp.FitFloor, 1.0, TEXT("getup"), TEXT("fit_floor"), OutError) ||
		!CheckRange(GetUp.FitEaseS, 10.0, TEXT("getup"), TEXT("fit_ease_s"), OutError) ||
		!CheckRange(GetUp.FitFreeZ, 1.0, TEXT("getup"), TEXT("fit_free_z"), OutError) ||
		!CheckRange(GetUp.Drag, 1.0, TEXT("getup"), TEXT("drag"), OutError) ||
		!CheckRange(GetUp.DragForceN, 100000.0, TEXT("getup"), TEXT("drag_force_n"), OutError) ||
		!CheckRange(GetUp.DragS, 10.0, TEXT("getup"), TEXT("drag_s"), OutError) ||
		!CheckRange(GetUp.DragMps, 100.0, TEXT("getup"), TEXT("drag_mps"), OutError) ||
		!CheckRange(GetUp.DragFreeS, 60.0, TEXT("getup"), TEXT("drag_free_s"), OutError) ||
		!CheckRange(GetUp.ArmAnswer, 3.0, TEXT("getup"), TEXT("arm_answer"), OutError) ||
		!CheckRange(GetUp.ArmYieldScale, 1.0, TEXT("getup"), TEXT("arm_yield_scale"), OutError) ||
		!CheckRange(GetUp.ArmReachCm, 100.0, TEXT("getup"), TEXT("arm_reach_cm"), OutError) ||
		!CheckRange(GetUp.ArmPullScale, 5.0, TEXT("getup"), TEXT("arm_pull_scale"), OutError) ||
		!CheckRange(GetUp.ArmPullInCm, 100.0, TEXT("getup"), TEXT("arm_pull_in_cm"), OutError) ||
		!CheckRange(GetUp.ArmFollowS, 10.0, TEXT("getup"), TEXT("arm_follow_s"), OutError) ||
		!CheckRange(Motion.ContactCm, 100.0, TEXT("motion"), TEXT("contact_cm"), OutError) ||
		!CheckRange(Motion.ContactMps, 100.0, TEXT("motion"), TEXT("contact_mps"), OutError))
	{
		return false;
	}
	if (GetUp.MatchPelvis + GetUp.MatchTrunk + GetUp.MatchHands + GetUp.MatchElbows + GetUp.MatchFeet + GetUp.MatchKnees <= 0.0)
	{
		OutError = TEXT("the get-up's matcher needs at least one of the places it compares weighed above 0 (getup.match_pelvis to match_knees)");
		return false;
	}
	if (GetUp.Drag > 0.5 && GetUp.DragForceN <= 0.0)
	{
		OutError = TEXT("getup.drag needs drag_force_n above 0");
		return false;
	}
	if (GetUp.Roll > 0.5 && (GetUp.RollS <= 0.0 || GetUp.RollDoneDeg >= GetUp.RollSideDeg))
	{
		OutError = TEXT("getup.roll needs roll_s above 0 and roll_done_deg below roll_side_deg");
		return false;
	}
	if (GetUp.HoldByBody > 0.5 && (GetUp.HoldChainFullN <= 0.0 || (GetUp.HoldForceN > 0.0 && GetUp.HoldSlowFromN >= GetUp.HoldForceN)))
	{
		OutError = TEXT("getup.hold_by_body needs hold_chain_full_n above 0 and hold_slow_from_n below hold_force_n");
		return false;
	}
	if (GetUp.HitByImpulse > 0.5 && GetUp.CarryYieldNs <= 0.0)
	{
		OutError = TEXT("getup.hit_by_impulse needs carry_yield_ns above 0: it is the impulse that takes all of the authority");
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

	// The surface. A combine mode is one of the engine's four, by its number.
	if (!CheckRange(Surface.Friction, 4.0, TEXT("surface"), TEXT("friction"), OutError) ||
		!CheckRange(Surface.FeetFriction, 4.0, TEXT("surface"), TEXT("feet_friction"), OutError) ||
		!CheckRange(Surface.Restitution, 1.0, TEXT("surface"), TEXT("restitution"), OutError) ||
		!CheckRange(Surface.FrictionCombine, 3.0, TEXT("surface"), TEXT("friction_combine"), OutError) ||
		!CheckRange(Surface.RestitutionCombine, 3.0, TEXT("surface"), TEXT("restitution_combine"), OutError))
	{
		return false;
	}
	if (Surface.FrictionCombine != FMath::RoundToDouble(Surface.FrictionCombine) ||
		Surface.RestitutionCombine != FMath::RoundToDouble(Surface.RestitutionCombine))
	{
		OutError = TEXT("surface.friction_combine and surface.restitution_combine must be whole numbers: 0 average, 1 the smaller, 2 the product, 3 the larger");
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
		!CheckRange(Balance.RestStepS, 10.0, TEXT("balance"), TEXT("rest_step_s"), OutError) ||
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
		!CheckRange(Balance.StepWidthMinCm, 100.0, TEXT("balance"), TEXT("step_width_min_cm"), OutError) ||
		!CheckRange(Balance.CrossOffsetCm, 100.0, TEXT("balance"), TEXT("cross_offset_cm"), OutError) ||
		!CheckRange(Balance.StepEaseS, 2.0, TEXT("balance"), TEXT("step_ease_s"), OutError) ||
		!CheckRange(Balance.LiftSmooth, 1.0, TEXT("balance"), TEXT("lift_smooth"), OutError) ||
		!CheckRange(Balance.SettleSmooth, 1.0, TEXT("balance"), TEXT("settle_smooth"), OutError) ||
		!CheckRange(Balance.ReleaseSmooth, 1.0, TEXT("balance"), TEXT("release_smooth"), OutError) ||
		!CheckRange(Balance.LandEaseS, 2.0, TEXT("balance"), TEXT("land_ease_s"), OutError) ||
		!CheckRange(Balance.FallEaseS, 2.0, TEXT("balance"), TEXT("fall_ease_s"), OutError) ||
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
		!CheckRange(Reactions.ThingSupport, 1.0, TEXT("reactions"), TEXT("thing_support"), OutError) ||
		!CheckRange(Reactions.HoldFelt, 1.0, TEXT("reactions"), TEXT("hold_felt"), OutError) ||
		!CheckRange(Reactions.ThingSupportMaxUpMps, 100.0, TEXT("reactions"), TEXT("thing_support_max_up_mps"), OutError) ||
		!CheckRange(Reactions.FlinchWeakening, 1.0, TEXT("reactions"), TEXT("flinch_weakening"), OutError) ||
		!CheckRange(Reactions.FlinchRecoveryS, 10.0, TEXT("reactions"), TEXT("flinch_recovery_s"), OutError))
	{
		return false;
	}
	for (const FSandboxFingerPose* Hand : { &Poses.Fingers.Rest, &Poses.Fingers.Fist, &Poses.Fingers.Splayed, &Poses.Fingers.Flat })
	{
		for (const double Angle : { Hand->CurlDeg[0], Hand->CurlDeg[1], Hand->CurlDeg[2], Hand->CurlDeg[3], Hand->CurlDeg[4], Hand->SpreadDeg })
		{
			if (!FMath::IsFinite(Angle) || FMath::Abs(Angle) > 360.0)
			{
				OutError = FString::Printf(TEXT("poses.fingers: %g degrees is outside -360..360"), Angle);
				return false;
			}
		}
	}
	if (!CheckRange(Poses.BlendS, 10.0, TEXT("poses"), TEXT("blend_s"), OutError) ||
		!CheckRange(Poses.Fingers.Weight, 1.0, TEXT("poses.fingers"), TEXT("weight"), OutError) ||
		!CheckRange(Poses.Fingers.BlendS, 10.0, TEXT("poses.fingers"), TEXT("blend_s"), OutError) ||
		!CheckRange(Poses.FallBlendS, 10.0, TEXT("poses"), TEXT("fall_blend_s"), OutError) ||
		!CheckRange(Poses.IdleWeight, 5.0, TEXT("poses.idle"), TEXT("weight"), OutError) ||
		!CheckRange(Poses.IdleSwayWeight, 5.0, TEXT("poses.idle"), TEXT("sway_weight"), OutError))
	{
		return false;
	}
	const TPair<const TCHAR*, const FSandboxPose*> PoseList[] = { { TEXT("stance"), &Poses.Stance }, { TEXT("flinch"), &Poses.Flinch }, { TEXT("double_over"), &Poses.DoubleOver },
		{ TEXT("fall_back"), &Poses.FallBack }, { TEXT("fall_forward"), &Poses.FallForward }, { TEXT("fall_side"), &Poses.FallSide },
		{ TEXT("balance_back"), &Poses.BalanceBack }, { TEXT("balance_forward"), &Poses.BalanceForward }, { TEXT("balance_side"), &Poses.BalanceSide },
		{ TEXT("roll_to_back"), &Poses.RollToBack }, { TEXT("roll_to_front"), &Poses.RollToFront },
		{ TEXT("fall_curl"), &Poses.FallCurl }, { TEXT("fall_sit"), &Poses.FallSit }, { TEXT("fall_turn"), &Poses.FallTurn },
		{ TEXT("protect_head"), &Poses.ProtectHead }, { TEXT("protect_trunk"), &Poses.ProtectTrunk }, { TEXT("protect_groin"), &Poses.ProtectGroin },
		{ TEXT("protect_leg"), &Poses.ProtectLeg } };
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
		!CheckRange(Reactions.FlinchGiveDeg, 90.0, TEXT("reactions"), TEXT("flinch_give_deg"), OutError) ||
		!CheckRange(Reactions.ReflexDelayS, 30.0, TEXT("reactions"), TEXT("reflex_delay_s"), OutError))
	{
		return false;
	}
	if (Reactions.FlinchPoseFullNs <= 0.0)
	{
		OutError = TEXT("reactions.flinch_pose_full_ns must be above 0");
		return false;
	}
	if (Reactions.DoubleOverNs < 0.0 || Reactions.DoubleOverS < 0.0 || Reactions.DoubleOverInS <= 0.0 || Reactions.DoubleOverOutS <= 0.0)
	{
		OutError = TEXT("reactions: double_over_ns and double_over_s must not be negative, double_over_in_s and double_over_out_s must be above 0");
		return false;
	}
	if (Reactions.KnockdownHeadNs < 0.0)
	{
		OutError = TEXT("reactions.knockdown_head_ns must not be negative");
		return false;
	}
	if (Reactions.DoubleOverFrom >= Reactions.DoubleOverTo)
	{
		OutError = TEXT("reactions.double_over_from must be below reactions.double_over_to");
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
	if (StartsWithAny({ TEXT("ball") }))
	{
		return ESandboxMuscleGroup::Toes;
	}
	if (StartsWithAny({ TEXT("thigh"), TEXT("calf"), TEXT("foot") }))
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
	case ESandboxMuscleGroup::Toes: return TEXT("toes");
	default: return TEXT("spine");
	}
}
