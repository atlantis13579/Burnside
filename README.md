# Burnside — NxNxN Rubik Cube Solver

Native C++17, Windows x64, Direct3D 11. Supports **2×2×2 through 10×10×10**
physical cubic puzzles, animated row/column controls, reversible history and an
independent solver library. Here N is the cube order: each of its six faces
contains N×N stickers.

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

The move list shows moves that have actually started. A queued random or
solver move appears when its animation starts, so the row and model advance
together. `M`, `R` and `S` denote manual, random and solver moves. Backward
seeks play inverse animations and shorten the displayed history; pending/undone
moves remain available through Step forward or Play / redo to end. Long
histories use virtualized rows.

An inverse manual action rewinds the preceding entry instead of recording a new
inverse entry. An opposite click during a manual animation smoothly reverses its
progress; two opposite clicks before the next frame cancel the unstarted turn.
Redo reuses the existing entry. A different action after rewinding creates a
branch and discards the old future.

Solve prepares and verifies a sequence without exposing its pending moves in
history. Demo animates to the solution's starting state and plays it. An
imported sequence also starts at the solved origin and is played with Play /
redo to end.

Completing a scrambled cube through manual turns, undo or a solution demo plays
a 3.2-second confetti celebration and congratulations banner. It starts after
the final animation finishes, fades automatically and is dismissed by a new
move. Startup, cube-order changes and Reset do not trigger a reward.

## Independent solver

`BurnsideCore.lib` exposes `Core/Cube.h`, `Core/Solver.h` and `Core/Timeline.h`.
It needs only the standard C++ runtime

Solution methods are available:

1. **State-only bidirectional BFS.** Search the sticker state without a history.
   Returns a shortest quarter-turn path within the configured bounds; a half turn
   has quarter-turn cost two. Library defaults are depth 8, 150,000 states,
   approximately 128 MiB and 3 seconds. Exhaustion is reported explicitly and is
   never described as solved or unreachable.

