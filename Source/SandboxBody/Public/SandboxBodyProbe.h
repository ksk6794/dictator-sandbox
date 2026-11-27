#pragma once

#include "CoreMinimal.h"

class AActor;
class USkeletalMeshComponent;
struct FConstraintInstance;

/** State of one simulated body at one instant. Unreal units: cm, cm/s, rad/s. */
struct FSandboxBodySample
{
	FName Bone;
	FTransform WorldTransform = FTransform::Identity;
	/** World position of the body's centre of mass. */
	FVector CenterOfMass = FVector::ZeroVector;
	FVector LinearVelocity = FVector::ZeroVector;
	FVector AngularVelocityRad = FVector::ZeroVector;
	double MassKg = 0.0;
	/** Principal moments of inertia, kg*cm^2, about the axes of InertiaRotation (world space). */
	FVector InertiaKgCm2 = FVector::ZeroVector;
	FQuat InertiaRotation = FQuat::Identity;
};

/**
 * How deep the body's collision shapes were found inside other things at one instant: the distance a shape
 * would have to move to be clear. Measured with scene queries, not read from the solver's contacts, so it
 * also shows what the solver let through.
 */
struct FSandboxPenetration
{
	/** Deepest any shape of the body is inside something that is not the body itself, cm. */
	double WorldCm = 0.0;
	/** The body that is, and the actor it is inside. */
	FName WorldBone;
	FName WorldInto;
	/** Deepest two bodies of the character that may collide are inside each other, cm, and which. */
	double SelfCm = 0.0;
	FName SelfBoneA;
	FName SelfBoneB;
	/** How many pairs of bodies the physics asset lets collide. With 0 the figure above cannot be anything but 0. */
	int32 SelfPairs = 0;
	/** Shapes that could not be measured (convex hulls, tapered capsules): the figures do not cover them. */
	int32 UnmeasuredShapes = 0;
};

/** Read-only measurements of a simulated skeletal mesh. Used by scenarios and debug overlays. */
struct SANDBOXBODY_API FSandboxBodyProbe
{
	/** One sample per valid body, in physics-asset order (stable between runs). */
	static void SampleBodies(const USkeletalMeshComponent* Mesh, TArray<FSandboxBodySample>& OutSamples);

	/**
	 * Largest distance (cm) between the two anchor frames of any joint whose linear axes are all locked.
	 * A perfectly solved joint reads 0; growth means the solver is letting the skeleton pull apart.
	 * Constraints with free or limited linear motion are skipped.
	 */
	static double MaxJointStretchCm(const USkeletalMeshComponent* Mesh, FName* OutWorstJoint = nullptr);

	/** The same separation for one joint; 0 if either body is missing. */
	static double JointStretchCm(const USkeletalMeshComponent* Mesh, const FConstraintInstance& Constraint);

	/**
	 * Fastest relative rotation (rad/s) between the two bodies of any rigid joint: how hard the fastest
	 * joint is being whipped around. Free and limited-linear constraints are skipped, as for stretch.
	 */
	static double MaxJointAngularSpeedRadS(const USkeletalMeshComponent* Mesh, FName* OutFastestJoint = nullptr);

	/** Mass-weighted centre of all bodies, world space, cm. */
	static FVector CenterOfMass(const TArray<FSandboxBodySample>& Samples);

	/**
	 * The support polygon: the convex hull, seen from above, of the soles of the given bodies (the feet that
	 * stand on something). A body's sole is the part of its collision shapes within SoleToleranceCm of its
	 * lowest point, so a foot on its edge supports with that edge only. Which bodies stand is the caller's
	 * knowledge (their contacts); the probe only does the geometry. Returns the height of the lowest sole
	 * point, cm; the polygon is empty if no body was given or found.
	 */
	static double SupportPolygon(const USkeletalMeshComponent* Mesh, TConstArrayView<FName> StandingBones, double SoleToleranceCm, TArray<FVector2D>& OutPolygon);

	/**
	 * Convex hull of points in a plane, counter-clockwise, without repeated or collinear points. Fewer than
	 * three distinct points come back as they are: a point or a segment.
	 */
	static void ConvexHull2D(TConstArrayView<FVector2D> Points, TArray<FVector2D>& OutHull);

