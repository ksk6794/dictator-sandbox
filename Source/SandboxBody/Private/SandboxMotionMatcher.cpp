#include "SandboxMotionMatcher.h"

void FSandboxMotionMatcher::FitTurn(TConstArrayView<FVector> From, TConstArrayView<FVector> To, TConstArrayView<double> Weights, double& OutYawRad, FVector2D& OutShiftCm)
{
	OutYawRad = 0.0;
	OutShiftCm = FVector2D::ZeroVector;
	const int32 Count = From.Num();
	if (Count == 0 || To.Num() != Count || Weights.Num() != Count)
	{
		return;
	}
	double Total = 0.0;
	FVector2D FromMean = FVector2D::ZeroVector;
	FVector2D ToMean = FVector2D::ZeroVector;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const double Weight = FMath::Max(Weights[Index], 0.0);
		Total += Weight;
		FromMean += FVector2D(From[Index].X, From[Index].Y) * Weight;
		ToMean += FVector2D(To[Index].X, To[Index].Y) * Weight;
	}
	if (Total <= 0.0)
	{
		return;
	}
	FromMean /= Total;
	ToMean /= Total;
	// The turn that lays the centred From on the centred To: the angle of the weighted sums (Kabsch in the plane). An average of
	// angles would point the other way across the seam at 180 degrees (docs/pitfalls.md).
	double Dot = 0.0;
	double Cross = 0.0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const double Weight = FMath::Max(Weights[Index], 0.0);
		const FVector2D A = FVector2D(From[Index].X, From[Index].Y) - FromMean;
		const FVector2D B = FVector2D(To[Index].X, To[Index].Y) - ToMean;
		Dot += Weight * (A.X * B.X + A.Y * B.Y);
		Cross += Weight * (A.X * B.Y - A.Y * B.X);
	}
	OutYawRad = Dot == 0.0 && Cross == 0.0 ? 0.0 : FMath::Atan2(Cross, Dot);
	const double Cos = FMath::Cos(OutYawRad);
	const double Sin = FMath::Sin(OutYawRad);
	OutShiftCm = ToMean - FVector2D(Cos * FromMean.X - Sin * FromMean.Y, Sin * FromMean.X + Cos * FromMean.Y);
}

FSandboxMatch FSandboxMotionMatcher::Score(const FSandboxMatchBody& Body, const FSandboxMatchSample& Sample, const FSandboxMatchSettings& Settings)
{
	FSandboxMatch Match;
	const int32 Count = Body.Places.Num();
	if (Count == 0 || Sample.Places.Num() != Count || Settings.Weights.Num() != Count)
	{
		return Match;
	}
	Match.Variant = Sample.Variant;
	Match.TimeS = Sample.TimeS;
	FitTurn(Sample.Places, Body.Places, Settings.Weights, Match.YawRad, Match.ShiftCm);
	const FQuat Turn(FVector::UpVector, Match.YawRad);
	const FVector Shift(Match.ShiftCm.X, Match.ShiftCm.Y, Body.SupportZ);
	double Total = 0.0;
	double Squared = 0.0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const double Weight = FMath::Max(Settings.Weights[Index], 0.0);
		Total += Weight;
		Squared += Weight * FVector::DistSquared(Turn.RotateVector(Sample.Places[Index]) + Shift, Body.Places[Index]);
	}
	if (Total <= 0.0)
	{
		return Match;
	}
	const auto Apart = [](const FVector& A, const FVector& B)
	{
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(A.GetSafeNormal(), B.GetSafeNormal()), -1.0, 1.0)));
	};
	Match.PlacesCm = FMath::Sqrt(Squared / Total);
	Match.UpDeg = 0.5 * (Apart(Body.PelvisUp, Sample.PelvisUp) + Apart(Body.ChestUp, Sample.ChestUp));
	Match.PenaltyCm = Sample.Variant != INDEX_NONE && Settings.PenalizedVariants.Contains(Sample.Variant) ? Settings.PenaltyCm : 0.0;
	Match.CostCm = Match.PlacesCm + Settings.UpCmPerDeg * Match.UpDeg + Match.PenaltyCm;
	if (Settings.LateCmPerS > 0.0)
	{
		Match.CostCm += Settings.LateCmPerS * Sample.TimeS;
	}
	Match.bValid = true;
	return Match;
}

void FSandboxMotionMatcher::BestByVariant(const FSandboxMatchBody& Body, TConstArrayView<FSandboxMatchSample> Samples, int32 VariantCount, const FSandboxMatchSettings& Settings, TArray<FSandboxMatch>& OutBest)
{
	OutBest.Reset();
	OutBest.SetNum(FMath::Max(VariantCount, 0));
	for (const FSandboxMatchSample& Sample : Samples)
	{
		if (!OutBest.IsValidIndex(Sample.Variant))
		{
			continue;
		}
		const FSandboxMatch Match = Score(Body, Sample, Settings);
		FSandboxMatch& Kept = OutBest[Sample.Variant];
		// The earlier sample wins a tie: of two equally near moments, the first.
		if (Match.bValid && (!Kept.bValid || Match.CostCm < Kept.CostCm))
		{
			Kept = Match;
		}
	}
}

void FSandboxMotionMatcher::Choose(TConstArrayView<FSandboxMatch> Best, TConstArrayView<bool> Allowed, FSandboxMatch& OutChosen, FSandboxMatch& OutNext)
{
	OutChosen = FSandboxMatch();
	OutNext = FSandboxMatch();
	for (int32 Index = 0; Index < Best.Num(); ++Index)
	{
		const FSandboxMatch& Match = Best[Index];
		if (!Match.bValid || !Allowed.IsValidIndex(Index) || !Allowed[Index])
		{
			continue;
		}
		if (!OutChosen.bValid || Match.CostCm < OutChosen.CostCm)
		{
			OutNext = OutChosen;
			OutChosen = Match;
		}
		else if (!OutNext.bValid || Match.CostCm < OutNext.CostCm)
		{
			OutNext = Match;
		}
	}
}

FTransform FSandboxMotionMatcher::Frame(const FSandboxMatch& Match, const FQuat& StartRotation, double SupportZ)
{
	return FTransform((FQuat(FVector::UpVector, Match.YawRad) * StartRotation).GetNormalized(), FVector(Match.ShiftCm.X, Match.ShiftCm.Y, SupportZ));
}
