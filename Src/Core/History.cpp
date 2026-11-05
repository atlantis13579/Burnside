#include "SolverMethods.h"
#include <stdexcept>

namespace Burnside
{
SolveResult SolveHistory(const Cube& state, const SolveOptions& options,
                         const std::vector<Move>* certificate)
{
    using Clock=std::chrono::steady_clock;
    const auto start=Clock::now();
    SolveResult result; result.Method="Verified history inverse";
    const auto finish=[&](SolveStatus status, const char* message)
    {
        result.Status=status; result.Message=message;
        result.Milliseconds=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
        return result;
    };
    const auto cancelled=[&]() { return options.Cancel && options.Cancel->load(); };
    const auto timedOut=[&]() { return Clock::now()-start>=options.TimeLimit; };
    if (cancelled()) return finish(SolveStatus::Cancelled,"Cancelled");
    if (state.IsSolved()) return finish(SolveStatus::AlreadySolved,"Already solved");
    if (!certificate) return finish(SolveStatus::InvalidCertificate,"History is unavailable for this cube state");
    Cube verify=state; verify.Reset();
    try
    {
        for (const Move& move:*certificate)
        {
            if (cancelled()) return finish(SolveStatus::Cancelled,"Cancelled");
            if (timedOut()) return finish(SolveStatus::BudgetExceeded,"Certificate verification timed out");
            verify.Apply(move);
        }
    }
    catch (const std::invalid_argument&)
    {
        return finish(SolveStatus::InvalidCertificate,"History contains an invalid move");
    }
    if (!(verify==state))
        return finish(SolveStatus::InvalidCertificate,"History does not match the current cube state");
    result.Moves=Reduce(Invert(*certificate),state.Order());
    verify=state;
    for (const Move& move:result.Moves)
    {
        if (cancelled()) return finish(SolveStatus::Cancelled,"Cancelled");
        if (timedOut()) return finish(SolveStatus::BudgetExceeded,"Solution verification timed out");
        verify.Apply(move);
    }
    if (!verify.IsSolved()) throw std::logic_error("History inverse verification failed");
    return finish(SolveStatus::Solved,"Verified history solution; shortest length is not guaranteed");
}
}
