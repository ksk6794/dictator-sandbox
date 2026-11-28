#pragma once

#include "CoreMinimal.h"

/**
 * The arbiter of limbs (R10, step 3; docs/recon/r10_plan.md). Several users of the body's limbs place them on top of the pose: R3's
 * catch and N6's reach in a fall, the steps' feet, R10's cover and lowering, N5's guarding arm, R2's answer to a held arm, a command,
 * staying down, and a scenario's own targets. Each user says, every tick, which limbs it asks for; the arbiter says which one holds
 * each limb: the asker of the highest priority (the lowest number of ESandboxLimbUser). It also keeps a record of who held what, when
 * the holder changed, and how long two users asked for one limb at once, for result files (control.behaviour.limbs).
 *
 * What a user that does not hold its limb does is the user's own: it lets the limb go over its pose's blend, as it would when it is done
 * (a target taken over in a step turned an arm over in R3's night). Pure arithmetic; it never reads a clock and touches no body.
 */
enum class ESandboxLimb : uint8
{
	LeftArm,
	RightArm,
	LeftLeg,
	RightLeg,
	Trunk,
	Head,
	Count
};

/** Who may place a limb, in the order of priority (the plan's, with a scenario's own targets first and the pose last). */
enum class ESandboxLimbUser : uint8
{
	/** A scenario's hand or foot target (SetFootTarget from outside): what a run asks for explicitly. */
	Script,
	/** The fall: R3's catch, N6's reach (priority 1 of the plan). */
	Fall,
	/** Balance: the steps' feet (2). */
	Balance,
	/** Protection: the cover of a struck place and a get-up being lowered (3). */
	Protect,
	/** N5's guarding arm (4). */
	Guard,
	/** R2's answer to a hand that holds an arm while the body gets up (5). */
	HoldAnswer,
	/** A command under way (6). */
	Command,
	/** The disposition: lying covered, staying down while afraid with the player near (7). */
	Disposition,
	Count,
	None = 255
};

class SANDBOXBODY_API FSandboxLimbClaims
{
public:
	static constexpr int32 NumLimbs = static_cast<int32>(ESandboxLimb::Count);
	static constexpr int32 NumUsers = static_cast<int32>(ESandboxLimbUser::Count);

	/** One change of a limb's holder (None: nobody asks for it, the pose has it). */
	struct FChange
	{
		double TimeS = 0.0;
		ESandboxLimb Limb = ESandboxLimb::LeftArm;
		ESandboxLimbUser From = ESandboxLimbUser::None;
		ESandboxLimbUser To = ESandboxLimbUser::None;
	};

	FSandboxLimbClaims()
	{
		for (ESandboxLimbUser& Holder : LastHolder)
		{
			Holder = ESandboxLimbUser::None;
		}
	}

	/** Starts again: nobody asks for anything, the record is empty. */
	void Reset();

	/** This tick's asks begin: every ask of the last tick is forgotten (a user asks again every tick it wants a limb). */
	void BeginTick();
	/** A user asks for a limb in this tick. */
	void Ask(ESandboxLimb Limb, ESandboxLimbUser User);
	/** A user asks for every limb. */
	void AskAll(ESandboxLimbUser User);
	/** Whether the user asks for the limb in this tick. */
	bool Asks(ESandboxLimb Limb, ESandboxLimbUser User) const;

	/** Who holds the limb: the asker of the highest priority, None if nobody asks. */
	ESandboxLimbUser Holder(ESandboxLimb Limb) const;
	/** Whether the user would hold the limb if it asked for it: nobody of a higher priority asks for it. */
	bool WouldHold(ESandboxLimb Limb, ESandboxLimbUser User) const;

	/** The tick's asks are all made: the record is brought up to TimeS (changes of holder, seconds held, seconds asked for twice). */
	void EndTick(double TimeS, double DeltaS);

	const TArray<FChange>& GetChanges() const { return Changes; }
	/** Changes beyond this many are counted and not kept. */
	static constexpr int32 MaxChangesKept = 400;
	int32 GetChangesDropped() const { return ChangesDropped; }
	/** Seconds each user held each limb. */
	double GetHeldS(ESandboxLimb Limb, ESandboxLimbUser User) const { return HeldS[static_cast<int32>(Limb)][static_cast<int32>(User)]; }
	/** Seconds in which two users asked for the limb at once, by the pair (the holder, the one it held off). */
	double GetContestedS(ESandboxLimb Limb, ESandboxLimbUser Holder, ESandboxLimbUser HeldOff) const
	{
		return ContestedS[static_cast<int32>(Limb)][static_cast<int32>(Holder)][static_cast<int32>(HeldOff)];
	}

	static const TCHAR* LimbName(ESandboxLimb Limb);
	static const TCHAR* UserName(ESandboxLimbUser User);

private:
	/** This tick's asks, a bit a user, by limb. */
	uint16 Asked[NumLimbs] = {};
	/** The holders at the end of the last tick (None for every limb to begin with: the constructor). */
	ESandboxLimbUser LastHolder[NumLimbs];
	TArray<FChange> Changes;
	int32 ChangesDropped = 0;
	double HeldS[NumLimbs][NumUsers] = {};
	double ContestedS[NumLimbs][NumUsers][NumUsers] = {};
};
