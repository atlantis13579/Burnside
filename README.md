# Burnside — NxNxN Rubik Cube Solver

Native C++17, Windows x64, Direct3D 11. Supports **2×2×2 through 10×10×10**
physical cubic puzzles, animated row/column controls, reversible history and an
independent solver library / command-line program. Here N is the cube order:
each of its six faces contains N×N stickers.

## Build and run

Prerequisites: Visual Studio 2022 **Desktop development with C++**, Windows SDK,
and CMake 3.24 or newer on PATH.

```bat
Build.bat Release
Binaries\Release\Burnside.exe
```

`Build.bat` generates the VS2022 solution, compiles it and runs the tests. For IDE use:

```bat
GenerateVS2022.bat
start Build\Burnside.sln
```

Choose `Debug|x64` or `Release|x64`, then run the `Burnside` startup project.
Run `GenerateVS2022.bat` again after changing CMake sources. Automatic CMake
regeneration is disabled so that the generation script can set MSBuild's
intermediate directories after project generation.

| Directory | Contents |
| --- | --- |
| `Src/Core` | Cube model, solver and animation timeline; no Windows / graphics headers |
| `Src/App` | Win32 window, DX11 renderer, perspective camera, picking and IMGUI |
| `Src/CLI` | Headless solver and state / move file interface |
| `Src/Tests` | Algebra, solver, timeline, camera and geometry tests |
| `Src/ThirdParty` | Vendored lightweight IMGUI and attribution |
| `Src/Tools` | VS output-path setup |
| `Build` | Generated VS2022 solution, projects and CMake metadata |
| `Intermediate` | Objects, PDBs, incremental-link data, test states and captures |
| `Binaries/Debug`, `Binaries/Release` | `Burnside.exe`, `BurnsideCLI.exe`, `BurnsideCore.lib`, `BurnsideTests.exe` |

Generated files are ignored by Git. The project has no runtime asset downloads
or dependency on an external engine checkout.

## Model and controls

Every order uses three axes, N layers per axis, N³ cubies and 6N² stickers.
Coordinates are integers from 0 to N−1. A move fixes one axis and layer and
rotates the other two axes by 90 or 180 degrees. For a positive quarter turn in
plane `(a,b)`, `(x[a],x[b])` becomes `(N−1−x[b],x[a])`; sticker normals rotate
with their cubies. Outer and inner slices use the same simulation and animation.
The solved state uses a fixed canonical orientation of the six face colors.

| Input | Action |
| --- | --- |
| Hold either mouse button and move | Orbit the camera |
| Left click a sticker | Turn the horizontal row cross through that cubie |
| Right click a sticker | Turn that cross in the inverse direction |
| Ctrl + left/right click | Turn the vertical column cross through that cubie |
| Opposite button at the same position | Undo the preceding turn; repeating the original button redoes it |
| Mouse wheel over the puzzle | Zoom |
| Mouse wheel over a panel | Scroll that panel |
| Space | Pause at the next move boundary, or play to the end |
| Escape | Cancel solving and stop at the next move boundary |

A drag exceeding five pixels orbits without turning a slice on release. Cross
selection follows the projected axes of the clicked face as the camera orbits.
A left click moves the row toward screen right, or the Ctrl column upward;
right click reverses it. The whole selected slice has a **white** highlight,
including while it animates. The footer identifies the exact axis and layer.
A face-center click on an odd-order cube selects the middle cross.

Hovering a sticker also shows cyan **LMB** and amber **RMB** curved arrows with
mouse-button icons. Each arrow projects the selected sticker's actual 3D turn
trajectory through the current camera, so it stays aligned after orbiting or
scrambling. Ctrl changes both the highlighted cross and its arrows. Hints are
hidden during a turn or camera drag.

The left panel contains cube order, Solve, Demo, animated Reset, Random, turn
speed and import/export. Random uses the current time as its seed. Changing the
order creates a fresh puzzle; Reset animates back to the solved origin before
clearing history.

## Animation and history

The right panel lists moves that have actually started. A queued random or
solver move appears when its animation starts, so the row and model advance
together. `M`, `R` and `S` denote manual, random and solver moves. Clicking a row
animates to the state after that move. Backward seeks play inverse animations
and shorten the displayed history; pending/undone moves remain available through
Step forward or Play / redo to end. Long histories use virtualized rows.

An inverse manual action rewinds the preceding entry instead of recording a new
inverse entry. An opposite click during a manual animation smoothly reverses its
progress; two opposite clicks before the next frame cancel the unstarted turn.
Redo reuses the existing entry. A different action after rewinding creates a
branch and discards the old future.

Solve prepares and verifies a sequence without exposing its pending moves in
history. Demo animates to the solution's starting state and plays it. The same
demo can be repeated. An imported sequence also starts at the solved origin and
is played with Play / redo to end.

Completing a scrambled cube through manual turns, undo or a solution demo plays
a 3.2-second confetti celebration and congratulations banner. It starts after
the final animation finishes, fades automatically and is dismissed by a new
move. Startup, cube-order changes and Reset do not trigger a reward.

## Rendering and interface

All orders render solid rounded cubies and rounded colored stickers. The camera
uses perspective with equal horizontal/vertical pixel scale, centered in the
space between the panels. Resizing preserves the cube's proportions. Geometry,
UV interpolation and depth-tested picking use the same perspective transform.
The initial zoom is 80% of the previous framing; mouse-wheel zoom remains available.

The DX11 renderer includes key/fill lighting, specular highlights, a soft
backdrop/contact shadow and 4× MSAA where supported, with 2× / 1× fallbacks.
Opaque panels and text are drawn after scene resolve into a complete frame,
then presented through a double-buffered flip swap chain. IMGUI uploads its
batched geometry once per frame. UI actions are applied after rendering so the
model and history use one consistent snapshot. Mouse events retain their
original coordinates even when multiple events arrive between frames.

