#pragma once

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "SandboxGrabJoint.h"
#include "SandboxInteractionTuning.h"
#include "SandboxTargetSource.h"
#include "SandboxWorldSpring.h"
#include "SandboxEffectorComponent.generated.h"

class UStaticMeshComponent;
class USandboxEffectorComponent;
struct FCollisionShape;

/** What one effector has done since it was started. For result files and the HUD. */
struct FSandboxEffectorStats
{
	/** Sum of the sizes of all contact impulses on the effector, N*s. */
	double ContactImpulseNs = 0.0;
	/**
	 * Largest net contact impulse received within one frame, N*s. Divided by the frame time it is a force,
	 * which may well exceed the spring's cap: a moving hand that is stopped gives up its momentum too.
	 */
	double MaxFrameImpulseNs = 0.0;
	/** Net contact impulse of the last completed physics step, N*s: what the hand is pressing with now. */
	double LastFrameImpulseNs = 0.0;
	/** Frames in which the effector touched something. */
	int32 ContactFrames = 0;
	/** Contact impulse by what was touched: the bone of a skeletal body, the actor's name otherwise. N*s. */
	TMap<FName, double> ImpulseByBody;
	/** Furthest the body was from its target, cm, and the fastest it moved, cm/s. */
	double MaxGapCm = 0.0;
	double MaxSpeedCmS = 0.0;
	/** Largest force the spring law asked for before the cap, N. */
	double MaxDemandN = 0.0;
	/** Times the body was put back on its target because it was further away than the snap distance. */
	int32 Snaps = 0;
	/** Times the hand changed from the ball to the open hand's plate or back (effectors.hand.palm_collides). */
	int32 ShapeChanges = 0;
	/**
	 * Times the grip closed while the open hand lay on something, and what it lay on the last time (named as
	 * in ImpulseByBody). Such a hand does not become a fist there: it is what a grab starts from (M4 part 3).
	 */
	int32 GripsOnBody = 0;
	FName LastGripped;
	/**
	 * Grabs (effectors.hand.grab): times a closing grip took hold of a simulated body, what it held the last
	 * time (named as in ImpulseByBody), the frames it held something, the times the grip slipped because the
	 * hand was left too far behind its target, the times what it held ceased to exist, and the furthest the
	 * two held points came apart, cm.
	 */
	int32 Grabs = 0;
	FName LastGrabbed;
	int32 GrabFrames = 0;
	int32 GrabSlips = 0;
	int32 GrabsLost = 0;
	double MaxGrabStretchCm = 0.0;
	/**
	 * The times the grip was torn open by the joint's own load (effectors.hand.grab_max_force_n), the largest
	 * force the joint carried at the end of a frame it held, N, and the sum of that force over those frames
	 * (for a mean). The engine keeps a joint's force of the last substep only: a jolt in a frame's first
	 * substep is not in these.
	 */
	int32 GrabOverloads = 0;
	double MaxGrabForceN = 0.0;
	double GrabForceSumN = 0.0;
	/** Blows: times the hand, having come fast, met something and had the higher cap for a moment (effectors.hand.strike_force_n). */
	int32 Strikes = 0;
	/** Of the grabs, those that took a thing by its handle (a point the thing names; effectors.hand.grab_handle_cm). */
	int32 HandleGrabs = 0;
	/**
	 * The first thing the effector ever touched (named as in ImpulseByBody), and how fast the effector was
	 * moving at the start of the physics step in which that happened, cm/s: the speed a strike arrived with.
	 */
	FName FirstTouched;
	double FirstContactSpeedCmS = 0.0;
};

/**
 * The open hand as a body: a flat box (USandboxEffectorComponent makes one the first time its hand opens).
 * It exists as a class of its own only to let go of its spring before its body is destroyed.
 */
UCLASS()
class SANDBOXINTERACTION_API USandboxPalmComponent : public UBoxComponent
{
	GENERATED_BODY()

public:
	USandboxPalmComponent();

	virtual void OnDestroyPhysicsState() override;

	TWeakObjectPtr<USandboxEffectorComponent> Hand;
};

/**
 * One of the player's hands as the physics world sees it: a body with real mass, pulled toward a target pose
 * by a spring with a force cap (FSandboxWorldSpring). It is never kinematic while it is the hand: a kinematic
 * hand has infinite mass and launches whatever it touches. What it touches it knows nothing about; the other
 * body feels the contact through its own contact events.
 *
 * No gravity acts on it: the arm that would carry the hand is not simulated, and its strength, the force
 * cap, should not be spent on the hand's own weight. Continuous collision detection is on.
 *
 * A fist is a ball: the sphere component itself, with a mesh of the same size attached so that it can be
 * seen. An open hand (the target's grip below effectors.hand.fist_grip) is shown as a flat plate, and with
 * effectors.hand.palm_collides it is one: a second body, a box, takes the ball's place, with the ball's pose
 * and motion, its own spring and its own mass, and gives it back when the hand closes. Only one of the two
 * collides and is simulated at a time; a hand that never opens never has the second. Whoever wants to know
 * where the hand is asks GetHandTransform and its like, not the component.
 *
 * With effectors.hand.grab, a grip that closes with a simulated body within reach of the hand takes hold of it: a
 * joint keeps the hand's own point and that body together (FSandboxGrabJoint) until the grip opens, the
 * hand is left too far behind its target (the grip slips), the joint carries more than it may for too long
 * (the grip is torn open) or the body is gone. A grip that closes in the
 * air is a fist. The hand knows nothing of what it holds; the held body is pulled as by any other force,
 * and the force is the hand's own spring's, with its cap. A hand that holds something collides with no
 * simulated body (it holds a point), and does again once it has let go and lies no deeper in one than
 * effectors.hand.grab_clear_cm.
 *
 * The owner gives it a target and calls Update once per frame, before the physics step.
 */
