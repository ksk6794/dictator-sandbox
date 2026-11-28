#pragma once

#include "CoreMinimal.h"

class ACameraActor;
class ASandboxEffectorRig;
class ASandboxRagdollActor;
class UWorld;
struct FScenarioSpec;
struct FSandboxSeen;

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
	 * Makes the world's physics give the same result however busy the machine is. Chaos rebuilds the structure it
	 * finds colliding pairs with in a background task and takes the new one in on whichever step the task happens
	 * to be done by; the dynamic bodies' boxes in it are padded from when they were last put in, so a pair can be
	 * found a step earlier or later, and with it a contact. The solver's determinism mode does not cover this. A
	 * body that fell came out differently in 3 to 18 runs of 100 on a loaded machine, never on an idle one.
	 * Before every step the task is now waited for, twice, and none runs during the step. Called by
	 * SpawnEnvironment; the callback lives and dies with the world's solver.
	 */
	static void KeepSolverInStep(UWorld& World);

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

	/**
	 * The scenario's things with mass ("props"): each a box that is simulated from the first step, with its
	 * mass, that collides with the world, the character and the hands, and that a hand can take hold of. In
	 * the order of the scenario's list; an entry is null if its box could not be made (logged).
	 */
	static TArray<AActor*> SpawnProps(UWorld& World, const FScenarioSpec& Spec);

	/**
	 * Tells the body which of its parts the hands hold now (USandboxBodyController::TellHolds). The hands do
	 * not know the character and the character does not know the hands; a run and a session know both, and
	 * call this once a frame. Either may be null.
	 */
	static void TellBodyOfHolds(const ASandboxEffectorRig* Rig, ASandboxRagdollActor* Body);
	/**
	 * Tells the body what moves near it (N5): the hands (with their grips) and feet of the rig and the things with mass, where they are and
	 * how fast; the player's body (the effector "body") and the viewers given (the player's head: a scenario's "viewer", a session's camera
	 * or headset) as ESandboxSeenKind::Viewer (R10, step 1).
	 */
	static void TellBodyWhatMoves(const ASandboxEffectorRig* Rig, TConstArrayView<TObjectPtr<AActor>> Props, ASandboxRagdollActor* Body,
		TConstArrayView<FSandboxSeen> Viewers = {}, bool bViewersOnly = false);
};
