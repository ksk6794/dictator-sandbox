# The natural body: requirements and plan

Proposed on 25 November 2026. **N1 is done** (25 November 2026), and so is the reconnaissance before N8 and N2 of the same day (close-ups, the jumps of C25; `docs/testing.md`, "The natural body, reconnaissance"). **In the night of 25 to 26 November, the owner away, N8 A and B were built; N2, N3, N6, N4 and N5 were begun and each stopped at a limit it did not meet (N2's jumps and weight, N3's caps, N6's reach, N4's matrix and stand); N6's hand target is built and meets its criterion on its geometry; everything behind switches that are off or in assets beside today's, for the owner to confirm** (`docs/testing.md`, the sections of that night). The same text is the tab "Natural body" of the plan in Claude Docs, and the two are kept in step by hand, as `docs/PRD.md` and its document are. Every figure in a criterion is a proposal for the owner to set.

The NPC's body is physically honest and stable, but nothing yet measures whether it moves and reacts like a living person. This plan defines what natural means in figures and lays out nine parts, N1 to N9, that take the body there. It is a proposal: every figure in a criterion is for the owner to set.

## Goal and principles

The goal is a body that behaves and reacts as a real one does: to a touch, a blow, a hold and a fall, and to what it sees coming. "Natural" is defined here by seven rules. Each has measures in "Requirements and criteria" and a part that builds it.

| # | Rule | What it means | Built in |
| --- | --- | --- | --- |
| 1 | Every force comes from inside the body | Muscles are torques between two bodies of the character; the floor and whatever touches the body do the rest. The world-space assist is the one exception today, and this track drives it to zero for an upright body | N2, N7 |
| 2 | Soft by default, stiff on purpose | A relaxed limb gives way to a finger. The body stiffens only when it braces, carries or catches itself | N2, N3 |
| 3 | Mechanics first, then reflex, then intent | For the first tens of milliseconds after a hit it did not see, only passive mechanics act. A reflex follows, a deliberate answer comes last | N4 |
| 4 | The body protects itself | Falling, it reaches for the floor and keeps its head off it. Hit, it guards the place | N6, N5 |
| 5 | The body perceives | It sees what comes at it and where the player is, and it feels a hold. It looks, and it answers before contact when it has the time | N5 |
| 6 | Never frozen, never the same twice | It breathes and shifts in every state, on the floor too. The same push twice gives two answers of the same kind, not one motion played twice | N7, N9 |
| 7 | Nothing jumps | No target, strength or pose changes in one step. This is a rule of the project already; the track adds its measure | N1 |

**What this track does not loosen.** The rules of `CLAUDE.md` hold as they are:

- The body is simulated and can be interacted with at every moment (FR-18). A clip or a pose is a target for the muscles, never a motion of the body.
- The character does not know the pawn. What the body sees reaches it the way a hold does: told by the session or the run, which know both sides.
- Physics timing is fixed and a run repeats. A run with the same seed gives the same hashes, with variation on or off.
- Every number is a tuning value in `Config/Tuning`, with no default in code.
- Every new behaviour sits behind a switch. With the switch off the suite's hashes are those of the baseline.
- Measuring changes nothing: a new measure is shown to leave every hash as it was before it is used.

**Not in this track, though part of the same goal:** soft tissue (FR-16), impact audio (FR-15) and the face (FR-23) stay in M5; walking and sitting stay in M4b; health and speech stay after M5. The track gives them one read-only view to work from ("what the body expresses", in N5).

## Where the body is now

The body stands, steps, falls and gets up under full simulation and passes every gate so far. What it lacks is listed at the end of this section, each gap with its evidence and the part that closes it.

**As built on 25 November 2026:**

| Layer | What exists | Where |
| --- | --- | --- |
| Model | 18 bodies, 17 joints, 80 kg by de Leva's fractions, limits from clinical ranges, left and right mirrored. Head and neck are one body (`neck_01`), each foot is one box, there are no finger bodies | `Specs/physics/npc_body.json` |
| Muscles | One spring-damper per joint through Physics Control, in force mode. Four groups: spine 600 N·m/rad (cap 300 N·m), neck 40 (50), arms 40 (80), legs 800 (250); other values per state | `muscles.*`, `states.*` in `Config/Tuning/body.json` |
| Assist | World-space springs on pelvis, chest, head and feet. Standing: 0.0063 of body weight. Stagger: no horizontal pull, an upright torque on the pelvis of up to 150 N·m. Get-up: 0.27 of body weight (90 N on the pelvis, 30 N a foot) | `assist.*` |
| Balance | Capture point; ankle and hip strategies as turns of the leg targets; recovery steps that are re-aimed and may cross; regrouping steps; arms thrown the way the body tips; a lean into a held push | `USandboxBodyController::UpdateStrategies`, `UpdateStep`, `UpdateRegroup` |
| Reactions | Flinch on the tick after the hit: a pose offset, the struck part gives way, its group is at 0.3 of its strength for 0.35 s. Doubling over from 40 N·s into the belly. Knockdown from 35 N·s on the head. A stream of events. Holds are told to the body and used for nothing | `FeelFrame`, `UpdatePoseTurns`, `TellHolds` |
| Poses | A stance; three fall poses, one chosen as the fall starts; get-up by four recorded clips; breathing and sway from the loop `MM_Idle` | `Config/Tuning/poses.json`, `FSandboxClipPose`, `FSandboxIdleMotion` |
| Content at hand, unused | 29 more ragdoll clips of Epic's sample: reaching in nine directions, three flails, poses on the ground, injured and rolling, get-ups to a crouch, a roll, a walk and a run | `Content/Characters/UEFN_Mannequin/Animations/Ragdoll` |
| Tools | Suite of 100 scenarios, push matrix, knockdowns, the stand of 50 falls on three seeds, grab pulls, sweeps, baselines, films, overlays. Every run records energy, penetration, joint stretch, limit violation, assist share and hashes | `Tools/`, `Scenarios/README.md` |

**What the films show** (the sheets under `Saved\Films`, looked at for this plan):

- `m3_fall_back`: after 200 N·s the body tips with straight legs for a quarter of a second. The arms stay at the chest and do not reach for the floor. From 4.6 s on it lies without a single motion.
- `m3_getup_back`: 5.6 s from the first move to standing, about 2 s of it (7.4 to 9.4 s) in one pose on hands and feet.
- `m3_push80_chest_back`: the trunk leans back as one piece while the body steps; the feet coming together afterwards reads well.
- `strike_punch_belly_steps`: doubled over with the hands at the belly reads well.

**What is missing:**

| Gap | Evidence | Part |
| --- | --- | --- |
| Nothing measured naturalness | Closed by N1: every run records them, and `Tools\natural.ps1` holds them against the criteria | N1, done |
| Stiffness is constant, and high where weight is carried | Legs 800 and spine 600 N·m/rad in every upright state; how soft the body is to a touch was never measured | N2, N3 |
| The assist does what muscles should, in a stagger and a get-up | 150 N·m on the pelvis from the world; 0.27 of body weight while getting up | N2, N7 |
| Muscles are tuned in four groups | The wrist has the shoulder's cap of 80 N·m, the ankle the hip's 250 N·m | N3 |
| The body answers faster than a person can | `balance.decision_s` 0.03, `balance.response_s` 0.05, the flinch 11 ms after the hit | N4 |
| The body does not see, and does not feel a hold | The character does not know the pawn; `TellHolds` changes no force, target or decision | N5 |
| The fall has fixed poses and no aim | Three poses; the film above | N6 |
| On the floor the body is dead, and the get-up waits | 53 of 203 get-up attempts are given up; a clip waits up to 5 s in one pose | N7 |
| The model is coarse where the eye looks | Head and neck as one body, a foot as one box; the fingers are never posed (close-ups, and by the traces 0.0000 degrees against the hand in every run) | N8 |
| The same input gives the same motion, and the idle is a loop | Runs repeat by design; the sway is a clip of fixed length | N9 |

## Requirements and criteria

The track adds nine requirements and 26 criteria. Every limit below is a proposal: the owner sets each figure when its part begins, as for every milestone. "Today" is what the body does on 25 November 2026, as N1's first scorecard measured it (`Saved\Natural\before_track`; `docs/testing.md`, "N1"). No limit has been changed by what was measured.

### Requirements

| ID | Requirement | Priority (proposed) | Part |
| --- | --- | --- | --- |
| FR-24 | The naturalness of the body is measured: every run records how a fall meets the floor, reaction timing, joint effort, stillness, compliance, sway and the likeness of the pose, and a scorecard holds them against the criteria | Must | N1 |
| FR-25 | An upright body is held up by its own joint torques: the world pulls on it with nothing while it stands, flinches or staggers | Must | N2 |
| FR-26 | Each joint has a person's strength, and the body's tone rises when it braces and falls when it relaxes or is dazed | Must | N3 |
| FR-27 | After a hit it did not see, the body moves by mechanics alone, then by reflex, then by intent, at a person's delays | Must | N4 |
| FR-28 | The body sees what moves near it and feels what holds it: it looks, braces before a blow it saw coming, guards, and pulls against a hold | Must | N5 |
| FR-29 | Falling, the body reaches for the floor, brakes with its arms and keeps its head off the floor (this sharpens FR-4) | Must | N6 |
| FR-30 | On the floor the body breathes, settles and stirs, and it gets up without holding a pose (this sharpens FR-5) | Must | N7 |
| FR-31 | The head turns on its neck, and the fingers take a pose that fits what the hand does | Should | N8 |
| FR-32 | Two runs with different seeds differ as two meetings with a person do, and a run repeats from its seed | Should | N9 |

### Criteria

**Forces and stiffness**

