#include "SandboxInteractionTuning.h"

#include "SandboxParamStore.h"

namespace
{
	const FName HandKind(TEXT("hand"));
	const FName FootKind(TEXT("foot"));

	/** Calls Visitor(name, value) for every tunable number. TuningType is the tuning, const or not. */
	template <typename TuningType, typename VisitorType>
	void VisitValues(TuningType& Tuning, VisitorType&& Visitor)
	{
		// What every effector has: a body and the spring that holds it to its target.
		const auto Body = [&Visitor](const TCHAR* Kind, auto& Values)
		{
			Visitor(FString::Printf(TEXT("effectors.%s.mass_kg"), Kind), Values.MassKg);
			Visitor(FString::Printf(TEXT("effectors.%s.radius_cm"), Kind), Values.RadiusCm);
			Visitor(FString::Printf(TEXT("effectors.%s.stiffness_n_per_m"), Kind), Values.StiffnessNPerM);
			Visitor(FString::Printf(TEXT("effectors.%s.damping_s"), Kind), Values.DampingS);
			Visitor(FString::Printf(TEXT("effectors.%s.max_force_n"), Kind), Values.MaxForceN);
			Visitor(FString::Printf(TEXT("effectors.%s.angular_stiffness_nm_per_rad"), Kind), Values.AngularStiffnessNmPerRad);
			Visitor(FString::Printf(TEXT("effectors.%s.angular_damping_s"), Kind), Values.AngularDampingS);
			Visitor(FString::Printf(TEXT("effectors.%s.max_torque_nm"), Kind), Values.MaxTorqueNm);
			Visitor(FString::Printf(TEXT("effectors.%s.snap_distance_cm"), Kind), Values.SnapDistanceCm);
		};
		// And what only a hand has.
		const auto Effector = [&Visitor, &Body](const TCHAR* Kind, auto& Values)
		{
			Body(Kind, Values);
			Visitor(FString::Printf(TEXT("effectors.%s.offset_thumb_cm"), Kind), Values.OffsetThumbCm);
			Visitor(FString::Printf(TEXT("effectors.%s.offset_palm_cm"), Kind), Values.OffsetPalmCm);
			Visitor(FString::Printf(TEXT("effectors.%s.offset_knuckles_cm"), Kind), Values.OffsetKnucklesCm);
			Visitor(FString::Printf(TEXT("effectors.%s.grip_pitch_deg"), Kind), Values.GripPitchDeg);
			Visitor(FString::Printf(TEXT("effectors.%s.grip_yaw_deg"), Kind), Values.GripYawDeg);
			Visitor(FString::Printf(TEXT("effectors.%s.grip_roll_deg"), Kind), Values.GripRollDeg);
			Visitor(FString::Printf(TEXT("effectors.%s.fist_grip"), Kind), Values.FistGrip);
			Visitor(FString::Printf(TEXT("effectors.%s.fist_grip_slack"), Kind), Values.FistGripSlack);
			Visitor(FString::Printf(TEXT("effectors.%s.palm_length_cm"), Kind), Values.PalmLengthCm);
			Visitor(FString::Printf(TEXT("effectors.%s.palm_width_cm"), Kind), Values.PalmWidthCm);
			Visitor(FString::Printf(TEXT("effectors.%s.palm_thickness_cm"), Kind), Values.PalmThicknessCm);
			Visitor(FString::Printf(TEXT("effectors.%s.palm_forward_cm"), Kind), Values.PalmForwardCm);
			Visitor(FString::Printf(TEXT("effectors.%s.palm_collides"), Kind), Values.PalmCollides);
			Visitor(FString::Printf(TEXT("effectors.%s.palm_mass_kg"), Kind), Values.PalmMassKg);
			Visitor(FString::Printf(TEXT("effectors.%s.grab"), Kind), Values.Grab);
			Visitor(FString::Printf(TEXT("effectors.%s.grab_reach_cm"), Kind), Values.GrabReachCm);
			Visitor(FString::Printf(TEXT("effectors.%s.grab_slip_cm"), Kind), Values.GrabSlipCm);
			Visitor(FString::Printf(TEXT("effectors.%s.grab_slip_s"), Kind), Values.GrabSlipS);
			Visitor(FString::Printf(TEXT("effectors.%s.grab_max_force_n"), Kind), Values.GrabMaxForceN);
			Visitor(FString::Printf(TEXT("effectors.%s.grab_overload_s"), Kind), Values.GrabOverloadS);
			Visitor(FString::Printf(TEXT("effectors.%s.grab_clear_cm"), Kind), Values.GrabClearCm);
			Visitor(FString::Printf(TEXT("effectors.%s.grab_hold_nm_per_rad"), Kind), Values.GrabHoldNmPerRad);
			Visitor(FString::Printf(TEXT("effectors.%s.grab_hold_damping_s"), Kind), Values.GrabHoldDampingS);
			Visitor(FString::Printf(TEXT("effectors.%s.grab_hold_max_torque_nm"), Kind), Values.GrabHoldMaxTorqueNm);
			Visitor(FString::Printf(TEXT("effectors.%s.grab_handle_cm"), Kind), Values.GrabHandleCm);
			Visitor(FString::Printf(TEXT("effectors.%s.grab_draw_s"), Kind), Values.GrabDrawS);
			Visitor(FString::Printf(TEXT("effectors.%s.strike_force_n"), Kind), Values.StrikeForceN);
			Visitor(FString::Printf(TEXT("effectors.%s.strike_stiffness_n_per_m"), Kind), Values.StrikeStiffnessNPerM);
			Visitor(FString::Printf(TEXT("effectors.%s.strike_through_cm"), Kind), Values.StrikeThroughCm);
			Visitor(FString::Printf(TEXT("effectors.%s.strike_return_cm"), Kind), Values.StrikeReturnCm);
			Visitor(FString::Printf(TEXT("effectors.%s.strike_held"), Kind), Values.StrikeHeld);
			Visitor(FString::Printf(TEXT("effectors.%s.strike_hold_scale"), Kind), Values.StrikeHoldScale);
			Visitor(FString::Printf(TEXT("effectors.%s.strike_held_full_speed_mps"), Kind), Values.StrikeHeldFullSpeedMps);
			Visitor(FString::Printf(TEXT("effectors.%s.strike_s"), Kind), Values.StrikeS);
			Visitor(FString::Printf(TEXT("effectors.%s.strike_speed_mps"), Kind), Values.StrikeSpeedMps);
			Visitor(FString::Printf(TEXT("effectors.%s.strike_full_speed_mps"), Kind), Values.StrikeFullSpeedMps);
		};
		Effector(TEXT("hand"), Tuning.Hand);
		Body(TEXT("foot"), Tuning.Foot);

		Visitor(FString(TEXT("tools.hands.rest_forward_cm")), Tuning.Tools.HandRestForwardCm);
		Visitor(FString(TEXT("tools.hands.rest_side_cm")), Tuning.Tools.HandRestSideCm);
		Visitor(FString(TEXT("tools.hands.rest_down_cm")), Tuning.Tools.HandRestDownCm);
		Visitor(FString(TEXT("tools.hands.reach_cm")), Tuning.Tools.HandReachCm);
		Visitor(FString(TEXT("tools.hands.reach_speed_mps")), Tuning.Tools.HandReachSpeedMps);
		Visitor(FString(TEXT("tools.hands.through_cm")), Tuning.Tools.HandThroughCm);
		Visitor(FString(TEXT("tools.hands.open_reach_speed_mps")), Tuning.Tools.HandOpenReachSpeedMps);
		Visitor(FString(TEXT("tools.kick.rest_forward_cm")), Tuning.Tools.KickRestForwardCm);
		Visitor(FString(TEXT("tools.kick.rest_right_cm")), Tuning.Tools.KickRestRightCm);
		Visitor(FString(TEXT("tools.kick.rest_down_cm")), Tuning.Tools.KickRestDownCm);
		Visitor(FString(TEXT("tools.kick.speed_mps")), Tuning.Tools.KickSpeedMps);
		Visitor(FString(TEXT("tools.kick.through_cm")), Tuning.Tools.KickThroughCm);
		Visitor(FString(TEXT("tools.kick.chamber_cm")), Tuning.Tools.KickChamberCm);
		Visitor(FString(TEXT("tools.kick.reach_cm")), Tuning.Tools.KickReachCm);
		Visitor(FString(TEXT("tools.kick.rest_force_n")), Tuning.Tools.KickRestForceN);
		Visitor(FString(TEXT("tools.kick.settle_s")), Tuning.Tools.KickSettleS);
		Visitor(FString(TEXT("tools.impulse.impulse_ns")), Tuning.Tools.ImpulseNs);
		Visitor(FString(TEXT("tools.impulse.wheel_factor")), Tuning.Tools.ImpulseWheelFactor);
		Visitor(FString(TEXT("tools.drag.stiffness_n_per_m")), Tuning.Tools.Drag.StiffnessNPerM);
		Visitor(FString(TEXT("tools.drag.damping_s")), Tuning.Tools.Drag.DampingS);
		Visitor(FString(TEXT("tools.drag.max_force_n")), Tuning.Tools.Drag.MaxForceN);
		Visitor(FString(TEXT("tools.wheel_distance_factor")), Tuning.Tools.WheelDistanceFactor);
		Visitor(FString(TEXT("tools.camera.speed_mps")), Tuning.Tools.CameraSpeedMps);
		Visitor(FString(TEXT("tools.camera.fast_factor")), Tuning.Tools.CameraFastFactor);
		Visitor(FString(TEXT("tools.camera.look_deg_per_count")), Tuning.Tools.CameraLookDegPerCount);
		Visitor(FString(TEXT("tools.time.slow_motion_divisor")), Tuning.Tools.SlowMotionDivisor);
		Visitor(FString(TEXT("tools.headset.stand_distance_cm")), Tuning.Tools.HeadsetStandDistanceCm);
		Visitor(FString(TEXT("tools.headset.hands_height_cm")), Tuning.Tools.HeadsetHandsHeightCm);
		Visitor(FString(TEXT("tools.headset.max_hand_speed_mps")), Tuning.Tools.HeadsetMaxHandSpeedMps);
		Visitor(FString(TEXT("tools.headset.catch_up_speed_mps")), Tuning.Tools.HeadsetCatchUpSpeedMps);
		Visitor(FString(TEXT("tools.headset.estimated_follow_s")), Tuning.Tools.HeadsetEstimatedFollowS);
		Visitor(FString(TEXT("tools.headset.runtime_velocity_share")), Tuning.Tools.HeadsetRuntimeVelocityShare);
		Visitor(FString(TEXT("tools.headset.new_take_hold_s")), Tuning.Tools.HeadsetNewTakeHoldS);
		Visitor(FString(TEXT("tools.headset.new_take_raise_cm")), Tuning.Tools.HeadsetNewTakeRaiseCm);
		Visitor(FString(TEXT("tools.headset.haptic_full_n")), Tuning.Tools.HeadsetHapticFullN);
		Visitor(FString(TEXT("tools.headset.take_buzz_s")), Tuning.Tools.HeadsetTakeBuzzS);
		Visitor(FString(TEXT("tools.headset.take_buzz_strength")), Tuning.Tools.HeadsetTakeBuzzStrength);
	}

