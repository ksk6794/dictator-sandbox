#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "SandboxBodyController.h"

/**
 * What the world gave the body over a run (control.authority; docs/active-ragdoll.md, section 5 and decision 15): the forces of
 * the old balance assist and of the led get-up's carry, frame by frame, added up by the body's state and, in the states asked for,
 * body by body. Every figure is a share of the body's weight: a frame's share is the sum over the bodies of the size of each force
 * (or of its upward part, its part over the floor, its downward part) over the weight; a state's mean is the mean of its frames'
 * shares, its peak the largest. The three directions need not add up to the whole. Torques are in N*m. Arithmetic only.
 */
class SANDBOXSCENARIO_API FSandboxAuthorityTally
{
public:
	struct FSums
	{
		int32 Frames = 0;
		double Sum[4] = { 0.0, 0.0, 0.0, 0.0 };
		double Peak[4] = { 0.0, 0.0, 0.0, 0.0 };
		double TorqueSumNm = 0.0;
		double TorquePeakNm = 0.0;
		double Mean(int32 Way) const { return Frames > 0 ? Sum[Way] / Frames : 0.0; }
	};
	/** The four ways a force is counted: whole, upward, over the floor, downward. */
	enum EWay : int32 { All = 0, Up = 1, Horizontal = 2, Down = 3 };

	struct FBodySums
	{
		FName Bone;
		FSandboxAuthorityEntry::ESource Source = FSandboxAuthorityEntry::ESource::Assist;
		double Sum[4] = { 0.0, 0.0, 0.0, 0.0 };
		double Peak = 0.0;
	};

	/**
	 * One frame: the entries the controller gave for it, in the state it was in. bByBody adds them body by body too (a body's mean is
	 * over every frame of the state, the frames it was given nothing in counting as 0).
	 */
	void AddFrame(const FString& State, TConstArrayView<FSandboxAuthorityEntry> Entries, double WeightN, double FrameS, bool bByBody);

	const FSums* FindState(const FString& State) const { return States.Find(State); }
	/** The bodies of a state, by their mean, largest first; only bodies given something. */
	TArray<FBodySums> BodiesOf(const FString& State) const;

	/** { "weight_n", "states": { name: {...} }, "<state>_bodies": [...] for each state counted body by body }. */
	TSharedRef<FJsonObject> ToJson() const;

private:
	TMap<FString, FSums> States;
	TMap<FString, double> SecondsOf;
	/** By state, then by body and source. */
	TMap<FString, TArray<FBodySums>> Bodies;
	double LastWeightN = 0.0;
};
