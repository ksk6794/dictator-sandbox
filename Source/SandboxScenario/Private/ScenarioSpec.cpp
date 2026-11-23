#include "ScenarioSpec.h"

#include "SandboxJson.h"

namespace
{
	bool ParseEvent(const TSharedPtr<FJsonObject>& Json, int32 Index, FScenarioEvent& OutEvent, FString& OutError)
	{
		if (!Json.IsValid())
		{
			OutError = FString::Printf(TEXT("events[%d] is not an object"), Index);
			return false;
		}
		if (!Json->TryGetNumberField(TEXT("t"), OutEvent.TimeS) || OutEvent.TimeS < 0.0)
		{
			OutError = FString::Printf(TEXT("events[%d].t must be a number >= 0"), Index);
			return false;
		}
		if (!Json->TryGetStringField(TEXT("type"), OutEvent.Type))
		{
			OutError = FString::Printf(TEXT("events[%d].type is missing"), Index);
			return false;
		}

		if (OutEvent.Type == TEXT("impulse"))
		{
			FString Body;
			if (!Json->TryGetStringField(TEXT("body"), Body) || Body.IsEmpty())
			{
				OutError = FString::Printf(TEXT("events[%d].body is missing"), Index);
				return false;
			}
			OutEvent.Body = FName(*Body);

			if (!Json->TryGetNumberField(TEXT("impulse_ns"), OutEvent.ImpulseNs) || OutEvent.ImpulseNs <= 0.0)
			{
				OutError = FString::Printf(TEXT("events[%d].impulse_ns must be a number > 0"), Index);
				return false;
			}

			FVector Direction;
			if (!SandboxJson::TryGetVector(Json, TEXT("direction"), Direction) || Direction.IsNearlyZero())
			{
				OutError = FString::Printf(TEXT("events[%d].direction must be a non-zero [x, y, z]"), Index);
				return false;
			}
			OutEvent.Direction = Direction.GetSafeNormal();
			if (Json->HasField(TEXT("duration_s")) && (!Json->TryGetNumberField(TEXT("duration_s"), OutEvent.DurationS) || OutEvent.DurationS < 0.0))
			{
				OutError = FString::Printf(TEXT("events[%d].duration_s must be a number >= 0"), Index);
				return false;
			}
			return true;
		}

		if (OutEvent.Type == TEXT("pin"))
		{
			FString Body;
			if (!Json->TryGetStringField(TEXT("body"), Body) || Body.IsEmpty())
			{
				OutError = FString::Printf(TEXT("events[%d].body is missing"), Index);
				return false;
			}
			OutEvent.Body = FName(*Body);
			return true;
		}

		if (OutEvent.Type == TEXT("console"))
		{
			if (!Json->TryGetStringField(TEXT("command"), OutEvent.Command) || OutEvent.Command.TrimStartAndEnd().IsEmpty())
			{
				OutError = FString::Printf(TEXT("events[%d].command is missing"), Index);
				return false;
			}
			return true;
		}

		if (OutEvent.Type == TEXT("edit_tuning_file"))
		{
			const TSharedPtr<FJsonObject>* Values = nullptr;
			if (!Json->TryGetObjectField(TEXT("values"), Values) || (*Values)->Values.Num() == 0)
			{
				OutError = FString::Printf(TEXT("events[%d].values must be an object of tuning names and numbers"), Index);
				return false;
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Values)->Values)
			{
				double Value = 0.0;
				if (!Entry.Value.IsValid() || !Entry.Value->TryGetNumber(Value))
				{
					OutError = FString::Printf(TEXT("events[%d].values.%s must be a number"), Index, *Entry.Key);
					return false;
				}
				OutEvent.Values.Emplace(Entry.Key, Value);
			}
			return true;
		}

