# N7: a get-up made for this body, step 1: the rise from a crouch by the body's own balance (26 November, 13:40), written before the runs

The owner's choice after unload-and-step missed: "a get-up made for this body", the back first, a film before going on.

## Design (behind getup.rise, 0 = today)
- From the back the clip is `M_ragdoll_getup_crouch_B` (getup.rise_clip_back) instead of stand_B, followed as today (waits,
  frame and pelvis following) only up to its crouch, getup.rise_at_back_s (0.95 s: a deep squat, both feet under the body).
- There the body takes over (once it has caught up and both feet carry weight): the muscles go over from the clip's crouch to
  the stance over getup.rise_s (the clip's layer fades from 1 to 0 over the stance as base); the pelvis's assist rises from where
  the pelvis is to its standing height above the floor, its tilt to upright (the yaw kept), and over the floor it is aimed so that
  the centre of mass goes over the middle of the feet; the feet's assist holds them where they stand.
- The rise waits (its share stands still) while the pelvis is more than lag_cm below its aim or the centre of mass is more than
  getup.rise_com_cm outside the feet; such waits count as the clip's do (stall_s, wait_s).
- Done when the rise is whole, the pelvis within upright_deg and both feet on the floor: Standing, as today.
- The other ways of lying keep today's clips (their rise clips come in a later step).

## Measures
plank_s, plank_max_cm, rise_feet_cm, rise_trunk_deg (kept since b65ecf9); new: when the rise began and how long it took.

## Limits (R), on the stand of 150 (three seeds) and the suite
- R1 the point, on the falls got up by the back's clip: plank_s median at most 0.3 s (today by stand_B 1.74) and rise_feet_cm
  median at most 25 (today 35.9).
- R2 the stand: at least 147 of 150 end standing and at least 127 at the first attempt (today 150, 130); of the falls got up by
  the back's clip, at least 80% at the first attempt (today 21 of 25).
- R3 the suite: with the switch at 0 the hashes of n2; with it on every scenario passes.
- R4 the back's get-up median no longer than 5.7 s (today 5.2).
- R5 the assist's mean share in those get-ups at most 0.30 (today 0.27).
- R6 films from the side, a sheet and a real-time video of getup_back, for the owner; I do not call them natural.

## Risks
- crouch_B reaches its squat by rolling back and swinging forward; this body has no swing and may wait or fail there (R2).
- In a deep squat the ankle's range (30 degrees forward) lifts the heels: the body may sit back or tip forward (R1 measures it).
- The pelvis's 90 N may not keep the centre of mass over the feet while the legs straighten: waits, R4.
- The stance's leg angles with feet staggered in the squat: the legs end as an A or a stride; Standing then regroups.

## Variants, values decided now
- K-a: rise 1, rise_at_back_s 0.95, rise_s 1.2, rise_com_cm 3
- K-b: as K-a with rise_s 1.6
No other variant without writing it here first.

## 14:05, first try (diagnostic, not judged)
getup_back with K-a: the body reaches the crouch (pelvis 53 cm, both feet loaded) at clip 0.96 s, the centre of mass 12 to 24 cm
ahead of the feet; the rise waits for it and never starts (the pelvis's 90 N does not move it); given up after 3 s. The wait on the
centre of mass blocks what would bring it back (the trunk coming up with the rise). Diagnostic next: rise_com_cm 100 (no wait on
the centre of mass), getup_back only, to see what the blend itself does. Not a variant; the variants above are still to run.

## 14:15, the diagnostic, and the change it calls for (before any judged run)
rise_com_cm 100: the body rises in 2.0 s, but the centre of mass stays 13 to 36 cm ahead of the feet the whole way: plank 2.5 s (today
1.7 in that fall). The clip's crouch on this body is a pose a person passes through moving forward; held still, its centre of mass is
22 cm ahead of the feet, and the pelvis's 90 N does not move it. Change, written now: the body moves its own centre of mass, as the
ankle strategy does standing: the whole body above the feet leans toward where the centre of mass should be, by a turn of both feet
against their calves, integrated from the error (getup.rise_lean_s: the error divided by the centre of mass's height, per that
time; at most getup.rise_lean_max_deg), during the rise and its waits; after the rise the lean fades over poses.blend_s by its own
clock. The variants K-a and K-b get rise_lean_s 0.3 and rise_lean_max_deg 30; the limits are unchanged.

## 14:30, the lean by the ankles (diagnostic on getup_back, not judged)
The centre of mass came from 23 to 18 cm ahead, then went to 63: the body tipped forward over its toes and the attempt was lost. The
ankles cannot bring back a centre of mass that is already ahead of the toes (the foot turns over its toes). Change, written now:
the lean is made at the hips instead (the trunk bends toward where the centre of mass should be, the thighs turning against the
pelvis, as the hip strategy does standing), same integrator and cap; diagnostic on getup_back first.

## 14:45, stopped before the stand
Hip lean: the body stays crouched with half its weight on its hands, the centre of mass 31 cm ahead; given up after 3 s. K-a and K-b
not run on the stand: every try on getup_back either never started the rise or rose as a plank (the diagnostic without the wait: up
in 2.0 s, plank 2.5 s against 1.7 today). No limit judged. Videos for the owner: Saved\Films\n7_back_today_side and n7_back_rise_side.
Code behind getup.rise at 0, suite with the n2 hashes, 48 tests; not committed.
