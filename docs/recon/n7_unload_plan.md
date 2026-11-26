# N7: unload, then step (the owner's decision of 26 November, 13:10), written before the runs

## What the owner sees
The body rises "like a plank, like a rocket on its launcher" (Tochka-U). In getup_back (today's body, n2 tuning):
- 9.0 s: half-kneel, pelvis 53 cm up, right knee on the floor, left foot 34 cm to the side of the pelvis (not ahead of it),
  right foot 34 cm behind; L 807 N, R 139 N; centre of mass 31 cm outside the feet.
- 9.5 s: pelvis 80 cm (standing is 90), both feet behind the pelvis along the lean (27 and 48 cm), trunk 50 degrees forward;
  feet carry 700 to 1200 N, centre of mass 21 to 33 cm outside them from 9.0 to 10.5 s, assist 0.3 of the weight.
- The clip moved the right foot forward 20 cm (clip 1.7 to 1.9 s) while it carried 260 to 520 N; it did not come; the clip's
  pelvis went on up and forward, the muscles and the assist (90 N, 200 N*m on the pelvis) took the body with it.

## Design (behind getup.unload_step, 0 = today)
- A foot that carries weight and is more than `getup.step_lag_cm` over the floor from where the clip has it owes a step, if the
  other foot carries weight too. One step at a time, the foot furthest off first.
- While a step is owed the clip waits (counted as `behind_step_s`).
- Unload: the pelvis's assist is aimed so that the centre of mass goes over the other foot (its target moved by the other foot's
  centre minus the centre of mass, at most `getup.shift_max_cm`, gliding with frame_follow_s). The clip's root follows only the other
  foot meanwhile.
- Step: once the foot carries no more than `getup.unload_share` of the weight it is carried to the clip's foot, its assist's target
  raised by `getup.step_lift_cm` until it is within foot_lag_cm over the floor; the step is done when it is there and on the floor.
- Bounded: a step not done in `getup.step_max_s` is let go, the clip goes on as today, and that foot owes no step until it has been
  within step_lag_cm of the clip's foot once.

## Measures (control.get_up, the last attempt; read only)
- `plank_s`: time after easing in with the feet carrying 0.8 of the weight or more and the centre of mass more than 5 cm outside
  them (the measure of 25 November); `plank_max_cm`: the furthest outside then.
- `rise_feet_cm`, `rise_trunk_deg`: when the pelvis first comes 70 cm above the floor: the farther foot from the pelvis over the
  floor, and the trunk (pelvis to chest) from vertical. Below 0: not reached.
- `steps`, `steps_done`, `steps_let_go`, `behind_step_s`.
- Known answers before use: plank_s against the same rule applied to series.csv of the same run (to a frame); rise_* against
  trace.json at the nearest checkpoints (getup_back: 9.5 s pelvis 80, feet 41 and 51, trunk about 50 by the head).

## Limits (U), judged on the stand of 150 (three seeds), the suite and films
- U1 the point: `plank_s` median at most half of today's (today's measured with the switch at 0 first, before any variant), and
  `rise_feet_cm` median at most 30.
- U2 the stand: at least 147 of 150 end standing, at least 129 at the first attempt (today 150, 130).
- U3 the suite: every scenario passes; with the switch at 0 the hashes of n2.
- U4 the get-up: median no longer than today's by more than 0.8 s.
- U5 the assist's share while getting up: no more than today's + 0.03.
- U6 films from the side of getup_back and getup_front, for the owner; I do not call them natural.
- If U1 to U5 are met: the pressed stand (-PressN 300) and the held stands (-HoldHands 1 and 2): one hand slows, two hold.
- Pauses are recorded, not judged (the owner: no visible difference).

## Risks
- The pelvis's 90 N may not move the weight over the other foot against the legs' muscles holding the clip's pose: steps let go
  after step_max_s, the clip goes on as today, and each costs that much time (U4).
- The other foot can itself be far from under the body (34 cm to the side at 9.0 s): the centre of mass cannot go over it within
  shift_max_cm. Same fallback.
- The clip waits more: more attempts may run into wait_s (U2).
- Every hash of a get-up scenario moves with it on: they are explained by their figures, not by their hashes.

## Variants, values decided now
- U-a: unload_step 1, step_lag_cm 20, unload_share 0.15, step_lift_cm 8, step_max_s 1.2, shift_max_cm 25
- U-b: as U-a with step_lag_cm 15 (steps owed earlier)
- U-c: as U-a with unload_share 0.25 (a foot lifted sooner)
No other variant without writing it here first.

## Results (13:05), against the limits above
- Today (u0): stand 150, 130; plank 1.36 s, furthest out 46 cm; at 70 cm foot 37 cm, trunk 70 deg; get-up 5.01 s; assist 0.28.
- U-a: 148, 107; plank 1.36; foot 33; get-up 6.52; 524 steps owed, 198 let go; suite 127. U1, U2, U4 missed.
- U-b: 132, 63; plank 1.13; foot 33; get-up 6.64; 3162 owed, 143 let go; 5 get-up scenarios fail. U1 to U4 missed.
- U-c: 148, 107; plank 1.23; foot 33; get-up 6.54; 539 owed, 186 let go; suite 127. U1, U2, U4 missed.
- Not met. Taken out (Saved\Recon\n7_unload_step_full.patch); the plank measures stay (suite with the n2 hashes).
- This repeats 25 November's third attempt ("the foot held until it is free"), which I missed when I put the question to the owner.
- Of 927 plank samples of today's stand, 633 have both feet within 20 cm of the clip's: mostly not a foot left behind on its own.
