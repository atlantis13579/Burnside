#include "SolverMethods.h"
#include <algorithm>
#include <stdexcept>
#include <unordered_map>

namespace Burnside
{
SolveResult SolveBreadthFirst(const Cube& state, const SolveOptions& options)
{
    using Clock=std::chrono::steady_clock;
    const auto start=Clock::now();
    SolveResult result; result.Method="State-only bidirectional breadth-first search";
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

    struct Node { std::string Key; size_t Parent; Move Via; };
    struct Side
    {
        std::vector<Node> Nodes;
        std::unordered_map<std::string,size_t> Seen;
        std::vector<size_t> Frontier;
        int Depth=0;
    };
    Side sides[2];
    Cube solved=state; solved.Reset();
    for (int side=0; side<2; ++side)
    {
        const auto key=side==0 ? state.Key() : solved.Key();
        sides[side].Nodes.push_back({key,0,{}});
        sides[side].Seen.emplace(key,0);
        sides[side].Frontier.push_back(0);
    }
    result.Visited=2;
    const auto generators=state.Geometry().Generators();
    const size_t bytesPerState=state.Colors().size()*2+sizeof(Node)+160;
    const size_t maxStates=std::min(options.MaxStates,options.MaxMemoryBytes/bytesPerState);
    Cube work=state;
    const auto trace=[](const Side& side, size_t index)
    {
        std::vector<Move> path;
        while (index!=0)
        {
            path.push_back(side.Nodes[index].Via);
            index=side.Nodes[index].Parent;
        }
        std::reverse(path.begin(),path.end());
        return path;
    };
    while (sides[0].Depth+sides[1].Depth<std::max(0,options.MaxDepth))
    {
        const int sideIndex=sides[0].Depth<=sides[1].Depth ? 0 : 1;
        Side& current=sides[sideIndex]; Side& other=sides[1-sideIndex];
        std::vector<size_t> next;
        for (size_t parent:current.Frontier)
        {
            const std::string parentKey=current.Nodes[parent].Key;
            const Move previous=current.Nodes[parent].Via;
            for (const Move& move:generators)
            {
                if (cancelled()) return finish(SolveStatus::Cancelled,"Cancelled");
                if (timedOut() || result.Visited>=maxStates)
                    return finish(SolveStatus::BudgetExceeded,"Search budget exhausted; no solution claimed");
                if (parent!=0 && move.SameSlice(previous) && move.Turns==-previous.Turns) continue;
                work.SetKey(parentKey); work.Apply(move);
                auto key=work.Key();
                if (current.Seen.find(key)!=current.Seen.end()) continue;
                const size_t index=current.Nodes.size();
                current.Seen.emplace(key,index);
                current.Nodes.push_back({key,parent,move});
                next.push_back(index); ++result.Visited;
                const auto match=other.Seen.find(key);
                if (match!=other.Seen.end())
                {
                    auto forward=trace(sides[0],sideIndex==0 ? index : match->second);
                    auto backward=Invert(trace(sides[1],sideIndex==1 ? index : match->second));
                    forward.insert(forward.end(),backward.begin(),backward.end());
                    result.Moves=Reduce(forward,state.Order());
                    work=state; work.Apply(result.Moves);
                    if (!work.IsSolved()) throw std::logic_error("Search reconstruction failed");
                    return finish(SolveStatus::Solved,"Shortest quarter-turn path found within search bounds");
                }
            }
        }
        current.Frontier=std::move(next); ++current.Depth;
        if (current.Frontier.empty()) break;
    }
    return finish(SolveStatus::DepthLimit,"No solution within the configured depth; reachability is not determined");
}
}
