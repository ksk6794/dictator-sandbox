#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

/** Small JSON helpers shared by scenarios, parameters and tool result files. */
namespace SandboxJson
{
	SANDBOXCORE_API bool ParseObject(const FString& Text, TSharedPtr<FJsonObject>& OutObject, FString& OutError);
	SANDBOXCORE_API bool LoadObject(const FString& FilePath, TSharedPtr<FJsonObject>& OutObject, FString& OutError);

	/**
	 * Writes UTF-8 without BOM, creating the directory if needed.
	 * Pretty output is for files people read; condensed output keeps large data files small.
	 */
	SANDBOXCORE_API bool SaveObject(const FString& FilePath, const TSharedRef<FJsonObject>& Object, FString& OutError, bool bPretty = true);

	SANDBOXCORE_API TArray<TSharedPtr<FJsonValue>> VectorToArray(const FVector& V);
	SANDBOXCORE_API TArray<TSharedPtr<FJsonValue>> QuatToArray(const FQuat& Q);

	/** Reads a 3-number array field. Returns false and leaves Out untouched if the field is missing or malformed. */
	SANDBOXCORE_API bool TryGetVector(const TSharedPtr<FJsonObject>& Object, const FString& Field, FVector& Out);
}