## Independent solver

`BurnsideCore.lib` exposes `Core/Cube.h`, `Core/Solver.h` and `Core/Timeline.h`.
It needs only the standard C++ runtime, with no GUI, DX11, Windows or Python
headers. The GUI and CLI use this same API.

Two solution methods are available:

1. **Verified construction certificate.** Replay the path from solved and compare
   all stickers against the input. If it matches, invert and reduce the path,
   combining commuting parallel slices, then verify the solution again. This
   handles application-generated states for every order 2–10.
2. **State-only bidirectional BFS.** Search the sticker state without a history.
   Returns a shortest quarter-turn path within the configured bounds; a half turn
   has quarter-turn cost two. Library defaults are depth 8, 150,000 states,
   approximately 128 MiB and 3 seconds. The GUI allows 5 seconds. Exhaustion is
   reported explicitly and is never described as solved or unreachable.

The default recovery method depends on a verified history. State-only search is
suitable for short scrambles; an efficient arbitrary-state solver for deeply
scrambled large-order cubes is not implemented. Time, memory, node and depth
limits and atomic cancellation are supported. Solving does not mutate the input;
the GUI runs it on a background worker.

```cpp
#include "Core/Solver.h"

Burnside::Cube cube(5); // 5x5x5
const auto construction = Burnside::Scramble(5, 30, Burnside::TimeSeed());
cube.Apply(construction);
Burnside::SolveOptions options;
const auto answer = Burnside::Solve(cube, options, &construction);
if (answer.Success())
{
    cube.Apply(answer.Moves);
    // cube.IsSolved() is now true.
}
// Omit the third argument to search without a construction certificate.
```

Examples without a window:

```bat
Binaries\Release\BurnsideCLI.exe --order 10 --scramble 50
Binaries\Release\BurnsideCLI.exe --order 3 --scramble 4 --seed 42 --state-only --depth 8
Binaries\Release\BurnsideCLI.exe --order 4 --scramble 20 --save Intermediate\example.state --solution Intermediate\solution.moves
Binaries\Release\BurnsideCLI.exe --load Intermediate\example.state --depth 6 --timeout-ms 5000
Binaries\Release\BurnsideCLI.exe --moves Binaries\Release\Burnside.moves
Binaries\Release\BurnsideCLI.exe --load Binaries\Release\Burnside.state --moves Binaries\Release\Burnside.moves
```

CLI exit codes: `0` verified solved, `2` search/verification budget exhausted or
depth limit reached, `1` invalid input/runtime error. `--state-only` forces search.
The CLI timeout defaults to 30 seconds. Seeds default to time; CLI `--seed` is
retained for reproducible scripts/tests. `--dimension` is a legacy alias for
`--order`, with the same NxNxN meaning.

## Files

Export writes three files beside the executable: `Burnside.state` (committed
sticker state), `Burnside.moves` (active construction prefix), and
`Burnside.timeline` (complete sequence, including its future).

Load move sequence opens a `.moves` or `.timeline` file at the solved origin.
A solver output `.moves` file applies to its input scrambled state; to replay a
complete scramble-and-solve demonstration, import the exported `.timeline`.

State format: `BURNSIDE_STATE 2 <order>`, followed by exactly 6N² integer colors
from 0 to 5. Face order is X−, X+, Y−, Y+, Z−, Z+; each face lists base-N
coordinates along the remaining axes, lowest axis first. Color counts are
validated but do not prove physical reachability.

Move format: `BURNSIDE_MOVES 2 <order>`, followed by one
`fixedAxis layer planeA planeB signedTurns` per line. Axes are 0–2, layer is
0–N−1, `planeA < planeB`, neither plane axis equals the fixed axis, and turns
are −2, −1, 1 or 2. Displayed layers are numbered 1–N for readability.

Version 1 files from the old 3D/3×3 implementation can be imported; legacy layers
−1/0/+1 are converted to 0/1/2. Version 1 spatial-dimension puzzles other than
3D are rejected because they do not represent NxNxN cubes.

## Validation

```bat
ctest --test-dir Build -C Release --output-on-failure
Binaries\Release\Burnside.exe --smoke --order 10 --capture Intermediate\Smoke10.bmp
Binaries\Release\Burnside.exe --smoke --order 3 --warp --capture Intermediate\Warp3.bmp
```

Core tests cover all orders 2–10, every slice's bijective permutation, inverses,
color conservation, known rotations, solver validation and bounds, file round
trips, synchronized history, undo/redo/branching, partial-animation reversal,
perspective aspect ratio, full geometry, picking and animated coordinates.
Feedback tests compare arrows against actual animated motion on scrambled cubes
at four camera orientations, including opposite faces and Ctrl columns, and
check completion triggering, suppression and expiry.

GUI tests run hidden DX11 windows for **every order 2–10**. They click Random
through the window input handler, compare the recorded prefix to the model on
every animation frame, solve/demo/rewind/reset, test both cross directions and
left/right undo/redo, reverse a partial turn and test drag-orbit. They compare
all panel pixels over 24 stationary frames at each of two window sizes, for
48 stable UI frames per test. Images and result reports go to `Intermediate`.
The tests also capture completion effects and both hint directions after orbiting
to another face, then click the displayed move and undo it.
`--warp` selects DX11 software rendering; hardware initialization also falls
back to WARP automatically.

## Attribution

The immediate-mode GUI derives from Mikko Mononen's IMGUI with Riemann extensions.
The DX11 backend is adapted from Riemann. See source notices and
`Src/ThirdParty/Riemann-LICENSE.txt`. Burnside is independent of S18 and does not
require the Riemann checkout.
