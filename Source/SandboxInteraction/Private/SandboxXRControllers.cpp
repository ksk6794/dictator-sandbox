#include "SandboxXRControllers.h"

#include "Engine/Engine.h"
#include "Features/IModularFeatures.h"
#include "GameFramework/PlayerController.h"
#include "IMotionController.h"
#include "IXRTrackingSystem.h"
#include "InputCoreTypes.h"
#include "StereoRendering.h"

namespace
{
	// The OpenXR plugin's names for the grip poses. ("Left" and "Right" mean the same there; other XR plugins
	// know only those, and none of them is used by this project.)
	const FName LeftGripSource(TEXT("LeftGrip"));
	const FName RightGripSource(TEXT("RightGrip"));

	// Unreal units are centimetres.
	constexpr float WorldToMeters = 100.0f;
}

bool FSandboxXRControllers::IsHeadsetOn()
{
	return GEngine && GEngine->XRSystem.IsValid() && GEngine->StereoRenderingDevice.IsValid() && GEngine->StereoRenderingDevice->IsStereoEnabled();
}

FString FSandboxXRControllers::DescribeSystem()
{
	return GEngine && GEngine->XRSystem.IsValid()
		? FString::Printf(TEXT("%s (%s)"), *GEngine->XRSystem->GetSystemName().ToString(), *GEngine->XRSystem->GetVersionString())
		: FString(TEXT("no XR system"));
}

void FSandboxXRControllers::Start()
{
	if (GEngine && GEngine->XRSystem.IsValid())
	{
		// A standing player: z = 0 of tracking space is the floor of the room the player is in.
		GEngine->XRSystem->SetTrackingOrigin(EHMDTrackingOrigin::LocalFloor);
	}
}

bool FSandboxXRControllers::GetController(FName Effector, FSandboxControllerState& OutState) const
{
	OutState = FSandboxControllerState();
	const bool bLeft = Effector == SandboxEffectors::HandLeft;
	if (!bLeft && Effector != SandboxEffectors::HandRight)
	{
		return false;
	}
	const FName Source = bLeft ? LeftGripSource : RightGripSource;

	// Whichever motion controller device has a pose for the source answers; with OpenXR there is one.
	const TArray<IMotionController*> Devices = IModularFeatures::Get().GetModularFeatureImplementations<IMotionController>(IMotionController::GetModularFeatureName());
	for (IMotionController* Device : Devices)
	{
		FRotator Orientation = FRotator::ZeroRotator;
		FVector Position = FVector::ZeroVector;
		bool bLinear = false;
		bool bAngular = false;
		bool bAcceleration = false;
		FVector Linear = FVector::ZeroVector;
		FVector Angular = FVector::ZeroVector;
		FVector Acceleration = FVector::ZeroVector;
		if (Device && Device->GetControllerOrientationAndPosition(0, Source, Orientation, Position, bLinear, Linear, bAngular, Angular, bAcceleration, Acceleration, WorldToMeters))
		{
			// A pose is given for a controller the headset does not see, too: the runtime's guess, which wandered
			// by a metre and a half in the first session. Only a pose the runtime marks as tracked is one.
			OutState.bTracked = Device->GetControllerTrackingStatus(0, Source) == ETrackingStatus::Tracked;
			OutState.bEstimated = !OutState.bTracked;
			OutState.Location = Position;
			OutState.Rotation = Orientation.Quaternion();
			OutState.bHasLinearVelocity = bLinear;
			OutState.LinearVelocity = Linear;
			// The engine hands the angular velocity over as the axis times the speed in rad/s, in its own axes
			// and sense of rotation: v = w x r holds as it is.
			OutState.bHasAngularVelocity = bAngular;
			OutState.AngularVelocity = Angular;
			break;
		}
	}
	OutState.Squeeze = ReadSqueeze(bLeft);
	OutState.Stick = ReadStick(bLeft);
	ReadButtons(bLeft, OutState.bFirstButton, OutState.bSecondButton);
	return true;
}

void FSandboxXRControllers::ReadButtons(bool bLeft, bool& bOutFirst, bool& bOutSecond) const
{
	bOutFirst = false;
	bOutSecond = false;
	const APlayerController* Controller = Player.Get();
	if (!Controller)
	{
		return;
	}
	// The first session in the headset (vr_check3) had OculusTouch_Right_A_Touch and _B_Touch among the keys that arrived: the
	// PS VR2 controllers answer as the Touch profile, whose buttons are A and B on the right and X and Y on the left. Index's are
	// A and B on both.
	const FKey First[] = {
		bLeft ? EKeys::OculusTouch_Left_X_Click : EKeys::OculusTouch_Right_A_Click,
		bLeft ? EKeys::ValveIndex_Left_A_Click : EKeys::ValveIndex_Right_A_Click };
	const FKey Second[] = {
		bLeft ? EKeys::OculusTouch_Left_Y_Click : EKeys::OculusTouch_Right_B_Click,
		bLeft ? EKeys::ValveIndex_Left_B_Click : EKeys::ValveIndex_Right_B_Click };
	for (const FKey& Key : First)
	{
		bOutFirst = bOutFirst || Controller->IsInputKeyDown(Key);
	}
	for (const FKey& Key : Second)
	{
		bOutSecond = bOutSecond || Controller->IsInputKeyDown(Key);
	}
}

bool FSandboxXRControllers::GetHead(FVector& OutLocation, FQuat& OutRotation) const
{
	OutLocation = FVector::ZeroVector;
	OutRotation = FQuat::Identity;
	if (!GEngine || !GEngine->XRSystem.IsValid())
	{
		return false;
	}
	// In tracking space and in the world's units (cm), as the camera under the play space is moved by the engine.
	return GEngine->XRSystem->GetCurrentPose(IXRTrackingSystem::HMDDeviceId, OutRotation, OutLocation);
}

