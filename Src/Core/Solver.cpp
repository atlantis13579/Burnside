#include "SolverMethods.h"

namespace Burnside
{
SolveResult Solve(const Cube& state, const SolveOptions& options,
                  const std::vector<Move>* certificate)
{
    if (certificate)
    {
        SolveResult history=SolveHistory(state,options,certificate);
        if (history.Status!=SolveStatus::InvalidCertificate) return history;
    }
    return SolveBreadthFirst(state,options);
}

SolveResult Solve(const Cube& state, SolveMethod method, const SolveOptions& options,
                  const std::vector<Move>* certificate)
{
    switch (method)
    {
    case SolveMethod::History: return SolveHistory(state,options,certificate);
    case SolveMethod::BreadthFirst: return SolveBreadthFirst(state,options);
    case SolveMethod::TwoPhase: return SolveTwoPhase(state,options);
    case SolveMethod::Reduction: return SolveReduction(state,options);
    case SolveMethod::Yau: return SolveYau(state,options);
    case SolveMethod::Hoya: return SolveHoya(state,options);
    }
    SolveResult result;
    result.Status=SolveStatus::UnsupportedState;
    result.Message="Unknown solve method";
    return result;
}
}
