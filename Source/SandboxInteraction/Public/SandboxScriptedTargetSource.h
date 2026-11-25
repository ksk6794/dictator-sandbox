#pragma once

#include "CoreMinimal.h"
#include "SandboxTargetSource.h"

/** One point of a scripted trajectory: where the effector's target is at a time after the trajectory began. */
struct FSandboxPathKey
{
	double TimeS = 0.0;
	FVector Location = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;
	/** 0 an open hand to 1 a fist. A scripted hand is a fist unless a key says otherwise. */
	float Grip = 1.0f;
};

/**
 * A target source that plays trajectories: what a scenario file's "effector" events turn into, and what a
 * recorded session will replay through (M4).
 *
 * A trajectory is a list of keys; the target moves in a straight line between two keys at constant speed
 * and stays on the last key when the trajectory has ended. Before an effector's first trajectory begins its
 * target rests on that trajectory's first key. A trajectory that begins while an earlier one of the same
 * effector is still running takes over.
 *
 * The owner sets the time; the source never reads a clock.
 */
class SANDBOXINTERACTION_API FSandboxScriptedTargetSource : public ISandboxTargetSource
{
public:
	/**
	 * Adds a trajectory that begins at StartS. Key times count from that moment, begin at 0 and increase.
	 * False with a reason if they do not.
	 */
	bool AddPath(FName Effector, double StartS, const TArray<FSandboxPathKey>& Keys, FString& OutError);

	void SetTime(double InTimeS) { TimeS = InTimeS; }
	double GetTime() const { return TimeS; }

	virtual bool GetTarget(FName Effector, FSandboxEffectorTarget& OutTarget) const override;

	/** The target of an effector at any time. False for an effector without a trajectory. */
	bool Evaluate(FName Effector, double AtTimeS, FSandboxEffectorTarget& OutTarget) const;

	/** Every effector that has a trajectory, in the order they were first named. */
	void GetEffectors(TArray<FName>& OutEffectors) const;

	/**
	 * Moves every key but the first of the trajectory that starts at StartS by an offset: for a trajectory
	 * written from a place that is known only when it begins (a part of a body that has fallen somewhere).
	 * The first key is where the hand waits until then, and stays. False if there is no such trajectory or
	 * it has no key after its first.
	 */
	bool ShiftPath(FName Effector, double StartS, const FVector& OffsetCm);

private:
	struct FPath
	{
		double StartS = 0.0;
		TArray<FSandboxPathKey> Keys;
	};

	/** Paths by effector, each list sorted by start time. */
	TArray<TPair<FName, TArray<FPath>>> Paths;
	double TimeS = 0.0;
};