	/** Min <= Value <= Max, or Min < Value when bAboveMin. The bounds catch a slipped decimal point, not physics. */
	bool CheckRange(const TCHAR* Name, double Value, double Min, double Max, bool bAboveMin, FString& OutError)
	{
		if (FMath::IsFinite(Value) && Value <= Max && (bAboveMin ? Value > Min : Value >= Min))
		{
			return true;
		}
		OutError = FString::Printf(TEXT("%s = %g is outside %s%g..%g"), Name, Value, bAboveMin ? TEXT("above ") : TEXT(""), Min, Max);
		return false;
	}
}

void FSandboxInteractionTuning::ListValues(TArray<TPair<FString, double>>& OutValues) const
{
	OutValues.Reset();
	VisitValues(*this, [&OutValues](const FString& Key, const double& Value) { OutValues.Emplace(Key, Value); });
}

bool FSandboxInteractionTuning::HasSameValues(const FSandboxInteractionTuning& Other) const
{
	TArray<TPair<FString, double>> Mine, Theirs;
	ListValues(Mine);
	Other.ListValues(Theirs);
	return Mine == Theirs;
}

const FSandboxEffectorTuning* FSandboxInteractionTuning::Effector(FName Kind) const
{
	return Kind == HandKind ? &Hand : Kind == FootKind ? &Foot : nullptr;
}