| # | Criterion | Measure | Today | People | Proposed limit | Part |
| --- | --- | --- | --- | --- | --- | --- |
| C1 | Nothing pulls on an upright body from the world | The assist's force and torque share from the world while standing, flinching and staggering | 0.0062 of body weight and 3.1 N·m standing; up to 0.18 of body weight in a flinch; no force and up to 328 N·m (pelvis, chest and head added up) in a stagger | None | 0 | N2 |
| C2 | The get-up is the body's own | The assist's mean force share while getting up | 0.27 of body weight | None | 0.10 or less | N7 |
| C3 | The ankles are as stiff as a person's | Ankle stiffness of both legs over m·g·h | 2.5 (800 N·m/rad a leg; m·g·h is 637 N·m: the centre of mass stands 0.81 m over the ankles) | 0.64 to 0.91 | 0.6 to 1.0 | N2 |
| C4 | Relaxed joints are soft | Stiffness of the wrist and the neck in the standing tone | 40 and 40 N·m/rad | 3 to 4 passive, 19 stiffened (wrist); 24 (neck, a model's fit) | Wrist 10 or less; neck 20 to 30 | N3 |
| C5 | Each joint is as strong as a man's | Each joint's torque cap against the published peak of its stronger direction | Ankle 250 against 112; hip 250 against 178; elbow 80 against 51; wrist 80 against 25; knee, shoulder, neck and spine fit | The table of peak torques | Within a fifth, or the reason is written down | N3 |
| C6 | Bracing is felt | Displacement under the same push, relaxed over braced, on the chest and the forearm | No tone. 20 N from the front: the chest gives 1.3 cm, the forearm 8.9 cm | The wrist stiffens 5 to 20 times | 2 or more | N3 |
| C7 | No muscle lives at its cap | Share of upright frames in which a muscle is at its cap, in quiet standing and after a push that is recovered | 0 standing; 1.6% at most | None | 5% or less for any joint | N1, N3 |

**Timing**

| # | Criterion | Measure | Today | People | Proposed limit | Part |
| --- | --- | --- | --- | --- | --- | --- |
| C8 | Mechanics come first | `react_passive_first`: the run with reactions and the run without are the same until the reflex delay has passed | They differ from the frame of the hit | The trunk first moves at 106 ms, the head at 142 ms | Passes | N4 |
| C9 | The reflex comes at a person's delay | `first_target_change_s` after a hit that was not seen | 0.011 s, all of 8 hits | 0.07 to 0.11 s | 0.07 to 0.11 s | N4 |
| C10 | The step comes at a person's delay | `step_lift_s` and `step_land_s` after the hit | 0.033 s and 0.30 s (80 N·s on the chest) | Lift 0.24 to 0.33 s; down by 0.44 to 0.58 s | Lift 0.20 s or later; down by 0.58 s. The owner's decision 3 | N4 |
| C11 | A blow that is seen is met | The struck part's displacement, seen over unseen; how long before the contact the brace began | No difference; no brace before a contact | 0.6 (17 mm against 28 mm); muscles on 65 to 145 ms before | 0.7 or less; 50 ms or more, for a threat in view for 0.35 s | N5 |
| C12 | A gesture is not taken for a blow | `false_alarms` over the owner's recordings of ordinary play | Not applicable | None | 0, or accepted one by one | N5 |
| C13 | The body looks | Mean gaze error on a slowly moving hand; share of quiet time with the gaze on the viewer | No gaze | None | 15 degrees or less; more than half | N5 |

**Falling**

| # | Criterion | Measure | Today | People | Proposed limit | Part |
| --- | --- | --- | --- | --- | --- | --- |
| C14 | The hands meet the floor first | Share of falls forward and sideways, felled by hand, in which a hand or forearm touches before the trunk and ahead of the chest along the fall (the owner's decision 11) | 86% of 7 by hand, 80% of 5 by blows; wherever it lands 100% and 80%. The head strikes the floor all the same (C15, C16) | 98% of young adults' sideways falls | 80% or more | N6 |
| C15 | The head stays off the floor | Share of falls by hand in which the head strikes the floor, coming down at 0.5 m/s or more (the owner's decision 12) | Strikes it at 0.5 m/s or more: 67% of 12 by hand, 50% of 8 by blows; touches it at all: 83% and 75% | 11% of young adults' sideways falls | 15% or less | N6 |
| C16 | A head that does land, lands softly | The head's downward speed at the floor, 90th percentile, falls by hand; the same for blows of 200 N·s, reported | 4.5 m/s by hand, 2.8 m/s by blows | 1.7 m/s in a soft backward fall, 3.2 m/s in a hard one | 2.0 m/s or less by hand; under 3.2 m/s for the blows | N6 |
| C17 | A fall still ends as M3 asked | Rest within 3 s, 40 J or less from the body's own muscles, 2 cm or less into the floor | 1.3 to 2.2 s; 5.6 to 25.5 J; 1.2 cm | None | As they are | N6 |

**On the floor and getting up**

| # | Criterion | Measure | Today | People | Proposed limit | Part |
| --- | --- | --- | --- | --- | --- | --- |
| C18 | A lying body is alive | `stillness.longest_still_s` while lying | 5.0 s at most, 3.0 s in the median of 20 falls (runs of 10 s; a limb settles for a second or two first) | People breathe 12 to 20 times a minute | 1.0 s or less | N7 |
| C19 | A get-up never holds a pose | `target_still_longest_s` while getting up; share of attempts given up | One pose asked for up to 3.6 s, the body still for up to 2.3 s; 24% (48 of 199) | None | 1.0 s or less; 10% or less | N7 |
| C20 | It gets up, mostly at once | Share of 50 falls that end standing; of those, at the first attempt; three seeds | 99%; 43, 45 and 44 of 50 | None | 95% or more (M5's figure); 45 or more | N7 |
| C21 | It takes as long as a person | Median time from the first move to standing, bare floor | 4.7 s in the median of 78 (3.2 to 6.4) | 2.6 to 2.9 s young; 4.2 s median at 20 to 50 years | 2.6 to 4.2 s | N7 |

**Standing, and from one time to the next**

| # | Criterion | Measure | Today | People | Proposed limit | Part |
| --- | --- | --- | --- | --- | --- | --- |
| C22 | It sways as a person does | Speed of the centre of pressure; its side-to-side RMS; `sway.period_s` | 12.9 mm/s; 4.8 mm; a period of 7.6 s (the idle clip) | 9.2 ± 1.6 mm/s; 4.0 ± 1.3 mm | 6 to 12 mm/s; 2 to 6 mm; no period | N9 |
| C23 | No two answers are the same | The pelvis's path after the same 80 N·s push on three seeds; the outcome | Identical | Only a quarter of people even get up the same way | 2 cm or more apart after 2 s; the same outcome | N9 |
| C24 | The outcome tables do not stand on a threshold | The push matrix by the owner's table, the knockdowns and the stand, each on three seeds | Passed without variation; one row is known to sit on a threshold | None | All pass on each seed | N9 |

**Always**

| # | Criterion | Measure | Today | Proposed limit | Part |
| --- | --- | --- | --- | --- | --- |
| C25 | Nothing jumps | `command_jump_max_nm` (a jump told from a fast motion); beside it `target_step_max_deg`, `strength_step_max`, `command_step_max_nm`; in every scenario | A jump of 367 N·m; over 100 N·m in 55 of 86 scenarios; 0.1 standing. Of N1's three cases the 28 degrees of the thigh after a lift and the 399 N·m of an ankle are defects; the 127 degrees as a get-up begins ask 2.7 N·m and are as intended | Set in N2 after the repair (decision 15); until then no larger than the baseline's | N1 on |
| C26 | The body can be interacted with in every new phase (FR-18) | A hit, a push and a hold during the brace, the reach, settling, the roll, the crouch | The get-up has these three scenarios | Each new phase has its three | Every part |

Beside these, each part ends with the owner's look in a session, and the pose's likeness (`pose_prior`) and the time outside a joint's range are reported for every part and must not be worse than before it.

## Human reference figures

These are the published figures the criteria lean on, looked up on 25 November 2026. Most were read in the papers' abstracts, a few in full text; none was measured here. Each is for the people and the conditions named, which are seldom an 80 kg man struck by a hand. They set ranges, not targets to the digit.

### Timing after a disturbance

| What | Figure | Measured on | Source |
| --- | --- | --- | --- |
| Stretch reflex in the calf, short and medium latency | 42 ms and 72 ms | 9 standing adults, the support tilted | [Schieppati and Nardone 1997](https://pubmed.ncbi.nlm.nih.gov/9379421/) |
| Automatic postural response, muscle onset | 73 to 110 ms, ankle first, then thigh, then trunk | Standing adults, the surface moved | [Horak and Nashner 1986](https://pubmed.ncbi.nlm.nih.gov/3734861/); 80 to 120 ms in [Nashner and Cordo 1981](https://pubmed.ncbi.nlm.nih.gov/7262232/) |
| First force against a fall forward | 91 ms; the fall is braked from 184 to 237 ms | 8 adults released from a forward lean | [Do, Breniere and Brenguier 1982](https://pubmed.ncbi.nlm.nih.gov/7166553/) |
| Foot leaves the floor for a recovery step | 236 to 328 ms | The same release | The same |
| Recovery step: swing, landing, length | Swing 167 ms, down 579 ms after the disturbance, 18% of body height | 20 adults, a platform pulse of 0.45 to 0.6 m/s | [Maki and McIlroy 1999](http://www.cs.cmu.edu/~cga/falling/maki.pdf) |
| The body is passive at the start of a fall | The trunk moves at 106 ms, the head at 142 ms; active movement from about 300 ms of a fall of about 700 ms | Young adults in a harness | [Le Goic et al. 2018](https://pmc.ncbi.nlm.nih.gov/articles/PMC6262780/) |
| Arm muscles after a release into a fall | 120 to 126 ms | 14 young adults | [Borrelli, Creath and Rogers 2020](https://pmc.ncbi.nlm.nih.gov/articles/PMC7054927/) |
| Neck muscle after the head drops | 22 to 25 ms | Supine adults, the head released | [Ito et al. 1995](https://pubmed.ncbi.nlm.nih.gov/8788954/) |
| Simple reaction to something seen | 213 to 231 ms; detection alone 131 ms | 1469 adults, a finger press | [Woods et al. 2015](https://pubmed.ncbi.nlm.nih.gov/25859198/) |
| A hand from the thigh to shoulder height | 279 ms, reaction time not counted | 20 young adults, seated | [DeGoede et al. 2001](https://pubmed.ncbi.nlm.nih.gov/11524453/) |

### Expecting a blow

| What | Figure | Measured on | Source |
| --- | --- | --- | --- |
| Muscles switched on before an impact that is seen coming | 65 to 145 ms before it | Young adults, a pendulum against the shoulders | [Kanekar and Aruin 2014](https://pmc.ncbi.nlm.nih.gov/articles/PMC3954907/) |
| What expecting it is worth | The centre of mass goes back 17 mm instead of 28 mm; the centre of pressure 28 mm instead of 60 mm | The same kind of impact, seen or not | [Santos, Kanekar and Aruin 2010](https://pubmed.ncbi.nlm.nih.gov/20156693/) |

### Stiffness and strength

| What | Figure | Measured on | Source |
| --- | --- | --- | --- |
| Ankle stiffness in quiet standing | 64% to 91% of the load stiffness m·g·h; about 290 to 340 N·m/rad a leg | Standing adults, very small disturbances | [Loram and Lakie 2002](https://pubmed.ncbi.nlm.nih.gov/12482906/); [Casadio, Morasso and Sanguineti 2005](https://pubmed.ncbi.nlm.nih.gov/15886131/) |
| Relaxed hip | 75 to 318 N·m/rad | Adults of 21 to 27, upright | [Huang et al. 2020](https://arxiv.org/pdf/1908.07800) |
| Relaxed knee | About 29 to 75 N·m/rad; two sources disagree | 12 adults; quoted from an earlier paper | [Ludvig et al. 2017](https://pmc.ncbi.nlm.nih.gov/articles/PMC6190719/); Huang et al. 2020 |
| Wrist | Passive 3 to 4 N·m/rad; stiffened 19; held by both muscle groups 32; the most 74 | Adults, wrist flexion and extension | [Milner et al. 1995](https://www.hs.mh.tum.de/fileadmin/w00bbr/nd/pdf/MilnerCloutierLegerFranklin_EBR_1995.pdf) |
| Neck | 24 N·m/rad, fitted by a model to videos of falls, not measured on a person | 12 backward falls of older adults who kept the head off the floor | [Kuo et al. 2020](https://pubmed.ncbi.nlm.nih.gov/33019070/) |
| Peak torques, men | Knee extension 247 N·m; hip flexion 156, extension 178; ankle down 112, up 44; shoulder 61 to 83; elbow 47 to 51; wrist flexion 22 to 25, extension 11 | 77 to 91 men of 15 to 83 years, 80 kg, 1.78 m | [Harbo, Brincks and Andersen 2012](https://link.springer.com/doi/10.1007/s00421-011-1975-3) |
| Neck strength, men | Extension 52 N·m, flexion 30, sideways 36, rotation 15 | 11 men, mean age 31 | [Vasavada, Li and Delp 2001](https://pubmed.ncbi.nlm.nih.gov/11568704/) |
| Lower back, extension | 236 to 465 N·m by posture | 56 men | [Graves et al. 1990](https://pubmed.ncbi.nlm.nih.gov/2141187/) |

### Standing still

| What | Figure | Measured on | Source |
| --- | --- | --- | --- |
| Speed of the centre of pressure, eyes open | 9.2 ± 1.6 mm/s; fore and aft 6.4, side to side 5.2 | 100 young adults | [Błaszczyk and Beck 2023](https://pmc.ncbi.nlm.nih.gov/articles/PMC10170532/) |
| Side-to-side sway of the centre of pressure | 4.0 ± 1.3 mm RMS | 67 young adults, eyes closed, feet 5 cm apart | [Reed et al. 2020](https://pmc.ncbi.nlm.nih.gov/articles/PMC7416949/) |
| Where the sway's power lies | Median frequency 0.15 to 0.5 Hz; 98% below 2 Hz | 20 young adults | [Sozzi, Ghai and Schieppati 2022](https://pmc.ncbi.nlm.nih.gov/articles/PMC9339954/) |

### Falling and getting up

| What | Figure | Measured on | Source |
| --- | --- | --- | --- |
| When hands and pelvis reach the floor | Wrist at 680 ± 116 ms, pelvis at 715 ± 160 ms; the hands are in place by 530 ms forward, 658 ms sideways | 30 young women | [Robinovitch et al. 2005](https://pubmed.ncbi.nlm.nih.gov/16424287/) |
| How often a hand lands | In 98% of falls, 50 ms before the pelvis | 44 adults of 19 to 26, falls sideways | [Feldman and Robinovitch 2007](https://pubmed.ncbi.nlm.nih.gov/17395188/) |
| Pelvis speed at the floor | 2.45 to 2.95 m/s | Young adults, falls with a turn | [Robinovitch et al. 2003](https://pubmed.ncbi.nlm.nih.gov/12854837/) |
| Wrist speed at the floor | 1.33 m/s forward; 2.0 to 2.3 m/s backward | Young adults | [Tan et al. 2006](https://pubmed.ncbi.nlm.nih.gov/16011836/) |
| Head speed at the floor, falling backward | 1.73 m/s down in a soft landing, 3.23 m/s in a hard one | 8 young adults | [Choi et al. 2017](https://pubmed.ncbi.nlm.nih.gov/28843128/) |
| How often the head lands | 11.4% of 88 falls | Young adults, falls sideways | [Wood et al. 2019](https://pmc.ncbi.nlm.nih.gov/articles/PMC6501264/) |
| What sitting down is worth | Impact speed 18% lower | 23 young women, falls backward | [Robinovitch, Brumer and Maurer 2004](https://pubmed.ncbi.nlm.nih.gov/15275840/) |
| From lying on the back to standing | 2.6 s and 2.9 s in young adults; a median of 4.2 s (3.1 to 6.0) in adults of 20 to 50 | 22 young adults; 14 adults | [Cattuzzo et al. 2020](https://pmc.ncbi.nlm.nih.gov/articles/PMC7460168/); [Schwickert et al. 2016](https://pmc.ncbi.nlm.nih.gov/articles/PMC5017442/) |
| How people get up | Only a quarter of 32 young adults used the same way; the others each differed | 32 young adults | [VanSant 1988](https://pubmed.ncbi.nlm.nih.gov/3340655/) |

### When a person steps

| What | Figure | Measured on | Source |
| --- | --- | --- | --- |
| A platform pulse of 0.45 to 0.6 m/s | A step in 98% of trials | Young and older adults | [McIlroy and Maki 1996](https://pubmed.ncbi.nlm.nih.gov/8914501/) |
| Largest forward lean caught with one step | 31 to 33 degrees | Young adults, released from a tether | [Thelen et al. 1997](https://pubmed.ncbi.nlm.nih.gov/9008663/); [Wojcik et al. 1999](https://pubmed.ncbi.nlm.nih.gov/10026662/) |
| First and second step | A first step from a belt speed of about 0.65 m/s, a second from about 2.3 m/s (worked out here for 80 kg and 1.78 m from the paper's torques; indicative) | Adults under 45, a treadmill pulse | [Crenshaw and Kaufman 2014](https://pmc.ncbi.nlm.nih.gov/articles/PMC3910088/) |

### What these say about the body as it is

- **The owner's push table agrees with people.** 30 N·s gives the body 0.38 m/s, 80 N·s gives 1.0 m/s, 200 N·s gives 2.5 m/s. People step from about 0.45 to 0.65 m/s and need a second step from about 2.3 m/s.
- **The body answers about ten times too soon.** 11 ms to the flinch against 70 to 110 ms; a foot lifting within tens of milliseconds against 240 to 330 ms.
- **Its ankles are about three times as stiff as a person's.** 800 N·m/rad a leg, where m·g·h for both legs together is 637 (measured in N1; 785 was assumed when this was written).
- **Its wrist and neck are stiffer than relaxed ones** (40 N·m/rad against 3 to 4 passive at the wrist and 24 at the neck), **and its wrist, elbow and ankle are stronger than a man's** (caps of 80, 80 and 250 N·m against 25, 51 and 112).
- **Its get-up is slower than a young adult's** (4.1 to 5.7 s against 2.6 to 2.9 s) and inside the range of adults up to 50.

### Not found

Fore-and-aft sway amplitude of the centre of pressure; relaxed against braced stiffness of the elbow, the shoulder and the trunk; a directly measured neck stiffness; isometric torques for hip extension, ankle plantarflexion and several others (the table has isokinetic ones); peak joint power; how often the head lands in falls backward of young adults; a head speed that counts as an injury; the latency of a blink or a drawn-back head to something that looms; the largest push held without a step. Where a criterion needs one of these, it is set from the body's own measurement and marked so.

## Order of work and the owner's decisions

The parts are numbered as they were first discussed, by what they give. They are built in another order, because a change to the physics asset moves every baseline and has to come before the muscles are tuned again.

**The order, decided by the owner on 25 November 2026 (decision 1):** the owner's session in the headset (done), then, by the owner's decision of the same day, the get-up inside M4 (tried, put off: `docs/testing.md`), then the rest of M4 (the reconnaissance before N8 and N2 runs beside it), then N8 A and B, N2, N3, N6, N4, N5, N8 D, then M4b together with N7, then N9 and N8 C, then M5.

| Order | Part | Why here | Against the milestones |
| --- | --- | --- | --- |
| - | N1, measures | Changes no behaviour; everything after it is tuned on its figures | Done, beside M4 |
| - | The reconnaissance: close-ups (N8's step 1), the jumps of C25 | Changes no behaviour; says what N8 and N2 begin with | Done on 25 November 2026, beside M4 |
| - | The owner's session in the headset, then the rest of M4 | M4 part 1, step 4 is next | M4 |
| 1 | N8 A, the head as a body; N8 B, fingers | The asset changes before the muscles are tuned on it; the fingers are cheap and show at once | After M4's gate |
| 2 | N2, weight carried by the joints | The base of a soft body; N3 builds on it | Before M4b |
| 3 | N3, tone | Needs N2's weight torque for relaxed arms | Before M4b |
| 4 | N6, the fall with an aim | Gives the hand target that N4, N5 and M4b use | Before M4b |
| 5 | N4, the order and timing of reactions | Needs the tone; its last step needs the hand target | Before M4b |
| 6 | N5, perception | Needs tone, timing and the hand target; ends in the headset | Before M4b, with a headset session |
| 7 | N8 D, toes | An experiment that serves walking, kneeling and sitting. Brought forward by the owner on 25 November 2026 for the get-up and tried inside M4 as a second asset; **put off again the same day by the limit set before the run (1.06 s against 0.7): see `docs/testing.md`**. The get-up itself waits too: three attempts, the cause is in `docs/testing.md` (25 November) | Just before M4b |
| 8 | N7, life on the floor and the get-up | Shares clips and the hand target with M4b's actions | With M4b |
| 9 | N9, variation; N8 C, the skin | Spreads what the other parts built; the skin is for M5's sign-off | After M4b, before M5 |

Why before M4b: walking, sitting and righting a chair will be tuned on the muscles. If N2 and N3 change them afterwards, M4b is tuned twice.

**Decisions for the owner.** Each is put one at a time when its part begins; the recommended answer is first.

| # | Decision | Options | Needed by |
| --- | --- | --- | --- |
| 1 | Where the track sits | **Decided on 25 November 2026: (a), as in the table above.** Not chosen: (b) all of it after M4b, at the price of tuning M4b's actions twice; (c) all of it before M4 ends, at the price of a later headset review | Decided |
| 2 | The figures of the criteria | Proposals in "Requirements and criteria"; the owner sets each when its part begins, as for every milestone | Each part |
| 3 | Human timing against the push matrix's table | (a) Delays from the low end of the human range, the table as it is. (b) Mid-range delays and a new table with more steps and falls. (c) No delay for the step, only for the reflex | N4 |
| 4 | A dazed body may take two steps where the table allows one | (a) Yes, for hits on the head from 15 N·s. (b) No daze below the knockdown | N3 |
| 5 | The head as a body, and the toes | (a) The head now, the toes as an experiment before M4b. (b) The head only. (c) Neither | N8 |
| 6 | Eyes and blinking brought forward from M5 to N5 | (a) Yes: the eyes follow the gaze and shut before a blow, with their cost in the frame measured first. (b) The head alone until M5 | N5 |
| 7 | The 40 J a falling body's own muscles may add | (a) Kept, and the reach tuned inside it. (b) Raised to what the reach measures | N6 |
| 8 | More of the sample's clips copied for the pose database (walking, turning, sitting) | **Decided on 25 November 2026: a chosen part of the sample** (159 clips, 240 MB, `Specs/pose_prior.json`; with the ragdoll and unarmed mannequin clips 224). It has people who are shoved, tackled and taken down, which this plan did not know | N1 |
| 9 | A video file of a motion for the owner's review | `ffmpeg` was on the machine already: `Tools\film.ps1 -Video` | N1 |
| 10 | Clips the sample does not have (ways of lying, a crawl) | (a) Written as poses first. (b) A clip pack, brought to the owner with its price and licence | N7 |
| 11 | C14 was met by hands that land beside the hips (found in N1) | **Decided on 25 November 2026: a hand counts only if it lands ahead of the chest along the fall** (`hands_ahead_first`). Measured by that rule today's body still meets it, 86% against 80%, with hands 10 to 20 cm ahead that do not brake: for N6 it is C15 and C16 that show the difference | N6 |
| 12 | C15 said "touches", and a head that is laid down after the fall touches too | **Decided on 25 November 2026: "strikes"**, 0.5 m/s or more downward (`head_struck`). The share that touches at all is printed beside it | N6 |
| 13 | The jumps at a step's lift (found in the reconnaissance, cases a and b: the leg's frame, the foot's level, the ankle's limit) | **Decided on 25 November 2026: (a), repaired together as the first thing of N2, before its step 2, the reconnaissance's other causes one by one in the same place.** M4 stays on today's baseline. Not chosen: (b) now, beside M4, at the price of a new baseline in the middle of it; (c) the frame alone, the foot with N8 D | Decided |
| 14 | The clip's pose is taken whole as a get-up begins (case c) | **Decided on 25 November 2026: (a), left as it is, and C25 counts it by its torque.** Blended in, the stand does not gain. The arm's whip (13 rad/s in the first moments) is for N3, whose get-up begins from a resting tone, and for N7's gathering. Not chosen: (b) blended in now; (c) a limit on how fast a target may turn while its strength comes up | Decided |
| 15 | C25's limit | **Decided on 25 November 2026: the figure is set in N2, from what is measured after the repair (decision 13), with what jumps by design listed apart.** Until then no part may raise `command_jump_max_nm` above its baseline (367 N·m today); 60 N·m is the aim. Not chosen: 60 at once (no trial comes under it: 38 scenarios over 100 N·m with all the corrections); today's largest, which is a defect | Decided; the figure in N2 |

## N1. Measures of naturalness

N1 adds the measures the criteria are written in and one tool that prints them as a scorecard. It changes no behaviour: every scenario keeps its hashes.

**Done on 25 November 2026.** The measures are `FNaturalMeasures` in `SandboxScenario` (on `FSandboxMotionStats` and `FSandboxPosePrior` in `SandboxBody`); the fields are described in `Scenarios/README.md`, "How natural the motion is"; what each was checked against, the first scorecard and what it cannot be relied on for are in `docs/testing.md`, "N1". What was built otherwise than the text below has it:

- **The fall's check drops the body head first** (`measure_drop_head_first`), since a body dropped flat lands on its trunk and its head is no longer in free fall.
- **The expectations are one**, `"measures"`: bounds on any figure of `metrics.json` by its path, in place of `max_head_impact_speed_mps`, `head_touched` and `hands_first`. Lists by joint are objects by joint for the same reason.
- **The centre of pressure is read from the solver's contact points after every substep.** The contact events, which N1 was to add up and N2's design takes the floor's push from, have the nearest point of a contact with the impulse of all of it: 8.6 cm off.
- **Stillness is judged by how far** (no part of the body further than 0.5 cm in a second) **and not by speed**: what breathes is slow.
- **The period of the sway is measured on a stand of its own** (`measure_stand`, 32 s; the scorecard's 62 s). `stand_npc` is 10 s and holds one loop.
- **Beyond the plan:** `command_step_max_nm` (what a jump asks of a muscle, since a target that jumps at no strength asks nothing), `assist_by_state`, `against_cm` of a push, `Tools\knockdowns.ps1 -ByHand` brought forward from N6 because C14 to C16 are written for falls by hand, and a known-answer check of the pose database at its build.

**Why first.** Every later part is tuned against these figures. Without them "more natural" is a look at a sheet of pictures, and the suite stays green while the body moves like a statue, as it did before the audit of November 2026.

### What it adds

| Measure | Fields in `metrics.json` | How it is taken | Shown to work by |
| --- | --- | --- | --- |
| How a fall meets the floor | `control.fall.touches[]` (bone, time, speed and downward speed of that body at the start of the step it touched in); `first_touch_bone`, `head_touched`, `head_impact_speed_mps`, `pelvis_impact_speed_mps`, `hands_first`, `time_to_first_touch_s`, `time_to_trunk_s` | The controller already tells contacts with the static world apart (`OnBodyHit`); it keeps, per body, when it first touched the world in this fall. The runner pairs that with the body's velocity of the frame before. The feet are left out | `measure_drop_flat`: a body with its muscles at 0 dropped flat from a known height reads sqrt(2 g h) within 5% for the head |
| Reaction timing | `control.reactions[]`, one entry a hit: `hit_t`, `bone`, `impulse_ns`, `first_target_change_s`, `first_strength_change_s`, `step_lift_s`, `step_land_s`, `fall_start_s` | Target change: the first frame in which some muscle's target is more than 1 degree from what it was at the hit. The idle turns a target that far in about 0.3 s, so the measure reads latencies below that. Steps and states come from the logs that exist | A hit on a body with `reactions.*` at today's values reads one frame; N4's delay then reads as set |
| Joint effort | `control.effort.joints[]` (`torque_max_nm`, `torque_p95_nm`, `cap_nm`, `at_cap_frames`, `power_max_w`, `work_j`); `at_cap_share`, `positive_work_j`, `power_peak_w` | From the spring law at the start of every step, as the assist's load is: stiffness times error less damping times relative turning speed, capped. An estimate, since the solver reports no drive torque | `measure_arm_held_out`: on a welded pelvis an arm asked to the horizontal reads its own weight moment at the shoulder within 10% |
| Time beyond a joint's range | `metrics.limit_excess_share` (joint-frames more than 0.5 degrees outside the range, of all joint-frames while not Lying), worst joint and its share; per joint in `body.joints[]` | `FSandboxPoseRig::LimitExcessDeg`, which measures as the solver limits | The knockdown scenarios read above 0, standing reads 0 |
| Jumps | `control.pose.target_step_max_deg` (largest turn of a muscle's target between two frames, with joint and time), `control.pose.strength_step_max` (largest relative change of a muscle's stiffness in a frame), `metrics.joint_speed_step_max_rads` | From `GetControlView` and the bodies, frame against frame | A scenario that sets `poses.blend_s` to 0.01 and changes the pose reads the pose's own angle |
| Smoothness of the head | `metrics.head_accel_max_mps2`, `metrics.head_jerk_rms_mps3` | Finite differences of the head's centre of mass at 90 Hz after a three-frame mean | A passive drop reads the impact; standing reads near 0 |
| Stillness | `metrics.stillness.longest_still_s` with its state and start; `still_share_by_state`; `target_still_longest_s` (targets unchanged while neither Standing nor Lying) | Still: the mass-weighted speeds below thresholds of the runner's own. They are set between a body that only breathes and one that does nothing, from `stand_npc` with `poses.idle.weight` 1 and 0 | A lying body reads its whole lying time today |
| Compliance | `control.pushes[]`, one entry for each `impulse` with a `duration_s`: `bone`, `force_n`, `displacement_cm`, `settled_displacement_cm`, `whole_body_cm`, `return_s`, `stiffness_n_per_cm` | The pushed body's centre of mass against where it was when the push began | A force on a welded, passive pendulum of known stiffness |
| Sway | `metrics.sway`: `com_rms_ap_cm`, `com_rms_ml_cm`, `com_speed_mean_cmps`, `cop_rms_ap_cm`, `cop_rms_ml_cm`, `cop_speed_mean_cmps`, `period_s` | Standing frames after the settle time. The centre of pressure needs where the floor pushes, not only how hard: the controller adds an impulse-weighted contact point per foot to what it already collects. `period_s` is the lag of the highest autocorrelation peak above 0.5 | `stand_npc` reads the idle clip's length as `period_s` |
| Likeness of the pose | `metrics.pose_prior`: `mean_deg`, `p95_deg`, `worst_deg`, `worst_t`, by state | The mean angle, joint by joint, to the nearest pose of a database of recorded human poses (below), heading removed. Sampled every third frame | A clip of the database replayed on a welded body reads its own tracking error |

### Tools

- **`Tools\natural.ps1`, the scorecard.** Runs a fixed set (a stand of 60 s, pushes of 30 and 80 N·s on the chest from four sides, the eight knockdowns, the get-ups from four sides, the strikes, the compliance set) and prints one row for each criterion of this plan: value, limit, verdict. `Saved\Natural\<label>\report.json`; `-Baseline <label>` holds two reports against each other; `-Tune` as everywhere. It reports and fails nothing until the owner has set the limits.
- **`Tools\compliance.ps1`.** 20 N and 60 N for 1.5 s on head, chest, pelvis, upper arm, forearm, hand, thigh and calf, from the front, the back and a side; one row a push with the stiffness in N/cm. `Saved\Matrix\compliance`.
- **`Tools\pose-prior.ps1 -Build`.** Makes the pose database from the clips that are present: the 33 ragdoll clips of Epic's sample and the stock mannequin's, carried to the body's joints by `FSandboxClipPose` as the get-ups are, one pose every 0.1 s, each also mirrored. The database is Epic's content in another shape: it is written under `Saved\PosePrior` and never committed. Whether the fetch may also copy the sample's walking, turning and sitting clips for it is a decision for the owner.
- **`Tools\film.ps1 -Around 3.0 -Span 0.6 -Every 0.033`.** A dense strip for questions of timing, which a sheet of twelve pictures over six seconds cannot answer.

### Limits of these measures

- The pose database holds no pose of a person being hit. A large distance means "unlike any recorded pose": it finds the frames to film and it catches a regression. Criteria on it compare with the baseline, except for standing.
- Effort is an estimate from the spring law, as the assist's load is. An estimate that is capped reads its cap: time at the cap is the figure to read, not the mean.
- Timing by target change sees only what changes a target. A response that is only a change of strength shows in `first_strength_change_s`.

### Must not change

Every scenario of the suite has the baseline's two hashes (`suite.ps1 -SaveBaseline pre_n1` before, `-Baseline pre_n1` after: no scenario listed). The suite's time stays within 15% of today's 39 s; the pose database is asked for only with `-PosePrior` or by the scorecard.

### Steps

1. Baseline of the suite, the push matrix and the stand on three seeds.
2. The fall's touches, `measure_drop_flat`, the expectations `max_head_impact_speed_mps`, `head_touched` and `hands_first`.
3. Reaction timing.
4. Joint effort, `measure_arm_held_out`.
5. Time beyond the range, jumps, smoothness, stillness.
6. Pushes and `Tools\compliance.ps1`.
7. Sway with the centre of pressure.
8. The pose database and its measure.
9. The scorecard; its first report, "the body before the track", goes into `docs/testing.md`.
10. The baseline comparison, `Scenarios/README.md`, the command table of `CLAUDE.md`.

### Done when

- [x] The suite's 101 scenarios have the baseline's hashes; unit tests and the bootstrap pass
- [x] Every measure has a scenario or a unit test with a known answer that fails when the measure is broken on purpose (the pose database: the checks of its build)
- [x] The first scorecard is recorded, with the human figure beside each row where one exists
- [x] The suite takes no more than 45 s (43 s, with ten scenarios more)

**Suggested model:** Opus 5.5, high. The work is well bounded and every step is checked by a run.

## N2. Weight carried by the joints

N2 has the muscles carry the body's weight by torques worked out from the pose, so that the springs can be several times softer, and delivers the assist's forces on an upright body as torques in the legs it stands on. The target is an upright body on which the world pulls with nothing.

**Why.** Today a joint holds weight only through its spring's error, so stiffness is what keeps the body up and cannot be low. A person stands the other way round: the muscles carry the weight with a steady torque and the joint stays soft around it. The second reason is the puppet look: in a stagger the world turns the pelvis upright with up to 150 N·m, and those are the moments the player is looking.

**Scope is decided on N1's figures.** Whether the standing body is in fact stiffer to a touch than a person, and where, is what N1's compliance table says. Step 0 reads it. If the legs are already within a person's range, steps 3 and 4 shrink to the joints the table names. Step 5, the assist as joint torques, stands either way.

### Design

**1. The weight torque.** A new plain class, `FSandboxWeightTorques` in `SandboxBody` (arithmetic on samples, like `FSandboxBodyProbe`, with no world needed to test it). The pelvis is the root. For every joint, with the bodies on its far side from the pelvis as its subtree S, the muscle has to cancel the moment about the joint of every outside force on S: the weights, and what the world pushes those bodies with.

```latex
\tau_j = -\sum_{i \in S_j} \Big[ (c_i - p_j) \times m_i\, g_{\mathrm{eff}} + (q_i - p_j) \times F_i^{\mathrm{world}} \Big]
```

Here p is the joint's place, c a body's centre of mass, F the world's push on that body in the last step and q where it acts. The controller already collects F per body (`SupportImpulseNs`). The point has to come from the solver's own contact points after each substep, as N1's centre of pressure does (`FNaturalMeasures::AddPressure`): a contact event has the nearest point of the contact, 8.6 cm from where its force goes through on a standing body.

- An arm, the neck or the trunk above the pelvis touches nothing: the torque is the weight moment of what hangs beyond the joint.
- A leg that stands: the floor's push on the foot enters with the leg's own weights, which is exactly what the leg transmits. Nothing has to be told which leg carries what share.
- The same rule covers a hand on the floor in a get-up, a body that lies, and a seat (M4b).
- **A body in free fall carries no weight.** g\_eff is gravity times the share of the body's weight that the world holds up (the sum of F over body weight, between 0 and 1, smoothed over `muscles.weight_follow_s`). Without it a falling body's limbs would be lifted by torques that have nothing to cancel.

**2. Applying it.** For each joint, `FBodyInstance::AddTorqueInRadians` with the torque on the child body and its opposite on the parent, in world space, in `TickComponent` before `Control->UpdateControls`. Physics Control has no such term (its only gravity setting scales gravity itself, which is a force from outside). The pair cancels: nothing is added to the body as a whole. Torque in the engine's units is N·m times 10 000.

**3. Tuning.**

| Name | Meaning | Start |
| --- | --- | --- |
| `muscles.<group>.weight_share` | How much of the worked-out torque the group's joints apply, 0 to 1. At 0 nothing is applied and the run is today's bit for bit | 0, then 1 |
| `muscles.weight_follow_s` | Smoothing of the world's pushes and of g\_eff | 0.05 |
| `muscles.weight_legs_by` | 0: the floor's measured push; 1: a share by where the centre of mass lies between the feet (the fallback if the measured push rocks the body) | 0 |
| `balance.ankle_as_torque` | The ankle strategy as a torque instead of a turn of the target | 0, then 1 |
| `assist.internal_share` | The part of the assist delivered as joint torques, 0 to 1 | 0, then 1 |

A muscle's spring keeps its cap less the size of the weight torque (never under a tenth of the cap), so that the joint's total stays within the strength of a person. The weight torque itself is cut to the joint's cap: the shock of a landing is not answered with more than the joint has.

**4. Softer springs.** With the weight carried, the legs' and the spine's stiffness is swept down (800 to 400, 200, 100; 600 to 300, 150) with the damping, on three seeds, against everything in "Must hold" below. Three places in the controller assume stiff legs and move with it:

- `UpdateStrategies` turns a wanted shift of the centre of pressure into a turn of the ankle's target by dividing by the legs' stiffness. With soft legs that turn passes `balance.ankle_max_deg`. The ankle strategy becomes a torque: the shift times the load on the foot, added at the ankle, bounded so that the centre of pressure stays inside the sole. The lean into a held push stays a turn of the target.
- The swinging leg (`balance.swing_stiffness_scale`, `swing_damping_scale`): a leg in the air now has its weight carried and lags its point less; the scales are set again.
- The get-up tracks its clip with the muscles of its state; its figures are measured again on the stand.

**5. The assist as joint torques, for an upright body.** The assist's wishes are worked out as now, from its anchors. They are delivered differently:

- A horizontal force wanted on the pelvis becomes the torques (p\_pelvis - p\_j) × F at the ankle, knee and hip of each leg that stands, by its share of the load. The floor returns the force through friction and a shift of the centre of pressure.
- An upright turn wanted on the pelvis, the chest or the head becomes a torque at the joints between that body and its support: the hips, the spine, the neck.
- Each is cut to what feet can give: the centre of pressure inside the sole, friction within `surface.feet_friction` times the load. What is cut off is not handed back to the world. A person could not have done it either, and the body steps.
- `UpdateAssistLoad` counts only what still comes from the world. That figure is the criterion.
- The feet's anchors (`assist.feet.*`) are swept to 0: friction holds a foot.

The get-up's assist is N7's.

### Scenarios

| Scenario | Shows |
| --- | --- |
| `weight_arm_held_out` | On a welded pelvis an arm asked to the horizontal at 10 N·m/rad is within 3 degrees with the share at 1 and hangs more than 20 degrees low with it at 0 |
| `weight_stand_soft` | 60 s of standing on the chosen soft legs: drift, assist and sway within their criteria |
| `weight_crouch_hold` | A pose with the knees at 60 degrees held for 10 s: the pelvis within 2 cm of the pose's height |
| `weight_free_fall` | A body dropped from 1 m: no joint turns more than 5 degrees before it touches down, energy gain under 1 J |
| `tune_live_weight_share` | The share changed in the middle of a run: the hashes differ from the unchanged run |
| `stand_npc_no_assist` | Standing 60 s and answering 30 N·s with every `assist.*` value of the world at 0 |

A unit test holds the arithmetic: a two-link arm against its analytic torques, a leg under a known load, and that the torques applied sum to zero.

### Must hold

- With every new value at its start, the suite has the baseline's hashes.
- With the chosen values: the push matrix by the owner's table (160 of 160), the eight knockdowns, the stand at 90% or more on three seeds with the first-attempt count within 3 of today's, the grab's 93 pulls, the strikes.
- `effector_hold_chest` does not rock from foot to foot: the swing of the two feet's loads against each other is no larger than in the baseline (a new figure, `control.balance.foot_load_swing_n`).
- The physics step is within 0.05 ms of today's 0.58 ms.
- A scenario whose hashes move is named in the part's report with what moved in it.

### Risks

| Risk | Guard | Fallback |
| --- | --- | --- |
| Torques from the floor's measured push close a loop. Leg turns weighted by each leg's load once rocked the body from foot to foot | Smoothing; `foot_load_swing_n`; three seeds | `muscles.weight_legs_by` 1 |
| Explicit torques on a heavy body on light feet make the feet rock, as 8 solver iterations did | The feet's turning speed in `stand_npc` against the baseline's 0.15 rad/s | Weight torque on trunk, neck and arms only, the legs as today |
| The floor's push is one frame old | The smoothing; `weight_crouch_hold` | A share by geometry |
| Soft legs are too slow for a recovery step | Landing error of the steps (`max_landing_error_cm`) | A stiffer swing leg; legs less soft |
| Every figure tuned so far moves | The whole set of "Must hold" is run again at the end, not only the suite | Stop at the last step that held |

### Steps

**First, before step 2: the jumps of a step (decision 13).** The leg's frame, the foot's level and the ankle's limit at a lift, together; then, one by one, the swing strength dropped at a landing, the first placement's reach, the settling's height and the strengths and legs let go as a fall begins. Its own baseline before and after; each correction alone on the suite, the push matrix and the stand on three seeds, with films; a scenario that then fails (`strike_kick_thigh` did in the trial) is read, not loosened. What the trials showed is in `docs/testing.md`, "The natural body, reconnaissance".

**The night of 25 to 26 November (for the owner to confirm):** the jumps' corrections are built behind eight values at 0; no set meets every limit; "all but reach" takes the largest jump to 352 N*m and the count over 200 N*m from 39 to 15, keeps the matrix and the stand, and changes two scenarios, to be looked at on films (`docs/testing.md`, "N2 first"). Steps 1 and 2 are built behind shares at 0 and are not usable: the torque given from outside arrives 1.6 to 1.7 times over (the solver's inertia conditioning), given through the drive it arrives whole (the "65 to 100%" first written was the spring's figure, not the arm: the hand ends where a stiff arm holds it), and with every share at 1 the suite fails 14 and the matrix fails ("N2 step 1 and 2"). The next step is what fails with the shares on.

0. Read N1's compliance and effort tables against the human figures; write down which joints are stiffer than a person's and by how much; agree the scope with the owner.
1. `FSandboxWeightTorques` with its unit tests.
2. Trunk, neck and arms. With the shares at 0 every hash is the baseline's.
3. Legs: `weight_stand_soft`, `weight_crouch_hold`, `weight_free_fall`.
4. The ankle strategy as a torque; the stiffness swept down on three seeds; values chosen.
5. The assist as joint torques; the feet's anchors to 0; `stand_npc_no_assist`.
6. Everything of "Must hold" run again; the scorecard; films of standing, a push, a fall and a get-up; the report.

### Done when

- [ ] The assist's force from the world is 0 for a body that stands, flinches or staggers
- [ ] The compliance table is within the range agreed in step 0
- [ ] Everything of "Must hold" holds
- [ ] The owner has seen the films and a session

**Suggested model:** Fable for step 0, the review of step 1's arithmetic and any figure that does not move with its setting; Opus 5.5 at xhigh for the rest.

## N3. Muscle tone

N3 gives every joint its own strength and gives the body a tone: how tense it is at this moment, as a value that rises when it braces and falls when it relaxes or is dazed. The same blow then lands differently on a body that expects it, one that does not and one that was just hit on the head.

**Why.** A person's stiffness is not a constant. A relaxed wrist is softer than a braced one by an order of magnitude (figures in "Human reference figures"). Today the body has four numbers for stiffness, changed only by its state, and one torque cap for the shoulder and the wrist alike.

### Design

**1. Muscles joint by joint.** Three values for each kind of joint, left and right alike: `muscles.joints.<joint>.stiffness_scale`, `damping_scale` (factors on the group's values) and `max_torque_nm`. The joints: `spine_02`, `spine_04`, `neck_01`, `clavicle`, `upperarm`, `lowerarm`, `hand`, `thigh`, `calf`, `foot` (and `head`, `ball` once N8 adds them). They start at 1, 1 and the group's cap, which is today's body bit for bit. Then the caps are set from published peak torques. One cap serves both directions of a joint, since Physics Control has one `MaxTorque`: the larger of the two is taken, and the list says so.

**2. Tone.** The controller keeps a tone per muscle group. Each follows its goal with `tone.rise_s` on the way up (bracing is fast) and `tone.fall_s` on the way down (letting go is slow), so no strength jumps. A tone is one of four named levels, each a pair of factors per group on stiffness and damping: `tone.limp`, `tone.relaxed`, `tone.ready`, `tone.braced`. `ready` is 1 everywhere: today's values. A muscle's strength is then, in this order: the state's value, times the joint's scale, times the tone, times the flinch's weakening, times the swing leg's scale, times the get-up's ease.

**3. What sets the tone.** Each rule has its own tuning and its own clock:

| Cause | Tone | Lasts | Tuning |
| --- | --- | --- | --- |
| Standing with nothing happening | Legs and spine `ready`; arms and neck `relaxed`, their pose held by N2's weight torque | Until something happens | `tone.standing.<group>` |
| A hit is felt | `braced`, the whole body, once N4's reflex delay has passed | `tone.brace_hold_s`, then down | `tone.brace_from_ns`, `tone.brace_hold_s` |
| A threat is seen (N5) | `braced` before the contact | While it is seen, plus the hold | N5 |
| A hit on the head below the knockdown's 35 N·s | Dazed: legs and spine at `tone.daze_scale`, N4's delays longer by `tone.daze_delay_scale` | `tone.daze_s`, graded from `tone.daze_from_ns` to the knockdown | `tone.daze_*` |
| A part is held (N5) | `braced` in the chain of the held part | While held | N5 |
| Falling | Arms and neck `braced` until the trunk meets the floor (N6), then let go | N6 | N6 |
| Lying | `limp` at the impact, then the resting tone of N7 | N7 | N7 |

**4. For whoever asks.** `USandboxBodyController::RequestTone(Level, Seconds)` and a scenario event `tone`, for tests and for M4b's actions. `Tools\compliance.ps1 -Tone relaxed|braced` measures the body at a tone.

### Scenarios

| Scenario | Shows |
| --- | --- |
| `tone_braced_takes_punch`, `tone_relaxed_takes_punch` | The same punch on the chest: a braced body's struck part moves less than half as far as a relaxed one's |
| `tone_dazed_steps_more` | 25 N·s on the head, then 30 N·s on the chest two seconds later: more steps than without the first hit |
| `tone_no_jump` | Through a brace and its release `control.pose.strength_step_max` stays under its criterion |
| `tone_live` | A level's value changed in the middle of a run: the hashes differ from the unchanged run |
| `tone_arms_swing` | 30 N·s on the chest of a body with relaxed arms: the hands travel, and come to rest again within 2 s |

### Must hold

- With every scale at 1, every cap at its group's and every tone at `ready`, the suite has the baseline's hashes.
- With the chosen values: the push matrix by the owner's table, the knockdowns, the stand on three seeds, the grab's pulls.
- A dazed body can take two steps where the owner's table allows one (a hit of 30 N·s on the head). That is a change of the table and goes to the owner with the figures; it is not decided here.

### Risks

| Risk | Guard | Fallback |
| --- | --- | --- |
| The number of values grows by about sixty | Scales start at 1; only values that a measure asks for are changed; sweeps one at a time on three seeds | Tone per body, not per group |
| A person's wrist is weaker than the get-up clips need | `control.effort` names the joints at their cap during a get-up | The wrist keeps a higher cap until M4's wrist model is built, and the list says so |
| Relaxed arms without the weight torque sag at the collarbone | N3's step 4 comes after N2's step 2 | Arms stay `ready` |

### Steps

**The night of 25 to 26 November (for the owner to confirm; `docs/testing.md`, "N3"):** steps 1, 3 and 5 are built and neutral (the hashes of `hold`). Step 2 does not meet its limits and nothing is changed: a man's ankle lets a forward knockdown rest after 3 s, and weaker arms cost the stand 3 to 11 first attempts. With the levels proposed that night, C6 is met for the forearm (3.4 times) and not for the chest (1.3); step 4's relaxed arms swing less, not more, with their damping at 0.7 (it wants to be near the stiffness's share); the daze changes nothing in the steps after a second hit.

1. Values joint by joint, all neutral: the baseline's hashes.
2. Caps from the published torques; "Must hold"; the joints at their cap in a get-up listed.
3. The tone, its four levels, all at 1; `RequestTone`, the event `tone`.
4. Standing tone: arms and neck `relaxed`. Compliance at both tones, a film of a push for the arms' swing.
5. The brace after a hit, and the daze. The push matrix with both, laid beside the owner's table.
6. The scorecard and the report.

### Done when

- [ ] Every joint's cap is within a fifth of the published figure for its stronger direction, or the list says why not
- [ ] A braced body is at least twice as stiff to a push on the chest and on the forearm as a relaxed one
- [ ] The relaxed wrist and neck are within the human range of "Human reference figures"
- [ ] Everything of "Must hold" holds, with the owner's word on the daze

**Suggested model:** Opus 5.5, high.

## N4. The order and timing of reactions

N4 puts a person's order into what the body does after a hit it did not see: first only mechanics, then a reflex, then a deliberate answer, each after a delay that is a tuning value. Today the flinch begins 11 ms after the hit and a step is decided in 30 ms, which no person can do.

**Why.** The first tenth of a second is what makes a hit read as physical: the head goes back on the neck, the trunk gives, and only then does the body catch itself. A body that answers in the same frame reads as a machine, however good the answer is.

### Design

**1. Three clocks, started when a hit is felt** (the hit `FeelFrame` tells of; a second hit during a delay does not start the clocks again):

| Phase | From | What the controller may do because of the hit |
| --- | --- | --- |
| Mechanics | 0 to `reactions.reflex_delay_s` | Nothing. The goals of the balance strategies and of the arms stay what they were at the hit; the flinch's pose and the brace have not begun; no step is decided. The body moves by its momentum and the muscles it had |
| Reflex | From `reactions.reflex_delay_s` | The flinch's pose (shoulders up, head in) and the brace of N3; the ankle and hip strategies work again on the capture point as it now is |
| Step | From `balance.step_delay_s` | The earliest moment a foot lifts. The step is aimed at where the capture point will be when the foot lands, as today, with the delay counted in |
| Intent | From `reactions.intent_delay_s` | A guard, a hand to the struck place, a look at who struck (N5), a turn toward them |

**2. What does not wait.** A fall decided by the posture (`thresholds.fall_*`) is mechanics and stays immediate. Quiet standing has no hit and no delay: a delay inside the standing loop itself risks an unstable body and is not built; a person's sway comes from N9's noise instead.

**3. A hit that was seen** (N5) has its clocks started when the threat was seen, so the reflex and the brace can come before the contact.

**4. A dazed body** (N3) has longer delays, by `tone.daze_delay_scale`.

**5. The flinch's weakening is put to the test.** Today the struck group drops to 0.3 of its strength at the hit. With the delay two forms are compared by measure (the struck part's displacement, the head's acceleration) and on a dense film strip: (a) today's weakening, at once; (b) nothing during the delay, then the brace. The owner chooses in a session.

**6. A hand to the struck place.** After a hit of `reactions.touch_from_ns` or more on the head, the trunk, an arm or a thigh, the nearer free hand goes to the place for `reactions.touch_s`, from the intent delay on. It needs the hand target of N6, so it is this part's last step and waits for it. A body that has doubled over then holds the place that was hit, not a fixed pose.

### What this does to the push matrix

A body that waits catches less: the capture point runs away by e^(ωt), about 1.9 times in 0.2 s. The matrix is run at four settings of the two delays (none; and three steps up to the middle of the human range), and the outcomes are laid beside the owner's table. Three ways out are the owner's to choose from: accept the new outcomes (more steps at 30 N·s, more falls at 80 N·s), take the delays from the low end of the human range, or change the table. The recovery step's own time (`balance.step_time_s`, 0.25 s) is held against the human figure in the same table.

### Scenarios

| Scenario | Shows |
| --- | --- |
| `react_unseen_chest_30` | `first_target_change_s` is the reflex delay within two frames; `step_lift_s` is no earlier than the step delay |
| `react_passive_first` | Run twice, with the reactions and with every reaction switched off: `Tools\compare-runs.ps1` finds the first differing frame no earlier than the reflex delay after the hit |
| `react_two_hits` | A second hit 30 ms after the first does not start the delay again |
| `react_touch_chest` | After a punch on the chest a hand is within 10 cm of the struck place inside a second and has left it after three |

### Must hold

- With the three delays at 0 and the touch off, the suite has the baseline's hashes.
- The knockdowns, the stand on three seeds and the strikes hold at the chosen delays. The push matrix holds by whatever the owner decides above.

### Risks

| Risk | Guard | Fallback |
| --- | --- | --- |
| The body feels sluggish in the headset | The owner's look decides; the delays start at the low end of the human range | Shorter delays for the reflex than for the step |
| The assist takes the blow back while the body waits (it once undid 200 N·s in three frames) | After N2 the assist can give only what feet can; in a flinch it has no horizontal pull | The delay applies only once N2's step 5 is done |
| More falls than the table allows | The matrix at four settings, before any value is kept | The low end of the range |

### Steps

**The night of 25 to 26 November (for the owner to confirm; `docs/testing.md`, "N4 step 1"):** the reflex's clock is built behind `reactions.reflex_delay_s` (0); the step's and the intent's are not. At 0.08 s the first change of a target comes at 0.100 s instead of 0.011, a second hit does not restart the clock, the knockdowns pass; the push matrix is 156, 155 and 153 of 160 at 0.05, 0.08 and 0.11 (cells that want a step end "held" while the assist holds the waiting body) and the stand is 145 and 120: the first risk below, with its fallback (after N2's step 5). Made again after an independent reading (the assist lets go at the hit, as in a flinch): matrix 155 to 156, the stand 150 and 127, the knockdowns by a blow 6 of 8. Off.

1. The three clocks and the frozen goals; `react_passive_first` first, since it proves the phase of mechanics.
2. The step's delay and the aim with the delay counted in.
3. The matrix at four settings, for the owner.
4. The flinch's weakening, both forms, measured and filmed.
5. The hand to the struck place, after N6's step 1.
6. The scorecard and the report.

### Done when

- [ ] The latencies of an unseen hit are inside the ranges of "Human reference figures"
- [ ] `react_passive_first` passes
- [ ] The owner has decided on the matrix and on the flinch's form, and has felt the timing in a session

**Suggested model:** Opus 5.5, high; Fable to read the matrix if its outcomes shift in ways the delays do not explain.

## N5. Perception

N5 lets the body see what moves near it and feel what holds it, and answer: it looks, it braces before a blow it saw coming, it guards, it pulls against a hold. This is the part the player meets first in the headset, and the one most likely to make the body read as alive.

**Why.** Today the body learns of a hand only when the hand has landed. A person watches the hand the whole way, and what a person does before the contact is most of what tells an onlooker that someone is there.

### Design

**1. What the body is told.** `USandboxBodyController::TellSeen(TConstArrayView<FSandboxSeen>)`, once a frame, by whoever knows both sides: the session (`USandboxPlaySubsystem`) or the run (`FScenarioStage`, beside `TellBodyOfHolds`). One entry is a point that moves: a name, a kind (hand, foot, held thing, the player's head), a place, a velocity, a radius. The body gets no pointer to anything of the player's side and no class of it. `FSandboxBodyHold` gains the hold's force as a vector and the holder's place; today it has only the force's size.

**2. What the body makes of it,** in a new `UpdatePerception`, by its own judgement:

- **In view:** within `perception.view_half_angle_deg` of where the head faces. A thing behind the body is not seen, and a blow from behind is met unprepared.
- **Noticed:** after `perception.notice_s` in view.
- **A threat:** closing on the head or the trunk faster than `perception.threat_speed_mps` with less than `perception.threat_time_s` to the contact; or held within `perception.threat_near_cm` of the face (a raised fist).
- **What it attends to:** a threat first, then a part that is held, then the nearest thing that moves, then the player's head.

**3. What it does.**

| Behaviour | What happens | Through |
| --- | --- | --- |
| Gaze | The head turns toward what the body attends to, within the neck's range, no faster than `perception.gaze_speed_dps`, by `perception.gaze_head_share` of the angle (the eyes take the rest, in M5). It dwells and moves on, it does not track like a camera | Turns of the neck's target, and the head's assist anchor turned with them |
| Anticipation | On a threat that was noticed: the brace of N3, the head turned away and drawn back, the shoulders up (the flinch's pose, before the contact), the eyes shut (in the view of item 4). With more than `perception.guard_time_s` to the contact the near arm rises into the blow's line | Tone, pose turns, a hand target (N6) |
| Giving ground | From a threat held near the face the body leans back by up to `perception.lean_away_cm`, and steps back if it stays | The lean of a held push, the other way; `RequestStep` |
| Answer to a hold | It looks at the held part and braces that chain. It leans against the pull by the hold's force, as it leans into a held push. After N4's intent delay the free hand goes to the hand that holds. A hold counts as a press on a body that gets up | `TellHolds` with the force as a vector. This is step 2 of M4's part 3, which was left open there |
| Attention after a hit | It looks at where it was hit, then at who hit it | Gaze |

**A fast punch is not anticipated, and that is right.** 60 cm at 5 m/s is 0.12 s, less than it takes to notice. A wound-up or slow blow, a raised fist and a hand that hovers are seen in time.

**4. What the body expresses.** `GetExpressionView()`, read by M5's face and audio and by nothing in the body: what the gaze is on, eyes shut (0 to 1), pain (it jumps with a hit's impulse and fades), alarm (the threat's level), effort (N1's share of muscles at their cap), the breath's phase and rate (N7).

**5. Where it is switched.** `perception.enabled`. A scenario's body perceives only with `"perception": true`, as it gets up only with `"get_up": true`, so the suite stays what it is. The hands of `effector` events and the props are told to the body by the run; the player's head is a scenario's `"viewer"` (a place, or a path). A session has it on once the owner has seen it.

**6. Recordings.** A recording holds the hands and not the player's head. The format gains the head's place and facing, frame by frame; a recording made before that replays with no viewer, and the replay's report says so.

### Measures

`control.perception`: `seen_frames`, `threats`, `anticipations`, `anticipation_lead_s` (how long before the contact the brace began), `false_alarms` (a brace with no contact within a second), `gaze_error_mean_deg`, `gaze_on_viewer_share`.

### Scenarios

| Scenario | Shows |
| --- | --- |
| `seen_fist_raised_guards` | A fist held 30 cm from the face for a second: the head draws back at least 5 cm, the body braces, the near arm rises; it neither falls nor flinches |
| `seen_slow_punch_braced`, `unseen_punch_from_behind` | The same impulse on the trunk, seen from the front and unseen from behind: the seen one moves the struck part at most 0.7 times as far, and the brace began at least 50 ms before the contact |
| `seen_fast_jab_too_late` | 5 m/s from 50 cm: no anticipation is counted |
| `gaze_follows_hand` | A hand circling 1 m away at 0.5 m/s: mean gaze error under 15 degrees, the neck never at its limit |
| `gaze_rests_on_viewer` | With nothing moving the body looks at the viewer for more than half the time, with glances away |
| `hold_arm_answer` | A forearm held and pulled with 150 N: the body leans against it by at least 2 cm, looks at it, and its free hand comes within 15 cm of the holder |
| `hover_slow_hand_no_alarm` | A hand moving at 0.3 m/s near the chest for 10 s: no brace |

### Must hold

- With `perception.enabled` at 0, or a scenario without `"perception"`, the suite has the baseline's hashes.
- A run with perception on repeats: the same hashes twice.
- The character's module still depends on nothing of the player's (`SandboxBody` has no include of `SandboxInteraction`).

### Risks

| Risk | Guard | Fallback |
| --- | --- | --- |
| The body is jumpy: a player's hands are always near and always moving | A threat needs closing speed and little time to contact; `false_alarms` on the owner's recordings (`Tools\recording-report.ps1`), which hold ten minutes of real hands | Higher thresholds; anticipation only for the head |
| A head that follows a hand reads as a camera | Dwell and jumps instead of pursuit; only part of the angle goes to the head | Gaze on the player's head only |
| A head that looks while the eyes stare is uncanny | The eyes and the blink of FR-23 are proposed to be brought forward from M5 (a decision for the owner) | Gaze by the head alone, small angles |
| The old recordings have no viewer | The replay says so | Scenarios with a scripted viewer |

### Steps

**The night of 25 to 26 November (for the owner to confirm; `docs/testing.md`, "N5 step 1"):** step 1 is built: the run tells the body the hands, feet and things (`"perception": true` in a scenario's `body`), the body judges view, notice and threat and keeps count in `control.perception`; the hashes are the baseline's with it on and off. A hand at the face from the front is a threat 0.21 s before the contact, but the time it reckons is 0.08 s long (the hand still gathers speed), against a limit of 0.05. Step 2, the gaze, is built behind `perception.gaze` (0) and meets its limits: a hand circling a metre in front is followed with a mean error of 10.5 degrees (15 asked), the neck never at its bound and its target never jumping; the eyes do not move yet, and the head follows like a camera on a steady circle. Of step 3 the look at a hold is built (with the gaze): a forearm held at the hip is looked at as far as the neck goes, 63 degrees off on average against a limit of 20 that this geometry cannot meet; the brace of the held chain is not built. Step 4, anticipation, is built behind `perception.anticipate` (0): a slow punch seen coming is braced for 0.10 s before it lands, a fast jab is not and a hovering hand raises no alarm, but the brace does not lessen how far the chest moves (16.5 cm against 15.6 without it), so the plan's 0.7 is missed; at the face it is 0.72 (18.0 cm against 25.1). Of step 5 the guarding arm is built behind `perception.guard` (0): against a fist held before the face the near forearm stands up before it, elbow down, and no target jumps; the free hand to a holder is not built.

1. `TellSeen`, the judgement of view and threat, the measures. No behaviour yet: with perception on, the hashes are still the baseline's.
2. Gaze.
3. The answer to a hold: the look, the brace, the lean, the press on a get-up.
4. Anticipation: the brace, the head, the flinch before the contact; `false_alarms` on the owner's recordings.
5. The guarding arm and the free hand to the holder, after N6's step 1.
6. The expression view; the recording's viewer.
7. The owner in the headset.

### What the headset session has to answer

| Question | How |
| --- | --- |
| Does it look at you | Stand still, move your head to a side |
| Does it watch a raised hand | Raise a fist slowly and hold it at its face |
| Does it brace for a blow it can see, and not for a gesture | A slow wound-up punch; then talk with your hands near it |
| Is a blow from behind met differently | Walk round it and push its back |
| Does it answer a hold | Take a forearm and pull |
| Is anything jumpy or mechanical | Two minutes of free play |

### Done when

- [ ] The seven scenarios pass and "Must hold" holds
- [ ] No false alarm in the owner's recordings of ordinary play, or the owner has accepted the ones there are
- [ ] The owner has answered the questions above in the headset

**Suggested model:** Opus 5.5, high.

## N6. The fall with an aim

N6 makes a falling body do what a person does on the way down: reach for the place it will land on, brake with its arms, keep its head off the floor, and turn or sit down when there is time. The measure is how the head and the hands meet the floor, not only that the body comes to rest.

**Why.** Today a fall is one of three fixed poses chosen once as the fall starts. On the film of a fall backward the arms stay at the chest. Whether the head strikes the floor, and how fast, has never been measured.

### Design

**1. A hand target.** `USandboxBodyController::SetHandTarget(Hand, WorldLocation, Weight)` and a scenario event `hand_target`, the arm's counterpart of `SetFootTarget`. The animation instance's two-bone placement (`FSandboxLimbTarget`) is made to take an arm: the hand as the end, forearm and upper arm as the segments, the elbow bent away from the trunk, the palm turned toward a given direction, every joint held to its range, the weight eased in. It is tested on a welded pelvis first (`pose_hand_place`), as the foot was: a point fixed in the world is a moving target for a body that turns. M4b needs the same target for the chair; it is built once, here.

**2. Where the fall is going,** worked out every tick while Falling: the way from the centre of mass's velocity over the floor and the pelvis's lean, as `ChooseFallWay` does once; the time left from the chest's height and its speed downward. A fall changes its way as the body turns, so nothing is fixed at its start.

**3. The base pose is a blend by direction,** not one of four. The sample's nine `M_ragdoll_reach_*` clips (forward, back, left, right, up, down and three between) and the three fall poses that exist are weighted by the fall's way, and the weights follow it smoothly. No pose is switched when the way changes.

**4. The arms reach and brake.**

- The hands' targets lie on the floor where the shoulders will come down, `fall.reach_ahead_cm` further along the fall's way, a shoulder's width apart, the palms down, the elbows bent (`fall.reach_share`). Falling backward they lie behind the hips.
- Until a hand touches, the arms are `braced` (N3): they have about a third of a second to get there.
- When a hand has touched, its arm gives way over `fall.arm_yield_s`: its stiffness falls and its damping rises, so that it takes speed out of the fall and does not throw the body back.

**5. The head is kept off the floor.** The neck is `braced` and its target is turned away from the floor along the fall: chin to the chest going backward, head back going forward, ear to the upper shoulder going sideways (`fall.head_away_deg`). With N8's head joint both joints share the turn.

**6. The trunk and the legs.**

- Backward: the hips and knees fold early, so that the body sits down rather than tips (`poses.fall_back` has the pose; what changes is when it starts).
- Backward or sideways with at least `fall.turn_min_s` left: the trunk twists toward the floor so that the body lands on a side and its hands.
- Forward: the knees fold, to land on knees and then hands.

**7. After the floor.** The muscles let go `thresholds.impact_let_go_s` after the trunk meets the floor, as today; what follows is N7.

**8. Falls made by hands.** The knockdowns are blows of 200 N·s and more, harder than a hand gives. `Tools\knockdowns.ps1 -ByHand` fells the body with a scripted shove from each of the eight directions, so that the criteria are also asked of the falls a player causes.

### Tuning

`fall.aim` (0: today's fall, bit for bit), `fall.reach_ahead_cm`, `fall.reach_share`, `fall.arm_yield_s`, `fall.arm_yield_stiffness`, `fall.arm_yield_damping_s`, `fall.head_away_deg`, `fall.turn_min_s`, `fall.turn_deg`, `fall.way_follow_s`.

### Scenarios

| Scenario | Shows |
| --- | --- |
| `pose_hand_place` | On a welded pelvis a hand is within 3 cm of points in front, to the side and below, with no joint outside its range |
| `fall_forward_hands_first` | 200 N·s on the back: a hand or a forearm touches before the trunk; the head does not touch |
| `fall_back_sits` | 200 N·s on the chest: the pelvis touches before the trunk and the head; the head's speed at the floor is under its criterion |
| `fall_side_turns` | 200 N·s from a side: the hand of that side touches before the trunk |
| `fall_way_changes` | A second push in the air from another side: no target jumps (`target_step_max_deg`) |
| The eight knockdowns, by blow and by hand; the three `hit_chest_200_*`; the kicks that fell | The criteria below, on every one |

### Must hold

- With `fall.aim` at 0 the suite has the baseline's hashes.
- Every fall still comes to rest within 3 s, sinks no deeper than 2 cm into the floor and has no joint faster than 30 rad/s when the arms meet the floor.
- The energy a falling body's own muscles add is 40 J at most by the owner's decision of 24 November. Arms that reach do more work than arms held still; if the figure passes 40 J it goes to the owner with the measurement, and is not raised here.
- The stand of 50 falls on three seeds: the bodies now lie differently, and the share that stands and the count at the first attempt are no worse than before.

### Risks

| Risk | Guard | Fallback |
| --- | --- | --- |
| The arms are too slow or too weak to get there | `control.effort` of the shoulder and elbow during the fall; the time from Falling to the first hand on the floor | The base pose by direction alone, without hand targets |
| A wrist snaps when the hand meets the floor (the wrist's model is weak until M4's part 3) | The joint speed limit of 30 rad/s; the hands' tripled inertia | The forearm as what brakes: targets for the elbows |
| Arms end under the body and the get-up starts worse | The stand on three seeds; N7's rest pose moves them | Arms let go earlier |
| The reach reads as a dive | The yield after the touch; films from the side | A shorter reach |
| The fall's way flips between two directions | `fall.way_follow_s`; `fall_way_changes` | The way fixed once the hands are within 0.15 s of the floor |

### Steps

**The night of 25 to 26 November (for the owner to confirm; `docs/testing.md`, "N6 step 1", "step 4 in part", "step 5"):** step 1 is built (`hand_target`): its geometry puts the hand within 0.5 cm of the points and `pose_hand_place` holds that, but the arms at their own strength sag 6 to 9 cm under their weight (N2's matter). Step 2 is the scorecard of that night. Step 4 was tried by values alone (the falling neck at 120 N*m/rad and the fall poses' necks turned further away): the head strikes in 33% of the falls by hand instead of 75%, but the stand is two worse and one scenario fails, so by "Must hold" it does not hold. Step 5 is built behind `fall.aim` (hands sent to the floor along the way, every tick, on top of today's poses; steps 3's blend by direction is not built): with the head's values it brings the head's strikes by hand to 17% and the hand first to 100% by blows, and gains up to 83 J in a backward fall by hand and costs the stand three first attempts. Without a reach in a fall backward (`fall.reach_back` 0) it comes closest: energy 38 J at most, the head strikes in 17% of the falls by hand, the matrix 160; it still misses three limits (the head by blows as today, the hand at 35 to 36 rad/s as it meets the floor, the stand one fall worse). Not met; off; recommended for the owner's look.

1. The hand target, on a welded pelvis.
2. N1's fall figures on today's falls, by blow and by hand: the state before.
3. The way of the fall every tick; the base pose as a blend by direction.
4. The head.
5. The arms: reach, brace, yield.
6. The trunk and the legs: sitting down, the turn, the knees.
7. Everything of "Must hold"; films of the three `m3_fall_*` again with a dense strip of each landing; the scorecard; the report.

### Done when

- [ ] The fall's criteria of "Requirements and criteria" are met on the knockdowns by blow and by hand
- [ ] "Must hold" holds, with the owner's word on the energy if it is needed
- [ ] The owner has seen the films and felled the body by hand in a session

**Suggested model:** Opus 5.5, high.

## N7. Life on the floor and the get-up

N7 makes a body on the floor a person on the floor: it breathes, settles into a way of lying, stirs, gets ready and then gets up, without holding one pose for seconds. The get-up itself loses most of its pull from the world.

**Why.** A body that has fallen lies without one motion until its get-up begins, and reads as dead. About one get-up attempt in four is given up (53 of 203 on three seeds), and while a clip waits the body holds one pose for up to 5 s.

### Design

**1. Breath, in every state.** A generator in the controller: a phase that advances at `breath.rate_per_min` and turns the spine and the collarbones by `breath.spine_deg` and `breath.clavicle_deg`, as turns on top of the pose. The rate rises with effort (a fall, a get-up, a run of steps, a hit) toward `breath.rate_max_per_min` and comes back over `breath.recover_s`. After a hit that doubles the body over the breath is held for `breath.winded_s` and then comes fast. Standing has its breath from `MM_Idle` today: the generator is used where the idle is not carried, and whether it takes the idle's place while standing is decided by a look at both.

**2. Settling.** After the muscles let go at the impact, the tone rises over `lying.settle_s` to a resting tone, and the pose goes to a rest pose for the way the body lies: `poses.lie_back`, `poses.lie_front`, `poses.lie_side` (mirrored for the other side), chosen by how the pelvis lies, as the get-up's clip is. The sample's `M_ragdoll_poses_OnGround` and `M_ragdoll_poses_injured` are the material to start from. The resting tone is low: the floor and the furniture win, and a limb that lies across another is drawn off it. That was a cause of get-ups that could not begin.

**3. Stirring.** While it lies, at seeded moments (N9): the head turns to the player (N5's gaze works lying down), a knee is drawn up, a hand goes to the struck place (N4), the weight rolls a little. `lying.stir_every_s`, `lying.stir_deg`.

**4. How long it lies depends on how it fell.** `getup.rest_s` is 1.5 s for every fall. It becomes a base plus a time per newton-second of the hit that felled it, longer when dazed (N3): a slip is got up from at once, a knockdown by a kick is not.

**5. Getting ready.** Before a clip is chosen, the body's pose is held against each candidate clip's first pose (N1's pose distance). If the nearest is further than `getup.ready_deg`, or a limb lies under the trunk, the body first rolls: the sample has `M_ragdoll_getup_roll_L` and `_R` and `M_ragdoll_poses_rolling`. A roll is a clip's pose asked of the muscles, like the get-up. This is the "roll to a clear pose first" of the plan's hardest problem 4, which was never built.

**6. Two stages where one fails.** The sample's `M_ragdoll_getup_crouch_*` end in a crouch. A get-up may go floor to crouch, pause with the head up and a look at the player, then crouch to standing. It is tried first where the stand shows the one-stage clips failing: beside the table, against a wall.

**7. A wait that is not a freeze.** When the clip waits for the body, the body does what a person does who did not make it up: it goes back a little and comes again. The clip's time is taken back by `getup.gather_s` and run forward with the arms' tone raised, at most `getup.gather_max` times, inside the bound of `getup.wait_s` that exists. Whether this lifts the share of first attempts is asked of the stand on three seeds; it stays only if it does.

**8. The pull from the world goes down.** With N2's weight torques working from the floor's push on whatever touches it (hands, knees, feet), the muscles carry what the assist carried. The caps of `states.get_up.assist` (90 N on the pelvis, 30 N a foot) are then swept down against the stand, as in M3's part 7.

**9. Held or pressed while down.** A body that is pressed with more than `getup.hold_force_n` waits motionless for up to 3 s today. It struggles instead: braced, stirring harder, looking at whoever holds it (N5). The rule itself (one hand slows, two hold) is not changed.

### Tuning

`breath.*`, `lying.settle_s`, `lying.stir_*`, `poses.lie_*`, `tone.resting.*`, `getup.rest_per_ns_s`, `getup.rest_max_s`, `getup.ready_deg`, `getup.gather_s`, `getup.gather_max`, `getup.clip_roll_left`, `getup.clip_roll_right`, `getup.clip_crouch_*`. The rate of a get-up (`getup.rate`) gets its spread in N9.

### Scenarios

| Scenario | Shows |
| --- | --- |
| `lie_back_breathes` | A lying body's chest travels at the breath's rate; `stillness.longest_still_s` is under its criterion; the runner still finds it at rest within 3 s of the fall |
| `lie_settles_limbs` | A body felled so that a foot lies across the other leg has its legs apart within 3 s |
| `getup_rolls_first` | A body laid in a pose far from every clip's first pose rolls, then gets up at the first attempt |
| `getup_two_stage_table` | Beside the table: up by way of the crouch, without an attempt given up |
| `getup_gathers` | A get-up that stalls goes back and comes again; no pose is held longer than a second |
| The `getup_*` scenarios that exist, the stand on three seeds, the stands with `-PressN` and `-HoldHands` | Nothing that held is lost |

### Must hold

- With every new value neutral, the suite has the baseline's hashes.
- FR-18: a body that is hit, pushed, pressed or held while it settles, rolls, crouches or gathers answers as at any other time. Each new phase gets the three scenarios the get-up has (`getup_push_weak`, `getup_hit_strong`, `getup_held`).
- The fall scenarios and the knockdowns keep their body down (`"get_up": false`), and their rest time and energy are still those of the fall: the breath and the settling stay under the runner's rest thresholds, and their work is counted in `max_energy_gain_j`.
- One hand slows a get-up and two hold it, measured on the stand with holding hands.

### Risks

| Risk | Guard | Fallback |
| --- | --- | --- |
| A breathing body never counts as at rest | The breath's speed is measured against the controller's and the runner's rest thresholds before it is switched on | A smaller amplitude lying down |
| A rest pose pushes a limb into furniture, or a roll moves the body under the table | The stand's falls at the table, the chair and the wall, on three seeds | No roll within reach of furniture |
| More phases, more ways to be stuck | Every phase has a bound in time, as the get-up has (`getup.wait_s`); the condition of every way out of a phase is written down and checked for values that satisfy none | Fewer phases |
| The sample has no clip for some way of lying | The pose distance names the falls nothing fits | Poses written as angles; a clip pack proposed to the owner under the rule for paid content |

### Steps

1. Breath; its speed against the rest thresholds.
2. Rest poses and the resting tone; `lie_settles_limbs`.
3. Stirring; the time lying by how the body fell.
4. Getting ready: the pose distance to the clips, the roll.
5. The two-stage get-up at furniture.
6. Gathering instead of waiting, kept only if the stand says so.
7. The assist's caps swept down, after N2.
8. Struggling when held.
9. Everything of "Must hold"; films of the four `m3_getup_*` and the three given-up ones again; the scorecard; the report.

### Done when

- [ ] The floor's and the get-up's criteria of "Requirements and criteria" are met on three seeds
- [ ] "Must hold" holds
- [ ] The owner has knocked the body down and watched it lie and get up in a session

**Suggested model:** Opus 5.5, high. It shares the hand target and the clips with M4b and is best built next to it.

## N8. The body model

N8 refines the model in the four places where the eye looks: the head on its neck, the fingers, the skin at twisted joints, and the toes. Two of the four change the physics asset and with it every baseline, so they are done before the muscles are tuned again in N2 and N3.

### A. The head as its own body

**Why.** Head and neck are one body on one joint at the base of the neck, so a slap swings the whole neck like a rod from the shoulders. A person's head nods and turns on top of the neck, and lags it.

**Design.** In `Specs/physics/npc_body.json`: `neck_01` keeps the neck's shapes, a new body `head` takes the head's; a second joint between them. The clinical range of the neck (flexion 60, extension 75, lateral 45, rotation 80 degrees) is shared between the two joints, with the larger part of the rotation at the upper one. The mass of 6.94% is split between head and neck from a published source found in the part's first step (the fractions for trunk, head and arms are an open check in the plan as it is). `rig.head` becomes `head`; `FSandboxBodyTuning::GroupOfBone` puts both in the neck's group; the pairs of `collision.disable` are read back from the asset, not from the plan.

**With it, the wrist (the owner's decision of 25 November 2026, from M4's part 3):** the hand's limits count in the hand's own frame and it bends back 85 degrees; the forearm's turn goes to the elbow, or the joint's two frames are exchanged (neither tried; `docs/testing.md`, "M4 part 3, what is left of it"). One new asset and one new baseline for both. The clips ask the hand for up to 98 degrees, against 25 today.

**Must be measured again, since every hash moves:** the push matrix's rows on the head, the strikes and slaps on the head, the knockdown's threshold of 35 N·s (set from what the head's body counts as a hit, and that body is now lighter), the kicks to the head, the stand, the grab's pulls on the neck.

**Go or no-go:** standing is as steady as before (drift, the feet's turning speed, the assist's share within a tenth); the neck's joints stretch no more than 0.5 cm under a kick; a dense strip of `palm_slap_head` shows the head turning on the neck; **a slap on the cheek with an open hand turns the head 20 degrees or more** (the owner's answer in the headset on 25 November 2026: a slapped face has to turn; today it turns 1 to 3). Otherwise the one-body neck stays.

### B. Fingers that are posed

**Why.** The hand is one body and its fingers have none. Nothing poses them: they are held as modelled, apart and slightly bent, in every state (close-ups in stance, in the air, at the floor and lying). A hand that never changes is a mannequin's.

**Design.** The fingers stay without bodies: they are bones the animation instance sets, relative to the hand. `poses.fingers.rest`, `fist`, `splayed`, `flat`: a curl for each finger and the thumb and one spread, about ten values a pose. Which is asked for follows the body: `rest` when relaxed, half a `fist` when braced (N3), `splayed` with the palms to the floor in a fall (N6), `flat` under a hand that carries weight in a get-up, `rest` on the body when a hand holds a struck place (N4). Changes are eased, as every pose is.

**Must hold:** the hash of the bodies is the baseline's in every scenario (the hash of the bones moves, by design). The hand's collision shape is unchanged, so posed fingers can pass through a thing: looked at on close-ups, and accepted or bounded there.

### C. Skin at twisted joints

**Looked at on 25 November 2026: the skin holds, and what was planned here is not needed.** The MetaHuman's own correctives are running (`ABP_Body_PostProcess`, the post-process blueprint of the assembled mesh; this plan had said they were not): with the wrist at 70 degrees the forearm's two twist bones turn by 56 and 14. Switched off, the shoulder collapses (`Saved\Films\n8_correctives_with_without.png`). What is left, for M5: they follow the pose the muscles are asked for and not the body (an arm lifted by a grab has none), and the mesh is assembled at the quality MEDIUM, 8568 vertices, whose facets show at the knee and the fingertips.

**Design.** The correctives from the body's own pose, natively after the physics blend (the forearm's twist by 0.8 and 0.2, as Epic's blueprint has it), and the assembly at a higher quality with its cost in the frame measured. Sooner only if M4's grab lifts arms over the shoulder in the headset. That a blueprint of Epic's runs on the body, where the rule says no Animation Blueprints, is the owner's to weigh.

### D. Toes (an experiment, before M4b)

**Why.** The foot is one box. It cannot roll from heel to toe, and it cannot bend at the ball when the body kneels or crouches: the get-up clips ask the foot for 48 degrees more than its range, which is partly the toes' bend. Walking and sitting in M4b will ask for the same.

**Design.** A body on `ball_l` and `ball_r`: the front third of today's box, with a hinge (about 60 degrees up, 30 down) and a small mass taken from the foot's. The sense of balance takes foot and toes together as one sole and one load (`FSandboxBodyProbe::SupportPolygon`, `MeasureBalance`, the places that ask whether a bone is a foot). A stiff muscle keeps the toes flat while standing.

**Go or no-go:** a light body under the whole weight is what the solver likes least. The toes stay only if standing is as steady as before (within a fifth on drift, the feet's turning speed and the assist's share), the push matrix passes, and the physics step costs no more than 0.05 ms. Otherwise the box stays, and the plan says so.

### Not here

The wrist's model is M4's part 3. Soft tissue (FR-16) and the face (FR-23) are M5.

### Steps

1. Close-ups: a hand, a twisted forearm, a raised shoulder, a bent knee. **Done on 25 November 2026** (`docs/testing.md`; `Saved\Films\n8_*`).
2. A: the head. A new baseline; everything listed above measured again; go or no-go. **Built on 25/26 November as a second asset, `PA_SandboxNPCHead`; go by every limit but one, for the owner to confirm:** the slap turns the head by 23 to 26 degrees with the palm on the cheek and by 17.7 with the hand held as the old test held it (`docs/testing.md`, "N8 A"). It needs the world's hold on the head off (`assist.head.angular_nm_per_rad` 0) and a neck damping of 0.05. **The wrist in the hand's own frame is no-go**: twenty fewer first attempts on the stand, from the frame and not the 85 degrees; its asset, `PA_SandboxNPCWrist`, stays unused.
3. B: the fingers, `rest` in the stance first, then `splayed` and `flat` for the floor (in a fall forward two fingers of the lying hand are in the floor). **Built on 25/26 November behind `poses.fingers.weight` (0): rest, half a fist, splayed and flat, chosen by what the body does; bodies' hashes unchanged. The fingers in the floor stay, for the owner to look at.**
4. C: the skin, as above; step 1 did not call for the twist.
5. D: the toes, just before M4b; a new baseline; go or no-go.

### Done when

- [ ] Each of A to D is built, or dropped with its measurement in `docs/testing.md`
- [ ] After A and after D: the suite, the push matrix, the knockdowns, the stand on three seeds and the grab's pulls pass, and every figure the plan quotes for the head or the feet is the new one
- [ ] The owner has seen the close-ups before and after

**Suggested model:** Opus 5.5, high; xhigh for D.

## N9. Variation

N9 makes two meetings with the body differ the way two meetings with a person do, while every run still repeats from its seed. It also replaces the looping sway with one that has a person's irregularity.

**Why.** The same push gives the same motion to the last digit. In a test that is a virtue; in a session the player sees the same stagger a third time and knows it is a machine. And the sway of the stance is a clip of fixed length that comes round again.

### Design

**1. A seed.** A scenario's `"seed"`: 0, the default, is no variation and today's run bit for bit. A session gives each character it makes a new seed from a counter of its own (never from the clock), writes it into its log and its recording, and the scenario that replays the recording has it.

**2. One stream of numbers for each purpose** (`FRandomStream`, from the seed and the purpose's index), advanced only in the controller's tick. The same seed gives the same hashes; one purpose switched off does not shift the others.

**3. What varies.** Each has its size in tuning under `variation.*`; 0 switches it off.

| What | How |
| --- | --- |
| Sway of the stance | A slowly wandering torque at the ankles and hips (`variation.sway_torque_nm`, `variation.sway_time_s`), set so that the centre of pressure moves as a person's does. It takes the place of the idle clip's sway (`poses.idle.sway_weight`) |
| Shifts of weight | Every 10 to 40 s the standing body moves its weight a few centimetres toward one foot, and now and then sets a foot a little differently |
| Delays | N4's delays, by a share either way |
| Steps | The target by a centimetre or two, the swing's time by a share |
| The fall | How far each arm reaches, the blend by direction |
| The floor | Which rest pose, when it stirs, how long it lies, which of two clips that fit equally, the get-up's rate |
| Gaze | When it glances away, how long it dwells |
| Breath | The length of each cycle |

**4. Every outcome table is held on three seeds.** `-Seeds "1,2,3"` on the push matrix, the knockdowns, the stand and the scorecard. A body that passes only without noise stands on a threshold; the push matrix is known to have one such row (30 N·s on the head from behind and a side). A cell that noise tips over is a finding about the threshold, to be fixed there, not a reason to make the noise smaller.

**5. The suite stays at seed 0.** It is the regression test, and its hashes mean what they meant.

### Scenarios

| Scenario | Shows |
| --- | --- |
| `variation_repeats` | The same seed twice: the same hashes. Two seeds: different hashes |
| `variation_stand_sway` | 60 s of standing with a seed: the sway's amplitude and speed within their criteria, and no period |
| `variation_push_differs` | 80 N·s on the chest with three seeds: the pelvis's paths are at least 2 cm apart after 2 s, and all three end standing |
| `variation_shifts_weight` | 120 s of standing: at least two shifts of weight, no step longer than 10 cm, no state but Standing |

### Must hold

- With the seed at 0 the suite has the baseline's hashes.
- The push matrix by the owner's table, the knockdowns and the stand hold on each of three seeds.
- A session's recording replays to the session's outcome within the limits of `Tools\record-replay.ps1`, with the seed it recorded.

### Risks

| Risk | Guard | Fallback |
| --- | --- | --- |
| Noise tips cells that stand on a threshold | Three seeds; the cells are named and their thresholds looked at | A smaller size for that one source, with the reason written down |
| A wandering torque with soft legs walks the body off its place | Drift over the ten-minute stand | A sway that is pulled back toward the stance |
| A replay does not repeat a session | The seed in the recording; the check that exists | Sessions recorded with variation off, when a replay matters |

### Steps

1. The seed, the streams, `variation_repeats`.
2. The sway, against the human figures; the idle clip's sway off.
3. Shifts of weight.
4. The spreads of N4, N6, N7 and N5, each as its part is done.
5. The outcome tables on three seeds; the scorecard; the report.

### Done when

- [ ] The sway's criteria of "Requirements and criteria" are met
- [ ] The outcome tables hold on three seeds
- [ ] The owner has pushed the body the same way several times in a session and seen answers that differ

**Suggested model:** Opus 5.5, high.

## How to work on this track

The rules of `CLAUDE.md` and the working agreements hold. These are the ones this track leans on hardest, each learned the hard way in M2 to M4.

**Before a part**

- Read `CLAUDE.md` (the rules and "Things that have already bitten"), this plan's section for the part, and the last sections of `docs/testing.md`.
- Open with a plan: classes and modules, scenarios and measures, risks. Put the part's decisions to the owner one at a time, with options and the recommended one.
- Take the reference: `Tools\suite.ps1 -SaveBaseline pre_<part>`, and the push matrix, the knockdowns, the stand on three seeds and the grab's pulls as they are now.

**While building**

- Every new behaviour has a tuning value whose neutral setting leaves every hash as it was. The name and its value go into the same build.
- A value that can change at run time gets a `tune_live_*` scenario that shows the change reached the solver.
- A new measure is shown to leave the hashes alone, and is checked on a case whose answer is known, before any figure from it is believed.
- A setting is chosen on three seeds. One scenario is one fall; a figure from one seed is luck.
- When a figure does not move with its setting, find what else bounds it before raising the setting.
- To say what a change does, switch that change alone (`-Tune`).
- When a state is never left, write down the condition of every way out and look for the values that satisfy none.

**Before calling a part done**

- Look at the motion: `Tools\film.ps1` sheets and a dense strip around each event. Say plainly what still looks unnatural. Passing every check is not moving like a person.
- Measure again everything a written claim rests on, not only the suite: the suite with `-Long`, the push matrix, the knockdowns, the stand on three seeds (with `-PressN` and `-HoldHands` if the get-up was touched), the grab's pulls, the record and replay, the scorecard, the bootstrap.
- Name every scenario whose hashes moved, and what moved in it.
- Have the diff read by someone who did not write it after unattended work.
- Close with the report: what was done, what was verified, what went off plan, and the questions for the owner's session.

**The owner's look**

- Each part ends with three to six questions for a session, at the desktop or in the headset.
- A run in the headset is started only on the owner's word, in that message, that the headset is on and SteamVR is up.

**Where things are written**

| What | Where |
| --- | --- |
| Status of a part, decisions | This tab and `docs/natural-body.md`; one line in the milestone list of `docs/PRD.md` |
| Measurements | `docs/testing.md`, a section a part |
| New pitfalls, new commands | `CLAUDE.md` |
| Scenario fields and expectations | `Scenarios/README.md` |
| Tuning names | `Config/Tuning/README.md` |

Commits are made on the owner's command only, to `main`.

## Sources

The human figures are linked where they stand, in "Human reference figures". They were gathered on 25 November 2026 by a research pass that read the abstracts, and the full text where it is named there.

**The project, read for this plan:**

- `Config/Tuning/body.json`, `Config/Tuning/poses.json`, `Specs/physics/npc_body.json`
- `Source/SandboxBody/Public/SandboxBodyController.h` and, in its `.cpp`, `Start`, `ApplyPose`, `ChooseFallWay`, `UpdateStrategies`, `UpdatePoseTurns`, `MuscleScale`, `EnterState`, `OnBodyHit`, `FeelFrame`, `UpdateState`, `PushTuningToControls`, `TickComponent`, `UpdateAssistLoad`
- `Source/SandboxBody/Public/SandboxAnimInstance.h`, `SandboxBodyTuning.h`; the runner's per-frame measures in `ScenarioRunnerSubsystem.cpp`
- `docs/PRD.md` (requirements, the active ragdoll's design, M3's gate and known limits, M4, M4b, M5), `Scenarios/README.md`, `docs/testing.md` (the torque caps of M3 part 5)
- The sheets `m3_fall_back`, `m3_getup_back`, `m3_push80_chest_back` and `strike_punch_belly_steps` under `Saved\Films`, and their `metrics.json` for the timing of steps

**The engine, read in its source (5.8):**

- `Plugins/Animation/PhysicsControl`: `FPhysicsControlData` has strength, damping, a force and a torque cap and a target velocity share; no term carries weight. The only gravity setting is a body modifier's `GravityMultiplier`
- `FBodyInstance::AddTorqueInRadians`, with substepping, is how a torque is put on one body

**Background that shaped the design, named from memory and not opened for this plan:**

- Torques from virtual forces and from the weight a joint carries, for a simulated biped: Coros, Beaudoin and van de Panne, "Generalized Biped Walking Control" (2010)
- A struck character whose muscles give for a moment and answer after a delay: Zordan and Hodgins, "Motion capture-driven simulations that hit and react" (2002)
- The capture point, SIMBICON and DReCon are in the main plan's sources

**Not chosen, and why.** A controller learned by reinforcement from recorded motion (DReCon and its successors) gives the most natural motion published. It needs hundreds of millions of simulated steps, which Chaos does not give at this project's speed, and it would replace a controller whose every value is named and tunable by one that cannot be read. The middle way stays open: a search over this plan's tuning values against N1's scorecard, on the sweeps that exist.
