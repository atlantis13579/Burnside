#pragma once
#include "Cube.h"
#include <atomic>
#include <chrono>

namespace Burnside
{
enum class SolveStatus { Solved, AlreadySolved, DepthLimit, BudgetExceeded, Cancelled, InvalidCertificate, UnsupportedState };
enum class SolveMethod { History, BreadthFirst, TwoPhase, Reduction, Yau, Hoya };
struct SolveOptions
{
    int MaxDepth = 8;
    size_t MaxStates = 150000;
    size_t MaxTwoPhaseNodes = 50000000;
    size_t MaxMemoryBytes = 128 * 1024 * 1024;
    std::chrono::milliseconds TimeLimit{3000};
    const std::atomic_bool* Cancel = nullptr;
    std::atomic<int>* ProgressStage = nullptr;
};
struct SolveResult
{
    SolveStatus Status = SolveStatus::DepthLimit;
    std::vector<Move> Moves;
    std::string Method;
    std::string Message;
    size_t Visited = 0;
    double Milliseconds = 0;
    bool Success() const { return Status == SolveStatus::Solved || Status == SolveStatus::AlreadySolved; }
};

// A certificate is an optional path from the canonical solved state to the input.
// It is checked against every input sticker before its inverse is accepted.
// The legacy overload uses bounded bidirectional BFS when no valid certificate is available.
// The method overload selects one solver explicitly. Two-phase supports orders 2 and 3;
// Center/edge reduction supports orders 4 through 16; staged Yau/Hoya variants
// support even orders 4 through 16.
SolveResult Solve(const Cube& state, const SolveOptions& options = {},
                  const std::vector<Move>* certificate = nullptr);
SolveResult Solve(const Cube& state, SolveMethod method, const SolveOptions& options = {},
                  const std::vector<Move>* certificate = nullptr);
}
