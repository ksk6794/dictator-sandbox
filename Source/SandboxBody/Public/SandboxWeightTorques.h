#pragma once

#include "CoreMinimal.h"

/**
 * The torques by which the joints of a body carry its weight, worked out from where its bodies are, what they weigh and
 * what the world pushes them with. Arithmetic on samples, with no world: the moment, about each joint, of every outside
 * force on what hangs beyond it, and the torque that cancels it.
 *
 *   tau_j = - sum over the bodies beyond j of [ (c - p_j) x m g  +  (q - p_j) x F ]
 *
 * with p the joint, c a body's centre of mass, F what the world pushes that body with and q where it acts. A part that
 * touches nothing is held up against its own weight; a leg that stands transmits what the floor pushes its foot with.
 */
class SANDBOXBODY_API FSandboxWeightTorques
{
public:
	/** One body: where it hangs, what it weighs and what pushes it. */
	struct FBody
	{
		/** Index of the body it hangs on; INDEX_NONE for the root, which has no joint. A body comes after its parent. */
		int32 Parent = INDEX_NONE;
		/** Its joint with the parent, world space, cm. */
		FVector JointCm = FVector::ZeroVector;
		FVector CenterOfMassCm = FVector::ZeroVector;
		double MassKg = 0.0;
		/** What the world pushes it with (N), as one force at one place. */
		FVector PushN = FVector::ZeroVector;
		FVector PushAtCm = FVector::ZeroVector;
	};

	/**
	 * The torque on each body about its joint, N*m, in the world's axes, to be applied to the body and the opposite of it
	 * to its parent. Gravity is the pull each body's weight is worked out with, m/s^2. False (and no torques) if a body
	 * comes before its parent.
	 */
	static bool Compute(TConstArrayView<FBody> Bodies, const FVector& GravityMps2, TArray<FVector>& OutTorquesNm);

	/**
	 * Where the floor has to push each of two feet for their pushes together to carry the weight at a place: the share of
	 * the weight on the first (0 to 1) by where that place lies between the two soles, and a point on each sole, the
	 * sole's middle moved by what is left over, and no further than the sole's edge. With one foot, all of it there.
	 * Soles are convex polygons on the floor, cm; an empty one is a foot that does not stand.
	 */
	static void ShareBetweenFeet(const TArray<FVector2D>& FirstSole, const TArray<FVector2D>& SecondSole, const FVector2D& Place, double& OutFirstShare, FVector2D OutAt[2]);

	/** The point of a convex polygon nearest to a point: the point itself if it lies inside. */
	static FVector2D NearestInside(const TArray<FVector2D>& Polygon, const FVector2D& Point);
};
