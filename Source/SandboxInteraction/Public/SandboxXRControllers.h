#pragma once

#include "CoreMinimal.h"
#include "SandboxTrackedTargetSource.h"

class APlayerController;

/**
 * Where the headset's controllers are, asked of the engine's XR system (OpenXR, whatever runtime is behind
 * it). This file and its .cpp are the only code of the project that knows a headset exists on the player's
 * side: everything else sees an ISandboxControllerTracker.
 *
 * The pose is the runtime's grip pose with the linear and angular velocity the runtime tracks, for the time
 * the frame will be shown at. The squeeze comes from the controller's grip axis as the player's input has it
 * (the engine turns OpenXR's squeeze into keys named after the controller profiles); a profile with a grip
 * button and no axis reads 0 or 1.
 */
class SANDBOXINTERACTION_API FSandboxXRControllers : public ISandboxControllerTracker
{
public:
	/** True when the process shows the world in a headset: an XR system is there and stereo is on. */
	static bool IsHeadsetOn();

	/** Puts the tracking origin on the floor, so that the pawn's origin is the floor under the middle of the play space. */
	void Start();

	/** Whose input the squeeze is read from. Without one the squeeze reads 0. */
	void SetPlayer(APlayerController* InPlayer) { Player = InPlayer; }

	virtual bool GetController(FName Effector, FSandboxControllerState& OutState) const override;

	/** The headset's pose in tracking space, as the XR system has it for this frame. */
	virtual bool GetHead(FVector& OutLocation, FQuat& OutRotation) const override;

	/**
	 * Makes one controller vibrate at this strength, 0 to 1 (0 stops it), through the player's haptics. The
	 * engine makes a vibration last one frame, so a vibration that is to go on is asked for in every frame.
	 */
	void Buzz(bool bLeft, float Amplitude) const;

	/**
	 * How strongly a controller vibrates for the force its hand is pressing or striking with: in proportion,
	 * full strength at FullForceN and beyond.
	 */
	static float HapticAmplitude(double ContactForceN, double FullForceN);

	/** The XR system's name and version, for the log. */
	static FString DescribeSystem();

	/**
	 * Every key of a hand-held controller, of whatever profile, that has ever been down or off its rest in the
	 * player's input, by the engine's key names; "none" if none has. It tells a button that was not pressed
	 * from one that did not arrive, and names the profile the runtime answers under.
	 */
	FString DescribeKeysSeen() const;

private:
	float ReadSqueeze(bool bLeft) const;
	/** The thumbstick of every profile is asked, as the grip is, and the one tilted most counts. */
	FVector2D ReadStick(bool bLeft) const;
	/** The face buttons of every profile that has them (Touch, Index), as the grip is: down if any profile's is down. */
	void ReadButtons(bool bLeft, bool& bOutFirst, bool& bOutSecond) const;

	TWeakObjectPtr<APlayerController> Player;
	/** Filled as the keys are read, which is a question and changes nothing else. */
	mutable TArray<FName> KeysSeen;
	mutable TArray<FKey> ControllerKeys;
};