FVector2D FSandboxXRControllers::ReadStick(bool bLeft) const
{
	const APlayerController* Controller = Player.Get();
	if (!Controller)
	{
		return FVector2D::ZeroVector;
	}
	// The PS VR2 controllers arrive as the Touch profile (session vr_check3: OculusTouch_*_Thumbstick_X, _Y and _2D).
	const TPair<FKey, FKey> Sticks[] = {
		{ bLeft ? EKeys::OculusTouch_Left_Thumbstick_X : EKeys::OculusTouch_Right_Thumbstick_X, bLeft ? EKeys::OculusTouch_Left_Thumbstick_Y : EKeys::OculusTouch_Right_Thumbstick_Y },
		{ bLeft ? EKeys::ValveIndex_Left_Thumbstick_X : EKeys::ValveIndex_Right_Thumbstick_X, bLeft ? EKeys::ValveIndex_Left_Thumbstick_Y : EKeys::ValveIndex_Right_Thumbstick_Y },
		{ bLeft ? EKeys::MixedReality_Left_Thumbstick_X : EKeys::MixedReality_Right_Thumbstick_X, bLeft ? EKeys::MixedReality_Left_Thumbstick_Y : EKeys::MixedReality_Right_Thumbstick_Y } };
	FVector2D Stick = FVector2D::ZeroVector;
	for (const TPair<FKey, FKey>& Axes : Sticks)
	{
		const FVector2D Each(Controller->GetInputAnalogKeyState(Axes.Key), Controller->GetInputAnalogKeyState(Axes.Value));
		if (Each.SizeSquared() > Stick.SizeSquared())
		{
			Stick = Each;
		}
	}
	return Stick;
}

void FSandboxXRControllers::Buzz(bool bLeft, float Amplitude) const
{
	if (APlayerController* Controller = Player.Get())
	{
		// The frequency is left to the runtime (0 is OpenXR's "unspecified"; the engine hands the number on as
		// hertz). One call lasts one frame: the engine gives the vibration the frame's own duration.
		Controller->SetHapticsByValue(0.0f, FMath::Clamp(Amplitude, 0.0f, 1.0f), bLeft ? EControllerHand::Left : EControllerHand::Right);
	}
}

float FSandboxXRControllers::HapticAmplitude(double ContactForceN, double FullForceN)
{
	return FullForceN > 0.0 ? static_cast<float>(FMath::Clamp(ContactForceN / FullForceN, 0.0, 1.0)) : 0.0f;
}

float FSandboxXRControllers::ReadSqueeze(bool bLeft) const
{
	const APlayerController* Controller = Player.Get();
	if (!Controller)
	{
		return 0.0f;
	}
	// The runtime chooses the controller profile the bindings are taken from, so every profile's grip is
	// asked and the largest counts: axes where a profile has one, a button where it has not.
	const FKey Axes[] = {
		bLeft ? EKeys::OculusTouch_Left_Grip_Axis : EKeys::OculusTouch_Right_Grip_Axis,
		bLeft ? EKeys::ValveIndex_Left_Grip_Axis : EKeys::ValveIndex_Right_Grip_Axis };
	const FKey Buttons[] = {
		bLeft ? EKeys::Vive_Left_Grip_Click : EKeys::Vive_Right_Grip_Click,
		bLeft ? EKeys::MixedReality_Left_Grip_Click : EKeys::MixedReality_Right_Grip_Click };
	const auto Read = [Controller](const FKey& Key, bool bAxis)
	{
		return bAxis ? Controller->GetInputAnalogKeyState(Key) : Controller->IsInputKeyDown(Key) ? 1.0f : 0.0f;
	};
	float Squeeze = 0.0f;
	for (const FKey& Key : Axes)
	{
		Squeeze = FMath::Max(Squeeze, Read(Key, /*bAxis*/ true));
	}
	for (const FKey& Key : Buttons)
	{
		Squeeze = FMath::Max(Squeeze, Read(Key, /*bAxis*/ false));
	}

	// Every key of every controller profile is looked at, once a frame, only to know what arrives at all: in
	// the first session the grips did and the triggers that were pressed did not.
	if (bLeft)
	{
		if (ControllerKeys.Num() == 0)
		{
			TArray<FKey> All;
			EKeys::GetAllKeys(All);
			for (const FKey& Key : All)
			{
				const FString Name = Key.ToString();
				if (!Key.IsDeprecated() && (Name.Contains(TEXT("_Left_")) || Name.Contains(TEXT("_Right_"))) && !Name.StartsWith(TEXT("Gamepad")))
				{
					ControllerKeys.Add(Key);
				}
			}
		}
		for (const FKey& Key : ControllerKeys)
		{
			const bool bAxis = Key.IsAnalog();
			const bool bActive = bAxis ? FMath::Abs(Controller->GetInputAnalogKeyState(Key)) > 0.05f || !Controller->GetInputVectorKeyState(Key).IsNearlyZero(0.05) : Controller->IsInputKeyDown(Key);
			if (bActive)
			{
				KeysSeen.AddUnique(Key.GetFName());
			}
		}
	}
	return Squeeze;
}

FString FSandboxXRControllers::DescribeKeysSeen() const
{
	FString Names;
	for (const FName Key : KeysSeen)
	{
		Names += (Names.IsEmpty() ? TEXT("") : TEXT(", ")) + Key.ToString();
	}
	return Names.IsEmpty() ? FString(TEXT("none")) : Names;
}
