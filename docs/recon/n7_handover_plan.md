# N7: catch the lean with a step (the owner's decision of 26 November, 15:00), written before the runs

The owner on both videos (today's get-up and the rise from the crouch): both still rise like a plank at the end. The plank at the
end is the world's: the pelvis's assist (up to 200 N*m, at its cap in the lean, 25 November) turns the straight body about its feet
to upright while the centre of mass is ahead of the toes. A person there catches the lean with a step forward.

## Design (behind getup.hand_over, 0 = today)
- While getting up by today's clips (getup.rise stays 0), once the pelvis is within getup.hand_over_drop_cm of its standing height
  above the floor (StandPelvisHeightCm) and tilted less than getup.hand_over_tilt_deg from upright (as Standing measures it, against
  the pelvis's standing turn), with both feet on the floor and not held down, the get-up ends there, waits and clip's end or not.
- Standing takes over as after any get-up (anchors from where the body is, regrouping); its balance decides: a capture point ahead of
  the feet is a step (Stagger), as after a push. Thresholds of a fall stay (pelvis 20 cm down or 35 degrees).

## Measures
- plank_s as before (while getting up), and new plank_after_s: the same rule for 3 s after a get-up ends standing (a plank that moved
  into Standing or Stagger is counted there).
- Known answer for plank_after_s: with the switch at 0 it is the same rule as plank_s applied to series.csv after the get-up; checked on
  getup_back against the series.

## Limits (H), on the stand of 150 (three seeds) and the suite
- H1 the point: median of plank_s + plank_after_s at most half of today's (today's measured first with the switch at 0).
- H2 the stand: at least 147 of 150 end standing, at least 127 at the first attempt (today 150, 130).
- H3 the suite: with the switch at 0 the hashes of n2; with it on every scenario passes.
- H4 the get-up's median no longer than today's.
- H5 the assist's mean share while getting up no more than today's + 0.03.
- H6 side videos of getup_back and getup_front for the owner; I do not call them natural.

## Variants, values decided now
- H-a: hand_over 1, hand_over_drop_cm 15, hand_over_tilt_deg 25
- H-b: hand_over 1, hand_over_drop_cm 15, hand_over_tilt_deg 32
No other variant without writing it here first.

## Risks
- Standing's anchor glide and the stagger's upright hold (up to 150 N*m) are world help too: the plank may move into Stagger (H1 counts it).
- A body handed over too early falls again: a second attempt (H2).

## Results (15:45)
- plank_after_s checked on getup_back: 0.389 s by the controller and by series.csv.
- Today (h0): 150, 130; plank with the 3 s after 1.61 s (1.36 while getting up, 0.24 after); get-up 5.01 s; assist 0.28.
- H-a (25 deg): 148, 113; plank 1.59 (1.17 + 0.42); get-up 4.19; suite: 3 fail (effector_shove_knockdown_gets_up, getup_back,
  getup_push_weak). H1, H2, H3 missed.
- H-b (32 deg): 149, 124; plank 1.60 (0.98 + 0.56); get-up 3.94; suite 127. H1, H2 missed.
- Not met. Taken out (Saved\Recon\n7_hand_over.patch); plank_after_s stays (suite with the n2 hashes, 48 tests).
- The plank is mostly at tilts above 25 to 32 degrees, before a hand-over is possible; what is handed over is held by the stagger's
  upright hold instead of the get-up's assist.
