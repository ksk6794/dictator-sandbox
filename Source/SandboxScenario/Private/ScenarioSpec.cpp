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

		if (OutEvent.Type == TEXT("prop_move"))
		{
			FString PropName;
			if (!Json->TryGetStringField(TEXT("prop"), PropName) || PropName.IsEmpty() ||
				!SandboxJson::TryGetVector(Json, TEXT("velocity_mps"), OutEvent.VelocityMps) || OutEvent.VelocityMps.Size2D() <= 0.0 ||
				!Json->TryGetNumberField(TEXT("duration_s"), OutEvent.DurationS) || OutEvent.DurationS <= 0.0 ||
				(Json->HasField(TEXT("ramp_s")) && (!Json->TryGetNumberField(TEXT("ramp_s"), OutEvent.RampS) || OutEvent.RampS < 0.0)))
			{
				OutError = FString::Printf(TEXT("events[%d]: 'prop_move' needs 'prop', a non-zero 'velocity_mps' [x, y, z] (x and y are used), 'duration_s' above 0 and optionally 'ramp_s'"), Index);
				return false;
			}
			OutEvent.Prop = FName(*PropName);
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
			if (const TSharedPtr<FJsonValue> Lock = Json->TryGetField(TEXT("lock_rotation")))
			{
				// Checked by type: the JSON reader would take a number for a boolean.
				if (Lock->Type != EJson::Boolean)
				{
					OutError = FString::Printf(TEXT("events[%d].lock_rotation must be true or false"), Index);
					return false;
				}
				OutEvent.bLockRotation = Lock->AsBool();
			}
			if (const TSharedPtr<FJsonValue> Follow = Json->TryGetField(TEXT("follow_clip")))
			{
				if (Follow->Type != EJson::Boolean)
				{
					OutError = FString::Printf(TEXT("events[%d].follow_clip must be true or false"), Index);
					return false;
				}
				OutEvent.bFollowClip = Follow->AsBool();
				if (OutEvent.bFollowClip && !OutEvent.bLockRotation)
				{
					OutError = FString::Printf(TEXT("events[%d].follow_clip needs lock_rotation: a clip gives the pelvis an orientation too"), Index);
					return false;
				}
			}
			if (Json->HasField(TEXT("raise_cm")) && !Json->TryGetNumberField(TEXT("raise_cm"), OutEvent.RaiseCm))
			{
				OutError = FString::Printf(TEXT("events[%d].raise_cm must be a number"), Index);
				return false;
			}
			return true;
		}

		if (OutEvent.Type == TEXT("tone"))
		{
			if (!Json->TryGetStringField(TEXT("level"), OutEvent.ToneLevel) ||
				(OutEvent.ToneLevel != TEXT("limp") && OutEvent.ToneLevel != TEXT("relaxed") && OutEvent.ToneLevel != TEXT("ready") && OutEvent.ToneLevel != TEXT("braced")) ||
				!Json->TryGetNumberField(TEXT("duration_s"), OutEvent.DurationS) || OutEvent.DurationS < 0.0)
			{
				OutError = FString::Printf(TEXT("events[%d]: 'tone' needs 'level' (limp, relaxed, ready or braced) and 'duration_s' (0 lets go)"), Index);
				return false;
			}
			return true;
		}

		if (OutEvent.Type == TEXT("step"))
		{
			FString Body;
			if (!Json->TryGetStringField(TEXT("body"), Body) || Body.IsEmpty())
			{
				OutError = FString::Printf(TEXT("events[%d].body is missing"), Index);
				return false;
			}
			OutEvent.Body = FName(*Body);
			if (!SandboxJson::TryGetVector(Json, TEXT("offset_cm"), OutEvent.OffsetCm))
			{
				OutError = FString::Printf(TEXT("events[%d].offset_cm must be [x, y, z]"), Index);
				return false;
			}
			return true;
		}

		if (OutEvent.Type == TEXT("hand_target"))
		{
			// The same as a foot's, for an arm: the runner places a limb's end, whichever it is.
			OutEvent.Type = TEXT("foot_target");
		}
		if (OutEvent.Type == TEXT("foot_target"))
		{
			FString Body;
			if (!Json->TryGetStringField(TEXT("body"), Body) || Body.IsEmpty())
			{
				OutError = FString::Printf(TEXT("events[%d].body is missing"), Index);
				return false;
			}
			OutEvent.Body = FName(*Body);
			if (!SandboxJson::TryGetVector(Json, TEXT("offset_cm"), OutEvent.OffsetCm))
			{
				OutError = FString::Printf(TEXT("events[%d].offset_cm must be [x, y, z]"), Index);
				return false;
			}
			if (!Json->TryGetNumberField(TEXT("weight"), OutEvent.Weight) || OutEvent.Weight < 0.0 || OutEvent.Weight > 1.0)
			{
				OutError = FString::Printf(TEXT("events[%d].weight must be a number from 0 to 1"), Index);
				return false;
			}
			return true;
		}

		if (OutEvent.Type == TEXT("clip_pose"))
		{
			if (const TSharedPtr<FJsonValue> Release = Json->TryGetField(TEXT("release")))
			{
				// Checked by type: the JSON reader would take a number for a boolean.
				if (Release->Type != EJson::Boolean)
				{
					OutError = FString::Printf(TEXT("events[%d].release must be true or false"), Index);
					return false;
				}
				OutEvent.bRelease = Release->AsBool();
			}
			if (OutEvent.bRelease)
			{
				return true;
			}
			if (!Json->TryGetStringField(TEXT("clip"), OutEvent.Clip) || OutEvent.Clip.TrimStartAndEnd().IsEmpty())
			{
				OutError = FString::Printf(TEXT("events[%d].clip must be the path of an animation sequence"), Index);
				return false;
			}
			if (!Json->TryGetNumberField(TEXT("time_s"), OutEvent.ClipTimeS) || OutEvent.ClipTimeS < 0.0)
			{
				OutError = FString::Printf(TEXT("events[%d].time_s must be a number >= 0"), Index);
				return false;
			}
			if (Json->HasField(TEXT("rate")) && (!Json->TryGetNumberField(TEXT("rate"), OutEvent.ClipRate) || OutEvent.ClipRate < 0.0))
			{
				OutError = FString::Printf(TEXT("events[%d].rate must be a number >= 0"), Index);
				return false;
			}
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
				OutError = FString::Printf(TEXT("events[%d].effector must be \"hand_left\", \"hand_right\", \"foot_left\" or \"foot_right\""), Index);
				return false;
			}
			OutEvent.Effector = FName(*Effector);
			FString AtBone;
			if (Json->HasField(TEXT("at_bone")))
			{
				if (!Json->TryGetStringField(TEXT("at_bone"), AtBone) || AtBone.IsEmpty())
				{
					OutError = FString::Printf(TEXT("events[%d].at_bone must be the name of a body of the character"), Index);
					return false;
				}
				OutEvent.AtBone = FName(*AtBone);
			}

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
				// And so with the grip: 1 a fist, which a scripted hand is unless told, 0 an open hand.
				if ((*Key)->HasField(TEXT("grip")))
				{
					double Grip = 0.0;
					if (!(*Key)->TryGetNumberField(TEXT("grip"), Grip) || Grip < 0.0 || Grip > 1.0)
					{
						OutError = FString::Printf(TEXT("events[%d].path[%d].grip must be a number from 0 (open) to 1 (fist)"), Index, KeyIndex);
						return false;
					}
					Out.Grip = static_cast<float>(Grip);
				}
				else if (KeyIndex > 0)
				{
					Out.Grip = OutEvent.Path[KeyIndex - 1].Grip;
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
	if (Json->HasField(TEXT("end_when_standing_s")))
	{
		if (!Json->TryGetNumberField(TEXT("end_when_standing_s"), OutSpec.EndWhenStandingS) || OutSpec.EndWhenStandingS <= 0.0)
		{
			OutError = TEXT("'end_when_standing_s' must be a number > 0");
			return false;
		}
	}
	if (Json->HasField(TEXT("pose_prior")) && (!Json->TryGetStringField(TEXT("pose_prior"), OutSpec.PosePrior) || OutSpec.PosePrior.IsEmpty()))
	{
		OutError = TEXT("'pose_prior' must be the path of a pose database");
		return false;
	}
	if (Json->HasField(TEXT("pose_prior_clips")) &&
		(!Json->TryGetStringArrayField(TEXT("pose_prior_clips"), OutSpec.PosePriorClips) || OutSpec.PosePriorClips.Num() == 0 || OutSpec.PosePrior.IsEmpty()))
	{
		OutError = TEXT("'pose_prior_clips' must be a list of animation paths, beside the 'pose_prior' to write");
		return false;
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

	if (Json->HasField(TEXT("props")))
	{
		const TArray<TSharedPtr<FJsonValue>>* Props = nullptr;
		if (!Json->TryGetArrayField(TEXT("props"), Props))
		{
			OutError = TEXT("'props' must be a list of things with mass");
			return false;
		}
		for (int32 Index = 0; Index < Props->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject>* Prop = nullptr;
			FScenarioProp& Out = OutSpec.Props.AddDefaulted_GetRef();
			FString Name;
			if (!(*Props)[Index].IsValid() || !(*Props)[Index]->TryGetObject(Prop) || !(*Prop)->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty() ||
				!(*Prop)->TryGetNumberField(TEXT("mass_kg"), Out.MassKg) || !SandboxJson::TryGetVector(*Prop, TEXT("location_cm"), Out.LocationCm) ||
				(*Prop)->HasField(TEXT("boxes")) == (*Prop)->HasField(TEXT("size_cm")) ||
				((*Prop)->HasField(TEXT("size_cm")) && !SandboxJson::TryGetVector(*Prop, TEXT("size_cm"), Out.SizeCm)))
			{
				OutError = FString::Printf(TEXT("props[%d] needs 'name', 'mass_kg', 'location_cm' [x, y, z] and either 'size_cm' [x, y, z] (a box, location_cm is its middle) or 'boxes'"), Index);
				return false;
			}
			Out.Name = FName(*Name);
			const TArray<TSharedPtr<FJsonValue>>* Boxes = nullptr;
			if ((*Prop)->HasField(TEXT("boxes")))
			{
				if (!(*Prop)->TryGetArrayField(TEXT("boxes"), Boxes) || Boxes->Num() == 0)
				{
					OutError = FString::Printf(TEXT("props[%d] '%s': 'boxes' must list at least one box"), Index, *Name);
					return false;
				}
				for (int32 BoxIndex = 0; BoxIndex < Boxes->Num(); ++BoxIndex)
				{
					const TSharedPtr<FJsonObject>* BoxJson = nullptr;
					FScenarioPropBox& Box = Out.Boxes.AddDefaulted_GetRef();
					if (!(*Boxes)[BoxIndex].IsValid() || !(*Boxes)[BoxIndex]->TryGetObject(BoxJson) || !SandboxJson::TryGetVector(*BoxJson, TEXT("center_cm"), Box.CenterCm) ||
						!SandboxJson::TryGetVector(*BoxJson, TEXT("size_cm"), Box.SizeCm) || Box.SizeCm.GetMin() < 1.0 || Box.SizeCm.GetMax() > 1000.0)
					{
						OutError = FString::Printf(TEXT("props[%d] '%s': boxes[%d] needs 'center_cm' [x, y, z] (from the thing's origin) and 'size_cm' [x, y, z], edges from 1 to 1000 cm"), Index, *Name, BoxIndex);
						return false;
					}
				}
			}
			if ((Out.Boxes.Num() == 0 && (Out.SizeCm.GetMin() < 1.0 || Out.SizeCm.GetMax() > 1000.0)) || Out.MassKg <= 0.0 || Out.MassKg > 1000.0)
			{
				OutError = FString::Printf(TEXT("props[%d] '%s': edges from 1 to 1000 cm and a mass above 0 and up to 1000 kg"), Index, *Name);
				return false;
			}
			if ((*Prop)->HasField(TEXT("rotation_deg")))
			{
				FVector PitchYawRoll;
				if (!SandboxJson::TryGetVector(*Prop, TEXT("rotation_deg"), PitchYawRoll))
				{
					OutError = FString::Printf(TEXT("props[%d].rotation_deg must be [pitch, yaw, roll]"), Index);
					return false;
				}
				Out.Rotation = FRotator(PitchYawRoll.X, PitchYawRoll.Y, PitchYawRoll.Z);
			}
			if ((*Prop)->HasField(TEXT("handle_cm")))
			{
				FVector Handle;
				if (!SandboxJson::TryGetVector(*Prop, TEXT("handle_cm"), Handle))
				{
					OutError = FString::Printf(TEXT("props[%d].handle_cm must be [x, y, z], a point of the thing from its middle"), Index);
					return false;
				}
				Out.HandleCm = Handle;
			}
			for (int32 Other = 0; Other < Index; ++Other)
			{
				if (OutSpec.Props[Other].Name == Out.Name)
				{
					OutError = FString::Printf(TEXT("props[%d]: there is a prop named '%s' already"), Index, *Name);
					return false;
				}
			}
		}
	}

	if (!OutSpec.AddRoomProps(OutError))
	{
		return false;
	}

	if (Json->HasField(TEXT("recording")))
	{
		if (!Json->TryGetStringField(TEXT("recording"), OutSpec.Recording) || OutSpec.Recording.IsEmpty())
		{
			OutError = TEXT("'recording' must be the path of a recording of the hands");
			return false;
		}
	}
	if (Json->HasField(TEXT("recording_start_s")))
	{
		if (OutSpec.Recording.IsEmpty() || !Json->TryGetNumberField(TEXT("recording_start_s"), OutSpec.RecordingStartS) || OutSpec.RecordingStartS < 0.0)
		{
			OutError = TEXT("'recording_start_s' must be a number >= 0, in a scenario with a 'recording'");
			return false;
		}
	}

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
	if ((*Body)->HasField(TEXT("laid_deg")))
	{
		if (!(*Body)->TryGetNumberField(TEXT("laid_deg"), OutSpec.LaidTurnDeg) || FMath::Abs(OutSpec.LaidTurnDeg) > 180.0)
		{
			OutError = TEXT("'body.laid_deg' must be a number of degrees from -180 to 180");
			return false;
		}
		// The body is spawned standing on the floor and laid there: a controller takes the height it starts at for its floor.
		if (!(*Body)->HasField(TEXT("location_cm")) || !FMath::IsNearlyZero(OutSpec.SpawnLocationCm.Z))
		{
			OutError = TEXT("'body.laid_deg' needs 'body.location_cm' with z 0: the body is spawned standing on the floor, then laid down");
			return false;
		}
		OutSpec.bLaid = true;
	}
	if ((*Body)->HasField(TEXT("laid_height_cm")))
	{
		if (!OutSpec.bLaid || !(*Body)->TryGetNumberField(TEXT("laid_height_cm"), OutSpec.LaidHeightCm) || OutSpec.LaidHeightCm < 0.0)
		{
			OutError = TEXT("'body.laid_height_cm' must be a height of 0 cm or more, beside 'body.laid_deg'");
			return false;
		}
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
	if (OutSpec.bLaid && !OutSpec.bSimulate)
	{
		OutError = TEXT("'body.laid_deg' needs a simulated body: a kinematic one is put back where its pose has it");
		return false;
	}

	if (const TSharedPtr<FJsonValue> GetUp = (*Body)->TryGetField(TEXT("get_up")))
	{
		if (GetUp->Type != EJson::Boolean)
		{
			OutError = TEXT("'body.get_up' must be true or false");
			return false;
		}
		OutSpec.bGetUp = GetUp->AsBool();
	}
	if (const TSharedPtr<FJsonValue> Perception = (*Body)->TryGetField(TEXT("perception")))
	{
		if (Perception->Type != EJson::Boolean)
		{
			OutError = TEXT("'body.perception' must be true or false");
			return false;
		}
		OutSpec.bPerception = Perception->AsBool();
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
			{ TEXT("min_pelvis_drift_cm"), &OutSpec.Expect.MinPelvisDriftCm },
			{ TEXT("min_head_height_cm"), &OutSpec.Expect.MinHeadHeightCm },
			{ TEXT("max_settled_speed_mps"), &OutSpec.Expect.MaxSettledSpeedMps },
			{ TEXT("max_settled_angular_speed_rads"), &OutSpec.Expect.MaxSettledAngularSpeedRadS },
			{ TEXT("min_settled_angular_speed_rads"), &OutSpec.Expect.MinSettledAngularSpeedRadS },
			{ TEXT("min_settled_head_travel_cm"), &OutSpec.Expect.MinSettledHeadTravelCm },
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
			{ TEXT("min_felt_impulse_ns"), &OutSpec.Expect.MinFeltImpulseNs },
			{ TEXT("min_prop_travel_cm"), &OutSpec.Expect.MinPropTravelCm },
			{ TEXT("max_prop_travel_cm"), &OutSpec.Expect.MaxPropTravelCm },
			{ TEXT("max_prop_speed_mps"), &OutSpec.Expect.MaxPropSpeedMps },
			{ TEXT("min_com_margin_cm"), &OutSpec.Expect.MinComMarginCm },
			{ TEXT("min_capture_margin_cm"), &OutSpec.Expect.MinCaptureMarginCm },
			{ TEXT("max_energy_gain_j"), &OutSpec.Expect.MaxEnergyGainJ },
			{ TEXT("max_rest_after_fall_s"), &OutSpec.Expect.MaxRestAfterFallS },
			{ TEXT("max_body_penetration_cm"), &OutSpec.Expect.MaxBodyPenetrationCm },
			{ TEXT("max_self_penetration_cm"), &OutSpec.Expect.MaxSelfPenetrationCm },
			{ TEXT("max_pose_error_mean_deg"), &OutSpec.Expect.MaxPoseErrorMeanDeg },
			{ TEXT("min_pose_offset_deg"), &OutSpec.Expect.MinPoseOffsetDeg },
			{ TEXT("max_foot_target_error_cm"), &OutSpec.Expect.MaxFootTargetErrorCm },
			{ TEXT("max_assist_force_share_mean"), &OutSpec.Expect.MaxAssistForceShareMean },
			{ TEXT("max_steps"), &OutSpec.Expect.MaxSteps },
			{ TEXT("min_steps"), &OutSpec.Expect.MinSteps },
			{ TEXT("max_landing_error_cm"), &OutSpec.Expect.MaxLandingErrorCm },
			{ TEXT("max_recovery_steps"), &OutSpec.Expect.MaxRecoverySteps },
			{ TEXT("max_stance_error_cm"), &OutSpec.Expect.MaxStanceErrorCm },
			{ TEXT("max_flinches"), &OutSpec.Expect.MaxFlinches },
			{ TEXT("double_overs"), &OutSpec.Expect.DoubleOvers },
			{ TEXT("knockdowns"), &OutSpec.Expect.Knockdowns },
			{ TEXT("min_get_ups"), &OutSpec.Expect.MinGetUps },
			{ TEXT("max_get_ups"), &OutSpec.Expect.MaxGetUps },
			{ TEXT("max_get_up_attempts"), &OutSpec.Expect.MaxGetUpAttempts },
			{ TEXT("min_get_up_attempts"), &OutSpec.Expect.MinGetUpAttempts },
			{ TEXT("hit_events"), &OutSpec.Expect.HitEvents },
			{ TEXT("grab_events"), &OutSpec.Expect.GrabEvents },
			{ TEXT("effector_shape_changes"), &OutSpec.Expect.EffectorShapeChanges },
			{ TEXT("effector_grips_on_body"), &OutSpec.Expect.EffectorGripsOnBody },
			{ TEXT("effector_grabs"), &OutSpec.Expect.EffectorGrabs },
			{ TEXT("effector_strikes"), &OutSpec.Expect.EffectorStrikes },
			{ TEXT("effector_grab_slips"), &OutSpec.Expect.EffectorGrabSlips },
			{ TEXT("max_grab_stretch_cm"), &OutSpec.Expect.MaxGrabStretchCm },
			{ TEXT("min_grab_frames"), &OutSpec.Expect.MinGrabFrames },
			{ TEXT("effector_grab_overloads"), &OutSpec.Expect.EffectorGrabOverloads },
			{ TEXT("effector_handle_grabs"), &OutSpec.Expect.EffectorHandleGrabs },
			{ TEXT("max_grab_force_n"), &OutSpec.Expect.MaxGrabForceN },
			{ TEXT("min_pelvis_anchor_shift_cm"), &OutSpec.Expect.MinPelvisAnchorShiftCm },
			{ TEXT("min_lean_cm"), &OutSpec.Expect.MinLeanCm } };

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

			if (Entry.Key == TEXT("measures"))
			{
				// Bounds on figures of the result, each by its path: min, max, or what it has to equal.
				const TSharedPtr<FJsonObject>* Measures = nullptr;
				if (!Entry.Value.IsValid() || !Entry.Value->TryGetObject(Measures))
				{
					OutError = TEXT("'expect.measures' must be an object of paths");
					return false;
				}
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Each : (*Measures)->Values)
				{
					FScenarioExpect::FMeasure& Measure = OutSpec.Expect.Measures.AddDefaulted_GetRef();
					Measure.Path = Each.Key;
					const TSharedPtr<FJsonObject>* Bounds = nullptr;
					bool bValid = Each.Value.IsValid() && Each.Value->TryGetObject(Bounds) && (*Bounds)->Values.Num() > 0;
					if (bValid)
					{
						for (const TPair<FString, TSharedPtr<FJsonValue>>& Bound : (*Bounds)->Values)
						{
							const EJson Kind = Bound.Value.IsValid() ? Bound.Value->Type : EJson::None;
							if (Bound.Key == TEXT("min") && Kind == EJson::Number)
							{
								Measure.Min = Bound.Value->AsNumber();
							}
							else if (Bound.Key == TEXT("max") && Kind == EJson::Number)
							{
								Measure.Max = Bound.Value->AsNumber();
							}
							else if (Bound.Key == TEXT("equals") && Kind == EJson::Boolean)
							{
								Measure.Equals = Bound.Value->AsBool();
							}
							else if (Bound.Key == TEXT("equals") && Kind == EJson::String)
							{
								Measure.EqualsText = Bound.Value->AsString();
							}
							else if (Bound.Key == TEXT("equals") && Kind == EJson::Null)
							{
								Measure.bNull = true;
							}
							else
							{
								bValid = false;
							}
						}
					}
					// A bound and an exact value at once: one of the two would never be checked. And a path needs a name in it.
					bValid &= !((Measure.Min.IsSet() || Measure.Max.IsSet()) && (Measure.Equals.IsSet() || Measure.EqualsText.IsSet() || Measure.bNull));
					TArray<FString> Parts;
					bValid &= Measure.Path.ParseIntoArray(Parts, TEXT(".")) > 0;
					if (!bValid)
					{
						OutError = FString::Printf(TEXT("'expect.measures.%s' must have 'min' and 'max' (numbers) or 'equals' (true, false, a text or null)"), *Each.Key);
						return false;
					}
				}
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
					bValid &= Name == TEXT("standing") || Name == TEXT("flinch") || Name == TEXT("stagger") || Name == TEXT("falling") || Name == TEXT("lying") ||
						Name == TEXT("get_up");
				}
				if (!bValid)
				{
					OutError = FString::Printf(TEXT("'expect.%s' must be %s of standing, flinch, stagger, falling, lying, get_up"), *Entry.Key,
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

FQuat FScenarioSpec::LaidRotation() const
{
	// In the body's own spawn frame (facing +y, up +z, its right toward -x): a quarter turn about x puts the head toward -y and the
	// face up; a turn about the head's direction then rolls it, +90 onto its right side. Taken into the world by the spawn's turn.
	const FQuat OnBack(FVector::XAxisVector, UE_DOUBLE_HALF_PI);
	const FQuat AboutLength(-FVector::YAxisVector, FMath::DegreesToRadians(LaidTurnDeg));
	const FQuat Spawn = SpawnRotation.Quaternion();
	return Spawn * (AboutLength * OnBack) * Spawn.Inverse();
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

bool FScenarioSpec::AddRoomProps(FString& OutError)
{
	if (MapPath.IsEmpty())
	{
		return true;
	}
	const FString Dir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Specs"), TEXT("rooms"));
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(Dir, TEXT("*.json")), true, false);
	Files.Sort();
	for (const FString& File : Files)
	{
		const FString Path = FPaths::Combine(Dir, File);
		FString Text;
		TSharedPtr<FJsonObject> Room;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Room) || !Room.IsValid())
		{
			OutError = FString::Printf(TEXT("the room spec '%s' cannot be read"), *Path);
			return false;
		}
		FString Output;
		const TArray<TSharedPtr<FJsonValue>>* Furniture = nullptr;
		if (!Room->TryGetStringField(TEXT("output"), Output) || Output != MapPath || !Room->TryGetArrayField(TEXT("furniture"), Furniture))
		{
			continue;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Furniture)
		{
			const TSharedPtr<FJsonObject>* Piece = nullptr;
			double MassKg = 0.0;
			FString PieceName;
			if (!Value.IsValid() || !Value->TryGetObject(Piece) || !(*Piece)->TryGetNumberField(TEXT("mass_kg"), MassKg) || !(*Piece)->TryGetStringField(TEXT("name"), PieceName))
			{
				continue; // A static piece: it is part of the map.
			}
			FScenarioProp Prop;
			Prop.Name = FName(*PieceName);
			Prop.MassKg = MassKg;
			double YawDeg = 0.0;
			(*Piece)->TryGetNumberField(TEXT("yaw_deg"), YawDeg);
			Prop.Rotation = FRotator(0.0, YawDeg, 0.0);
			const TArray<TSharedPtr<FJsonValue>>* Boxes = nullptr;
			bool bGood = SandboxJson::TryGetVector(*Piece, TEXT("location_cm"), Prop.LocationCm) && (*Piece)->TryGetArrayField(TEXT("boxes"), Boxes) && Boxes->Num() > 0;
			for (int32 Index = 0; bGood && Index < Boxes->Num(); ++Index)
			{
				const TSharedPtr<FJsonObject>* Box = nullptr;
				FScenarioPropBox& Out = Prop.Boxes.AddDefaulted_GetRef();
				bGood = (*Boxes)[Index].IsValid() && (*Boxes)[Index]->TryGetObject(Box) && SandboxJson::TryGetVector(*Box, TEXT("center_cm"), Out.CenterCm) &&
					SandboxJson::TryGetVector(*Box, TEXT("size_cm"), Out.SizeCm) && Out.SizeCm.GetMin() >= 1.0;
			}
			if (!bGood)
			{
				OutError = FString::Printf(TEXT("%s: the piece '%s' has mass but no usable 'location_cm' and 'boxes'"), *Path, *PieceName);
				return false;
			}
			const bool bOwn = Props.ContainsByPredicate([&Prop](const FScenarioProp& Each) { return Each.Name == Prop.Name; });
			if (!bOwn)
			{
				Props.Add(Prop);
			}
		}
	}
	return true;
}
