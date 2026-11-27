#pragma once

#include "CoreMinimal.h"
#include "SandboxMotionMatcher.h"

class FSandboxClipPose;
class UBodySetup;
class USkeletalMeshComponent;

/** One clip of the library's list (Specs/motion_library.json). */
struct FSandboxMotionSpecClip
{
	FString Name;
	/** "getup" or "roll". */
	FString Kind;
	/** How the recording begins, for reports. */
	FString Lies;
	bool bMirror = false;
	/** Whether the matcher may choose it (decision 39); every clip is scored. */
	bool bMatch = false;
};

/** The library's list as written: where the clips are, the bones the matcher compares by group, the clips. */
struct SANDBOXBODY_API FSandboxMotionSpec
{
	static constexpr int32 GroupCount = 6;
	/** The groups of compared bones, in the order of their weights getup.match_<group>. */
	static const TCHAR* const GroupNames[GroupCount];

	FString Folder;
	TArray<FName> FeatureBones;
	/** The group of each compared bone, an index into GroupNames. */
	TArray<int32> FeatureGroups;
	TArray<FSandboxMotionSpecClip> Clips;

	/** False with a reason for a list that is not exactly as Specs/README.md says (an unknown field or group, a number for a boolean). */
	static bool Parse(const FString& Text, FSandboxMotionSpec& Out, FString& OutError);
	static bool Read(const FString& File, FSandboxMotionSpec& Out, FString& OutError);
	/** The clip's path as the controller keeps it: "<folder>/<name>.<name>", and the mirror's mark for its mirror image. */
	FString KeyOf(int32 Clip, bool bMirrored) const;
};

/** A stretch of a motion in which one compared body touches the clip's floor, and how high its lowest point lay meanwhile (cm; below 0: under the floor). */
struct FSandboxMotionContact
{
	FName Bone;
	double FromS = 0.0;
	double ToS = 0.0;
	double LowestMinCm = 0.0;
	double LowestMaxCm = 0.0;
};

/** One motion of the library: a clip, or its mirror image, carried to this body; everything in the space of the clip's root, its floor at 0. */
struct FSandboxMotionVariant
{
	FString Key;
	FString Name;
	FString Kind;
	FString Lies;
	bool bMirrored = false;
	bool bMatch = false;
	double LengthS = 0.0;
	double KeyS = 0.0;
	int32 Keys = 0;
	/** Per key: the compared bones' places (as the body reaches them: FSandboxClipPose::ReachedBodyAt), the pelvis's and the chest's orientation. */
	TArray<TArray<FVector>> Places;
	TArray<FQuat> Pelvis;
	TArray<FQuat> Chest;
	/** Per key: which compared bones touch the floor (bit i for bone i). */
	TArray<uint32> Touches;
	TArray<FSandboxMotionContact> Contacts;
	/** When the pelvis first comes within StandsWithinCm of its last height with both feet on the floor; below 0: never. s. */
	double StandsS = -1.0;
	/** How far the pelvis moves from where it starts within the matcher's window, at most. cm. */
	double WindowTravelCm = 0.0;
	/** A mirror image: its places against its clip's counterparts seen in this mesh's mirror, worst over every key and bone; below 0 for a clip itself. cm. */
	double MirrorErrorCm = -1.0;
	/** A mirror image: how far the clip skeleton's reference pose is from its own mirror (FSandboxClipPose::GetMirrorAsymmetryCm). cm. */
	double SourceAsymmetryCm = 0.0;
	/** Of the other get-ups, the one whose first moment lies nearest this one's, and the matcher's cost between them: whether two recordings are one. */
	FString Nearest;
	double NearestCostCm = -1.0;
};

/**
 * The motion library (docs/active-ragdoll.md, R2; docs/recon/r2_step8_plan.md): the clips a text list names, carried to this body
 * (FSandboxClipPose) and mirrored, each with what the matcher compares at every key and the contacts it makes with its floor. Built in
 * memory when a body first gets up with getup.match 1; nothing of Epic's is written anywhere. It reads clips through the controller,
 * which keeps them; no pointer to a clip is held (the controller's map may move them).
 */
class SANDBOXBODY_API FSandboxMotionLibrary
{
public:
	/** What marks a clip's key as its mirror image ("<path>|mirror"). */
	static const TCHAR* const MirrorMark;
	/** The pelvis's height counted as standing, from its last one. cm. A measure's definition, read only. */
	static constexpr double StandsWithinCm = 5.0;

	struct FBuildSettings
	{
		FName Pelvis;
		FName Chest;
		double ContactCm = 0.0;
		double ContactMps = 0.0;
		double WindowS = 0.0;
		/** For Nearest: the matcher's weights, one a compared bone, and its lean's price. */
		FSandboxMatchSettings Match;
	};
	/** Reads a clip by its key ("<path>" or "<path>|mirror"); null with a reason. OutKey is what it is kept by. */
	using FReadClip = TFunctionRef<const FSandboxClipPose*(const FString& Key, FString& OutKey, FString& OutError)>;

	bool Build(const FSandboxMotionSpec& Spec, const USkeletalMeshComponent* Mesh, const FBuildSettings& Settings, FReadClip ReadClip, FString& OutError);
	bool IsBuilt() const { return Variants.Num() > 0; }
	const TArray<FSandboxMotionVariant>& GetVariants() const { return Variants; }
	const FSandboxMotionSpec& GetSpec() const { return Spec; }
	/** Which of the compared bones is the pelvis (an index into the places of a key). */
	int32 GetPelvisFeature() const { return PelvisFeature; }
	int32 FindVariant(const FString& Key) const;
	/** The same clip read the other way (its mirror image, or the clip of a mirror image); INDEX_NONE without one. */
	int32 CounterpartOf(int32 Variant) const;

	/** Every variant's keys within WindowS of its start, as the matcher takes them: the places turned by StartRotation, the ups in that turn. */
	void Samples(const FQuat& StartRotation, double WindowS, TArray<FSandboxMatchSample>& OutSamples) const;

	/** The worst mirror error over the variants, cm; below 0 without a mirror image. */
	double MirrorErrorMaxCm() const;
	/** The get-ups whose first key has the pelvis or the trunk on the floor. */
	int32 GetUpsLyingAtStart() const;

	/**
	 * Which keys touch the floor: the lowest point within ContactCm of it (or under it) and slower than ContactMps; and the stretches
	 * of keys that do, as (from, to) in seconds. Pure.
	 */
	static void MarkContacts(TConstArrayView<double> LowestCm, TConstArrayView<double> SpeedMps, double KeyS, double ContactCm, double ContactMps, TArray<bool>& OutTouch, TArray<FVector2D>& OutStretches);
	/** The lowest point of a body's boxes, capsules and spheres when the body is at Body (cm, in Body's space's z). */
	static double LowestPointCm(const UBodySetup& Setup, const FTransform& Body);

private:
	FSandboxMotionSpec Spec;
	TArray<FSandboxMotionVariant> Variants;
	int32 PelvisFeature = INDEX_NONE;
};