UCLASS()
class SANDBOXINTERACTION_API USandboxEffectorComponent : public USphereComponent
{
	GENERATED_BODY()

public:
	USandboxEffectorComponent();

	/**
	 * Makes the body at Pose and ties it to its spring. Call on a registered component. False with a reason
	 * if the sphere mesh cannot be loaded or no physics body was created.
	 */
	bool Start(FName InId, const FSandboxEffectorTuning& InTuning, const FTransform& Pose, FString& OutError);

	/** New mass, spring and caps. The radius and the plate's size cannot change after Start. */
	void ApplyTuning(const FSandboxEffectorTuning& InTuning);

	/**
	 * The share of the spring's force cap the effector has from now on, 0 to 1. Update sets it from the
	 * target's strength (FSandboxEffectorTarget::Strength), which is 1 unless a source says otherwise.
	 */
	void SetForceShare(double Share);

	/** The pose to pull toward from the next Update on. */
	void SetTarget(const FSandboxEffectorTarget& InTarget) { Target = InTarget; }
	const FSandboxEffectorTarget& GetTarget() const { return Target; }

	/** Once per frame, before the physics step: hands the target to the spring and keeps the record. */
	void Update(float DeltaTime);

	FName GetId() const { return Id; }
	/** True while the hand is a fist: its target's grip is above the tuning's fist grip. A new hand is a fist. */
	bool IsFist() const { return bFist; }
	/** True while the open hand's plate is the body that collides. */
	bool IsPalmActive() const { return bPalmActive; }
	/** True while the hand holds a body, and what it holds (the bone of a skeletal body, None otherwise). */
	bool IsGrabbing() const { return Grab.IsAttached(); }
	FName GetGrabbedBone() const { return Grab.GetBone(); }
	/**
	 * True while the hand does not collide with simulated bodies: while it holds something, and after it has
	 * let go for as long as it still lies in one. Such a hand is not in anything it could have gone through.
	 */
	bool IsPassingBodies() const { return bPassesBodies; }
	/** The force the hand held on with in the last physics substep, N; 0 while it holds nothing. */
	double GetGrabForceN() const { return GrabForceN; }
	UPrimitiveComponent* GetGrabbedComponent() const { return Grab.GetComponent(); }
	bool IsStarted() const { return bStarted; }
	const FSandboxEffectorTuning& GetTuning() const { return Tuning; }
	const FSandboxEffectorStats& GetStats() const { return Stats; }
	/** Largest net contact force over one frame so far, N. */
	double GetMaxContactForceN() const { return FrameDt > 0.0 ? Stats.MaxFrameImpulseNs / FrameDt : 0.0; }
	/** Net contact force over the last completed physics step, N. A hand held against something reads its force cap. */
	double GetContactForceN() const { return FrameDt > 0.0 ? Stats.LastFrameImpulseNs / FrameDt : 0.0; }
	/** Distance from the body to its target now, cm. */
	double GetGapCm() const { return ActiveSpring().GetGapCm(); }
	/** The force the spring law asked for at the last Update, before its cap, N. */
	double GetDemandN() const { return ActiveSpring().GetDemandN(); }

	/** The hand's own frame in the world, from the body that is the hand now: the ball's pose, or the plate's held point. */
	FTransform GetHandTransform() const;
	/** The velocity of the hand's origin, cm/s. */
	FVector GetHandVelocity() const;
	/** The mass of the body that is the hand now, kg. */
	double GetHandMassKg() const;
	/**
	 * The hand's shape for a scene query: the shape, where its middle is and how it is turned, and half its
	 * smallest extent, cm (the ball's radius, half the plate's thickness): no body can be deeper in another
	 * than that without having gone through.
	 */
	void GetProbe(FCollisionShape& OutShape, FVector& OutCenter, FQuat& OutRotation, double& OutThinHalfCm) const;

