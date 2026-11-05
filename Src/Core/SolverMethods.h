#pragma once
#include "Solver.h"

namespace Burnside
{
SolveResult SolveHistory(const Cube& state, const SolveOptions& options,
                         const std::vector<Move>* certificate);
SolveResult SolveBreadthFirst(const Cube& state, const SolveOptions& options);
SolveResult SolveTwoPhase(const Cube& state, const SolveOptions& options);
SolveResult SolveReduction(const Cube& state, const SolveOptions& options);
SolveResult SolveYau(const Cube& state, const SolveOptions& options);
SolveResult SolveHoya(const Cube& state, const SolveOptions& options);
}
