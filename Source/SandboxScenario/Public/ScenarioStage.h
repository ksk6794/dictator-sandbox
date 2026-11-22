#pragma once

#include "CoreMinimal.h"

class ACameraActor;
class ASandboxRagdollActor;
class UWorld;
struct FScenarioSpec;

/**
 * Builds in a world what a scenario file describes: the floor and lights (unless the scenario's map brings
 * its own) and the body with its control. Shared by the scenario runner, which then measures the run, and
 * the play session, which hands the same scene to a person.
 */
struct SANDBOXSCENARIO_API FScenarioStage
{
	/** True when the process renders in stereo through a headset. */
	static bool IsStereo();

	/**
	 * Floor and, in a rendered run, two lights, when the scenario names no map. With bSpawnCamera a camera
	 * actor at the scenario's camera (or one that frames the origin) is returned, in rendered runs only.
	 */
	static ACameraActor* SpawnEnvironment(UWorld& World, const FScenarioSpec& Spec, bool bSpawnCamera);

	/**
	 * The body: mesh, physics asset, attached meshes, and for "control": "active" the body controller on
	 * the process-wide tuning store, which is loaded if it was not. Null with a reason on failure.
	 */
	static ASandboxRagdollActor* SpawnBody(UWorld& World, const FScenarioSpec& Spec, FString& OutError);
};