void FSandboxInteractionTuning::DeclareParams(FSandboxParamStore& Params)
{
	FSandboxInteractionTuning Names;
	VisitValues(Names, [&Params](const FString& Key, double&) { Params.DeclareNumber(Key); });
	Params.AddValidator([](const FSandboxParamStore& Store, FString& OutError)
	{
		FSandboxInteractionTuning Tuning;
		return FromParams(Store, Tuning, OutError);
	});
}

bool FSandboxInteractionTuning::FromParams(const FSandboxParamStore& Params, FSandboxInteractionTuning& OutTuning, FString& OutError)
{
	OutTuning = FSandboxInteractionTuning();
	FString Missing;
	VisitValues(OutTuning, [&](const FString& Key, double& Target)
	{
		// The player's side has no states: the base values are all there is.
		if (!Params.FindNumber(Key, FString(), Target) && Missing.IsEmpty())
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

bool FSandboxInteractionTuning::Validate(FString& OutError) const
{
	// An effector without mass, spring or force cap is not a hand: a cap of zero would mean "no cap" to the
	// solver, which is the kinematic hand this design rules out.
	if (!CheckRange(TEXT("effectors.hand.mass_kg"), Hand.MassKg, 0.0, 50.0, true, OutError) ||
		!CheckRange(TEXT("effectors.hand.radius_cm"), Hand.RadiusCm, 0.5, 30.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.stiffness_n_per_m"), Hand.StiffnessNPerM, 0.0, 200000.0, true, OutError) ||
		!CheckRange(TEXT("effectors.hand.damping_s"), Hand.DampingS, 0.0, 2.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.max_force_n"), Hand.MaxForceN, 0.0, 10000.0, true, OutError) ||
		!CheckRange(TEXT("effectors.hand.angular_stiffness_nm_per_rad"), Hand.AngularStiffnessNmPerRad, 0.0, 20000.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.angular_damping_s"), Hand.AngularDampingS, 0.0, 2.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.max_torque_nm"), Hand.MaxTorqueNm, 0.0, 5000.0, true, OutError) ||
		!CheckRange(TEXT("effectors.hand.snap_distance_cm"), Hand.SnapDistanceCm, 0.0, 5000.0, true, OutError) ||
		!CheckRange(TEXT("effectors.hand.offset_thumb_cm"), Hand.OffsetThumbCm, -30.0, 30.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.offset_palm_cm"), Hand.OffsetPalmCm, -30.0, 30.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.offset_knuckles_cm"), Hand.OffsetKnucklesCm, -30.0, 30.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.grip_pitch_deg"), Hand.GripPitchDeg, -180.0, 180.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.grip_yaw_deg"), Hand.GripYawDeg, -180.0, 180.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.grip_roll_deg"), Hand.GripRollDeg, -180.0, 180.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.fist_grip"), Hand.FistGrip, 0.1, 0.9, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.fist_grip_slack"), Hand.FistGripSlack, 0.0, 0.1, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.palm_length_cm"), Hand.PalmLengthCm, 1.0, 40.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.palm_width_cm"), Hand.PalmWidthCm, 1.0, 30.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.palm_thickness_cm"), Hand.PalmThicknessCm, 0.5, 20.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.palm_forward_cm"), Hand.PalmForwardCm, -20.0, 20.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.palm_collides"), Hand.PalmCollides, 0.0, 1.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.palm_mass_kg"), Hand.PalmMassKg, 0.0, 50.0, true, OutError) ||
		!CheckRange(TEXT("effectors.hand.grab"), Hand.Grab, 0.0, 1.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.grab_reach_cm"), Hand.GrabReachCm, 0.0, 50.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.grab_slip_cm"), Hand.GrabSlipCm, 0.0, 500.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.grab_slip_s"), Hand.GrabSlipS, 0.0, 10.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.grab_max_force_n"), Hand.GrabMaxForceN, 0.0, 100000.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.grab_overload_s"), Hand.GrabOverloadS, 0.0, 10.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.grab_clear_cm"), Hand.GrabClearCm, 0.0, 1.5, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.grab_hold_nm_per_rad"), Hand.GrabHoldNmPerRad, 0.0, 20000.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.grab_hold_damping_s"), Hand.GrabHoldDampingS, 0.0, 2.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.grab_hold_max_torque_nm"), Hand.GrabHoldMaxTorqueNm, 0.0, 5000.0, true, OutError) ||
		!CheckRange(TEXT("effectors.hand.grab_handle_cm"), Hand.GrabHandleCm, 0.0, 100.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.grab_draw_s"), Hand.GrabDrawS, 0.0, 5.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.strike_force_n"), Hand.StrikeForceN, 0.0, 10000.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.strike_stiffness_n_per_m"), Hand.StrikeStiffnessNPerM, 0.0, 100000.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.strike_through_cm"), Hand.StrikeThroughCm, 0.0, 100.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.strike_return_cm"), Hand.StrikeReturnCm, 0.0, 100.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.strike_held"), Hand.StrikeHeld, 0.0, 1.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.strike_hold_scale"), Hand.StrikeHoldScale, 1.0, 50.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.strike_held_full_speed_mps"), Hand.StrikeHeldFullSpeedMps, 0.0, 100.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.strike_s"), Hand.StrikeS, 0.0, 2.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.strike_speed_mps"), Hand.StrikeSpeedMps, 0.0, 30.0, false, OutError) ||
		!CheckRange(TEXT("effectors.hand.strike_full_speed_mps"), Hand.StrikeFullSpeedMps, 0.0, 30.0, false, OutError))
	{
		return false;
	}
	// The foot: the same reasons as for the hand's body and spring.
	if (!CheckRange(TEXT("effectors.foot.mass_kg"), Foot.MassKg, 0.0, 50.0, true, OutError) ||
		!CheckRange(TEXT("effectors.foot.radius_cm"), Foot.RadiusCm, 0.5, 30.0, false, OutError) ||
		!CheckRange(TEXT("effectors.foot.stiffness_n_per_m"), Foot.StiffnessNPerM, 0.0, 200000.0, true, OutError) ||
		!CheckRange(TEXT("effectors.foot.damping_s"), Foot.DampingS, 0.0, 2.0, false, OutError) ||
		!CheckRange(TEXT("effectors.foot.max_force_n"), Foot.MaxForceN, 0.0, 10000.0, true, OutError) ||
		!CheckRange(TEXT("effectors.foot.angular_stiffness_nm_per_rad"), Foot.AngularStiffnessNmPerRad, 0.0, 5000.0, false, OutError) ||
		!CheckRange(TEXT("effectors.foot.angular_damping_s"), Foot.AngularDampingS, 0.0, 2.0, false, OutError) ||
		!CheckRange(TEXT("effectors.foot.max_torque_nm"), Foot.MaxTorqueNm, 0.0, 5000.0, true, OutError) ||
		!CheckRange(TEXT("effectors.foot.snap_distance_cm"), Foot.SnapDistanceCm, 0.0, 5000.0, true, OutError))
	{
		return false;
	}
	if (Hand.Grab != 0.0 && Hand.Grab != 1.0)
	{
		OutError = FString::Printf(TEXT("effectors.hand.grab = %g must be 0 or 1"), Hand.Grab);
		return false;
	}
	if (Hand.PalmCollides != 0.0 && Hand.PalmCollides != 1.0)
	{
		OutError = FString::Printf(TEXT("effectors.hand.palm_collides = %g must be 0 or 1"), Hand.PalmCollides);
		return false;
	}

	if (!CheckRange(TEXT("tools.hands.rest_forward_cm"), Tools.HandRestForwardCm, 0.0, 200.0, false, OutError) ||
		!CheckRange(TEXT("tools.hands.rest_side_cm"), Tools.HandRestSideCm, 0.0, 200.0, false, OutError) ||
		!CheckRange(TEXT("tools.hands.rest_down_cm"), Tools.HandRestDownCm, 0.0, 200.0, false, OutError) ||
		!CheckRange(TEXT("tools.hands.reach_cm"), Tools.HandReachCm, 0.0, 300.0, true, OutError) ||
		!CheckRange(TEXT("tools.hands.reach_speed_mps"), Tools.HandReachSpeedMps, 0.0, 30.0, true, OutError) ||
		!CheckRange(TEXT("tools.hands.through_cm"), Tools.HandThroughCm, 0.0, 100.0, false, OutError) ||
		!CheckRange(TEXT("tools.hands.open_reach_speed_mps"), Tools.HandOpenReachSpeedMps, 0.0, 30.0, true, OutError) ||
		!CheckRange(TEXT("tools.kick.rest_forward_cm"), Tools.KickRestForwardCm, -200.0, 200.0, false, OutError) ||
		!CheckRange(TEXT("tools.kick.rest_right_cm"), Tools.KickRestRightCm, -200.0, 200.0, false, OutError) ||
		!CheckRange(TEXT("tools.kick.rest_down_cm"), Tools.KickRestDownCm, 0.0, 300.0, false, OutError) ||
		!CheckRange(TEXT("tools.kick.speed_mps"), Tools.KickSpeedMps, 0.0, 30.0, true, OutError) ||
		!CheckRange(TEXT("tools.kick.through_cm"), Tools.KickThroughCm, 0.0, 200.0, false, OutError) ||
		!CheckRange(TEXT("tools.kick.chamber_cm"), Tools.KickChamberCm, 0.0, 200.0, false, OutError) ||
		!CheckRange(TEXT("tools.kick.reach_cm"), Tools.KickReachCm, 0.0, 500.0, true, OutError) ||
		!CheckRange(TEXT("tools.kick.rest_force_n"), Tools.KickRestForceN, 0.0, 10000.0, true, OutError) ||
		!CheckRange(TEXT("tools.kick.settle_s"), Tools.KickSettleS, 0.0, 5.0, false, OutError) ||
		!CheckRange(TEXT("tools.impulse.impulse_ns"), Tools.ImpulseNs, 0.0, 1000.0, true, OutError) ||
		!CheckRange(TEXT("tools.impulse.wheel_factor"), Tools.ImpulseWheelFactor, 1.0, 10.0, true, OutError) ||
		!CheckRange(TEXT("tools.drag.stiffness_n_per_m"), Tools.Drag.StiffnessNPerM, 0.0, 200000.0, true, OutError) ||
		!CheckRange(TEXT("tools.drag.damping_s"), Tools.Drag.DampingS, 0.0, 2.0, false, OutError) ||
		!CheckRange(TEXT("tools.drag.max_force_n"), Tools.Drag.MaxForceN, 0.0, 10000.0, true, OutError) ||
		!CheckRange(TEXT("tools.wheel_distance_factor"), Tools.WheelDistanceFactor, 1.0, 10.0, true, OutError) ||
		!CheckRange(TEXT("tools.camera.speed_mps"), Tools.CameraSpeedMps, 0.0, 100.0, true, OutError) ||
		!CheckRange(TEXT("tools.camera.fast_factor"), Tools.CameraFastFactor, 1.0, 100.0, false, OutError) ||
		!CheckRange(TEXT("tools.camera.look_deg_per_count"), Tools.CameraLookDegPerCount, 0.0, 10.0, true, OutError) ||
		!CheckRange(TEXT("tools.time.slow_motion_divisor"), Tools.SlowMotionDivisor, 2.0, 64.0, false, OutError) ||
		!CheckRange(TEXT("tools.headset.stand_distance_cm"), Tools.HeadsetStandDistanceCm, 0.0, 500.0, false, OutError) ||
		!CheckRange(TEXT("tools.headset.hands_height_cm"), Tools.HeadsetHandsHeightCm, 0.0, 250.0, false, OutError) ||
		!CheckRange(TEXT("tools.headset.max_hand_speed_mps"), Tools.HeadsetMaxHandSpeedMps, 1.0, 200.0, false, OutError) ||
		!CheckRange(TEXT("tools.headset.catch_up_speed_mps"), Tools.HeadsetCatchUpSpeedMps, 0.0, 50.0, true, OutError) ||
		!CheckRange(TEXT("tools.headset.estimated_follow_s"), Tools.HeadsetEstimatedFollowS, 0.0, 10.0, false, OutError) ||
		!CheckRange(TEXT("tools.headset.runtime_velocity_share"), Tools.HeadsetRuntimeVelocityShare, 0.0, 1.0, false, OutError) ||
		!CheckRange(TEXT("tools.headset.new_take_hold_s"), Tools.HeadsetNewTakeHoldS, 0.0, 10.0, false, OutError) ||
		!CheckRange(TEXT("tools.headset.new_take_raise_cm"), Tools.HeadsetNewTakeRaiseCm, -100.0, 100.0, false, OutError) ||
		!CheckRange(TEXT("tools.headset.haptic_full_n"), Tools.HeadsetHapticFullN, 0.0, 10000.0, true, OutError) ||
		!CheckRange(TEXT("tools.headset.take_buzz_s"), Tools.HeadsetTakeBuzzS, 0.0, 5.0, false, OutError) ||
		!CheckRange(TEXT("tools.headset.take_buzz_strength"), Tools.HeadsetTakeBuzzStrength, 0.0, 1.0, false, OutError))
	{
		return false;
	}
	if (Tools.SlowMotionDivisor != FMath::RoundToDouble(Tools.SlowMotionDivisor))
	{
		OutError = FString::Printf(TEXT("tools.time.slow_motion_divisor = %g must be a whole number"), Tools.SlowMotionDivisor);
		return false;
	}
	return true;
}
