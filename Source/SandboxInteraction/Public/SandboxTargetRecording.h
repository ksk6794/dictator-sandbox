#pragma once

#include "CoreMinimal.h"
#include "SandboxTargetSource.h"

/**
 * What a target source answered, frame after frame: where each hand should be at the start of each physics
 * step of a run. A session keeps one of what a person's hands did (ASandboxEffectorRig::SetRecording), and a
 * scenario replays it through FSandboxRecordedTargetSource.
 *
 * A recording has no times in it, only frames: it is replayed at the fixed step it was made at, frame for
 * frame, with nothing worked out in between. It is a text file, one line a frame.
 *
 * The targets are points of the world. A recording replayed against a body that does not move as the body
 * of the session did reaches for where that body was.
 */
struct SANDBOXINTERACTION_API FSandboxTargetRecording
{
	/** Physics steps per second of the run it was made in. */
	int32 Fps = 0;
	/** The effectors it has targets for, in the order of each frame's targets. */
	TArray<FName> Effectors;
	/** Frame after frame, one target for each effector. */
	TArray<FSandboxEffectorTarget> Targets;
	/**
	 * R10, step 1: where the player's head was in each frame (the camera on the desktop, the head in a headset), world, cm; one a frame,
	 * kept with that frame's targets, so that a replay tells the body of the viewer its session had. Empty in a recording made before.
	 */
	TArray<FVector> Heads;
	/** R10, step 8 in part: the commands a person gave the body while it was recorded (the console's Sandbox.Command), each at its frame. */
	TArray<TPair<int32, FString>> Commands;

	int32 NumFrames() const { return Effectors.Num() > 0 ? Targets.Num() / Effectors.Num() : 0; }

	/**
	 * The target of an effector in a frame. Before the first frame it is the first frame's and after the last
	 * the last one's, both at rest: a hand waits where its motion begins and stays where it ended. False for an
	 * effector the recording does not have, or an empty recording.
	 */
	bool Find(FName Effector, int32 Frame, FSandboxEffectorTarget& OutTarget) const;

	/** Writes the file, creating its directory. */
	bool Save(const FString& FilePath, FString& OutError) const;
	static bool Load(const FString& FilePath, FSandboxTargetRecording& OutRecording, FString& OutError);
};

/**
 * A target source that plays a recording back. The owner sets the frame; the source never reads a clock.
 */
class SANDBOXINTERACTION_API FSandboxRecordedTargetSource : public ISandboxTargetSource
{
public:
	explicit FSandboxRecordedTargetSource(TSharedRef<const FSandboxTargetRecording> InRecording) : Recording(MoveTemp(InRecording)) {}

	/** The frame of the recording whose targets are answered with; it may lie before its start or after its end. */
	void SetFrame(int32 InFrame) { Frame = InFrame; }
	const FSandboxTargetRecording& GetRecording() const { return *Recording; }

	virtual bool GetTarget(FName Effector, FSandboxEffectorTarget& OutTarget) const override { return Recording->Find(Effector, Frame, OutTarget); }

private:
	TSharedRef<const FSandboxTargetRecording> Recording;
	int32 Frame = 0;
};