		if (OutEvent.Type == TEXT("effector"))
		{
			FString Effector;
			if (!Json->TryGetStringField(TEXT("effector"), Effector) || !SandboxEffectors::IsKnown(FName(*Effector)))
			{
				OutError = FString::Printf(TEXT("events[%d].effector must be \"hand_left\" or \"hand_right\""), Index);
				return false;
			}
			OutEvent.Effector = FName(*Effector);

			const TArray<TSharedPtr<FJsonValue>>* Path = nullptr;
			if (!Json->TryGetArrayField(TEXT("path"), Path) || Path->Num() == 0)
			{
				OutError = FString::Printf(TEXT("events[%d].path must be a list of keys"), Index);
				return false;
			}
			for (int32 KeyIndex = 0; KeyIndex < Path->Num(); ++KeyIndex)
			{
				const TSharedPtr<FJsonObject>* Key = nullptr;
				FSandboxPathKey& Out = OutEvent.Path.AddDefaulted_GetRef();
				if (!(*Path)[KeyIndex].IsValid() || !(*Path)[KeyIndex]->TryGetObject(Key) ||
					!(*Key)->TryGetNumberField(TEXT("t"), Out.TimeS) || !SandboxJson::TryGetVector(*Key, TEXT("location_cm"), Out.Location))
				{
					OutError = FString::Printf(TEXT("events[%d].path[%d] needs 't' and 'location_cm' [x, y, z]"), Index, KeyIndex);
					return false;
				}
				// A key without a rotation keeps the one before it.
				if ((*Key)->HasField(TEXT("rotation_deg")))
				{
					FVector PitchYawRoll;
					if (!SandboxJson::TryGetVector(*Key, TEXT("rotation_deg"), PitchYawRoll))
					{
						OutError = FString::Printf(TEXT("events[%d].path[%d].rotation_deg must be [pitch, yaw, roll]"), Index, KeyIndex);
						return false;
					}
					Out.Rotation = FRotator(PitchYawRoll.X, PitchYawRoll.Y, PitchYawRoll.Z).Quaternion();
				}
				else if (KeyIndex > 0)
				{
					Out.Rotation = OutEvent.Path[KeyIndex - 1].Rotation;
				}
			}
			// The same checks the source makes when the path is played: first key at 0, times increasing.
			FSandboxScriptedTargetSource Probe;
			FString PathError;
			if (!Probe.AddPath(OutEvent.Effector, OutEvent.TimeS, OutEvent.Path, PathError))
			{
				OutError = FString::Printf(TEXT("events[%d].path: %s"), Index, *PathError);
				return false;
			}
			return true;
		}

		OutError = FString::Printf(TEXT("events[%d].type '%s' is not supported"), Index, *OutEvent.Type);
		return false;
	}
}

