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
        bool stateOnly = false;
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
            else if (arg == "--max-states") options.MaxStates = std::stoull(value());
            else if (arg == "--state-only") stateOnly = true;
            else if (arg == "--load") input = value();
            else if (arg == "--save") output = value();
            else if (arg == "--solution") solution = value();
            else if (arg == "--moves") movesFile = value();
            else if (arg == "--help")
            {
                std::cout << "BurnsideCLI [--order 2..10] [--scramble N] [--seed N]\n"
                    "  [--state-only] [--depth N] [--timeout-ms N] [--max-states N]\n"
                    "  [--load state.txt] [--save state.txt] [--solution moves.txt]\n"
                    "  [--moves construction.moves] (replaces the random scramble)\n"
                    "Without --state-only, a validated construction certificate is used.\n"
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
        const bool useCertificate = !stateOnly && (input.empty() || !movesFile.empty());
        const auto result = Solve(cube, options, useCertificate ? &path : nullptr);
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