	/**
	 * How far a point is inside a counter-clockwise convex polygon: the distance to the nearest edge, positive
	 * inside and negative outside. This is the margin of a centre of mass (or, later, a capture point) to
	 * the support polygon. A polygon of one or two points has no inside. An empty one gives -infinity.
	 */
	static double MarginInsidePolygon(TConstArrayView<FVector2D> Polygon, const FVector2D& Point);

	/**
	 * The point of a convex polygon nearest to a point: the point itself if it is inside, else the nearest point
	 * of the edges. A polygon of one or two points is a point or a segment. An empty one gives the point back.
	 */
	static FVector2D NearestPointOfPolygon(TConstArrayView<FVector2D> Polygon, const FVector2D& Point);

	/**
	 * How far a point inside a counter-clockwise convex polygon can go along a direction before it leaves it, cm.
	 * 0 for a point outside, for a zero direction and for a polygon of fewer than three points.
	 */
	static double DistanceToEdgeAlong(TConstArrayView<FVector2D> Polygon, const FVector2D& Point, const FVector2D& Direction);

	/** Velocity of the centre of mass, cm/s. */
	static FVector CenterOfMassVelocity(const TArray<FSandboxBodySample>& Samples);

	/**
	 * The capture point of a linear inverted pendulum: where on the floor the body would have to be supported
	 * to come to rest, given where its centre of mass is and how fast it moves. It lies ahead of the centre of
	 * mass by the horizontal velocity times sqrt(height / gravity). While it is inside the support polygon
	 * the body can stop without a step. HeightCm is the centre of mass above the support, GravityCmS2 positive.
	 */
	static FVector2D CapturePoint(const FVector& CenterOfMassCm, const FVector& VelocityCmS, double HeightCm, double GravityCmS2);

	/**
	 * How long until a point HeightCm above a floor, moving up at VzCmS (down below 0) and falling freely under GravityCmS2, comes within
	 * RadiusCm of the floor; 0 if it is that near already. Seconds. (R3: the time until the trunk meets the floor.)
	 */
	static double TimeToFloorS(double HeightCm, double VzCmS, double GravityCmS2, double RadiusCm);

	/**
	 * An angle from -pi to pi taken on Reference's side of the seam at pi: itself, or 2 pi less or more when the two lie more than half a
	 * turn apart, so that a mean of the two is between them (two feet at 175 and -175 degrees mean 180, not 0). Radians.
	 */
	static double OnSideOf(double AngleRad, double ReferenceRad);

	/**
	 * Where the head is asked to turn for a target at TargetYawDeg from the chest's front (from -180 to 180), the head turning HeadShare
	 * of that and now at HeadYawDeg: behind the shoulders the target may also be taken a turn the other way round (190 for -170), and
	 * is, when the head is nearer to that; so that a target crossing straight behind does not swing the head between its limits, and
	 * one just behind a shoulder is still turned to. Degrees, before the head's share and limit.
	 */
	static double GazeYawDeg(double TargetYawDeg, double HeadYawDeg, double HeadShare);

	/**
	 * Speed of the body as a whole: the root mean square over its bodies, weighted by mass, of their linear
	 * speed (m/s) and of their angular speed (rad/s). A trembling hand barely moves these; a moving trunk does.
	 */
	static void MassWeightedSpeeds(const TArray<FSandboxBodySample>& Samples, double& OutLinearMps, double& OutAngularRadS);

	/** Kinetic energy in joules, translation and rotation of every body. */
	static double KineticEnergyJ(const TArray<FSandboxBodySample>& Samples);

	/** Gravitational energy in joules of the bodies' centres of mass above FloorZCm. GravityCmS2 positive. */
	static double PotentialEnergyJ(const TArray<FSandboxBodySample>& Samples, double GravityCmS2, double FloorZCm);

	/**
	 * Penetration of the body's shapes into the world around it and into each other, at this instant. The
	 * world is everything that blocks except the body's own actor and the actors in Ignored (the player's
	 * hands are measured on their own). Bodies of the character count against each other only where the
	 * physics asset lets them collide. Boxes, capsules and spheres are measured; other shapes are counted in
	 * UnmeasuredShapes.
	 */
	static void MeasurePenetration(const USkeletalMeshComponent* Mesh, TConstArrayView<const AActor*> Ignored, FSandboxPenetration& OutPenetration);

	/** Translational kinetic energy in joules (rotational energy is not included). */
	static double TranslationalKineticEnergyJ(const TArray<FSandboxBodySample>& Samples);

	static double TotalMassKg(const TArray<FSandboxBodySample>& Samples);
};