	/** The spring lets go here, while the body it holds still exists. */
	virtual void OnDestroyPhysicsState() override;
	/** The meshes and the plate that ride on the hand go with it. */
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	/** For the plate: its spring, and a grab it holds, let go before its body is destroyed. */
	void ReleasePalmSpring();

private:
	UFUNCTION()
	void OnEffectorHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);

	/** What a thing in the hand meets (effectors.hand.strike_held). */
	UFUNCTION()
	void OnHeldHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);

	/** Starts and stops listening to the thing in the hand, and reads how it moves as a step begins. */
	void UpdateHeldThing();
	/** Holds and turns the thing in the hand for a blow (Share of effectors.hand.strike_hold_scale), or as usual again. */
	void SetHeldStrength(bool bBlow);

	const FSandboxWorldSpring& ActiveSpring() const { return bPalmActive ? PalmSpring : Spring; }
	FSandboxWorldSpring& ActiveSpring() { return bPalmActive ? PalmSpring : Spring; }
	FBodyInstance* ActiveBody() const;
	/** The plate's middle in the hand's own frame. */
	FVector PalmOffset() const { return FVector(Tuning.PalmForwardCm, 0.0, 0.0); }
	/** Hands the hand over from the ball to the plate or back: pose, motion, spring and mass. */
	void SwitchBody(bool bToPalm);
	/** True if the other body, put where the hand is now, would lie in something of the world or of another body. */
	bool WouldOverlap(bool bAsPalm) const;
	/** How deep the hand's shape lies in the simulated bodies that a holding hand passes, the other hand among them, cm; 0 if in none. */
	double DepthInBodiesCm() const;
	/** Whether the hand's bodies collide with simulated bodies (the character's, a prop's, the other hand). */
	void SetPassesBodies(bool bPass);
	bool MakePalmBody();
	/** Takes hold, lets go and keeps the record of a grab; once per Update, after the grip has been read. */
	void UpdateGrab(float DeltaTime, bool bGripClosed);
	/** The simulated body nearest to the hand among those within effectors.hand.grab_reach_cm of its shape; false if there is none. */
	bool FindGrabbable(UPrimitiveComponent*& OutComponent, FName& OutBone) const;

	/** What is seen of the hand: a sphere mesh without collision that rides on the body. */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Visual;

	/** The open hand as it is seen: a flat plate in the hand's own frame, shown while the hand is not a fist. */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> PalmVisual;

	/** The open hand as a body, once the hand has opened with effectors.hand.palm_collides on. */
	UPROPERTY()
	TObjectPtr<USandboxPalmComponent> PalmBody;

	FName Id;
	FSandboxEffectorTuning Tuning;
	FSandboxEffectorTarget Target;
	FSandboxWorldSpring Spring;
	FSandboxWorldSpring PalmSpring;
	/** What the hand holds, on whichever of its two bodies was the hand when the grip closed. */
	FSandboxGrabJoint Grab;
	/** For how long the hand that holds something has been too far behind its target, s. */
	double GrabSlipS = 0.0;
	/** For how long the joint of a grab has carried more than it may, s. */
	double GrabOverloadS = 0.0;
	double GrabForceN = 0.0;
	/** After a thing was taken by its handle: for how long it is still being drawn to the hand, s. */
	double GrabDrawLeftS = 0.0;
	bool bPassesBodies = false;
	double ForceShare = 1.0;
	/** A blow: how long the higher cap still lasts, s, and whether the spring has it now. */
	double StrikeLeftS = 0.0;
	bool bStrikeOn = false;
	/** The fist springs back from a blow to its target (effectors.hand.strike_return_cm): what it meets is no new blow. */
	bool bStrikeReturning = false;
	/** A blow of the thing in the hand: the thing listened to, how it moved as the step began, and the last frame it met something. */
	TWeakObjectPtr<UPrimitiveComponent> HeldThing;
	FVector HeldVelocityCmS = FVector::ZeroVector;
	FVector HeldAngularRadS = FVector::ZeroVector;
	FVector HeldComCm = FVector::ZeroVector;
	int64 HeldImpulseFrame = -10;
	bool bHeldBlow = false;
	/** How much of a blow's strength this one has, 0 to 1, by the speed the hand came at. */
	double StrikeShare = 0.0;
	double CapNow() const;
	double StiffnessNow() const;
	FSandboxEffectorStats Stats;
	bool bStarted = false;
	bool bFist = true;
	bool bPalmActive = false;
	/** A change of body that waits for room, and whether a closing grip that waits has been counted. */
	bool bGripCounted = false;
	/** What the hand touched last, and in which of its frames. */
	FName LastTouched;
	int64 LastTouchedFrame = -1;

	/** Frames counted by Update, and the frame the contact impulses in FrameImpulseNs belong to. */
	int64 Frame = 0;
	int64 ImpulseFrame = -1;
	FVector FrameImpulseNs = FVector::ZeroVector;
	double FrameDt = 0.0;
	/** Speed of the body at the last Update: at the start of the step whose contacts are reported next. */
	double StepStartSpeedCmS = 0.0;
	FVector StepStartVelocityCmS = FVector::ZeroVector;
	/** The way the fist came as its blow landed, and where it was then: the blow is carried on from there, that way. */
	FVector StrikeDirection = FVector::ZeroVector;
	FVector StrikePoint = FVector::ZeroVector;
};
