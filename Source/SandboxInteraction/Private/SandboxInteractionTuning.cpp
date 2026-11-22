#include "SandboxInteractionTuning.h"

#include "SandboxParamStore.h"

namespace
{
	const FName HandKind(TEXT("hand"));

	/** Calls Visitor(name, value) for every tunable number. TuningType is the tuning, const or not. */
	template <typename TuningType, typename VisitorType>
	void VisitValues(TuningType& Tuning, VisitorType&& Visitor)
	{
		const auto Effector = [&Visitor](const TCHAR* Kind, auto& Values)
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
		Effector(TEXT("hand"), Tuning.Hand);

		Visitor(FString(TEXT("tools.hands.rest_forward_cm")), Tuning.Tools.HandRestForwardCm);
		Visitor(FString(TEXT("tools.hands.rest_side_cm")), Tuning.Tools.HandRestSideCm);
		Visitor(FString(TEXT("tools.hands.rest_down_cm")), Tuning.Tools.HandRestDownCm);
		Visitor(FString(TEXT("tools.hands.reach_cm")), Tuning.Tools.HandReachCm);
		Visitor(FString(TEXT("tools.hands.reach_speed_mps")), Tuning.Tools.HandReachSpeedMps);
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
	return Kind == HandKind ? &Hand : nullptr;
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
		!CheckRange(TEXT("effectors.hand.snap_distance_cm"), Hand.SnapDistanceCm, 0.0, 5000.0, true, OutError))
	{
		return false;
	}

	if (!CheckRange(TEXT("tools.hands.rest_forward_cm"), Tools.HandRestForwardCm, 0.0, 200.0, false, OutError) ||
		!CheckRange(TEXT("tools.hands.rest_side_cm"), Tools.HandRestSideCm, 0.0, 200.0, false, OutError) ||
		!CheckRange(TEXT("tools.hands.rest_down_cm"), Tools.HandRestDownCm, 0.0, 200.0, false, OutError) ||
		!CheckRange(TEXT("tools.hands.reach_cm"), Tools.HandReachCm, 0.0, 300.0, true, OutError) ||
		!CheckRange(TEXT("tools.hands.reach_speed_mps"), Tools.HandReachSpeedMps, 0.0, 30.0, true, OutError) ||
		!CheckRange(TEXT("tools.impulse.impulse_ns"), Tools.ImpulseNs, 0.0, 1000.0, true, OutError) ||
		!CheckRange(TEXT("tools.impulse.wheel_factor"), Tools.ImpulseWheelFactor, 1.0, 10.0, true, OutError) ||
		!CheckRange(TEXT("tools.drag.stiffness_n_per_m"), Tools.Drag.StiffnessNPerM, 0.0, 200000.0, true, OutError) ||
		!CheckRange(TEXT("tools.drag.damping_s"), Tools.Drag.DampingS, 0.0, 2.0, false, OutError) ||
		!CheckRange(TEXT("tools.drag.max_force_n"), Tools.Drag.MaxForceN, 0.0, 10000.0, true, OutError) ||
		!CheckRange(TEXT("tools.wheel_distance_factor"), Tools.WheelDistanceFactor, 1.0, 10.0, true, OutError) ||
		!CheckRange(TEXT("tools.camera.speed_mps"), Tools.CameraSpeedMps, 0.0, 100.0, true, OutError) ||
		!CheckRange(TEXT("tools.camera.fast_factor"), Tools.CameraFastFactor, 1.0, 100.0, false, OutError) ||
		!CheckRange(TEXT("tools.camera.look_deg_per_count"), Tools.CameraLookDegPerCount, 0.0, 10.0, true, OutError) ||
		!CheckRange(TEXT("tools.time.slow_motion_divisor"), Tools.SlowMotionDivisor, 2.0, 64.0, false, OutError))
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
