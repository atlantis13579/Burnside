#pragma once
#include "Solver.h"

namespace Burnside::ReductionDetail
{
// Face-indexed working storage also carries marker bytes for orbit permutations.
// Public solver inputs and returned moves use Burnside's cube representation.
enum class Face : uint8_t
{
    U,
    R,
    F,
    D,
    L,
    B
};
enum class Turn : uint8_t
{
    CW,
    Half,
    CCW
};
class State
{
  public:
    explicit State(int order);
    int Order() const { return m_Order; }
    int StickerCount() const { return FaceCount * m_Order * m_Order; }
    uint8_t GetSticker(int face, int row, int col) const;
    void SetSticker(int face, int row, int col, uint8_t color);
    uint8_t* FaceData(int face);
    const uint8_t* FaceData(int face) const;
    uint8_t* Data() { return m_Stickers.data(); }
    const uint8_t* Data() const { return m_Stickers.data(); }
    bool IsSolved() const;

  private:
    int m_Order;
    std::vector<uint8_t> m_Stickers;
};
struct SliceMove
{
    Face Side;
    int OuterDepth;
    int InnerDepth;
    Turn Rotation;
};
using MoveStep = std::vector<SliceMove>;
enum class ParityState : uint8_t
{
    Valid,
    OLL,
    PLL,
    Both
};

State FromCube(const Cube& state);
std::vector<Move> ToBurnsideMoves(const SliceMove& step, int order);
bool NormalizeFixedCenters(const Cube& state, Cube& normalized, std::vector<Move>& prefix);
bool CentersSelected(const State& state, uint8_t faceMask);
bool CrossWingsSelected(const State& state, uint16_t edgeMask);
std::vector<MoveStep> SolveCenters(State& state);
std::vector<MoveStep> SolveCentersSelected(State& state, uint8_t faceMask);
std::vector<MoveStep> SolveEdges(State& state);
std::vector<MoveStep> SolveCrossWingsSelected(State& state, uint16_t edgeMask);
bool CentersReduced(const State& state);
bool WingsReduced(const State& state);
ParityState DetectParity(const State& state);
std::vector<SliceMove> CollapseMoves(const std::vector<SliceMove>& moves);
SolveResult FinishAs3x3(State& state, const SolveOptions& options, std::vector<SliceMove>& moves);
} // namespace Burnside::ReductionDetail
