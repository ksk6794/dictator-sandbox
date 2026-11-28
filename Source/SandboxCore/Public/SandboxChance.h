#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

/**
 * Chance with a seed (R10, step 2; N9's items 1 and 2 brought forward, docs/active-ragdoll.md, R10, "Chance with a seed"). One stream
 * per purpose, made from the run's seed and the purpose's index, drawn only when a choice is made: the same seed draws the same, and
 * a purpose's draws do not move when another purpose draws. Seed 0 draws nothing: every choice takes the fixed answer its caller
 * gives (N9's meaning of 0, no variation). No noise is added anywhere by this: a draw chooses between answers the owner named, and
 * what each does is the physics'. Pure arithmetic; it never reads a clock.
 */
class SANDBOXCORE_API FSandboxChance
{
public:
	/** What a draw is for. Each has a stream of its own; a new purpose is added at the end, so that older ones keep their streams. */
	enum class EPurpose : uint8
	{
		/** R10, step 7: whether the body struggles in a drag (behaviour.struggle_chance). */
		Struggle,
		/** R10, step 7: an attempt's vigour (behaviour.struggle_vigour_min to 1). */
		Vigour,
		Count
	};

	/** One draw, for result files: its purpose, the stream's draw number, the value drawn (in [0, 1)). */
	struct FDraw
	{
		EPurpose Purpose = EPurpose::Struggle;
		int32 Index = 0;
		double Value = 0.0;
	};

	/** Starts every stream again from this seed; 0 draws nothing. */
	void Reset(int32 InSeed);
	int32 GetSeed() const { return Seed; }

	/** A value in [0, 1) from the purpose's stream; with seed 0, FixedAnswer, and nothing is drawn or kept. */
	double Draw(EPurpose Purpose, double FixedAnswer);
	/** Whether a choice of the given chance comes out yes: a draw below Chance; with seed 0, bFixedAnswer. */
	bool Choose(EPurpose Purpose, double Chance, bool bFixedAnswer);

	const TArray<FDraw>& GetDraws() const { return Draws; }
	static const TCHAR* PurposeName(EPurpose Purpose);
	/** The seed a purpose's stream starts from, for a run's seed: the same pair always gives the same. */
	static int32 StreamSeed(int32 RunSeed, EPurpose Purpose);

private:
	int32 Seed = 0;
	FRandomStream Streams[static_cast<int32>(EPurpose::Count)];
	int32 DrawCount[static_cast<int32>(EPurpose::Count)] = {};
	TArray<FDraw> Draws;
};
