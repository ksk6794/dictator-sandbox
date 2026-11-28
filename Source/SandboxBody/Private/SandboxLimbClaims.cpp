#include "SandboxLimbClaims.h"

void FSandboxLimbClaims::BeginTick()
{
	for (uint16& Bits : Asked)
	{
		Bits = 0;
	}
}

void FSandboxLimbClaims::Ask(ESandboxLimb Limb, ESandboxLimbUser User)
{
	if (Limb < ESandboxLimb::Count && User < ESandboxLimbUser::Count)
	{
		Asked[static_cast<int32>(Limb)] |= static_cast<uint16>(1u << static_cast<uint32>(User));
	}
}

void FSandboxLimbClaims::AskAll(ESandboxLimbUser User)
{
	for (int32 Limb = 0; Limb < NumLimbs; ++Limb)
	{
		Ask(static_cast<ESandboxLimb>(Limb), User);
	}
}

bool FSandboxLimbClaims::Asks(ESandboxLimb Limb, ESandboxLimbUser User) const
{
	return Limb < ESandboxLimb::Count && User < ESandboxLimbUser::Count &&
		(Asked[static_cast<int32>(Limb)] & static_cast<uint16>(1u << static_cast<uint32>(User))) != 0;
}

ESandboxLimbUser FSandboxLimbClaims::Holder(ESandboxLimb Limb) const
{
	if (Limb >= ESandboxLimb::Count)
	{
		return ESandboxLimbUser::None;
	}
	const uint16 Bits = Asked[static_cast<int32>(Limb)];
	for (int32 User = 0; User < NumUsers; ++User)
	{
		if (Bits & static_cast<uint16>(1u << User))
		{
			return static_cast<ESandboxLimbUser>(User);
		}
	}
	return ESandboxLimbUser::None;
}

bool FSandboxLimbClaims::WouldHold(ESandboxLimb Limb, ESandboxLimbUser User) const
{
	if (Limb >= ESandboxLimb::Count || User >= ESandboxLimbUser::Count)
	{
		return false;
	}
	// Nobody of a higher priority (a lower number) asks for it.
	const uint16 Higher = static_cast<uint16>((1u << static_cast<uint32>(User)) - 1u);
	return (Asked[static_cast<int32>(Limb)] & Higher) == 0;
}

void FSandboxLimbClaims::EndTick(double TimeS, double DeltaS)
{
	for (int32 Index = 0; Index < NumLimbs; ++Index)
	{
		const ESandboxLimb Limb = static_cast<ESandboxLimb>(Index);
		const ESandboxLimbUser Now = Holder(Limb);
		if (Now != LastHolder[Index])
		{
			if (Changes.Num() < MaxChangesKept)
			{
				FChange& Change = Changes.AddDefaulted_GetRef();
				Change.TimeS = TimeS;
				Change.Limb = Limb;
				Change.From = LastHolder[Index];
				Change.To = Now;
			}
			else
			{
				++ChangesDropped;
			}
			LastHolder[Index] = Now;
		}
		if (Now == ESandboxLimbUser::None)
		{
			continue;
		}
		HeldS[Index][static_cast<int32>(Now)] += DeltaS;
		for (int32 User = 0; User < NumUsers; ++User)
		{
			if (User != static_cast<int32>(Now) && (Asked[Index] & static_cast<uint16>(1u << User)))
			{
				ContestedS[Index][static_cast<int32>(Now)][User] += DeltaS;
			}
		}
	}
}

const TCHAR* FSandboxLimbClaims::LimbName(ESandboxLimb Limb)
{
	switch (Limb)
	{
	case ESandboxLimb::LeftArm: return TEXT("left_arm");
	case ESandboxLimb::RightArm: return TEXT("right_arm");
	case ESandboxLimb::LeftLeg: return TEXT("left_leg");
	case ESandboxLimb::RightLeg: return TEXT("right_leg");
	case ESandboxLimb::Trunk: return TEXT("trunk");
	case ESandboxLimb::Head: return TEXT("head");
	default: return TEXT("none");
	}
}

const TCHAR* FSandboxLimbClaims::UserName(ESandboxLimbUser User)
{
	switch (User)
	{
	case ESandboxLimbUser::Script: return TEXT("script");
	case ESandboxLimbUser::Fall: return TEXT("fall");
	case ESandboxLimbUser::Balance: return TEXT("balance");
	case ESandboxLimbUser::Protect: return TEXT("protect");
	case ESandboxLimbUser::Guard: return TEXT("guard");
	case ESandboxLimbUser::HoldAnswer: return TEXT("hold_answer");
	case ESandboxLimbUser::Command: return TEXT("command");
	case ESandboxLimbUser::Disposition: return TEXT("disposition");
	default: return TEXT("pose");
	}
}
