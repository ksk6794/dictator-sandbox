#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** A scripted stimulus applied at a fixed simulation time. M0 supports one type: "impulse". */
struct FScenarioEvent
{
	double TimeS = 0.0;
	FString Type;
	FName Body;
	/** Impulse magnitude in newton-seconds. */
	double ImpulseNs = 0.0;
	/** Unit direction in world space. */
	FVector Direction = FVector::ForwardVector;
};

/** Optional pass criteria. Unset values are not checked. */
struct FScenarioExpect
{
	TOptional<double> RestWithinS;
	TOptional<double> MaxJointStretchCm;
};

/**
 * One scenario file from Scenarios/*.json. See Scenarios/README.md for the format.
 * Parsing is strict: unknown event types and malformed fields are errors, so a typo cannot silently pass.
 */
struct SANDBOXSCENARIO_API FScenarioSpec
{
	FString Name;
	double DurationS = 0.0;
	int32 FixedFps = 90;
	double CheckpointIntervalS = 0.5;

	FString MeshPath;
	FString PhysicsAssetPath;
	FVector SpawnLocationCm = FVector(0.0, 0.0, 150.0);
	/** Pitch, yaw, roll in degrees. */
	FRotator SpawnRotation = FRotator::ZeroRotator;

	TArray<FScenarioEvent> Events;
	FScenarioExpect Expect;

	double FixedDt() const { return 1.0 / FixedFps; }
	int32 TotalFrames() const { return FMath::RoundToInt32(DurationS * FixedFps); }

	static bool FromJson(const TSharedPtr<FJsonObject>& Json, FScenarioSpec& OutSpec, FString& OutError);
	static bool FromString(const FString& Text, FScenarioSpec& OutSpec, FString& OutError);
	static bool LoadFile(const FString& FilePath, FScenarioSpec& OutSpec, FString& OutError);
};
