#include "Core/Solver.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace Burnside;
int main(int argc, char** argv)
{
    try
    {
        int order = 3, count = 25; uint32_t seed = TimeSeed();
        bool stateOnly = false, explicitMethod = false;
        SolveMethod selectedMethod = SolveMethod::History;
        std::string input, output, solution, movesFile;
        SolveOptions options; options.TimeLimit = std::chrono::seconds(30);
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            const auto value = [&]() -> std::string
            {
                if (++i >= argc) throw std::invalid_argument("Missing value for " + arg);
                return argv[i];
            };
            if (arg == "--order" || arg == "--dimension") order = std::stoi(value());
            else if (arg == "--scramble") count = std::stoi(value());
            else if (arg == "--seed") seed = static_cast<uint32_t>(std::stoul(value()));
            else if (arg == "--depth") options.MaxDepth = std::stoi(value());
            else if (arg == "--timeout-ms") options.TimeLimit = std::chrono::milliseconds(std::stoll(value()));
            else if (arg == "--max-states") options.MaxStates = options.MaxTwoPhaseNodes = std::stoull(value());
            else if (arg == "--state-only") stateOnly = true;
            else if (arg == "--method")
            {
                const auto name=value(); explicitMethod=true;
                if (name=="history") selectedMethod=SolveMethod::History;
                else if (name=="bfs") selectedMethod=SolveMethod::BreadthFirst;
                else if (name=="two-phase") selectedMethod=SolveMethod::TwoPhase;
                else if (name=="reduction") selectedMethod=SolveMethod::Reduction;
                else if (name=="yau") selectedMethod=SolveMethod::Yau;
                else if (name=="hoya") selectedMethod=SolveMethod::Hoya;
                else throw std::invalid_argument("Unknown solve method: " + name);
            }
            else if (arg == "--load") input = value();
            else if (arg == "--save") output = value();
            else if (arg == "--solution") solution = value();
            else if (arg == "--moves") movesFile = value();
            else if (arg == "--help")
            {
                std::cout << "BurnsideCLI [--order 2..16] [--scramble N] [--seed N]\n"
                    "  [--method history|bfs|two-phase|reduction|yau|hoya] [--state-only] [--depth N]\n"
                    "  [--timeout-ms N] [--max-states N]\n"
                    "  [--load state.txt] [--save state.txt] [--solution moves.txt]\n"
                    "  [--moves construction.moves] (replaces the random scramble)\n"
                    "--state-only is an alias for --method bfs.\n"
                    "Two-phase solves 2x2 and 3x3 from sticker state without history.\n"
                    "Reduction solves 4x4 through 16x16 from sticker state.\n"
                    "Yau/Hoya-style staging supports even orders 4x4 through 16x16.\n"
                    "Order N means a physical NxNxN cube with six NxN faces.\n"
                    "State-only BFS is bounded, not a general efficient arbitrary-state solver.\n"
                    "Exit: 0 verified solution, 2 search limit, 1 input/runtime error.\n";
                return 0;
            }
            else throw std::invalid_argument("Unknown option: " + arg);
        }
        Cube cube = input.empty() ? Cube(order) : LoadState(input);
        std::vector<Move> path;
        if (!movesFile.empty())
        {
            const auto sequence = LoadMoves(movesFile);
            if (input.empty()) { cube = Cube(sequence.Order); cube.Apply(sequence.Moves); }
            else if (sequence.Order != cube.Order()) throw std::invalid_argument("State and sequence orders differ");
            path = sequence.Moves;
        }
        else if (input.empty()) { path = Scramble(order, count, seed); cube.Apply(path); }
        if (!output.empty()) SaveState(cube, output);
        const bool haveCertificate = input.empty() || !movesFile.empty();
        const SolveMethod method=stateOnly ? SolveMethod::BreadthFirst :
            explicitMethod ? selectedMethod : haveCertificate ? SolveMethod::History : SolveMethod::BreadthFirst;
        const auto result = Solve(cube, method, options,
            method==SolveMethod::History && haveCertificate ? &path : nullptr);
        std::cout << cube.Order() << 'x' << cube.Order() << 'x' << cube.Order() << ", " << cube.Colors().size() << " stickers\n"
            << result.Method << '\n' << result.Message << '\n'
            << "Visited: " << result.Visited << ", time: " << result.Milliseconds << " ms\n";
        if (!result.Success()) return 2;
        for (const auto& m : result.Moves) std::cout << m.ToString(cube.Order()) << '\n';
        Cube verified = cube; verified.Apply(result.Moves);
        if (!verified.IsSolved()) throw std::logic_error("Returned solution failed verification");
        if (!solution.empty())
        {
            SaveMoves({cube.Order(),result.Moves},solution);
        }
        std::cout << "VERIFIED SOLVED (" << result.Moves.size() << " moves)\n"; return 0;
    }
    catch (const std::exception& e) { std::cerr << "Error: " << e.what() << '\n'; return 1; }
}
