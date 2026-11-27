#pragma once

#include "CoreMinimal.h"

/**
 * One moment of a recorded motion as the matcher sees it (R2, step 8): the places of the compared bones in the space of the clip's
 * root, already turned as the body stood when its control began (the start's rotation), with the clip's floor at 0; and the
 * world's up in the pelvis's and in the chest's own space, which no turn about the vertical changes. Variant is the motion's index
 * in the library.
 */
struct FSandboxMatchSample
{
	int32 Variant = INDEX_NONE;
	double TimeS = 0.0;
	TArray<FVector> Places;
	FVector PelvisUp = FVector::UpVector;
	FVector ChestUp = FVector::UpVector;
};

/** The body as it lies: the same bones' places in the world, the same ups, and the height of what it lies on. */
struct FSandboxMatchBody
{
	TArray<FVector> Places;
	FVector PelvisUp = FVector::UpVector;
	FVector ChestUp = FVector::UpVector;
	double SupportZ = 0.0;
};

struct FSandboxMatchSettings
{
	/** One weight a place, in the places' order. */
	TArray<double> Weights;
	/** A degree of lean counted as this many cm. */
	double UpCmPerDeg = 1.0;
	/** The variants that cost more (the one whose attempt was just given up and its mirror image: the same motion from the same place), and by how much. */
	TArray<int32> PenalizedVariants;
	double PenaltyCm = 0.0;
};

/** A sample laid on the body: where (a turn about the vertical and a shift over the floor) and how far from it. */
struct FSandboxMatch
{
	bool bValid = false;
	int32 Variant = INDEX_NONE;
	double TimeS = 0.0;
	double YawRad = 0.0;
	FVector2D ShiftCm = FVector2D::ZeroVector;
	/** The weighted root mean square of the places' distances in 3D, cm. */
	double PlacesCm = 0.0;
	/** The mean of the pelvis's and the chest's lean apart, degrees. */
	double UpDeg = 0.0;
	double PenaltyCm = 0.0;
	/** PlacesCm + UpCmPerDeg * UpDeg + PenaltyCm. */
	double CostCm = 0.0;
};

/**
 * The matcher of the get-up (docs/active-ragdoll.md, R2; docs/recon/r2_step8_plan.md): pure arithmetic on places, read from the body
 * and from the motion library by whoever calls it. Each sample is laid on the body by the turn about the vertical and the shift over
 * the floor that bring its places nearest the body's, weighted (the 2D fit of two point sets: their centroids, then the turn as the
 * angle of the weighted sums of cross and dot products, never an average of angles); its height is not fitted (the clip's floor is
 * put on the support). The cost is how far the places then lie apart, plus how differently the pelvis and the chest lean.
 */
class SANDBOXBODY_API FSandboxMotionMatcher
{
public:
	/**
	 * The turn about the vertical and the shift over the floor that lay From on To nearest, weighted, in the horizontal plane:
	 * To ~ Rotate(Yaw, From) + Shift. Places and weights of the same count; with no weight at all, no turn and no shift.
	 */
	static void FitTurn(TConstArrayView<FVector> From, TConstArrayView<FVector> To, TConstArrayView<double> Weights, double& OutYawRad, FVector2D& OutShiftCm);

	/** One sample against the body. Invalid if the counts do not agree. */
	static FSandboxMatch Score(const FSandboxMatchBody& Body, const FSandboxMatchSample& Sample, const FSandboxMatchSettings& Settings);

	/** The cheapest sample of every variant (OutBest[Variant]; invalid where a variant has none). */
	static void BestByVariant(const FSandboxMatchBody& Body, TConstArrayView<FSandboxMatchSample> Samples, int32 VariantCount, const FSandboxMatchSettings& Settings, TArray<FSandboxMatch>& OutBest);

	/**
	 * Of the variants Allowed marks, the cheapest and the next; the first in order wins a tie. Invalid when none is allowed. Index
	 * of OutBest, Allowed by variant.
	 */
	static void Choose(TConstArrayView<FSandboxMatch> Best, TConstArrayView<bool> Allowed, FSandboxMatch& OutChosen, FSandboxMatch& OutNext);

	/** Where a chosen sample lays the clip's root in the world: turned by StartRotation and then by the match's turn, shifted, its floor at SupportZ. */
	static FTransform Frame(const FSandboxMatch& Match, const FQuat& StartRotation, double SupportZ);
};
