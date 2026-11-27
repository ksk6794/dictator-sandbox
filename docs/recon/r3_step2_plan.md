# R3, step 2: the fall's pose blended by its way and its time to impact

**Outcome (27 November, 04:55): a first part built behind `fall.blend` at 0, for the owner's look** (`docs/testing.md`, "R3, step 2";
decision 37). With two hours of the window left the plan below was begun after all, as R2's steps 3, 4 and 6 were: the four reaching
poses by the sensed way, on the arms only (over the whole body the sample's legs, poses of a body in the air, held a leg up as the
body lay), letting go over `poses.fall_blend_s` as the body lies. Not built: the flails by the time to impact, `fall.blend_written`
as a share of its own (`fall.blend_share` lays the blend over the written pose's arms), the arms' strength on the time to impact, the
`_up_*` and `_down` poses. **The owner's look (27 November): kept at 0, to be worked on**; the arms' strength on the time to impact
and their giving way at the touch come next, then new videos. The plan as written follows.

Written 27 November 2026, 04:30, the owner away (the autonomous window of 22:12 to 06:42), after R3's first step built the sense this
step would use (`FSandboxFallSense`: the way every tick, the time to the trunk's meeting with the floor or a thing, 0.020 s late in the
median at the first tick). **Not begun: it changes how every fall looks, which is the owner's to judge, and it meets the 40 J of the
fall's energy that the owner kept back.** For the owner to approve, change or drop.

Read before it (`docs/pitfalls.md`): Falls and the get-up (all of it: "The pose of a fall is for meeting the floor", the N6 entries on
the reach); Muscles, poses and the standing body ("A change to a path that several features share is measured on each of them", in
CLAUDE.md); Measuring ("Passing every check is not moving like a person").

## What (R3's second and third items, `docs/active-ragdoll.md`)

- Today a fall takes one of three written poses (`poses.fall_back`, `fall_forward`, `fall_side`) chosen once by `ChooseFallWay` at its
  start, and N6's reach (`fall.aim`, off) sends the hands to points of the floor. The references (Epic's sample, Euphoria) blend a set
  of recorded poses by the direction the body goes and by how soon it lands, every tick, so that nothing switches when the way turns.
- **The blend.** While Falling, behind `fall.blend` (0: today's fall, bit for bit), the target pose is a weighted blend of the sample's
  reaching poses (`M_ragdoll_reach_forward`, `_back`, `_left`, `_right`, `_down` and the three `_up_*`; read once with
  `FSandboxClipPose`, each at one time of its clip) by the sensed way in the body's frame (forward, back, left, right; the weights
  follow the way smoothly over `fall.blend_follow_s`), and the sample's three flails (`M_ragdoll_flail_*`, looped) weighted in while the
  time to impact is long (over `fall.flail_from_s`), out as it shortens. Our three written poses stay as the fourth set of the blend
  (`fall.blend_written`, a share), since the owner judged them better for a body going over from standing than the sample's
  (`docs/testing.md`, "The sample's other poses next to ours").
- **The arms' strength on the time to impact** (the sample's `Ragdoll_UpdatePhysicsStrengthsFromCurves`): the arms at their braced
  tone from `fall.brace_from_s` before impact, let go to `fall.arm_yield_*` the moment a hand touches. Not a target in the world
  (N6's mistake); the arms reach where the pose has them, in the trunk's frame.
- The blend needs a weighted blend of joint rotations: `FSandboxPoseRig` gets a pure function for it (quaternions on one hemisphere,
  weighted and normalised), unit-tested; the animation instance's clip layer takes a blended pose as it takes one clip's.

## Settings (all new, all in `Config/Tuning/body.json` and `VisitValues`)

`fall.blend` (0), `fall.blend_follow_s`, `fall.blend_written`, `fall.flail_from_s`, `fall.brace_from_s`, and the clip times the poses
are read at. Their first values from the sample's blend spaces where it has them.

## Checks

- `fall.blend` 0: every hash of `laid`. 1: the suite's list of changed hashes, each explained; the knockdowns by blow and by hand
  (`-ByHand`): C14 to C17 (the hand first, the head kept off the floor, the head's speed at the floor, rest within 3 s), the energy a
  fall's own muscles add (today at most 14.2 J by blow; **40 J is the owner's limit and is not raised here**), the push matrix's
  falls, the stand of 150 on three seeds (the bodies lie otherwise, and C19 to C21, C27 follow from how they lie).
- Films of every knockdown from the side and the front, 0 and 1 side by side (`Tools\film-pair.ps1 -TuneB "fall.blend=1"`), for the
  owner: the judgement is theirs.

## Risks

| Risk | Guard | Fallback |
| --- | --- | --- |
| The sample's poses are for a body thrown through the air, not one going over from standing | `fall.blend_written`; films | Our poses with the sample's only as the time shortens |
| Arms that reach add energy (N6 gained up to 83 J by hand) | The energy by blow and by hand every run | Lower arm tone; the reach only within `fall.brace_from_s` |
| A blend of eight poses lands between them, a pose nobody recorded | `metrics.pose_prior` against the sample's clips | Fewer poses: four by direction |
| The way flips near straight down | The weights' follow time; `fall_way_changes`-like scenario (a second push in the air) | Weights held once the time to impact is under 0.15 s |
| The stand gets worse (bodies lie with arms under them) | The stand on three seeds | The arms let go earlier |
