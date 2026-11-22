#include "SandboxScriptedTargetSource.h"

namespace
{
	// Times are sums of frame intervals: 270 frames of 1/90 s must count as 3 s.
	constexpr double TimeToleranceS = 1.0e-6;
}

bool FSandboxScriptedTargetSource::AddPath(FName Effector, double StartS, const TArray<FSandboxPathKey>& Keys, FString& OutError)
{
	if (Keys.Num() == 0)
	{
		OutError = TEXT("a path needs at least one key");
		return false;
	}
	if (Keys[0].TimeS != 0.0)
	{
		OutError = TEXT("a path's first key must be at t = 0");
		return false;
	}
	for (int32 Index = 1; Index < Keys.Num(); ++Index)
	{
		if (!(Keys[Index].TimeS > Keys[Index - 1].TimeS))
		{
			OutError = FString::Printf(TEXT("path key %d is not later than the key before it"), Index);
			return false;
		}
	}

	TPair<FName, TArray<FPath>>* Entry = Paths.FindByPredicate([Effector](const TPair<FName, TArray<FPath>>& Candidate) { return Candidate.Key == Effector; });
	if (!Entry)
	{
		Entry = &Paths.Emplace_GetRef(Effector, TArray<FPath>());
	}
	Entry->Value.Add(FPath{ StartS, Keys });
	Entry->Value.StableSort([](const FPath& A, const FPath& B) { return A.StartS < B.StartS; });
	return true;
}

void FSandboxScriptedTargetSource::GetEffectors(TArray<FName>& OutEffectors) const
{
	OutEffectors.Reset();
	for (const TPair<FName, TArray<FPath>>& Entry : Paths)
	{
		OutEffectors.Add(Entry.Key);
	}
}

bool FSandboxScriptedTargetSource::GetTarget(FName Effector, FSandboxEffectorTarget& OutTarget) const
{
	return Evaluate(Effector, TimeS, OutTarget);
}

bool FSandboxScriptedTargetSource::Evaluate(FName Effector, double AtTimeS, FSandboxEffectorTarget& OutTarget) const
{
	const TPair<FName, TArray<FPath>>* Entry = Paths.FindByPredicate([Effector](const TPair<FName, TArray<FPath>>& Candidate) { return Candidate.Key == Effector; });
	if (!Entry)
	{
		return false;
	}

	// The last trajectory that has begun; before the first one, the first.
	const FPath* Path = &Entry->Value[0];
	for (const FPath& Candidate : Entry->Value)
	{
		if (Candidate.StartS <= AtTimeS + TimeToleranceS)
		{
			Path = &Candidate;
		}
	}

	const TArray<FSandboxPathKey>& Keys = Path->Keys;
	const double Local = AtTimeS - Path->StartS;
	OutTarget = FSandboxEffectorTarget();
	if (Local < -TimeToleranceS || Keys.Num() == 1)
	{
		OutTarget.Location = Keys[0].Location;
		OutTarget.Rotation = Keys[0].Rotation;
		return true;
	}
	for (int32 Index = 0; Index + 1 < Keys.Num(); ++Index)
	{
		const FSandboxPathKey& From = Keys[Index];
		const FSandboxPathKey& To = Keys[Index + 1];
		if (Local < To.TimeS - TimeToleranceS)
		{
			const double Span = To.TimeS - From.TimeS;
			const double Alpha = FMath::Clamp((Local - From.TimeS) / Span, 0.0, 1.0);
			OutTarget.Location = FMath::Lerp(From.Location, To.Location, Alpha);
			OutTarget.Rotation = FQuat::Slerp(From.Rotation, To.Rotation, Alpha);
			OutTarget.Velocity = (To.Location - From.Location) / Span;
			return true;
		}
	}
	OutTarget.Location = Keys.Last().Location;
	OutTarget.Rotation = Keys.Last().Rotation;
	return true;
}
