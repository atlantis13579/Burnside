#include "SolverMethods.h"
#include "Reduction.h"
#include <stdexcept>

namespace Burnside
{
SolveResult SolveYau(const Cube& state, const SolveOptions& options)
{
    using Clock=std::chrono::steady_clock;
    const auto start=Clock::now();
    SolveResult result; result.Method="Yau-style staged reduction";
    const auto finish=[&](SolveStatus status, const std::string& message)
    {
        result.Status=status; result.Message=message;
        result.Milliseconds=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
        return result;
    };
    const auto stopped=[&]() { return options.Cancel && options.Cancel->load(); };
    const auto timedOut=[&]() { return Clock::now()-start>=options.TimeLimit; };
    if (stopped()) return finish(SolveStatus::Cancelled,"Cancelled");
    if (state.IsSolved()) return finish(SolveStatus::AlreadySolved,"Already solved");
    if (state.Order()<4 || state.Order()>MaxOrder || state.Order()%2)
        return finish(SolveStatus::UnsupportedState,"Yau-style staging supports even orders 4 through 16");
    constexpr uint8_t oppositeCenters=(1 << 0) | (1 << 3);
    constexpr uint16_t threeCrossEdges=(1 << 4) | (1 << 5) | (1 << 6);
    Cube normalized=state; std::vector<Move> prefix;
    if (!ReductionDetail::NormalizeFixedCenters(state,normalized,prefix))
        return finish(SolveStatus::UnsupportedState,"Invalid fixed-center orientation");
    auto cube=ReductionDetail::FromCube(normalized);
    const auto report=[&](int stage) { if (options.ProgressStage) options.ProgressStage->store(stage,std::memory_order_relaxed); };
    std::vector<ReductionDetail::SliceMove> raw;
    const auto append=[&](const std::vector<ReductionDetail::MoveStep>& steps)
    {
        for (const auto& step:steps) raw.insert(raw.end(),step.begin(),step.end());
    };
    report(1); append(ReductionDetail::SolveCentersSelected(cube,oppositeCenters));
    if (!ReductionDetail::CentersSelected(cube,oppositeCenters))
        return finish(SolveStatus::DepthLimit,"Could not solve the first two centers");
    if (stopped()) return finish(SolveStatus::Cancelled,"Cancelled");
    if (timedOut()) return finish(SolveStatus::BudgetExceeded,"Yau-style time limit exceeded");
    report(2); append(ReductionDetail::SolveCrossWingsSelected(cube,threeCrossEdges));
    if (!ReductionDetail::CrossWingsSelected(cube,threeCrossEdges) ||
        !ReductionDetail::CentersSelected(cube,oppositeCenters))
        return finish(SolveStatus::DepthLimit,"Could not build the first three cross edges");
    report(3); append(ReductionDetail::SolveCenters(cube));
    if (!ReductionDetail::CentersReduced(cube))
        return finish(SolveStatus::DepthLimit,"Could not finish the centers");
    report(4); append(ReductionDetail::SolveCrossWingsSelected(cube,threeCrossEdges));
    if (!ReductionDetail::CrossWingsSelected(cube,threeCrossEdges) ||
        !ReductionDetail::CentersReduced(cube))
        return finish(SolveStatus::DepthLimit,"Could not restore the cross after centers");
    if (stopped()) return finish(SolveStatus::Cancelled,"Cancelled");
    if (timedOut()) return finish(SolveStatus::BudgetExceeded,"Yau-style time limit exceeded");
    report(5); append(ReductionDetail::SolveEdges(cube));
    if (!ReductionDetail::WingsReduced(cube) ||
        !ReductionDetail::CentersReduced(cube) ||
        ReductionDetail::DetectParity(cube)!=ReductionDetail::ParityState::Valid)
        return finish(SolveStatus::DepthLimit,"Could not pair the remaining edges");
    if (stopped()) return finish(SolveStatus::Cancelled,"Cancelled");
    if (timedOut()) return finish(SolveStatus::BudgetExceeded,"Staged reduction time limit exceeded");
    SolveOptions remaining=options;
    remaining.TimeLimit-=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start);
    report(6); const auto last=ReductionDetail::FinishAs3x3(cube,remaining,raw);
    result.Visited=last.Visited;
    if (!last.Success()) return finish(last.Status,last.Message);
    if (!cube.IsSolved()) return finish(SolveStatus::DepthLimit,"Could not finish the reduced cube");
    if (stopped()) return finish(SolveStatus::Cancelled,"Cancelled");
    if (timedOut()) return finish(SolveStatus::BudgetExceeded,"Yau-style time limit exceeded");
    report(7); result.Moves=std::move(prefix);
    for (const auto& step:ReductionDetail::CollapseMoves(raw))
    {
        auto mapped=ReductionDetail::ToBurnsideMoves(step,state.Order());
        result.Moves.insert(result.Moves.end(),mapped.begin(),mapped.end());
    }
    result.Moves=Reduce(result.Moves,state.Order());
    Cube verify=state; verify.Apply(result.Moves);
    if (!verify.IsSolved()) throw std::logic_error("Yau-style solution verification failed");
    return finish(SolveStatus::Solved,"Verified Yau-style staged solution");
}
}
