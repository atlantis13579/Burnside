#pragma once
#include "Cube.h"
#include <atomic>
#include <chrono>

namespace Burnside
{
enum class SolveStatus { Solved, AlreadySolved, DepthLimit, BudgetExceeded, Cancelled };
struct SolveOptions
{
    int MaxDepth = 8;
    size_t MaxStates = 150000;
    size_t MaxMemoryBytes = 128 * 1024 * 1024;
    std::chrono::milliseconds TimeLimit{3000};
    const std::atomic_bool* Cancel = nullptr;
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
// Without it, bounded bidirectional BFS operates only on the supplied color state.
SolveResult Solve(const Cube& state, const SolveOptions& options = {},
                  const std::vector<Move>* certificate = nullptr);
}
