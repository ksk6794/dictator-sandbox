#include "SandboxChance.h"

void FSandboxChance::Reset(int32 InSeed)
{
	Seed = InSeed;
	Draws.Reset();
	for (int32 Purpose = 0; Purpose < static_cast<int32>(EPurpose::Count); ++Purpose)
	{
		Streams[Purpose].Initialize(StreamSeed(Seed, static_cast<EPurpose>(Purpose)));
		DrawCount[Purpose] = 0;
	}
}

int32 FSandboxChance::StreamSeed(int32 RunSeed, EPurpose Purpose)
{
	// The run's seed and the purpose's index mixed (a multiplicative hash, the same on every platform): neighbouring seeds and
	// neighbouring purposes start far apart in the stream's sequence.
	uint32 Mixed = static_cast<uint32>(RunSeed) * 2654435761u;
	Mixed ^= (static_cast<uint32>(Purpose) + 1u) * 2246822519u;
	Mixed ^= Mixed >> 15;
	Mixed *= 2654435761u;
	Mixed ^= Mixed >> 13;
	return static_cast<int32>(Mixed & 0x7fffffffu);
}

double FSandboxChance::Draw(EPurpose Purpose, double FixedAnswer)
{
	const int32 Index = static_cast<int32>(Purpose);
	if (Seed == 0 || Index < 0 || Index >= static_cast<int32>(EPurpose::Count))
	{
		return FixedAnswer;
	}
	FDraw& Drawn = Draws.AddDefaulted_GetRef();
	Drawn.Purpose = Purpose;
	Drawn.Index = DrawCount[Index]++;
	Drawn.Value = static_cast<double>(Streams[Index].GetFraction());
	return Drawn.Value;
}

bool FSandboxChance::Choose(EPurpose Purpose, double Chance, bool bFixedAnswer)
{
	if (Seed == 0)
	{
		return bFixedAnswer;
	}
	return Draw(Purpose, 0.0) < Chance;
}

const TCHAR* FSandboxChance::PurposeName(EPurpose Purpose)
{
	switch (Purpose)
	{
	case EPurpose::Struggle: return TEXT("struggle");
	case EPurpose::Vigour: return TEXT("vigour");
	default: return TEXT("other");
	}
}
