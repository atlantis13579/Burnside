# Burnside — NxNxN Rubik Cube Solver

Supports **2×2×2 through 16×16×16** physical cubic puzzles, animated row/column 
controls, reversible history and an independent solver library. Here N is the 
cube order: each of its six faces contains N×N stickers.

<img width="1445" height="874" alt="cube" src="https://github.com/user-attachments/assets/7ab54aa8-586b-49ed-96e7-462b509b7e8e" />

## Cube model

Every order uses three axes, N layers per axis, N³ cubies and 6N² stickers.
Coordinates are integers from 0 to N−1. A move fixes one axis and layer and
rotates the other two axes by 90 or 180 degrees. For a positive quarter turn in
plane `(a,b)`, `(x[a],x[b])` becomes `(N−1−x[b],x[a])`; sticker normals rotate
with their cubies. Outer and inner slices use the same simulation and animation.
The solved state uses a fixed canonical orientation of the six face colors.

A drag exceeding five pixels orbits without turning a slice on release. Cross
selection follows the projected axes of the clicked face as the camera orbits.
The whole selected slice has a **white** highlight, including while it animates.
The footer identifies the exact axis and layer. A face-center click on an
odd-order cube selects the middle cross.

Hovering a sticker also shows cyan **LMB** and amber **RMB** curved arrows with
mouse-button icons. Each arrow projects the selected sticker's actual 3D turn
trajectory through the current camera, so it stays aligned after orbiting or
scrambling. Hints are hidden during a turn or camera drag.

## Animation and history

The recorded-move list shows manual and random moves as their animations start.
After Solve, the right panel switches to the complete solution sequence and
starts at move zero; the earlier history is hidden there. `M`, `R` and `S`
denote manual, random and solver moves. Backward seeks play inverse animations.
Pending moves remain available through Step forward or Play / redo to end.
Long lists use virtualized rows.
The right header shows current/total moves only during automatic solution
playback. Cube turning, orbiting and zooming are locked while a solver is running.

Random scramble uses 20 moves at order 3 and scales the count with cube order
(rounded to the nearest move). Each turn plays at three times the normal animation
speed. Reset restores the solved cube immediately and clears
both completed and pending timeline moves.

An inverse manual action rewinds the preceding entry instead of recording a new
inverse entry. An opposite click during a manual animation smoothly reverses its
progress; two opposite clicks before the next frame cancel the unstarted turn.
Redo reuses the existing entry. A different action after rewinding creates a
branch and discards the old future.

Solve prepares and verifies a sequence, and reports its current stage and elapsed
time while running. Demo animates to the solution's starting state and plays it. An
imported sequence also starts at the solved origin and is played with Play /
redo to end.

The renderer skips fully hidden interior cubies and cubie patches facing away
from the camera. Turn simulation still covers all N layers and 6N² stickers.

Completing a scrambled cube through manual turns, undo or a solution demo plays
a 3.2-second confetti celebration and congratulations banner. It starts after
the final animation finishes, fades automatically and is dismissed by a new
move. Startup, cube-order changes and Reset do not trigger a reward.

## Independent solver

`BurnsideCore.lib` exposes `Core/Cube.h`, `Core/Solver.h` and `Core/Timeline.h`.
It needs only the standard C++ runtime. The GUI's Solve method selector and the
CLI's `--method` option provide these explicit methods. Each method has its own
implementation file in `Src/Core`:

1. **History inverse.** Verify that the recorded moves reproduce every input
   sticker, then reverse and reduce them. Supports all cube orders from 2 to 16.
2. **Bidirectional BFS.** Search the sticker state without a history. Returns a
   shortest quarter-turn path within the configured bounds; a half turn has
   quarter-turn cost two. Library defaults are depth 8, 150,000 states,
   approximately 128 MiB and 3 seconds. Exhaustion is reported explicitly.
3. **Kociemba two-phase.** Solve a 3×3 from its stickers without history. Center
   rotations caused by middle-slice moves are normalized first. A 2×2 is embedded
   in the 3×3 corner model, so the same solver also handles 2×2 sticker states.
   The search uses orientation and slice coordinates in phase one, then corner
   and edge permutations in phase two. It verifies the returned sequence on the
   original sticker model. This method is available for orders 2 and 3; larger
   orders report that the method is unsupported. Solutions are not guaranteed
   to be shortest. The default search cap is 50 million nodes and 3 seconds.
4. **NxN reduction.** For orders 4 through 16, solve center orbits, pair wing
   edges, correct reduction parity, and solve the resulting 3×3-equivalent cube.
   Odd-order fixed-center orientation is normalized first. The returned moves
   are replayed on Burnside's original sticker model before being accepted.
   This state-only method can produce several thousand moves on large cubes.
   The orbit-reduction algorithms are implemented directly in Burnside and share
   its two-phase solver for the 3×3 finish. They are adapted from the MIT-licensed
   [NxN Cube Solver Engine](https://github.com/krishvijay100/NxN-Cube-Solver-Engine);
   the copyright and license are preserved in [LICENSE-NxNReduction.txt](LICENSE-NxNReduction.txt).
5. **Yau-style staged reduction.** For even orders 4 through 16, solve two
   opposite center faces, build three cross edges, finish the centers while
   restoring those edges, then pair the remaining edges and finish as a 3×3.
6. **Hoya-style staged reduction.** For the same even orders, solve four center
   faces, build the full cross, finish the last two centers while restoring the
   cross, then complete the remaining edges and 3×3 stage.

The Yau and Hoya options reproduce their defining stage order, using exact
orbit permutations rather than human speed-solving cases. They are deliberately
labeled "style" because the generated sequences are long and do not target
competitive move counts. Odd-order Yau/Hoya staging is not exposed: the fixed
middle edges require an additional early cross stage. Standard reduction still
solves odd orders 5 through 15.

The two-phase subgroup and coordinate definitions follow
[Herbert Kociemba's algorithm description](https://www.kociemba.org/twophase.htm)
and [coordinate description](https://www.kociemba.org/math/twophase.htm).
The stage orders follow [Feliks Zemdegs's Yau guide](https://www.cubeskills.com/uploads/pdf/tutorials/intermediate-tips-and-yau-method.pdf)
and the [Hoya method guide](https://www.ocf.berkeley.edu/~dadams/hoya/).

