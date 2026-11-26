# N7 first: a get-up without pauses (the owner's look of 26 November: "several clips with clear pauses, it begins after a
# delay, robotic"; question answered: take it now)

Written before the runs.

## Built (all 0 in the files: today's get-up, bit for bit)
- `getup.slow_from`, `getup.rate_follow_s`: the clip slows as the body falls behind (from that share of today's stop lags to a
  stop at them), runs while the body eases in, and each change of its speed is smoothed.
- `getup.rest_per_ns_s`, `getup.rest_max_s`: the body lies `rest_s` plus this per N*s of the hit that felled it, bounded.
- Measures: `control.get_up.paused_s` (the clip under a tenth of its rate, end included), `lay_s` (lying before the get-up).

## Variants (by -Tune)
- V1 `slow_from` 0.5, `rate_follow_s` 0.15.
- V2 V1 and `rest_s` 0.4, `rest_per_ns_s` 0.004, `rest_max_s` 2.0 (a blow of 250 N*s: 1.4 s; a slip: 0.4 s).

## Limits
- L1 switches off: the suite with the hashes of `n2` (127).
- L2 stand on three seeds: 147 of 150 stand or more, 129 or more at the first attempt (today 150, 130).
- L3 the clip's pause (`paused_s`) over the stand's get-ups: median at most half of today's.
- L4 a get-up from its start to standing (`duration_s`): median not longer than today's.
- L5 V2 only: from lying to standing (`lay_s` + `duration_s`) median at least 1 s shorter than today's.
- L6 the suite's get-up scenarios pass; pressed with 300 N and held by one hand: within the spread of three seeds of today
  (39 stood pressed of 150; 46 pulled free).
- Films of a get-up from the back and the front, today and the chosen variant, for the owner. Not called natural by me.

## After V1 and V2 missed (11:10), written before the next runs
V1 (147, 126 first; pause 1.72; get-up 6.55 s) and V2 (143, 125; suite fails prop_getup_on_plate) missed L2 to L4: slowing the
clip stretched the get-up. Today's pause split (measured, 150 get-ups): easing in 0.60 s, waiting for the body mid-clip 1.81 s
median, at the end 0.01. The waiting is for a foot in the air (0.96 s median) and the pelvis aside (0.73 s), rarely its height.
Next variants, values only (the same limits L2 to L4, judged on the stand of three seeds):
- W1 `getup.foot_lag_cm` 25, `getup.foot_wait_s` 0.35
- W2 `getup.side_lag_cm` 30
- W3 W1 and W2
- W4 W3 and `getup.ease_s` 0.3

## After W1 to W4 (11:26), written before the next runs
Letting the clip wait less made get-ups shorter and their pauses smaller (W4: 0.93 s, 3.5 s) and failed more (first attempts
103 to 123 of 150; two to four get-up scenarios). The body cannot follow the clip. The get-up's muscles damp against a still target
(`pose_velocity_share` 0) while the clip's pose moves all the time. Next, same limits:
- X1 `states.get_up.muscles.{spine,arms,legs}.pose_velocity_share` 1, the waits as today
- X2 X1 and W4's values

## After X1, X2, R1 (11:40): why the feet lag (the owner: find it), written before the next runs
In the waits for a foot the pelvis is in its place (2 cm aside, 1 cm in height) and the foot is off 18 cm as the wait begins and
18 cm as it ends (0.5 s later, cut by `foot_wait_s`): the foot does not come, the clip asks where it cannot reach (stand_B at
0.72 s: 26 of the waits). The pelvis aside comes closer in some waits (stand_B, stand_R: 27 to 23 cm) and hovers at the 20 cm in
others (stand_F, stand_L: 22 to 21). Built behind `getup.progress_cm` (0: as today): the clip waits for a foot (and with
`getup.progress_pelvis` 1 for the pelvis) only while it comes closer by `progress_cm` in every `getup.progress_window_s`.
- P1 feet: `progress_cm` 1, `progress_window_s` 0.15
- P2 P1 and the pelvis
- P3 P2 and `ease_s` 0.4
The same limits: L2 147 and 129 at the first attempt; L3 the pause at most 1.21 s; L4 the get-up at most 5.0 s; the suite.

## After P1 to P3 (11:52), written before the next runs
Not waiting for what does not come cost first attempts (114, 91, 91): while the clip waits for a foot that stays off, the rest of
the body settles, and that is what the waits buy. Next, whether the body is too weak to keep up: the get-up's own muscles stronger
(`states.get_up.muscles.*`; today the get-up has the standing legs, 800 N*m/rad capped at 250, and spine 600 at 300). Same limits.
- S1 legs 1200 (cap 350), spine 900 (cap 400)
- S2 legs 1600 (cap 450), spine 1200 (cap 500)

## After S1, S2 (11:58), written before the next runs
Stronger get-up legs and spine cut the pelvis's waits and raised the feet's (148 to 194 s), first attempts 118 and 114. The feet's
wait compares the foot with the clip's own foot (the sample's skeleton, scaled), not with where the body's legs put it.
`getup.reached_feet` 1 (behind a switch, 0 in the file) aims the feet's assist and the wait at the foot of the pose the muscles
are asked for, chained on the mesh's proportions. Checked first on getup_back: the two points are 5 to 10 cm apart throughout
(not the 18 cm a hip clamped 25 degrees was expected to give: that prediction was wrong); at the clip's 0.72 s the body's foot is
17.7 cm from the clip's and 11 from the reached. Same limits:
- F1 `reached_feet` 1
- F2 F1 and S1's muscles

## Before the phases (12:25): a wait that is not a freeze (N7 item 7), written before the runs
`getup.gather_s` (0 in the file): while the clip waits, its time goes back by up to that and comes again at `gather_rate` of its
rate, never past where the wait began. The waits stay (so should the first attempts); the freeze goes. `paused_s` counts the clip
under a tenth of its rate either way. Same limits, L3 read as the freeze.
- G1 `gather_s` 0.15, `gather_rate` 0.5
- G2 `gather_s` 0.3, `gather_rate` 0.5

- GS (12:30, before its run): G1 and S1's muscles; the same limits.