bool FScenarioSpec::FromJson(const TSharedPtr<FJsonObject>& Json, FScenarioSpec& OutSpec, FString& OutError)
{
	OutSpec = FScenarioSpec();
	if (!Json.IsValid())
	{
		OutError = TEXT("Scenario is not a JSON object");
		return false;
	}

	if (!Json->TryGetStringField(TEXT("name"), OutSpec.Name) || OutSpec.Name.IsEmpty())
	{
		OutError = TEXT("'name' is missing");
		return false;
	}
	if (!Json->TryGetNumberField(TEXT("duration_s"), OutSpec.DurationS) || OutSpec.DurationS <= 0.0)
	{
		OutError = TEXT("'duration_s' must be a number > 0");
		return false;
	}
	if (Json->HasField(TEXT("fixed_fps")))
	{
		if (!Json->TryGetNumberField(TEXT("fixed_fps"), OutSpec.FixedFps) || OutSpec.FixedFps < 1 || OutSpec.FixedFps > 1000)
		{
			OutError = TEXT("'fixed_fps' must be an integer between 1 and 1000");
			return false;
		}
	}
	if (Json->HasField(TEXT("checkpoint_interval_s")))
	{
		if (!Json->TryGetNumberField(TEXT("checkpoint_interval_s"), OutSpec.CheckpointIntervalS) || OutSpec.CheckpointIntervalS <= 0.0)
		{
			OutError = TEXT("'checkpoint_interval_s' must be a number > 0");
			return false;
		}
	}

	Json->TryGetStringField(TEXT("map"), OutSpec.MapPath);

	const TSharedPtr<FJsonObject>* Body = nullptr;
	if (!Json->TryGetObjectField(TEXT("body"), Body))
	{
		OutError = TEXT("'body' object is missing");
		return false;
	}
	if (!(*Body)->TryGetStringField(TEXT("mesh"), OutSpec.MeshPath) || OutSpec.MeshPath.IsEmpty())
	{
		OutError = TEXT("'body.mesh' is missing");
		return false;
	}
	(*Body)->TryGetStringField(TEXT("physics_asset"), OutSpec.PhysicsAssetPath);
	if ((*Body)->HasField(TEXT("location_cm")) && !SandboxJson::TryGetVector(*Body, TEXT("location_cm"), OutSpec.SpawnLocationCm))
	{
		OutError = TEXT("'body.location_cm' must be [x, y, z]");
		return false;
	}
	if ((*Body)->HasField(TEXT("rotation_deg")))
	{
		FVector PitchYawRoll;
		if (!SandboxJson::TryGetVector(*Body, TEXT("rotation_deg"), PitchYawRoll))
		{
			OutError = TEXT("'body.rotation_deg' must be [pitch, yaw, roll]");
			return false;
		}
		OutSpec.SpawnRotation = FRotator(PitchYawRoll.X, PitchYawRoll.Y, PitchYawRoll.Z);
	}

	const TArray<TSharedPtr<FJsonValue>>* Attached = nullptr;
	if ((*Body)->TryGetArrayField(TEXT("attached_meshes"), Attached))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Attached)
		{
			FString Path;
			if (!Value.IsValid() || !Value->TryGetString(Path) || Path.IsEmpty())
			{
				OutError = TEXT("'body.attached_meshes' must be an array of mesh paths");
				return false;
			}
			OutSpec.AttachedMeshPaths.Add(Path);
		}
	}
	if ((*Body)->HasField(TEXT("simulate")) && !(*Body)->TryGetBoolField(TEXT("simulate"), OutSpec.bSimulate))
	{
		OutError = TEXT("'body.simulate' must be true or false");
		return false;
	}

	if ((*Body)->HasField(TEXT("control")))
	{
		FString Control;
		(*Body)->TryGetStringField(TEXT("control"), Control);
		if (Control == TEXT("active"))
		{
			OutSpec.Control = EScenarioBodyControl::Active;
		}
		else if (Control != TEXT("passive"))
		{
			OutError = TEXT("'body.control' must be \"passive\" or \"active\"");
			return false;
		}
		if (OutSpec.Control == EScenarioBodyControl::Active && !OutSpec.bSimulate)
		{
			OutError = TEXT("'body.control' \"active\" needs a simulated body ('body.simulate' must not be false)");
			return false;
		}
	}

	if (Json->HasField(TEXT("settle_s")))
	{
		if (!Json->TryGetNumberField(TEXT("settle_s"), OutSpec.SettleS) || OutSpec.SettleS < 0.0)
		{
			OutError = TEXT("'settle_s' must be a number >= 0");
			return false;
		}
	}

	const TSharedPtr<FJsonObject>* Camera = nullptr;
	if (Json->TryGetObjectField(TEXT("camera"), Camera))
	{
		if (!SandboxJson::TryGetVector(*Camera, TEXT("location_cm"), OutSpec.CameraLocationCm) ||
			!SandboxJson::TryGetVector(*Camera, TEXT("look_at_cm"), OutSpec.CameraLookAtCm))
		{
			OutError = TEXT("'camera' needs 'location_cm' and 'look_at_cm', each [x, y, z]");
			return false;
		}
		OutSpec.bHasCamera = true;
	}

	const TArray<TSharedPtr<FJsonValue>>* Events = nullptr;
	if (Json->TryGetArrayField(TEXT("events"), Events))
	{
		for (int32 Index = 0; Index < Events->Num(); ++Index)
		{
			FScenarioEvent Event;
			const TSharedPtr<FJsonValue>& Value = (*Events)[Index];
			if (!ParseEvent(Value.IsValid() ? Value->AsObject() : nullptr, Index, Event, OutError))
			{
				return false;
			}
			OutSpec.Events.Add(Event);
		}
		// Stable sort keeps file order for events that share a timestamp.
		OutSpec.Events.StableSort([](const FScenarioEvent& A, const FScenarioEvent& B) { return A.TimeS < B.TimeS; });
	}

	const TSharedPtr<FJsonObject>* Expect = nullptr;
	if (Json->TryGetObjectField(TEXT("expect"), Expect))
	{
		const TPair<const TCHAR*, TOptional<double>*> Fields[] = {
			{ TEXT("rest_within_s"), &OutSpec.Expect.RestWithinS },
			{ TEXT("max_joint_stretch_cm"), &OutSpec.Expect.MaxJointStretchCm },
			{ TEXT("max_pelvis_drift_cm"), &OutSpec.Expect.MaxPelvisDriftCm },
			{ TEXT("min_head_height_cm"), &OutSpec.Expect.MinHeadHeightCm },
			{ TEXT("max_settled_speed_mps"), &OutSpec.Expect.MaxSettledSpeedMps },
			{ TEXT("max_settled_angular_speed_rads"), &OutSpec.Expect.MaxSettledAngularSpeedRadS },
			{ TEXT("min_settled_angular_speed_rads"), &OutSpec.Expect.MinSettledAngularSpeedRadS },
			{ TEXT("max_joint_speed_rads"), &OutSpec.Expect.MaxJointSpeedRadS },
			{ TEXT("max_physics_step_mean_ms"), &OutSpec.Expect.MaxPhysicsStepMeanMs },
			{ TEXT("min_fall_time_s"), &OutSpec.Expect.MinFallTimeS },
			{ TEXT("max_joint_limit_violation_deg"), &OutSpec.Expect.MaxJointLimitViolationDeg },
			{ TEXT("tuning_changes"), &OutSpec.Expect.TuningChanges },
			{ TEXT("min_effector_impulse_ns"), &OutSpec.Expect.MinEffectorImpulseNs },
			{ TEXT("max_effector_impulse_ns"), &OutSpec.Expect.MaxEffectorImpulseNs },
			{ TEXT("max_effector_force_n"), &OutSpec.Expect.MaxEffectorForceN },
			{ TEXT("min_effector_final_force_n"), &OutSpec.Expect.MinEffectorFinalForceN },
			{ TEXT("max_effector_final_force_n"), &OutSpec.Expect.MaxEffectorFinalForceN },
			{ TEXT("max_effector_penetration_cm"), &OutSpec.Expect.MaxEffectorPenetrationCm },
			{ TEXT("min_effector_impact_speed_mps"), &OutSpec.Expect.MinEffectorImpactSpeedMps },
			{ TEXT("min_struck_displacement_cm"), &OutSpec.Expect.MinStruckDisplacementCm },
			{ TEXT("min_com_margin_cm"), &OutSpec.Expect.MinComMarginCm },
			{ TEXT("min_capture_margin_cm"), &OutSpec.Expect.MinCaptureMarginCm },
			{ TEXT("max_energy_gain_j"), &OutSpec.Expect.MaxEnergyGainJ },
			{ TEXT("max_rest_after_fall_s"), &OutSpec.Expect.MaxRestAfterFallS },
			{ TEXT("max_body_penetration_cm"), &OutSpec.Expect.MaxBodyPenetrationCm },
			{ TEXT("max_self_penetration_cm"), &OutSpec.Expect.MaxSelfPenetrationCm } };

		// Every key must be a known expectation with a numeric value: a misspelt one would otherwise
		// never be checked and the scenario would pass for the wrong reason.
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Expect)->Values)
		{
			if (Entry.Key == TEXT("fell"))
			{
				// Checked by type: the JSON reader would take a number for a boolean.
				if (!Entry.Value.IsValid() || Entry.Value->Type != EJson::Boolean)
				{
					OutError = TEXT("'expect.fell' must be true or false");
					return false;
				}
				OutSpec.Expect.Fell = Entry.Value->AsBool();
				continue;
			}

			if (Entry.Key == TEXT("end_state") || Entry.Key == TEXT("state_sequence"))
			{
				// Each name must be a state of the body; a misspelt one could never be reached.
				FString Text;
				TArray<FString> Names;
				if (Entry.Value.IsValid() && Entry.Value->Type == EJson::String)
				{
					Text = Entry.Value->AsString();
					Text.ParseIntoArray(Names, TEXT(">"), /*bCullEmpty*/ false);
				}
				bool bValid = Names.Num() > 0 && (Entry.Key == TEXT("state_sequence") || Names.Num() == 1);
				for (const FString& Name : Names)
				{
					bValid &= Name == TEXT("standing") || Name == TEXT("flinch") || Name == TEXT("falling") || Name == TEXT("lying");
				}
				if (!bValid)
				{
					OutError = FString::Printf(TEXT("'expect.%s' must be %s of standing, flinch, falling, lying"), *Entry.Key,
						Entry.Key == TEXT("end_state") ? TEXT("one") : TEXT("a '>'-separated list"));
					return false;
				}
				(Entry.Key == TEXT("end_state") ? OutSpec.Expect.EndState : OutSpec.Expect.StateSequence) = Text;
				continue;
			}

			double Value = 0.0;
			bool bKnown = false;
			for (const TPair<const TCHAR*, TOptional<double>*>& Field : Fields)
			{
				if (Entry.Key == Field.Key)
				{
					bKnown = true;
					if (!Entry.Value.IsValid() || !Entry.Value->TryGetNumber(Value))
					{
						OutError = FString::Printf(TEXT("'expect.%s' must be a number"), *Entry.Key);
						return false;
					}
					*Field.Value = Value;
				}
			}
			if (!bKnown)
			{
				OutError = FString::Printf(TEXT("'expect.%s' is not a known expectation"), *Entry.Key);
				return false;
			}
		}
	}

	return true;
}

bool FScenarioSpec::FromString(const FString& Text, FScenarioSpec& OutSpec, FString& OutError)
{
	TSharedPtr<FJsonObject> Json;
	return SandboxJson::ParseObject(Text, Json, OutError) && FromJson(Json, OutSpec, OutError);
}

bool FScenarioSpec::LoadFile(const FString& FilePath, FScenarioSpec& OutSpec, FString& OutError)
{
	TSharedPtr<FJsonObject> Json;
	if (!SandboxJson::LoadObject(FilePath, Json, OutError))
	{
		return false;
	}
	if (!FromJson(Json, OutSpec, OutError))
	{
		OutError = FString::Printf(TEXT("%s (in '%s')"), *OutError, *FilePath);
		return false;
	}
	return true;
}
