#pragma once

#include "CoreMinimal.h"

struct FRoomLightSpec
{
	FVector LocationCm = FVector::ZeroVector;
	/** Point-light intensity in the engine's default point-light units. */
	double Intensity = 0.0;
	FLinearColor Color = FLinearColor::White;
	double AttenuationRadiusCm = 1000.0;
	double SourceRadiusCm = 5.0;
	/** Static lights are baked entirely; stationary lights bake indirect light and keep dynamic shadows. */
	bool bStationary = false;
};

/**
 * A box room described in Specs/rooms/*.json (format in Specs/README.md).
 * The floor's top face is at z = 0 and the room is centred on the origin, matching the scenarios.
 */
struct SANDBOXEDITOR_API FRoomSpec
{
	FString Name;
	/** Long package name of the generated map, e.g. /Game/Generated/Maps/TestRoom. */
	FString OutputPath;
	/** Interior size: x, y, height. */
	FVector SizeCm = FVector(500.0, 400.0, 270.0);
	double WallThicknessCm = 20.0;
	int32 LightmapResolution = 128;
	TArray<FRoomLightSpec> Lights;

	static bool LoadFile(const FString& FilePath, FRoomSpec& OutSpec, FString& OutError);
	static TArray<FString> FindSpecFiles();
};

/** Builds the room's map (static shell, lights, reflection capture, player start) and saves it unlit. */
struct SANDBOXEDITOR_API FRoomGenerator
{
	static bool Generate(const FRoomSpec& Spec, FString& OutError);
};
