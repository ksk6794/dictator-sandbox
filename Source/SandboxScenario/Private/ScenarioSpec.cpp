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
		double Value = 0.0;
		if ((*Expect)->TryGetNumberField(TEXT("rest_within_s"), Value))
		{
			OutSpec.Expect.RestWithinS = Value;
		}
		if ((*Expect)->TryGetNumberField(TEXT("max_joint_stretch_cm"), Value))
		{
			OutSpec.Expect.MaxJointStretchCm = Value;
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
