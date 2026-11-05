// Orbit reduction adapted from NxN Cube Solver Engine by Krish Vijay.
// Upstream commit: 56444c0e6747c6cf55bf86efd1f44c5c8444e039 (MIT).
// See LICENSE-NxNReduction.txt in the project root for the copyright and license.
#include "Reduction.h"
#include "SolverMethods.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <queue>
#include <stdexcept>
#include <tuple>
#include <unordered_set>

namespace Burnside
{
namespace ReductionDetail
{
namespace
{
void RotateFace(State& cube, int face, Turn turn);
}
State::State(int n) : m_Order(n), m_Stickers(FaceCount * n * n)
{
    assert(n >= 2 && "n×n cube requires n >= 2");

    const int fs = n * n;
    for (int f = 0; f < FaceCount; ++f)
    {
        for (int i = 0; i < fs; ++i)
        {
            m_Stickers[f * fs + i] = static_cast<uint8_t>(f);
        }
    }
}

uint8_t State::GetSticker(int face, int row, int col) const
{
    return m_Stickers[face * m_Order * m_Order + row * m_Order + col];
}

void State::SetSticker(int face, int row, int col, uint8_t color)
{
    m_Stickers[face * m_Order * m_Order + row * m_Order + col] = color;
}

uint8_t* State::FaceData(int face)
{
    return m_Stickers.data() + face * m_Order * m_Order;
}

const uint8_t* State::FaceData(int face) const
{
    return m_Stickers.data() + face * m_Order * m_Order;
}

bool State::IsSolved() const
{
    const int fs = m_Order * m_Order;
    for (int f = 0; f < FaceCount; ++f)
    {
        const uint8_t expected = static_cast<uint8_t>(f);
        for (int i = 0; i < fs; ++i)
        {
            if (m_Stickers[f * fs + i] != expected) return false;
        }
    }
    return true;
}

namespace
{

enum class Dir : uint8_t
{
    RowLR, // row `line`, read left-to-right
    RowRL, // row `line`, read right-to-left
    ColTB, // column `line`, read top-to-bottom
    ColBT, // column `line`, read bottom-to-top
};

enum class EdgeSide : uint8_t
{
    FromFirst,
    FromLast
};

struct Strip
{
    Face Side;
    Dir dir;
    EdgeSide side;
};

inline int StripLine(EdgeSide side, int depth, int n)
{
    return (side == EdgeSide::FromFirst) ? depth : n - 1 - depth;
}

// read the n stickers of `s` at the given depth into `out` in the direction dictated by s.dir
inline void ReadStrip(const State& cube, const Strip& s, int depth, uint8_t* out)
{
    const int n = cube.Order();
    const int line = StripLine(s.side, depth, n);
    const uint8_t* fdata = cube.FaceData(static_cast<int>(s.Side));
    switch (s.dir)
    {
    case Dir::RowLR:
        for (int i = 0; i < n; ++i) out[i] = fdata[line * n + i];
        break;
    case Dir::RowRL:
        for (int i = 0; i < n; ++i) out[i] = fdata[line * n + (n - 1 - i)];
        break;
    case Dir::ColTB:
        for (int i = 0; i < n; ++i) out[i] = fdata[i * n + line];
        break;
    case Dir::ColBT:
        for (int i = 0; i < n; ++i) out[i] = fdata[(n - 1 - i) * n + line];
        break;
    }
}

// write n stickers from `in` into `s` at the given depth in the direction dictated by s.dir
inline void WriteStrip(State& cube, const Strip& s, int depth, const uint8_t* in)
{
    const int n = cube.Order();
    const int line = StripLine(s.side, depth, n);
    uint8_t* fdata = cube.FaceData(static_cast<int>(s.Side));
    switch (s.dir)
    {
    case Dir::RowLR:
        for (int i = 0; i < n; ++i) fdata[line * n + i] = in[i];
        break;
    case Dir::RowRL:
        for (int i = 0; i < n; ++i) fdata[line * n + (n - 1 - i)] = in[i];
        break;
    case Dir::ColTB:
        for (int i = 0; i < n; ++i) fdata[i * n + line] = in[i];
        break;
    case Dir::ColBT:
        for (int i = 0; i < n; ++i) fdata[(n - 1 - i) * n + line] = in[i];
        break;
    }
}

struct OuterMove
{
    Strip strips[4];
};

constexpr OuterMove U_MOVE = {{
    {Face::F, Dir::RowLR, EdgeSide::FromFirst},
    {Face::L, Dir::RowLR, EdgeSide::FromFirst},
    {Face::B, Dir::RowLR, EdgeSide::FromFirst},
    {Face::R, Dir::RowLR, EdgeSide::FromFirst},
}};

constexpr OuterMove R_MOVE = {{
    {Face::F, Dir::ColTB, EdgeSide::FromLast},
    {Face::U, Dir::ColTB, EdgeSide::FromLast},
    {Face::B, Dir::ColBT, EdgeSide::FromFirst},
    {Face::D, Dir::ColTB, EdgeSide::FromLast},
}};

constexpr OuterMove F_MOVE = {{
    {Face::U, Dir::RowLR, EdgeSide::FromLast},
    {Face::R, Dir::ColTB, EdgeSide::FromFirst},
    {Face::D, Dir::RowRL, EdgeSide::FromFirst},
    {Face::L, Dir::ColBT, EdgeSide::FromLast},
}};

constexpr OuterMove D_MOVE = {{
    {Face::F, Dir::RowLR, EdgeSide::FromLast},
    {Face::R, Dir::RowLR, EdgeSide::FromLast},
    {Face::B, Dir::RowLR, EdgeSide::FromLast},
    {Face::L, Dir::RowLR, EdgeSide::FromLast},
}};

constexpr OuterMove L_MOVE = {{
    {Face::F, Dir::ColTB, EdgeSide::FromFirst},
    {Face::D, Dir::ColTB, EdgeSide::FromFirst},
    {Face::B, Dir::ColBT, EdgeSide::FromLast},
    {Face::U, Dir::ColTB, EdgeSide::FromFirst},
}};

constexpr OuterMove B_MOVE = {{
    {Face::U, Dir::RowRL, EdgeSide::FromFirst},
    {Face::L, Dir::ColTB, EdgeSide::FromFirst},
    {Face::D, Dir::RowLR, EdgeSide::FromLast},
    {Face::R, Dir::ColBT, EdgeSide::FromLast},
}};

inline const OuterMove& OuterTable(Face f)
{
    switch (f)
    {
    case Face::U:
        return U_MOVE;
    case Face::R:
        return R_MOVE;
    case Face::F:
        return F_MOVE;
    case Face::D:
        return D_MOVE;
    case Face::L:
        return L_MOVE;
    case Face::B:
        return B_MOVE;
    }
    return U_MOVE; // unreachable
}

void CycleStripsOnce(State& cube, const OuterMove& m, int depth)
{
    const int n = cube.Order();
    std::vector<uint8_t> a(n), b(n), c(n), d(n);
    ReadStrip(cube, m.strips[0], depth, a.data());
    ReadStrip(cube, m.strips[1], depth, b.data());
    ReadStrip(cube, m.strips[2], depth, c.data());
    ReadStrip(cube, m.strips[3], depth, d.data());
    WriteStrip(cube, m.strips[1], depth, a.data());
    WriteStrip(cube, m.strips[2], depth, b.data());
    WriteStrip(cube, m.strips[3], depth, c.data());
    WriteStrip(cube, m.strips[0], depth, d.data());
}

} // namespace

namespace
{
void ApplyWideMove(State& cube, Face f, int outerDepth, int innerDepth, Turn t)
{
    assert(0 <= outerDepth && outerDepth <= innerDepth);
    assert(innerDepth < cube.Order());

    // rotate the outer face iff depth 0 is included in the range
    if (outerDepth == 0)
    {
        RotateFace(cube, static_cast<int>(f), t);
    }

    const OuterMove& m = OuterTable(f);
    const int shifts = (t == Turn::CW) ? 1 : (t == Turn::Half) ? 2 : 3;
    for (int depth = outerDepth; depth <= innerDepth; ++depth)
    {
        for (int i = 0; i < shifts; ++i) CycleStripsOnce(cube, m, depth);
    }
}

// write the rotated stickers into a temp array using turn formula then copy back
void RotateFace(State& cube, int face, Turn t)
{
    const int n = cube.Order();
    uint8_t* src = cube.FaceData(face);
    std::vector<uint8_t> scratch(n * n);

    for (int r = 0; r < n; ++r)
    {
        for (int c = 0; c < n; ++c)
        {
            int nr, nc;
            switch (t)
            {
            case Turn::CW:
                nr = c;
                nc = n - 1 - r;
                break;
            case Turn::CCW:
                nr = n - 1 - c;
                nc = r;
                break;
            case Turn::Half:
                nr = n - 1 - r;
                nc = n - 1 - c;
                break;
            }
            scratch[nr * n + nc] = src[r * n + c];
        }
    }

    for (int i = 0; i < n * n; ++i) src[i] = scratch[i];
}

void ApplyMove(State& cube, const SliceMove& m)
{
    ApplyWideMove(cube, m.Side, m.OuterDepth, m.InnerDepth, m.Rotation);
}

void ApplyMoveStep(State& cube, const MoveStep& step)
{
    for (const SliceMove& move : step) ApplyMove(cube, move);
}
} // namespace

namespace
{

inline int TurnQuarters(Turn t)
{
    switch (t)
    {
    case Turn::CW:
        return 1;
    case Turn::Half:
        return 2;
    case Turn::CCW:
        return 3;
    }
    return 0;
}

inline std::optional<Turn> TurnFromQuarters(int q)
{
    q = ((q % 4) + 4) % 4;
    switch (q)
    {
    case 0:
        return std::nullopt; // net-zero rotation, drop the move
    case 1:
        return Turn::CW;
    case 2:
        return Turn::Half;
    case 3:
        return Turn::CCW;
    }
    return std::nullopt;
}

inline bool SameLayer(const SliceMove& a, const SliceMove& b)
{
    return a.Side == b.Side && a.OuterDepth == b.OuterDepth && a.InnerDepth == b.InnerDepth;
}

} // namespace

namespace
{

uint64_t MoveCode(const SliceMove& m)
{
    return (static_cast<uint64_t>(m.Side) << 24) | (static_cast<uint64_t>(m.OuterDepth) << 16) |
           (static_cast<uint64_t>(m.InnerDepth) << 8) | static_cast<uint64_t>(m.Rotation);
}

bool SamePosition(const State& a, const State& b)
{
    if (a.Order() != b.Order()) return false;
    for (int i = 0; i < a.StickerCount(); ++i)
    {
        if (a.Data()[i] != b.Data()[i]) return false;
    }
    return true;
}

bool MovesCommute(const SliceMove& a, const SliceMove& b, int n)
{
    State AbLo(n), abHi(n), baLo(n), baHi(n);
    for (int i = 0; i < AbLo.StickerCount(); ++i)
    {
        const auto id = static_cast<uint16_t>(i);
        AbLo.Data()[i] = baLo.Data()[i] = static_cast<uint8_t>(id);
        abHi.Data()[i] = baHi.Data()[i] = static_cast<uint8_t>(id >> 8);
    }
    for (State* cube : {&AbLo, &abHi})
    {
        ApplyMove(*cube, a);
        ApplyMove(*cube, b);
    }
    for (State* cube : {&baLo, &baHi})
    {
        ApplyMove(*cube, b);
        ApplyMove(*cube, a);
    }
    return SamePosition(AbLo, baLo) && SamePosition(abHi, baHi);
}

bool InversePair(const SliceMove& a, const SliceMove& b)
{
    return SameLayer(a, b) &&
           (a.Rotation == Turn::Half ? b.Rotation == Turn::Half
                                     : (a.Rotation == Turn::CW ? b.Rotation == Turn::CCW : b.Rotation == Turn::CW));
}

auto MoveSortKey(const SliceMove& m)
{
    return std::tuple<int, int, int, int>{static_cast<int>(m.Side), m.OuterDepth, m.InnerDepth,
                                          static_cast<int>(m.Rotation)};
}

std::vector<SliceMove> OptimizeMoves(const std::vector<SliceMove>& input, int n)
{
    std::vector<SliceMove> out = CollapseMoves(input);
    if (n < 4 || out.size() < 3) return out;

    std::map<std::pair<uint64_t, uint64_t>, bool> commuteCache;
    auto commute = [&](const SliceMove& a, const SliceMove& b) {
        uint64_t x = MoveCode(a), y = MoveCode(b);
        if (x > y) std::swap(x, y);
        const auto key = std::make_pair(x, y);
        const auto it = commuteCache.find(key);
        if (it != commuteCache.end()) return it->second;
        const bool result = MovesCommute(a, b, n);
        commuteCache.emplace(key, result);
        return result;
    };

    bool changed = true;
    while (changed)
    {
        changed = false;

        // canonically reorder commuting moves so later same-layer turns meet
        for (size_t i = 1; i < out.size(); ++i)
        {
            if (commute(out[i - 1], out[i]) && MoveSortKey(out[i]) < MoveSortKey(out[i - 1]))
            {
                std::swap(out[i - 1], out[i]);
                changed = true;
            }
        }
        out = CollapseMoves(out);

        // A B A^-1 == B when A and B commute; remove the redundant conjugate if possible
        for (size_t i = 0; i + 2 < out.size(); ++i)
        {
            if (InversePair(out[i], out[i + 2]) && commute(out[i], out[i + 1]))
            {
                out.erase(out.begin() + static_cast<std::ptrdiff_t>(i + 2));
                out.erase(out.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                break;
            }
        }
        out = CollapseMoves(out);
    }
    return out;
}

} // namespace

std::vector<SliceMove> CollapseMoves(const std::vector<SliceMove>& moves)
{
    std::vector<SliceMove> out;
    out.reserve(moves.size());
    for (const SliceMove& m : moves)
    {
        if (!out.empty() && SameLayer(out.back(), m))
        {
            int q = TurnQuarters(out.back().Rotation) + TurnQuarters(m.Rotation);
            auto composed = TurnFromQuarters(q);
            if (composed.has_value())
            {
                out.back().Rotation = *composed;
            }
            else
            {
                out.pop_back();
            }
        }
        else
        {
            out.push_back(m);
        }
    }
    return out;
}

namespace
{

struct FaceletPosition
{
    Face Side;
    int row;
    int col;
};
struct WingPair
{
    FaceletPosition a;
    FaceletPosition b;
};
struct EdgeSlot
{
    WingPair w1;
    WingPair w2;
    Face faceA;
    Face faceB;
};

std::array<EdgeSlot, 12> MakeEdgeSlots(int n)
{
    const int t1 = 1;
    const int t2 = n - 2;
    return {{
        {{{Face::U, n - 1, t1}, {Face::F, 0, t1}}, {{Face::U, n - 1, t2}, {Face::F, 0, t2}}, Face::U, Face::F},
        {{{Face::U, n - 1 - t1, n - 1}, {Face::R, 0, t1}},
         {{Face::U, n - 1 - t2, n - 1}, {Face::R, 0, t2}},
         Face::U,
         Face::R},
        {{{Face::U, 0, n - 1 - t1}, {Face::B, 0, t1}}, {{Face::U, 0, n - 1 - t2}, {Face::B, 0, t2}}, Face::U, Face::B},
        {{{Face::U, t1, 0}, {Face::L, 0, t1}}, {{Face::U, t2, 0}, {Face::L, 0, t2}}, Face::U, Face::L},
        {{{Face::D, 0, t1}, {Face::F, n - 1, t1}}, {{Face::D, 0, t2}, {Face::F, n - 1, t2}}, Face::D, Face::F},
        {{{Face::D, t1, n - 1}, {Face::R, n - 1, n == 4 ? n - 1 - t1 : t1}},
         {{Face::D, t2, n - 1}, {Face::R, n - 1, n == 4 ? n - 1 - t2 : t2}},
         Face::D,
         Face::R},
        {{{Face::D, n - 1, n - 1 - t1}, {Face::B, n - 1, t1}},
         {{Face::D, n - 1, n - 1 - t2}, {Face::B, n - 1, t2}},
         Face::D,
         Face::B},
        {{{Face::D, n - 1 - t1, 0}, {Face::L, n - 1, n == 4 ? n - 1 - t1 : t1}},
         {{Face::D, n - 1 - t2, 0}, {Face::L, n - 1, n == 4 ? n - 1 - t2 : t2}},
         Face::D,
         Face::L},
        {{{Face::F, t1, n - 1}, {Face::R, t1, 0}}, {{Face::F, t2, n - 1}, {Face::R, t2, 0}}, Face::F, Face::R},
        {{{Face::R, t1, n - 1}, {Face::B, t1, 0}}, {{Face::R, t2, n - 1}, {Face::B, t2, 0}}, Face::R, Face::B},
        {{{Face::B, t1, n - 1}, {Face::L, t1, 0}}, {{Face::B, t2, n - 1}, {Face::L, t2, 0}}, Face::B, Face::L},
        {{{Face::L, t1, n - 1}, {Face::F, t1, 0}}, {{Face::L, t2, n - 1}, {Face::F, t2, 0}}, Face::L, Face::F},
    }};
}

struct Edge3x3
{
    const char* name;
    uint8_t colorA;       // face_a color (also the slot's primary face)
    uint8_t colorB;       // face_b color
    uint8_t primaryColor; // color that means "oriented"
};

constexpr Edge3x3 EDGES_3x3[12] = {
    {"UF", 0, 2, 0}, {"UR", 0, 1, 0}, {"UB", 0, 5, 0}, {"UL", 0, 4, 0}, {"DF", 3, 2, 3}, {"DR", 3, 1, 3},
    {"DB", 3, 5, 3}, {"DL", 3, 4, 3}, {"FR", 2, 1, 2}, {"FL", 2, 4, 2}, {"BR", 5, 1, 5}, {"BL", 5, 4, 5},
};

struct Corner3x3
{
    const char* name;
    uint8_t colorPrimary; // U or D color — primary sticker
    uint8_t colorB;
    uint8_t colorC;
    FaceletPosition s[3]; // sticker positions in canonical order (primary first)
};

std::array<Corner3x3, 8> MakeCornerSlots(int n)
{
    const int m = n - 1;
    return {{
        {"UFR", 0, 2, 1, {{Face::U, m, m}, {Face::F, 0, m}, {Face::R, 0, 0}}},
        {"URB", 0, 1, 5, {{Face::U, 0, m}, {Face::R, 0, m}, {Face::B, 0, 0}}},
        {"UBL", 0, 5, 4, {{Face::U, 0, 0}, {Face::B, 0, m}, {Face::L, 0, 0}}},
        {"ULF", 0, 4, 2, {{Face::U, m, 0}, {Face::L, 0, m}, {Face::F, 0, 0}}},
        {"DRF", 3, 1, 2, {{Face::D, 0, m}, {Face::R, m, 0}, {Face::F, m, m}}},
        {"DBR", 3, 5, 1, {{Face::D, m, m}, {Face::B, m, 0}, {Face::R, m, m}}},
        {"DLB", 3, 4, 5, {{Face::D, m, 0}, {Face::L, m, 0}, {Face::B, m, m}}},
        {"DFL", 3, 2, 4, {{Face::D, 0, 0}, {Face::F, m, 0}, {Face::L, m, m}}},
    }};
}

inline bool SameColorPair(uint8_t a1, uint8_t b1, uint8_t a2, uint8_t b2)
{
    return (a1 == a2 && b1 == b2) || (a1 == b2 && b1 == a2);
}

bool IdentifyEdgeAtSlot(const State& cube, const std::array<EdgeSlot, 12>& edgeSlots, int slotIdx, int& homeIdx,
                        int& orient)
{
    const EdgeSlot& slot = edgeSlots[slotIdx];
    const uint8_t ca = cube.GetSticker(static_cast<int>(slot.w1.a.Side), slot.w1.a.row, slot.w1.a.col);
    const uint8_t cb = cube.GetSticker(static_cast<int>(slot.w1.b.Side), slot.w1.b.row, slot.w1.b.col);
    for (int i = 0; i < 12; ++i)
    {
        if (SameColorPair(ca, cb, EDGES_3x3[i].colorA, EDGES_3x3[i].colorB))
        {
            homeIdx = i;
            const uint8_t homePrimary = EDGES_3x3[i].primaryColor;
            if (ca == homePrimary) orient = 0;
            else if (cb == homePrimary) orient = 1;
            else return false;
            return true;
        }
    }
    return false;
}

bool IdentifyCornerAtSlot(const State& cube, const std::array<Corner3x3, 8>& cornerSlots, int slotIdx, int& homeIdx,
                          int& orient)
{
    const Corner3x3& slot = cornerSlots[slotIdx];
    uint8_t cols[3];
    for (int k = 0; k < 3; ++k)
    {
        cols[k] = cube.GetSticker(static_cast<int>(slot.s[k].Side), slot.s[k].row, slot.s[k].col);
    }
    for (int i = 0; i < 8; ++i)
    {
        const uint8_t h[3] = {
            cornerSlots[i].colorPrimary,
            cornerSlots[i].colorB,
            cornerSlots[i].colorC,
        };
        int match = 0;
        bool used[3] = {false, false, false};
        for (int t = 0; t < 3; ++t)
        {
            for (int k = 0; k < 3; ++k)
            {
                if (!used[k] && cols[k] == h[t])
                {
                    used[k] = true;
                    ++match;
                    break;
                }
            }
        }
        if (match == 3)
        {
            homeIdx = i;
            for (int k = 0; k < 3; ++k)
            {
                if (cols[k] == cornerSlots[i].colorPrimary)
                {
                    orient = k;
                    return true;
                }
            }
            return false;
        }
    }
    return false;
}

// parity sign of a permutation via cycle decomposition; +1 for even, -1 for odd
int PermutationSign(const int* perm, int n)
{
    std::vector<bool> visited(n, false);
    int sign = 1;
    for (int i = 0; i < n; ++i)
    {
        if (visited[i]) continue;
        int length = 0, j = i;
        while (!visited[j])
        {
            visited[j] = true;
            j = perm[j];
            ++length;
        }
        if (length % 2 == 0) sign = -sign;
    }
    return sign;
}

struct Analysis
{
    int edgePerm[12];
    int edgeOrient[12];
    int cornerPerm[8];
    int cornerOrient[8];
    int edgeOrientSum;
    int cornerOrientSum;
    int edgePermSign;
    int cornerPermSign;
};

bool Analyze3x3State(const State& cube, Analysis& out)
{
    const auto edgeSlots = MakeEdgeSlots(cube.Order());
    const auto cornerSlots = MakeCornerSlots(cube.Order());
    for (int i = 0; i < 12; ++i)
    {
        if (!IdentifyEdgeAtSlot(cube, edgeSlots, i, out.edgePerm[i], out.edgeOrient[i]))
        {
            return false;
        }
    }
    for (int i = 0; i < 8; ++i)
    {
        if (!IdentifyCornerAtSlot(cube, cornerSlots, i, out.cornerPerm[i], out.cornerOrient[i]))
        {
            return false;
        }
    }
    int eoSum = 0, coSum = 0;
    for (int i = 0; i < 12; ++i) eoSum += out.edgeOrient[i];
    for (int i = 0; i < 8; ++i) coSum += out.cornerOrient[i];
    out.edgeOrientSum = eoSum % 2;
    out.cornerOrientSum = coSum % 3;
    out.edgePermSign = PermutationSign(out.edgePerm, 12);
    out.cornerPermSign = PermutationSign(out.cornerPerm, 8);
    return true;
}

} // namespace

ParityState DetectParity(const State& cube)
{
    Analysis a;
    if (!Analyze3x3State(cube, a))
    {
        return ParityState::Both;
    }
    const bool oll = (a.edgeOrientSum != 0);
    const bool pll = (a.edgePermSign != a.cornerPermSign);
    if (oll && pll) return ParityState::Both;
    if (oll) return ParityState::OLL;
    if (pll) return ParityState::PLL;
    return ParityState::Valid;
}

namespace
{
enum class CenterFlavor
{
    Fixed,
    XCenter,
    PlusCenter,
    Oblique
};
} // namespace

namespace
{

CenterFlavor ClassifyCenter(int dr, int dc)
{
    const int adr = std::abs(dr);
    const int adc = std::abs(dc);
    if (dr == 0 && dc == 0) return CenterFlavor::Fixed;
    if (dr == 0 || dc == 0) return CenterFlavor::PlusCenter;
    if (adr == adc) return CenterFlavor::XCenter;
    return CenterFlavor::Oblique;
}

int CenterRing(int dr, int dc)
{
    const int adr = std::abs(dr);
    const int adc = std::abs(dc);
    return (adr > adc) ? adr : adc;
}

} // namespace

// move construction helpers
namespace
{

inline SliceMove MkOuter(Face f, Turn t)
{
    return SliceMove{f, 0, 0, t};
}

inline SliceMove MkSlice(Face f, int d, Turn t)
{
    assert(d >= 1);
    return SliceMove{f, d - 1, d - 1, t};
}

inline SliceMove MkWide(Face f, int d, Turn t)
{
    assert(d >= 2);
    return SliceMove{f, 0, d - 1, t};
}

inline Turn InverseTurn(Turn t)
{
    if (t == Turn::CW) return Turn::CCW;
    if (t == Turn::CCW) return Turn::CW;
    return Turn::Half;
}

inline SliceMove InverseMove(const SliceMove& m)
{
    return SliceMove{m.Side, m.OuterDepth, m.InnerDepth, InverseTurn(m.Rotation)};
}

MoveStep InverseStep(const MoveStep& step)
{
    MoveStep out;
    out.reserve(step.size());
    for (auto it = step.rbegin(); it != step.rend(); ++it) out.push_back(InverseMove(*it));
    return out;
}

// commutator templates
MoveStep Xcenter3cycle(int d)
{
    return {
        MkSlice(Face::R, d, Turn::CCW), MkOuter(Face::D, Turn::CCW),   MkSlice(Face::B, d, Turn::CW),
        MkOuter(Face::D, Turn::CW),     MkSlice(Face::R, d, Turn::CW), MkOuter(Face::D, Turn::CCW),
        MkSlice(Face::B, d, Turn::CCW), MkOuter(Face::D, Turn::CW),
    };
}

MoveStep Wing3cycle(int d)
{
    assert(d >= 2);
    auto wideOrOuter = [](int width, Turn t) -> SliceMove {
        if (width == 1) return MkOuter(Face::R, t);
        return MkWide(Face::R, width, t);
    };
    return {
        wideOrOuter(d - 1, Turn::CW),  MkWide(Face::R, d, Turn::CCW), MkOuter(Face::U, Turn::CW),
        MkOuter(Face::R, Turn::CW),    MkOuter(Face::U, Turn::CCW),   MkWide(Face::R, d, Turn::CW),
        wideOrOuter(d - 1, Turn::CCW), MkOuter(Face::U, Turn::CW),    MkOuter(Face::R, Turn::CCW),
        MkOuter(Face::U, Turn::CCW),
    };
}

MoveStep PlusCenter3cycle(int d, int n)
{
    assert(n % 2 == 1);
    const int eDepth = (n + 1) / 2;
    return {
        MkOuter(Face::U, Turn::CW),           MkSlice(Face::R, d, Turn::CW),  MkSlice(Face::D, eDepth, Turn::Half),
        MkSlice(Face::R, d, Turn::CCW),       MkOuter(Face::U, Turn::CCW),    MkSlice(Face::R, d, Turn::CW),
        MkSlice(Face::D, eDepth, Turn::Half), MkSlice(Face::R, d, Turn::CCW),
    };
}

// oblique 3-cycle commutator derivation, self-verifying and returns the first one that works for that exact input
static MoveStep FindCleanObliqueAlg(int n, int a, int b, int chir)
{
    auto isTarget = [&](int dr, int dc) {
        if (ClassifyCenter(dr, dc) != CenterFlavor::Oblique) return false;
        int adr = std::abs(dr), adc = std::abs(dc);
        if (std::min(adr, adc) != a || std::max(adr, adc) != b) return false;
        return (dr * dc > 0 ? 1 : -1) == chir;
    };
    auto isClean = [&](const MoveStep& alg) {
        State c(n);
        ApplyMoveStep(c, alg);
        int targetMoved = 0, otherMoved = 0;
        for (int f = 0; f < FaceCount; ++f)
        {
            for (int r = 0; r < n; ++r)
            {
                for (int col = 0; col < n; ++col)
                {
                    if (c.GetSticker(f, r, col) == static_cast<uint8_t>(f)) continue;
                    const int dr = 2 * r - (n - 1), dc = 2 * col - (n - 1);
                    if (isTarget(dr, dc)) ++targetMoved;
                    else ++otherMoved;
                }
            }
        }
        return otherMoved == 0 && targetMoved == 3;
    };

    const Face faces[6] = {Face::U, Face::R, Face::F, Face::D, Face::L, Face::B};
    const Turn qturns[2] = {Turn::CW, Turn::CCW};
    for (Face fa : faces)
    {
        for (int da = 1; da <= n; ++da)
        {
            for (Turn ta : qturns)
            {
                const SliceMove A = MkSlice(fa, da, ta);
                const SliceMove Ainv = MkSlice(fa, da, ta == Turn::CW ? Turn::CCW : Turn::CW);
                for (Face fb : faces)
                {
                    if (fb == fa) continue;
                    for (Turn tb : qturns)
                    {
                        const Turn tbInv = (tb == Turn::CW) ? Turn::CCW : Turn::CW;
                        for (Face fc : faces)
                        {
                            for (int dc_ = 1; dc_ <= n; ++dc_)
                            {
                                for (Turn tc : qturns)
                                {
                                    const Turn tcInv = (tc == Turn::CW) ? Turn::CCW : Turn::CW;
                                    MoveStep candidate = {
                                        A,    MkOuter(fb, tb), MkSlice(fc, dc_, tc),    MkOuter(fb, tbInv),
                                        Ainv, MkOuter(fb, tb), MkSlice(fc, dc_, tcInv), MkOuter(fb, tbInv),
                                    };
                                    if (isClean(candidate)) return candidate;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return {}; // no clean candidate found in this family
}

MoveStep MiddleEdge3cycle(int n)
{
    assert(n % 2 == 1 && n >= 5);
    const int mDepth = (n + 1) / 2;
    return {
        MkSlice(Face::L, mDepth, Turn::Half), MkOuter(Face::U, Turn::CW),          MkSlice(Face::L, mDepth, Turn::CW),
        MkOuter(Face::U, Turn::Half),         MkSlice(Face::L, mDepth, Turn::CCW), MkOuter(Face::U, Turn::CW),
        MkSlice(Face::L, mDepth, Turn::Half),
    };
}

// flip exactly two middle-edges in place
MoveStep MiddleEdgeFlip(int n)
{
    assert(n % 2 == 1 && n >= 5);
    const int m = (n + 1) / 2;
    return {
        MkSlice(Face::R, m, Turn::CCW), MkOuter(Face::U, Turn::CW),     MkSlice(Face::R, m, Turn::CCW),
        MkOuter(Face::U, Turn::CW),     MkSlice(Face::R, m, Turn::CCW), MkOuter(Face::U, Turn::Half),
        MkSlice(Face::R, m, Turn::CW),  MkOuter(Face::U, Turn::CW),     MkSlice(Face::R, m, Turn::CW),
        MkOuter(Face::U, Turn::CW),     MkSlice(Face::R, m, Turn::CW),  MkOuter(Face::U, Turn::Half),
    };
}

MoveStep OllParityGeneral(int k)
{
    const int d = k + 1; // inner-slice depth
    return {
        MkSlice(Face::R, d, Turn::Half), MkOuter(Face::B, Turn::Half),  MkOuter(Face::U, Turn::Half),
        MkSlice(Face::L, d, Turn::CW),   MkOuter(Face::U, Turn::Half),  MkSlice(Face::R, d, Turn::CCW),
        MkOuter(Face::U, Turn::Half),    MkSlice(Face::R, d, Turn::CW), MkOuter(Face::U, Turn::Half),
        MkOuter(Face::F, Turn::Half),    MkSlice(Face::R, d, Turn::CW), MkOuter(Face::F, Turn::Half),
        MkSlice(Face::L, d, Turn::CCW),  MkOuter(Face::B, Turn::Half),  MkSlice(Face::R, d, Turn::Half),
    };
}

// enumerate all setup moves for a given N: outer faces + slice moves at each valid depth
// Slice depths: 2 through N-1 inclusive
std::vector<SliceMove> SetupMovesForN(int n)
{
    std::vector<SliceMove> moves;
    for (int f = 0; f < 6; ++f)
    {
        for (Turn t : {Turn::CW, Turn::Half, Turn::CCW})
        {
            moves.push_back(MkOuter(static_cast<Face>(f), t));
        }
    }
    for (int f = 0; f < 6; ++f)
    {
        for (int d = 2; d < n; ++d)
        {
            for (Turn t : {Turn::CW, Turn::Half, Turn::CCW})
            {
                moves.push_back(MkSlice(static_cast<Face>(f), d, t));
            }
        }
    }
    return moves;
}

// a position within an orbit packed into a single uint16
using Pos = uint16_t;
// encode (face, r, c) into a Pos
inline Pos EncodePos(int face, int r, int c)
{
    return static_cast<Pos>((face << 12) | ((r & 0x3F) << 6) | (c & 0x3F));
}

inline void DecodePos(Pos p, int& face, int& r, int& c)
{
    face = (p >> 12) & 0xF;
    r = (p >> 6) & 0x3F;
    c = p & 0x3F;
}

// maps each position in the orbit to an index
struct OrbitGraph
{
    std::vector<Pos> positions;
    std::vector<int> posToIndex;
    std::vector<SliceMove> moves;
    std::vector<std::vector<int>> perm;
    int nCube = 0;
};

// permutation + the concrete cube-move word realizing it
struct PermWord
{
    std::vector<int> perm;
    MoveStep word;
};

std::vector<int> IdentityPerm(int n)
{
    std::vector<int> p(n);
    for (int i = 0; i < n; ++i) p[i] = i;
    return p;
}

bool IsIdentityPerm(const std::vector<int>& p)
{
    for (int i = 0; i < static_cast<int>(p.size()); ++i)
        if (p[i] != i) return false;
    return true;
}

std::vector<int> ComposePerm(const std::vector<int>& a, const std::vector<int>& b)
{
    assert(a.size() == b.size());
    std::vector<int> out(a.size());
    for (int i = 0; i < static_cast<int>(a.size()); ++i) out[i] = b[a[i]];
    return out;
}

PermWord InversePw(const PermWord& a)
{
    PermWord out;
    out.perm.resize(a.perm.size());
    for (int i = 0; i < static_cast<int>(a.perm.size()); ++i) out.perm[a.perm[i]] = i;
    out.word = InverseStep(a.word);
    return out;
}

// build the OrbitGraph using supplied move set
OrbitGraph BuildOrbitGraph(int n, const std::function<bool(int, int, int)>& inOrbit,
                           const std::vector<SliceMove>& moveSet)
{
    OrbitGraph g;
    g.nCube = n;
    g.posToIndex.assign(1 << 16, -1);
    g.moves = moveSet;
    for (int f = 0; f < FaceCount; ++f)
    {
        for (int r = 0; r < n; ++r)
        {
            for (int c = 0; c < n; ++c)
            {
                if (inOrbit(f, r, c))
                {
                    Pos p = EncodePos(f, r, c);
                    g.posToIndex[p] = static_cast<int>(g.positions.size());
                    g.positions.push_back(p);
                }
            }
        }
    }
    const int M = static_cast<int>(moveSet.size());
    g.perm.assign(g.positions.size(), std::vector<int>(M, -1));
    assert(g.positions.size() < 128);
    for (int mIdx = 0; mIdx < M; ++mIdx)
    {
        State marker(n);
        for (int i = 0; i < static_cast<int>(g.positions.size()); ++i)
        {
            int f, r, c;
            DecodePos(g.positions[i], f, r, c);
            marker.SetSticker(f, r, c, static_cast<uint8_t>(128 + i));
        }
        ApplyMove(marker, moveSet[mIdx]);
        for (int dstIdx = 0; dstIdx < static_cast<int>(g.positions.size()); ++dstIdx)
        {
            int f, r, c;
            DecodePos(g.positions[dstIdx], f, r, c);
            uint8_t v = marker.GetSticker(f, r, c);
            if (v >= 128 && v < 128 + (int)g.positions.size())
            {
                int src = v - 128;
                g.perm[src][mIdx] = dstIdx;
            }
        }
    }
    return g;
}

// discover the 3 action slots of commutator on a solved cube
struct ActionTriple
{
    int sourceIdx;
    int targetIdx;
    int bufferIdx;
};

ActionTriple DetectActionTriple(const OrbitGraph& g, const MoveStep& alg)
{
    const int n = g.nCube;
    State marker(n);
    assert(g.positions.size() < 128);
    for (int i = 0; i < static_cast<int>(g.positions.size()); ++i)
    {
        int f, r, c;
        DecodePos(g.positions[i], f, r, c);
        marker.SetSticker(f, r, c, static_cast<uint8_t>(128 + i));
    }
    for (const auto& m : alg) ApplyMove(marker, m);
    std::vector<int> permAfter(g.positions.size(), -1);
    for (int dst = 0; dst < static_cast<int>(g.positions.size()); ++dst)
    {
        int f, r, c;
        DecodePos(g.positions[dst], f, r, c);
        uint8_t v = marker.GetSticker(f, r, c);
        if (v >= 128 && v < 128 + (int)g.positions.size()) permAfter[dst] = v - 128;
    }
    ActionTriple t{-1, -1, -1};
    for (int i = 0; i < static_cast<int>(g.positions.size()); ++i)
    {
        if (permAfter[i] == -1 || permAfter[i] == i) continue;
        int a = i;
        int b = -1, c = -1;
        for (int j = 0; j < static_cast<int>(g.positions.size()); ++j)
        {
            if (permAfter[j] == a)
            {
                b = j;
                break;
            }
        }
        if (b < 0 || b == a) continue;
        for (int j = 0; j < static_cast<int>(g.positions.size()); ++j)
        {
            if (permAfter[j] == b)
            {
                c = j;
                break;
            }
        }
        if (c < 0 || c == a || c == b) continue;
        if (permAfter[a] != c) continue;
        t = {a, b, c};
        break;
    }
    return t;
}

// home face for a position in an orbit (the face on which its color matches)
inline int HomeFace(const OrbitGraph& g, int posIdx)
{
    int f, r, c;
    DecodePos(g.positions[posIdx], f, r, c);
    (void)r;
    (void)c;
    return f;
}

// read the color currently at orbit position pos_idx
inline uint8_t ReadColor(const State& cube, const OrbitGraph& g, int posIdx)
{
    int f, r, c;
    DecodePos(g.positions[posIdx], f, r, c);
    return cube.GetSticker(f, r, c);
}

struct DisjointSet
{
    std::vector<int> parent, rank;
    explicit DisjointSet(int n) : parent(n), rank(n, 0)
    {
        for (int i = 0; i < n; ++i) parent[i] = i;
    }
    int find(int x) { return parent[x] == x ? x : parent[x] = find(parent[x]); }
    bool unite(int a, int b)
    {
        a = find(a);
        b = find(b);
        if (a == b) return false;
        if (rank[a] < rank[b]) std::swap(a, b);
        parent[b] = a;
        if (rank[a] == rank[b]) ++rank[a];
        return true;
    }
};

std::vector<PermWord> BuildConnectedConjugateGenerators(const OrbitGraph& g, const MoveStep& alg,
                                                        const ActionTriple& slots)
{
    const int K = static_cast<int>(g.positions.size());
    const int64_t stateCount = static_cast<int64_t>(K) * K * K;
    auto encode = [K](int a, int b, int c) {
        return static_cast<int64_t>(a) * K * K + static_cast<int64_t>(b) * K + c;
    };
    auto decode = [K](int64_t s, int& a, int& b, int& c) {
        c = static_cast<int>(s % K);
        b = static_cast<int>((s / K) % K);
        a = static_cast<int>(s / (K * K));
    };

    const int64_t start = encode(slots.sourceIdx, slots.targetIdx, slots.bufferIdx);
    std::vector<int64_t> parent(stateCount, -1);
    std::vector<int16_t> parentMove(stateCount, -1);
    std::vector<uint8_t> visited(stateCount, 0);
    std::queue<int64_t> q;
    q.push(start);
    visited[start] = 1;

    DisjointSet dsu(K);
    int components = K;
    std::vector<PermWord> generators;

    auto maybeAdd = [&](int64_t state) {
        int a, b, c;
        decode(state, a, b, c);
        bool joined = false;
        if (dsu.unite(a, b))
        {
            --components;
            joined = true;
        }
        if (dsu.unite(b, c))
        {
            --components;
            joined = true;
        }
        if (!joined && !generators.empty()) return;

        MoveStep setupFromBase;
        for (int64_t s = state; s != start; s = parent[s]) setupFromBase.push_back(g.moves[parentMove[s]]);
        std::reverse(setupFromBase.begin(), setupFromBase.end());

        MoveStep word = InverseStep(setupFromBase);
        word.insert(word.end(), alg.begin(), alg.end());
        word.insert(word.end(), setupFromBase.begin(), setupFromBase.end());
        std::vector<int> perm = IdentityPerm(K);
        perm[a] = b;
        perm[b] = c;
        perm[c] = a;
        generators.push_back(PermWord{std::move(perm), std::move(word)});
    };

    maybeAdd(start);
    while (!q.empty() && components > 1)
    {
        const int64_t cur = q.front();
        q.pop();
        int a, b, c;
        decode(cur, a, b, c);
        for (int m = 0; m < static_cast<int>(g.moves.size()); ++m)
        {
            const int na = g.perm[a][m], nb = g.perm[b][m], nc = g.perm[c][m];
            if (na < 0 || nb < 0 || nc < 0) continue;
            const int64_t ns = encode(na, nb, nc);
            if (visited[ns]) continue;
            visited[ns] = 1;
            parent[ns] = cur;
            parentMove[ns] = static_cast<int16_t>(m);
            q.push(ns);
            maybeAdd(ns);
        }
    }
    if (components != 1)
    {

        return {};
    }
    return generators;
}

bool PermutationIsEven(const std::vector<int>& p)
{
    int inversions = 0;
    for (int i = 0; i < static_cast<int>(p.size()); ++i)
        for (int j = i + 1; j < static_cast<int>(p.size()); ++j)
            if (p[i] > p[j]) inversions ^= 1;
    return inversions == 0;
}

std::array<int, 3> TripleFrom3cycle(const std::vector<int>& p)
{
    for (int a = 0; a < static_cast<int>(p.size()); ++a)
    {
        if (p[a] == a) continue;
        const int b = p[a], c = p[b];
        if (a != b && b != c && c != a && p[c] == a) return {a, b, c};
    }
    return {-1, -1, -1};
}

std::vector<int> Make3cyclePerm(int K, int a, int b, int c)
{
    std::vector<int> p = IdentityPerm(K);
    p[a] = b;
    p[b] = c;
    p[c] = a;
    return p;
}

// decompose an arbitrary even permutation into oriented 3-cycles
bool DecomposeEvenPerm(const std::vector<int>& target, std::vector<std::array<int, 3>>& cyclesOut)
{
    if (!PermutationIsEven(target)) return false;
    const int K = static_cast<int>(target.size());
    std::vector<int> rem = target;
    std::vector<std::vector<int>> eliminators;

    for (;;)
    {
        int a = -1;
        for (int i = 0; i < K; ++i)
        {
            if (rem[i] != i && rem[rem[i]] != i)
            {
                a = i;
                break;
            }
        }
        if (a < 0) break;
        const int b = rem[a], c = rem[b];
        auto op = Make3cyclePerm(K, b, a, c);
        rem = ComposePerm(rem, op);
        eliminators.push_back(std::move(op));
    }

    std::vector<std::pair<int, int>> swaps;
    std::vector<uint8_t> used(K, 0);
    for (int i = 0; i < K; ++i)
    {
        if (rem[i] == i || used[i]) continue;
        const int j = rem[i];
        if (j < 0 || j >= K || rem[j] != i) return false;
        used[i] = used[j] = 1;
        swaps.emplace_back(i, j);
    }
    if (swaps.size() % 2 != 0) return false;

    for (size_t s = 0; s < swaps.size(); s += 2)
    {
        const int v[4] = {swaps[s].first, swaps[s].second, swaps[s + 1].first, swaps[s + 1].second};
        std::vector<int> pairPerm = IdentityPerm(K);
        pairPerm[v[0]] = v[1];
        pairPerm[v[1]] = v[0];
        pairPerm[v[2]] = v[3];
        pairPerm[v[3]] = v[2];
        std::vector<std::vector<int>> candidates;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                if (j != i)
                    for (int k = 0; k < 4; ++k)
                        if (k != i && k != j) candidates.push_back(Make3cyclePerm(K, v[i], v[j], v[k]));
        bool found = false;
        for (const auto& a : candidates)
        {
            for (const auto& b : candidates)
            {
                if (IsIdentityPerm(ComposePerm(ComposePerm(pairPerm, a), b)))
                {
                    rem = ComposePerm(ComposePerm(rem, a), b);
                    eliminators.push_back(a);
                    eliminators.push_back(b);
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (!found) return false;
    }
    if (!IsIdentityPerm(rem)) return false;

    cyclesOut.clear();
    for (auto it = eliminators.rbegin(); it != eliminators.rend(); ++it)
    {
        auto inv = InversePw(PermWord{*it, {}}).perm;
        auto triple = TripleFrom3cycle(inv);
        if (triple[0] < 0) return false;
        cyclesOut.push_back(triple);
    }

    std::vector<int> check = IdentityPerm(K);
    for (const auto& t : cyclesOut) check = ComposePerm(check, Make3cyclePerm(K, t[0], t[1], t[2]));
    return check == target;
}

// turn the abstract 3-cycle decomposition into cube moves
bool Realize3cycles(const std::vector<PermWord>& generators, const std::vector<std::array<int, 3>>& requested,
                    MoveStep& solutionOut)
{
    if (generators.empty()) return false;
    const int K = static_cast<int>(generators.front().perm.size());
    std::vector<PermWord> alpha;
    alpha.reserve(generators.size() * 2);
    for (const auto& g : generators)
    {
        alpha.push_back(g);
        alpha.push_back(InversePw(g));
    }
    const auto base = TripleFrom3cycle(generators.front().perm);
    if (base[0] < 0) return false;
    auto encode = [K](int a, int b, int c) {
        return static_cast<int64_t>(a) * K * K + static_cast<int64_t>(b) * K + c;
    };
    auto decode = [K](int64_t s, int& a, int& b, int& c) {
        c = static_cast<int>(s % K);
        b = static_cast<int>((s / K) % K);
        a = static_cast<int>(s / (K * K));
    };
    const int64_t count = static_cast<int64_t>(K) * K * K;
    const int64_t start = encode(base[0], base[1], base[2]);
    std::vector<int64_t> parent(count, -1);
    std::vector<int16_t> parentGen(count, -1);
    std::vector<uint8_t> visited(count, 0), needed(count, 0);
    int remaining = 0;
    for (const auto& t : requested)
    {
        const int64_t s = encode(t[0], t[1], t[2]);
        if (!needed[s])
        {
            needed[s] = 1;
            ++remaining;
        }
    }
    std::queue<int64_t> q;
    q.push(start);
    visited[start] = 1;
    if (needed[start])
    {
        needed[start] = 0;
        --remaining;
    }
    while (!q.empty() && remaining > 0)
    {
        const int64_t cur = q.front();
        q.pop();
        int a, b, c;
        decode(cur, a, b, c);
        for (int gi = 0; gi < static_cast<int>(alpha.size()); ++gi)
        {
            const auto& p = alpha[gi].perm;
            const int64_t ns = encode(p[a], p[b], p[c]);
            if (visited[ns]) continue;
            visited[ns] = 1;
            parent[ns] = cur;
            parentGen[ns] = static_cast<int16_t>(gi);
            q.push(ns);
            if (needed[ns])
            {
                needed[ns] = 0;
                --remaining;
            }
        }
    }
    if (remaining != 0) return false;

    solutionOut.clear();
    for (const auto& t : requested)
    {
        const int64_t goal = encode(t[0], t[1], t[2]);
        std::vector<int> path;
        for (int64_t s = goal; s != start; s = parent[s])
        {
            if (s < 0 || parent[s] < 0) return false;
            path.push_back(parentGen[s]);
        }
        std::reverse(path.begin(), path.end());
        MoveStep conjugator;
        for (int gi : path) conjugator.insert(conjugator.end(), alpha[gi].word.begin(), alpha[gi].word.end());
        MoveStep inv = InverseStep(conjugator);
        solutionOut.insert(solutionOut.end(), inv.begin(), inv.end());
        solutionOut.insert(solutionOut.end(), generators.front().word.begin(), generators.front().word.end());
        solutionOut.insert(solutionOut.end(), conjugator.begin(), conjugator.end());
    }
    return true;
}

// complete center-orbit solver
// note that same-colored centers are interchangeable, so if
// arbitrary assignment is odd, we will swap the homes of two equal-colored
// stickers to resolve parity if needed, without affecting the solve
bool SolveCenterOrbitExact(State& cube, std::vector<MoveStep>& outSeq, const OrbitGraph& g, const MoveStep& alg,
                           uint8_t faceMask = 0x3f)
{
    const int K = static_cast<int>(g.positions.size());
    if (K == 0) return true;
    const ActionTriple slots = DetectActionTriple(g, alg);
    if (slots.sourceIdx < 0) return false;

    std::array<std::vector<int>, FaceCount> currentByColor;
    std::array<std::vector<int>, FaceCount> homesByColor;
    for (int i = 0; i < K; ++i)
    {
        const int color = static_cast<int>(ReadColor(cube, g, i));
        if (color < 0 || color >= FaceCount) return false;
        currentByColor[color].push_back(i);
        homesByColor[HomeFace(g, i)].push_back(i);
    }

    std::vector<int> target = IdentityPerm(K); // current position -> assigned home
    std::vector<int> remainingCurrent, remainingHomes;
    for (int color = 0; color < FaceCount; ++color)
    {
        if (currentByColor[color].size() != homesByColor[color].size()) return false;
        if ((faceMask & (1 << color)) == 0)
        {
            remainingCurrent.insert(remainingCurrent.end(), currentByColor[color].begin(), currentByColor[color].end());
            remainingHomes.insert(remainingHomes.end(), homesByColor[color].begin(), homesByColor[color].end());
            continue;
        }
        for (int j = 0; j < static_cast<int>(currentByColor[color].size()); ++j)
            target[currentByColor[color][j]] = homesByColor[color][j];
    }
    if (remainingCurrent.size() != remainingHomes.size()) return false;
    for (size_t i = 0; i < remainingCurrent.size(); ++i) target[remainingCurrent[i]] = remainingHomes[i];
    if (!PermutationIsEven(target))
    {
        bool fixed = false;
        if (remainingCurrent.size() >= 2)
        {
            std::swap(target[remainingCurrent[0]], target[remainingCurrent[1]]);
            fixed = true;
        }
        for (int color = 0; color < FaceCount && !fixed; ++color)
        {
            if (currentByColor[color].size() < 2) continue;
            std::swap(target[currentByColor[color][0]], target[currentByColor[color][1]]);
            fixed = true;
        }
        if (!fixed || !PermutationIsEven(target)) return false;
    }

    const auto generators = BuildConnectedConjugateGenerators(g, alg, slots);
    if (generators.empty()) return false;
    std::vector<std::array<int, 3>> cycles;
    if (!DecomposeEvenPerm(target, cycles)) return false;
    MoveStep solution;
    if (!Realize3cycles(generators, cycles, solution)) return false;

    State trial = cube;
    ApplyMoveStep(trial, solution);
    for (int i = 0; i < K; ++i)
        if ((faceMask & (1 << HomeFace(g, i))) && ReadColor(trial, g, i) != static_cast<uint8_t>(HomeFace(g, i)))
            return false;
    cube = std::move(trial);
    if (!solution.empty()) outSeq.push_back(std::move(solution));
    return true;
}

std::vector<int> ConjugateComponentVertices(const OrbitGraph& g, const ActionTriple& slots)
{
    const int K = static_cast<int>(g.positions.size());
    auto encode = [K](int a, int b, int c) {
        return static_cast<int64_t>(a) * K * K + static_cast<int64_t>(b) * K + c;
    };
    auto decode = [K](int64_t s, int& a, int& b, int& c) {
        c = static_cast<int>(s % K);
        b = static_cast<int>((s / K) % K);
        a = static_cast<int>(s / (K * K));
    };
    const int64_t count = static_cast<int64_t>(K) * K * K;
    const int64_t start = encode(slots.sourceIdx, slots.targetIdx, slots.bufferIdx);
    std::vector<uint8_t> visited(count, 0), vertexSeen(K, 0);
    std::queue<int64_t> q;
    visited[start] = 1;
    q.push(start);
    while (!q.empty())
    {
        const int64_t cur = q.front();
        q.pop();
        int a, b, c;
        decode(cur, a, b, c);
        vertexSeen[a] = vertexSeen[b] = vertexSeen[c] = 1;
        for (int m = 0; m < static_cast<int>(g.moves.size()); ++m)
        {
            const int na = g.perm[a][m], nb = g.perm[b][m], nc = g.perm[c][m];
            if (na < 0 || nb < 0 || nc < 0) continue;
            const int64_t ns = encode(na, nb, nc);
            if (!visited[ns])
            {
                visited[ns] = 1;
                q.push(ns);
            }
        }
    }
    std::vector<int> out;
    for (int i = 0; i < K; ++i)
        if (vertexSeen[i]) out.push_back(i);
    return out;
}

OrbitGraph InducedOrbitGraph(const OrbitGraph& full, const std::vector<int>& vertices)
{
    OrbitGraph sub;
    sub.nCube = full.nCube;
    sub.moves = full.moves;
    sub.posToIndex.assign(1 << 16, -1);
    std::vector<int> oldToNew(full.positions.size(), -1);
    for (int old : vertices)
    {
        const int ni = static_cast<int>(sub.positions.size());
        oldToNew[old] = ni;
        sub.positions.push_back(full.positions[old]);
        sub.posToIndex[full.positions[old]] = ni;
    }
    sub.perm.assign(vertices.size(), std::vector<int>(sub.moves.size(), -1));
    for (int ni = 0; ni < static_cast<int>(vertices.size()); ++ni)
    {
        const int old = vertices[ni];
        for (int m = 0; m < static_cast<int>(sub.moves.size()); ++m)
        {
            const int oldDst = full.perm[old][m];
            if (oldDst >= 0) sub.perm[ni][m] = oldToNew[oldDst];
        }
    }
    return sub;
}

} // namespace

std::vector<MoveStep> SolveCentersSelected(State& cube, uint8_t faceMask)
{
    const int n = cube.Order();
    assert(n >= 3);
    std::vector<MoveStep> out;
    if (n == 3) return out;

    // max slice depth for X-center rings
    const int maxRegularD = (n % 2 == 1) ? (n - 1) / 2 : n / 2;

    // x-centers, ring-by-ring (smallest ring first for smaller orbit size)
    for (int d = maxRegularD; d >= 2; --d)
    {
        const int ring = n + 1 - 2 * d;
        auto inOrbit = [ring, n](int, int r, int c) {
            const int dr = 2 * r - (n - 1);
            const int dc = 2 * c - (n - 1);
            if (ClassifyCenter(dr, dc) != CenterFlavor::XCenter) return false;
            return CenterRing(dr, dc) == ring;
        };
        OrbitGraph g = BuildOrbitGraph(n, inOrbit, SetupMovesForN(n));
        if (!SolveCenterOrbitExact(cube, out, g, Xcenter3cycle(d), faceMask)) return out;
    }
    // plus-centers (odd N)
    if (n % 2 == 1)
    {
        for (int d = maxRegularD; d >= 2; --d)
        {
            const int ring = n + 1 - 2 * d;
            auto inOrbit = [ring, n](int, int r, int c) {
                const int dr = 2 * r - (n - 1);
                const int dc = 2 * c - (n - 1);
                if (ClassifyCenter(dr, dc) != CenterFlavor::PlusCenter) return false;
                return CenterRing(dr, dc) == ring;
            };
            OrbitGraph g = BuildOrbitGraph(n, inOrbit, SetupMovesForN(n));
            if (!SolveCenterOrbitExact(cube, out, g, PlusCenter3cycle(d, n), faceMask)) return out;
        }
    }

    // true obliques may split into independent components even when their
    // coordinate taxonomy puts them in one ring; discover those components
    // from the conjugate action and solve each one independently
    if (n >= 6)
    {
        std::vector<int> obliqueRings;
        for (int r = 1; r <= n - 2; ++r)
        {
            for (int c = 1; c <= n - 2; ++c)
            {
                const int dr = 2 * r - (n - 1);
                const int dc = 2 * c - (n - 1);
                if (ClassifyCenter(dr, dc) != CenterFlavor::Oblique) continue;
                int ringKey = std::abs(dr) * 100 + std::abs(dc);
                if (std::abs(dr) > std::abs(dc)) ringKey = std::abs(dc) * 100 + std::abs(dr);
                if (std::find(obliqueRings.begin(), obliqueRings.end(), ringKey) == obliqueRings.end())
                {
                    obliqueRings.push_back(ringKey);
                }
            }
        }
        for (int ringKey : obliqueRings)
        {
            int a = ringKey / 100;
            int b = ringKey % 100;
            auto inOrbit = [n, a, b](int, int r, int c) {
                const int dr = 2 * r - (n - 1);
                const int dc = 2 * c - (n - 1);
                if (ClassifyCenter(dr, dc) != CenterFlavor::Oblique) return false;
                int adr = std::abs(dr), adc = std::abs(dc);
                return std::min(adr, adc) == a && std::max(adr, adc) == b;
            };
            OrbitGraph full = BuildOrbitGraph(n, inOrbit, SetupMovesForN(n));

            std::vector<uint8_t> covered(full.positions.size(), 0);
            std::unordered_set<std::string> componentKeys;
            for (int chir : {1, -1})
            {
                MoveStep alg = FindCleanObliqueAlg(n, a, b, chir);
                if (alg.empty()) continue;
                ActionTriple action = DetectActionTriple(full, alg);
                if (action.sourceIdx < 0) continue;
                std::vector<int> vertices = ConjugateComponentVertices(full, action);
                std::sort(vertices.begin(), vertices.end());
                std::string key;
                for (int v : vertices) key.push_back(static_cast<char>(v));
                if (!componentKeys.insert(key).second) continue;
                OrbitGraph component = InducedOrbitGraph(full, vertices);
                if (!SolveCenterOrbitExact(cube, out, component, alg, faceMask)) return out;
                for (int v : vertices) covered[v] = 1;
            }
            if (std::find(covered.begin(), covered.end(), 0) != covered.end()) return out;
        }
    }

    return out;
}

std::vector<MoveStep> SolveCenters(State& cube)
{
    return SolveCentersSelected(cube, 0x3f);
}

bool CentersReduced(const State& cube)
{
    const int n = cube.Order();
    for (int f = 0; f < FaceCount; ++f)
        for (int r = 1; r < n - 1; ++r)
            for (int c = 1; c < n - 1; ++c)
                if (cube.GetSticker(f, r, c) != static_cast<uint8_t>(f)) return false;
    return true;
}

// Edge geometry for general N.
namespace
{

struct EdgeSticker
{
    Face Side;
    int row;
    int col;
};
struct EdgeSlotGeom
{
    Face faceA, faceB;
    std::function<EdgeSticker(int t, int n)> stickersA;
    std::function<EdgeSticker(int t, int n)> stickersB;
};

std::vector<EdgeSlotGeom> BuildEdgeGeom()
{
    std::vector<EdgeSlotGeom> out(12);
    out[0] = {Face::U, Face::F, [](int t, int n) { return EdgeSticker{Face::U, n - 1, t}; },
              [](int t, int) { return EdgeSticker{Face::F, 0, t}; }};
    out[1] = {Face::U, Face::R, [](int t, int n) { return EdgeSticker{Face::U, n - 1 - t, n - 1}; },
              [](int t, int) { return EdgeSticker{Face::R, 0, t}; }};
    out[2] = {Face::U, Face::B, [](int t, int n) { return EdgeSticker{Face::U, 0, n - 1 - t}; },
              [](int t, int) { return EdgeSticker{Face::B, 0, t}; }};
    out[3] = {Face::U, Face::L, [](int t, int) { return EdgeSticker{Face::U, t, 0}; },
              [](int t, int) { return EdgeSticker{Face::L, 0, t}; }};
    out[4] = {Face::D, Face::F, [](int t, int) { return EdgeSticker{Face::D, 0, t}; },
              [](int t, int n) { return EdgeSticker{Face::F, n - 1, t}; }};
    out[5] = {Face::D, Face::R, [](int t, int n) { return EdgeSticker{Face::D, t, n - 1}; },
              [](int t, int n) { return EdgeSticker{Face::R, n - 1, t}; }};
    out[6] = {Face::D, Face::B, [](int t, int n) { return EdgeSticker{Face::D, n - 1, n - 1 - t}; },
              [](int t, int n) { return EdgeSticker{Face::B, n - 1, t}; }};
    out[7] = {Face::D, Face::L, [](int t, int n) { return EdgeSticker{Face::D, n - 1 - t, 0}; },
              [](int t, int n) { return EdgeSticker{Face::L, n - 1, t}; }};
    out[8] = {Face::F, Face::R, [](int t, int n) { return EdgeSticker{Face::F, t, n - 1}; },
              [](int t, int) { return EdgeSticker{Face::R, t, 0}; }};
    out[9] = {Face::R, Face::B, [](int t, int n) { return EdgeSticker{Face::R, t, n - 1}; },
              [](int t, int) { return EdgeSticker{Face::B, t, 0}; }};
    out[10] = {Face::B, Face::L, [](int t, int n) { return EdgeSticker{Face::B, t, n - 1}; },
               [](int t, int) { return EdgeSticker{Face::L, t, 0}; }};
    out[11] = {Face::L, Face::F, [](int t, int n) { return EdgeSticker{Face::L, t, n - 1}; },
               [](int t, int) { return EdgeSticker{Face::F, t, 0}; }};
    return out;
}
inline std::pair<int, int> WingTs(int w, int n)
{
    return {w + 1, n - 2 - w};
}

// piece-tracking wing solver

struct PieceOrbit
{
    int n;
    int t1, t2;
    std::vector<EdgeSticker> faceAStickers;
    std::vector<EdgeSticker> faceBStickers;
    std::vector<int> homeFaceA;
    std::vector<int> homeFaceB;
    std::vector<SliceMove> moves;
    std::vector<std::vector<int>> perm;
    // perm_flip[i][m] = true iff applying move m to a piece at slot i lands
    // its face_b sticker where face_a's tracked position is; needed for middle edges only
    std::vector<std::vector<bool>> permFlip;
};

// build the piece orbit for wing_index w on N-cube (using canonical edge order)
PieceOrbit BuildWingPieceOrbit(int n, int w, const std::vector<EdgeSlotGeom>& edges,
                               const std::vector<SliceMove>& moveSet)
{
    PieceOrbit p;
    p.n = n;
    p.t1 = w + 1;
    p.t2 = n - 2 - w;
    for (int e = 0; e < 12; ++e)
    {
        for (int side = 0; side < 2; ++side)
        {
            int t = (side == 0) ? p.t1 : p.t2;
            EdgeSticker sa = edges[e].stickersA(t, n);
            EdgeSticker sb = edges[e].stickersB(t, n);
            p.faceAStickers.push_back(sa);
            p.faceBStickers.push_back(sb);
            p.homeFaceA.push_back(static_cast<int>(edges[e].faceA));
            p.homeFaceB.push_back(static_cast<int>(edges[e].faceB));
        }
    }
    const int K = static_cast<int>(p.faceAStickers.size());
    p.moves = moveSet;
    const int M = static_cast<int>(moveSet.size());
    p.perm.assign(K, std::vector<int>(M, -1));
    p.permFlip.assign(K, std::vector<bool>(M, false));
    constexpr int kFaceAOffset = 32;
    constexpr int kFaceBOffset = 160;
    for (int mIdx = 0; mIdx < M; ++mIdx)
    {
        State marker(n);
        for (int i = 0; i < K; ++i)
        {
            const EdgeSticker& sa = p.faceAStickers[i];
            const EdgeSticker& sb = p.faceBStickers[i];
            marker.SetSticker(static_cast<int>(sa.Side), sa.row, sa.col, static_cast<uint8_t>(kFaceAOffset + i));
            marker.SetSticker(static_cast<int>(sb.Side), sb.row, sb.col, static_cast<uint8_t>(kFaceBOffset + i));
        }
        ApplyMove(marker, moveSet[mIdx]);
        for (int dstIdx = 0; dstIdx < K; ++dstIdx)
        {
            const EdgeSticker& sa = p.faceAStickers[dstIdx];
            uint8_t v = marker.GetSticker(static_cast<int>(sa.Side), sa.row, sa.col);
            int src;
            bool flipped = false;
            if (v >= kFaceAOffset && v < kFaceAOffset + K) src = v - kFaceAOffset;
            else if (v >= kFaceBOffset && v < kFaceBOffset + K)
            {
                src = v - kFaceBOffset;
                flipped = true;
            }
            else continue;
            p.permFlip[src][mIdx] = flipped;
            p.perm[src][mIdx] = dstIdx;
        }
    }

    return p;
}

// build the piece orbit for the 12 middle-edge pieces (odd N only, one sticker-pair per edge at t=(n-1)/2)
PieceOrbit BuildMiddlePieceOrbit(int n, const std::vector<EdgeSlotGeom>& edges, const std::vector<SliceMove>& moveSet)
{
    PieceOrbit p;
    p.n = n;
    const int tMid = (n - 1) / 2;
    p.t1 = tMid;
    p.t2 = tMid;
    for (int e = 0; e < 12; ++e)
    {
        EdgeSticker sa = edges[e].stickersA(tMid, n);
        EdgeSticker sb = edges[e].stickersB(tMid, n);
        p.faceAStickers.push_back(sa);
        p.faceBStickers.push_back(sb);
        p.homeFaceA.push_back(static_cast<int>(edges[e].faceA));
        p.homeFaceB.push_back(static_cast<int>(edges[e].faceB));
    }
    const int K = static_cast<int>(p.faceAStickers.size());
    p.moves = moveSet;
    const int M = static_cast<int>(moveSet.size());
    p.perm.assign(K, std::vector<int>(M, -1));
    p.permFlip.assign(K, std::vector<bool>(M, false));
    constexpr int kFaceAOffset = 32;
    constexpr int kFaceBOffset = 160;
    for (int mIdx = 0; mIdx < M; ++mIdx)
    {
        State marker(n);
        for (int i = 0; i < K; ++i)
        {
            const EdgeSticker& sa = p.faceAStickers[i];
            const EdgeSticker& sb = p.faceBStickers[i];
            marker.SetSticker(static_cast<int>(sa.Side), sa.row, sa.col, static_cast<uint8_t>(kFaceAOffset + i));
            marker.SetSticker(static_cast<int>(sb.Side), sb.row, sb.col, static_cast<uint8_t>(kFaceBOffset + i));
        }
        ApplyMove(marker, moveSet[mIdx]);
        for (int dstIdx = 0; dstIdx < K; ++dstIdx)
        {
            const EdgeSticker& sa = p.faceAStickers[dstIdx];
            uint8_t v = marker.GetSticker(static_cast<int>(sa.Side), sa.row, sa.col);
            int src;
            bool flipped;
            if (v >= kFaceAOffset && v < kFaceAOffset + K)
            {
                src = v - kFaceAOffset;
                flipped = false;
            }
            else if (v >= kFaceBOffset && v < kFaceBOffset + K)
            {
                src = v - kFaceBOffset;
                flipped = true;
            }
            else continue;
            p.permFlip[src][mIdx] = flipped;
            p.perm[src][mIdx] = dstIdx;
        }
    }
    return p;
}

inline std::pair<uint8_t, uint8_t> ReadPair(const State& cube, const PieceOrbit& p, int i)
{
    const EdgeSticker& sa = p.faceAStickers[i];
    const EdgeSticker& sb = p.faceBStickers[i];
    uint8_t ca = cube.GetSticker(static_cast<int>(sa.Side), sa.row, sa.col);
    uint8_t cb = cube.GetSticker(static_cast<int>(sb.Side), sb.row, sb.col);
    if (ca > cb) std::swap(ca, cb);
    return {ca, cb};
}

inline std::pair<uint8_t, uint8_t> HomePair(const PieceOrbit& p, int i)
{
    uint8_t a = static_cast<uint8_t>(p.homeFaceA[i]);
    uint8_t b = static_cast<uint8_t>(p.homeFaceB[i]);
    if (a > b) std::swap(a, b);
    return {a, b};
}

inline bool PieceSolved(const State& cube, const PieceOrbit& p, int i)
{
    return ReadPair(cube, p, i) == HomePair(p, i);
}

// orientation-sensitive check
inline bool PieceSolvedExact(const State& cube, const PieceOrbit& p, int i)
{
    const EdgeSticker& sa = p.faceAStickers[i];
    const EdgeSticker& sb = p.faceBStickers[i];
    uint8_t ca = cube.GetSticker(static_cast<int>(sa.Side), sa.row, sa.col);
    uint8_t cb = cube.GetSticker(static_cast<int>(sb.Side), sb.row, sb.col);
    return ca == static_cast<uint8_t>(p.homeFaceA[i]) && cb == static_cast<uint8_t>(p.homeFaceB[i]);
}

// detect the 3-cycle action of the alg on the piece orbit
struct WingActionTriple
{
    int sourceIdx;
    int targetIdx;
    int bufferIdx;
};
WingActionTriple DetectWingAction(const PieceOrbit& p, const MoveStep& alg)
{
    const int n = p.n;
    const int K = static_cast<int>(p.faceAStickers.size());
    constexpr int kFaceAOffset = 32;
    constexpr int kFaceBOffset = 160;
    State marker(n);
    for (int i = 0; i < K; ++i)
    {
        const EdgeSticker& sa = p.faceAStickers[i];
        const EdgeSticker& sb = p.faceBStickers[i];
        marker.SetSticker(static_cast<int>(sa.Side), sa.row, sa.col, static_cast<uint8_t>(kFaceAOffset + i));
        marker.SetSticker(static_cast<int>(sb.Side), sb.row, sb.col, static_cast<uint8_t>(kFaceBOffset + i));
    }
    for (const auto& m : alg) ApplyMove(marker, m);
    std::vector<int> permAfter(K, -1);
    for (int dst = 0; dst < K; ++dst)
    {
        const EdgeSticker& sa = p.faceAStickers[dst];
        uint8_t v = marker.GetSticker(static_cast<int>(sa.Side), sa.row, sa.col);
        int src;
        if (v >= kFaceAOffset && v < kFaceAOffset + K) src = v - kFaceAOffset;
        else if (v >= kFaceBOffset && v < kFaceBOffset + K) src = v - kFaceBOffset;
        else continue;
        permAfter[dst] = src;
    }
    WingActionTriple t{-1, -1, -1};
    for (int i = 0; i < K; ++i)
    {
        if (permAfter[i] == i) continue;
        int a = i, b = -1, c = -1;
        for (int j = 0; j < K; ++j)
            if (permAfter[j] == a)
            {
                b = j;
                break;
            }
        if (b < 0 || b == a) continue;
        for (int j = 0; j < K; ++j)
            if (permAfter[j] == b)
            {
                c = j;
                break;
            }
        if (c < 0 || c == a || c == b) continue;
        if (permAfter[a] != c) continue;
        t = {a, b, c};
        break;
    }
    return t;
}

std::vector<PermWord> BuildConnectedWingGenerators(const PieceOrbit& p, const MoveStep& alg,
                                                   const WingActionTriple& slots)
{
    const int K = static_cast<int>(p.faceAStickers.size());
    auto encode = [K](int a, int b, int c) {
        return static_cast<int64_t>(a) * K * K + static_cast<int64_t>(b) * K + c;
    };
    auto decode = [K](int64_t s, int& a, int& b, int& c) {
        c = static_cast<int>(s % K);
        b = static_cast<int>((s / K) % K);
        a = static_cast<int>(s / (K * K));
    };
    const int64_t count = static_cast<int64_t>(K) * K * K;
    const int64_t start = encode(slots.sourceIdx, slots.targetIdx, slots.bufferIdx);
    std::vector<int64_t> parent(count, -1);
    std::vector<int16_t> parentMove(count, -1);
    std::vector<uint8_t> visited(count, 0);
    std::queue<int64_t> q;
    visited[start] = 1;
    q.push(start);
    DisjointSet dsu(K);
    int components = K;
    std::vector<PermWord> generators;

    auto addIfConnecting = [&](int64_t state) {
        int a, b, c;
        decode(state, a, b, c);
        bool joined = false;
        if (dsu.unite(a, b))
        {
            --components;
            joined = true;
        }
        if (dsu.unite(b, c))
        {
            --components;
            joined = true;
        }
        if (!joined && !generators.empty()) return;
        MoveStep setup;
        for (int64_t s = state; s != start; s = parent[s]) setup.push_back(p.moves[parentMove[s]]);
        std::reverse(setup.begin(), setup.end());
        MoveStep word = InverseStep(setup);
        word.insert(word.end(), alg.begin(), alg.end());
        word.insert(word.end(), setup.begin(), setup.end());
        generators.push_back(PermWord{Make3cyclePerm(K, a, b, c), std::move(word)});
    };
    addIfConnecting(start);
    while (!q.empty() && components > 1)
    {
        const int64_t cur = q.front();
        q.pop();
        int a, b, c;
        decode(cur, a, b, c);
        for (int m = 0; m < static_cast<int>(p.moves.size()); ++m)
        {
            const int na = p.perm[a][m], nb = p.perm[b][m], nc = p.perm[c][m];
            if (na < 0 || nb < 0 || nc < 0) continue;
            const int64_t ns = encode(na, nb, nc);
            if (visited[ns]) continue;
            visited[ns] = 1;
            parent[ns] = cur;
            parentMove[ns] = static_cast<int16_t>(m);
            q.push(ns);
            addIfConnecting(ns);
        }
    }
    if (components != 1)
    {

        return {};
    }
    return generators;
}

bool ApplyWingPairFlipExact(State& cube, std::vector<MoveStep>& out, const PieceOrbit& p, const MoveStep& flipAlg,
                            int edgeId);

bool SolveWingOrbitExact(State& cube, std::vector<MoveStep>& outSeq, const PieceOrbit& p, const MoveStep& alg,
                         uint16_t edgeMask = 0x0fff)
{
    const int K = static_cast<int>(p.faceAStickers.size());
    const WingActionTriple slots = DetectWingAction(p, alg);
    if (slots.sourceIdx < 0)
    {

        return false;
    }

    std::map<std::pair<uint8_t, uint8_t>, std::vector<int>> current, homes;
    for (int i = 0; i < K; ++i)
    {
        current[ReadPair(cube, p, i)].push_back(i);
        homes[HomePair(p, i)].push_back(i);
    }
    if (current.size() != homes.size())
    {

        return false;
    }
    std::vector<int> target = IdentityPerm(K);
    std::vector<uint8_t> usedCurrent(K, 0), usedHome(K, 0);
    for (int edge = 0; edge < 12; ++edge)
    {
        if ((edgeMask & (1 << edge)) == 0) continue;
        const auto pair = HomePair(p, 2 * edge);
        auto from = current.find(pair), to = homes.find(pair);
        if (from == current.end() || to == homes.end() || from->second.size() != to->second.size()) return false;
        for (size_t j = 0; j < from->second.size(); ++j)
        {
            target[from->second[j]] = to->second[j];
            usedCurrent[from->second[j]] = 1;
            usedHome[to->second[j]] = 1;
        }
    }
    std::vector<int> remainingCurrent, remainingHomes;
    for (int i = 0; i < K; ++i)
    {
        if (!usedCurrent[i]) remainingCurrent.push_back(i);
        if (!usedHome[i]) remainingHomes.push_back(i);
    }
    if (remainingCurrent.size() != remainingHomes.size()) return false;
    for (size_t i = 0; i < remainingCurrent.size(); ++i) target[remainingCurrent[i]] = remainingHomes[i];
    if (!PermutationIsEven(target))
    {
        bool fixed = false;
        if (remainingCurrent.size() >= 2)
        {
            std::swap(target[remainingCurrent[0]], target[remainingCurrent[1]]);
            fixed = true;
        }
        for (const auto& [pair, positions] : current)
        {
            (void)pair;
            if (fixed) break;
            if (positions.size() < 2) continue;
            std::swap(target[positions[0]], target[positions[1]]);
            fixed = true;
            break;
        }
        if (!fixed || !PermutationIsEven(target)) return false;
    }

    const auto generators = BuildConnectedWingGenerators(p, alg, slots);
    if (generators.empty()) return false;
    std::vector<std::array<int, 3>> cycles;
    if (!DecomposeEvenPerm(target, cycles)) return false;
    MoveStep solution;
    if (!Realize3cycles(generators, cycles, solution)) return false;

    State trial = cube;
    ApplyMoveStep(trial, solution);
    for (int edge = 0; edge < 12; ++edge)
        if (edgeMask & (1 << edge))
            for (int side = 0; side < 2; ++side)
                if (!PieceSolved(trial, p, 2 * edge + side)) return false;
    std::vector<MoveStep> corrections;
    if (edgeMask != 0x0fff)
    {
        for (int edge = 0; edge < 12; ++edge)
        {
            if ((edgeMask & (1 << edge)) == 0) continue;
            if (PieceSolvedExact(trial, p, 2 * edge) && PieceSolvedExact(trial, p, 2 * edge + 1)) continue;
            if (!ApplyWingPairFlipExact(trial, corrections, p, OllParityGeneral(p.t1), edge)) return false;
            if (!PieceSolvedExact(trial, p, 2 * edge) || !PieceSolvedExact(trial, p, 2 * edge + 1)) return false;
        }
    }

    cube = std::move(trial);
    if (!solution.empty()) outSeq.push_back(std::move(solution));
    outSeq.insert(outSeq.end(), std::make_move_iterator(corrections.begin()),
                  std::make_move_iterator(corrections.end()));
    return true;
}

bool ApplyWingPermutationExact(State& cube, std::vector<MoveStep>& outSeq, const PieceOrbit& p, const MoveStep& alg,
                               const std::vector<int>& target)
{
    const int K = static_cast<int>(p.faceAStickers.size());
    if (static_cast<int>(target.size()) != K || !PermutationIsEven(target)) return false;
    const WingActionTriple slots = DetectWingAction(p, alg);
    if (slots.sourceIdx < 0) return false;
    const auto generators = BuildConnectedWingGenerators(p, alg, slots);
    if (generators.empty()) return false;
    std::vector<std::array<int, 3>> cycles;
    if (!DecomposeEvenPerm(target, cycles)) return false;
    MoveStep solution;
    if (!Realize3cycles(generators, cycles, solution)) return false;
    State trial = cube;
    ApplyMoveStep(trial, solution);
    cube = std::move(trial);
    if (!solution.empty()) outSeq.push_back(std::move(solution));
    return true;
}

bool FindCompletePairSetup(const PieceOrbit& p, int fromA, int fromB, int toA, int toB, MoveStep& setupOut);

std::pair<uint8_t, uint8_t> ReadOrderedPair(const State& cube, const PieceOrbit& p, int slot)
{
    const auto& a = p.faceAStickers[slot];
    const auto& b = p.faceBStickers[slot];
    return {cube.GetSticker((int)a.Side, a.row, a.col), cube.GetSticker((int)b.Side, b.row, b.col)};
}

bool ApplyWingPairFlipExact(State& cube, std::vector<MoveStep>& out, const PieceOrbit& p, const MoveStep& flipAlg,
                            int edgeId)
{
    const int a = 2 * edgeId, b = a + 1;
    const auto beforeA = ReadOrderedPair(cube, p, a);
    const auto beforeB = ReadOrderedPair(cube, p, b);
    if (beforeA != beforeB) return false;
    MoveStep setup;
    if (!FindCompletePairSetup(p, a, b, 0, 1, setup))
    {
        if (!FindCompletePairSetup(p, a, b, 1, 0, setup)) return false;
    }
    MoveStep operation = setup;
    operation.insert(operation.end(), flipAlg.begin(), flipAlg.end());
    MoveStep undo = InverseStep(setup);
    operation.insert(operation.end(), undo.begin(), undo.end());
    State trial = cube;
    ApplyMoveStep(trial, operation);
    const auto afterA = ReadOrderedPair(trial, p, a);
    const auto afterB = ReadOrderedPair(trial, p, b);
    if (afterA != afterB || afterA.first != beforeA.second || afterA.second != beforeA.first) return false;
    cube = std::move(trial);
    out.push_back(std::move(operation));
    return true;
}

bool FindCompletePairSetup(const PieceOrbit& p, int fromA, int fromB, int toA, int toB, MoveStep& setupOut)
{
    const int K = static_cast<int>(p.faceAStickers.size());
    const int start = fromA * K + fromB;
    const int goal = toA * K + toB;
    if (start == goal)
    {
        setupOut.clear();
        return true;
    }
    std::vector<int> parent(K * K, -1), parentMove(K * K, -1);
    std::vector<uint8_t> visited(K * K, 0);
    std::queue<int> q;
    visited[start] = 1;
    q.push(start);
    while (!q.empty())
    {
        const int cur = q.front();
        q.pop();
        const int a = cur / K, b = cur % K;
        for (int m = 0; m < static_cast<int>(p.moves.size()); ++m)
        {
            const int na = p.perm[a][m], nb = p.perm[b][m];
            if (na < 0 || nb < 0) continue;
            const int ns = na * K + nb;
            if (visited[ns]) continue;
            visited[ns] = 1;
            parent[ns] = cur;
            parentMove[ns] = m;
            if (ns == goal)
            {
                setupOut.clear();
                for (int s = goal; s != start; s = parent[s]) setupOut.push_back(p.moves[parentMove[s]]);
                std::reverse(setupOut.begin(), setupOut.end());
                return true;
            }
            q.push(ns);
        }
    }
    return false;
}

bool SolveMiddleEdgesExact(State& cube, std::vector<MoveStep>& out, const PieceOrbit& p, const MoveStep& cycleAlg,
                           const MoveStep& flipAlg)
{
    const int K = static_cast<int>(p.faceAStickers.size());
    const State before = cube;
    std::vector<MoveStep> generated;
    const WingActionTriple slots = DetectWingAction(p, cycleAlg);
    if (slots.sourceIdx < 0) return false;

    std::map<std::pair<uint8_t, uint8_t>, int> homeForPair;
    for (int i = 0; i < K; ++i) homeForPair[HomePair(p, i)] = i;
    if (static_cast<int>(homeForPair.size()) != K) return false;
    std::vector<int> target(K, -1);
    for (int i = 0; i < K; ++i)
    {
        auto it = homeForPair.find(ReadPair(cube, p, i));
        if (it == homeForPair.end()) return false;
        target[i] = it->second;
    }
    if (!PermutationIsEven(target)) return false;

    const auto generators = BuildConnectedWingGenerators(p, cycleAlg, slots);
    if (generators.empty()) return false;
    std::vector<std::array<int, 3>> cycles;
    if (!DecomposeEvenPerm(target, cycles)) return false;
    MoveStep positionSolution;
    if (!Realize3cycles(generators, cycles, positionSolution)) return false;
    State trial = cube;
    ApplyMoveStep(trial, positionSolution);
    for (int i = 0; i < K; ++i)
        if (!PieceSolved(trial, p, i)) return false;
    if (!positionSolution.empty()) generated.push_back(std::move(positionSolution));

    std::vector<int> flipped;
    for (int i = 0; i < K; ++i)
        if (!PieceSolvedExact(trial, p, i)) flipped.push_back(i);
    if (flipped.size() % 2 != 0) return false;
    while (!flipped.empty())
    {
        const int a = flipped[0], b = flipped[1];
        MoveStep setup;
        if (!FindCompletePairSetup(p, a, b, 0, 2, setup))
        {
            if (!FindCompletePairSetup(p, a, b, 2, 0, setup)) return false;
        }
        MoveStep operation = setup;
        operation.insert(operation.end(), flipAlg.begin(), flipAlg.end());
        MoveStep undo = InverseStep(setup);
        operation.insert(operation.end(), undo.begin(), undo.end());
        ApplyMoveStep(trial, operation);
        generated.push_back(std::move(operation));

        flipped.clear();
        for (int i = 0; i < K; ++i)
        {
            if (!PieceSolved(trial, p, i)) return false;
            if (!PieceSolvedExact(trial, p, i)) flipped.push_back(i);
        }
    }

    // pure middle-edge stage must leave every other sticker byte-for-byte unchanged
    std::vector<uint8_t> isMiddle(before.StickerCount(), 0);
    for (int i = 0; i < K; ++i)
    {
        const auto& a = p.faceAStickers[i];
        const auto& b = p.faceBStickers[i];
        const int fs = p.n * p.n;
        isMiddle[(int)a.Side * fs + a.row * p.n + a.col] = 1;
        isMiddle[(int)b.Side * fs + b.row * p.n + b.col] = 1;
    }
    for (int i = 0; i < before.StickerCount(); ++i)
        if (!isMiddle[i] && trial.Data()[i] != before.Data()[i]) return false;
    cube = std::move(trial);
    out.insert(out.end(), std::make_move_iterator(generated.begin()), std::make_move_iterator(generated.end()));
    return true;
}

void SolveMiddlesGeneral(State& cube, std::vector<MoveStep>& out, int n, const std::vector<EdgeSlotGeom>& edges)
{
    if (n % 2 == 0) return;
    PieceOrbit p = BuildMiddlePieceOrbit(n, edges, SetupMovesForN(n));
    SolveMiddleEdgesExact(cube, out, p, MiddleEdge3cycle(n), MiddleEdgeFlip(n));
}

} // namespace

std::vector<MoveStep> SolveEdges(State& cube)
{
    const int n = cube.Order();
    assert(n >= 3);
    if (n == 3) return {};

    std::vector<MoveStep> out;
    const auto edges = BuildEdgeGeom();

    const int wingPairs = (n - 2) / 2;
    for (int w = 0; w < wingPairs; ++w)
    {
        PieceOrbit orbit = BuildWingPieceOrbit(n, w, edges, SetupMovesForN(n));
        if (!SolveWingOrbitExact(cube, out, orbit, Wing3cycle(w + 2))) return out;
    }

    // align every inner wing orbit to orbit 0 using pure two-wing flip conjugate
    PieceOrbit reference = BuildWingPieceOrbit(n, 0, edges, SetupMovesForN(n));
    for (int w = 1; w < wingPairs; ++w)
    {
        PieceOrbit orbit = BuildWingPieceOrbit(n, w, edges, SetupMovesForN(n));
        for (int e = 0; e < 12; ++e)
        {
            const auto ref = ReadOrderedPair(cube, reference, 2 * e);
            const auto cur = ReadOrderedPair(cube, orbit, 2 * e);
            if (cur == ref) continue;
            if (cur.first != ref.second || cur.second != ref.first) return out;
            if (!ApplyWingPairFlipExact(cube, out, orbit, OllParityGeneral(w + 1), e)) return out;
        }
    }

    ParityState parity = DetectParity(cube);
    if (parity == ParityState::OLL || parity == ParityState::Both)
    {
        for (int w = 0; w < wingPairs; ++w)
        {
            PieceOrbit orbit = BuildWingPieceOrbit(n, w, edges, SetupMovesForN(n));
            if (!ApplyWingPairFlipExact(cube, out, orbit, OllParityGeneral(w + 1), 0)) return out;
        }
    }

    parity = DetectParity(cube);
    if (parity == ParityState::PLL || parity == ParityState::Both)
    {
        for (int w = 0; w < wingPairs; ++w)
        {
            PieceOrbit orbit = BuildWingPieceOrbit(n, w, edges, SetupMovesForN(n));
            std::vector<int> swapGroups = IdentityPerm(24);
            std::swap(swapGroups[0], swapGroups[2]);
            std::swap(swapGroups[1], swapGroups[3]);
            if (!ApplyWingPermutationExact(cube, out, orbit, Wing3cycle(w + 2), swapGroups)) return out;
        }
    }

    return out;
}

std::vector<MoveStep> SolveCrossWingsSelected(State& cube, uint16_t edgeMask)
{
    const int n = cube.Order();
    std::vector<MoveStep> out;
    if (n < 4) return out;
    const auto edges = BuildEdgeGeom();
    const int wingPairs = (n - 2) / 2;
    for (int w = 0; w < wingPairs; ++w)
    {
        PieceOrbit orbit = BuildWingPieceOrbit(n, w, edges, SetupMovesForN(n));
        if (!SolveWingOrbitExact(cube, out, orbit, Wing3cycle(w + 2), edgeMask)) return out;
    }
    return out;
}

bool WingsReduced(const State& cube)
{
    const int n = cube.Order();
    if (n == 3) return true;
    const auto edges = BuildEdgeGeom();
    const int wingPairs = (n - 2) / 2;
    for (int e = 0; e < 12; ++e)
    {
        std::pair<uint8_t, uint8_t> reference{uint8_t{255}, uint8_t{255}};
        for (int w = 0; w < wingPairs; ++w)
        {
            const auto [t1, t2] = WingTs(w, n);
            const auto a1 = edges[e].stickersA(t1, n);
            const auto b1 = edges[e].stickersB(t1, n);
            const auto a2 = edges[e].stickersA(t2, n);
            const auto b2 = edges[e].stickersB(t2, n);
            const std::pair<uint8_t, uint8_t> p1{cube.GetSticker((int)a1.Side, a1.row, a1.col),
                                                 cube.GetSticker((int)b1.Side, b1.row, b1.col)};
            const std::pair<uint8_t, uint8_t> p2{cube.GetSticker((int)a2.Side, a2.row, a2.col),
                                                 cube.GetSticker((int)b2.Side, b2.row, b2.col)};
            if (p1 != p2 || p1.first == p1.second) return false;
            if (w == 0) reference = p1;
            else if (p1 != reference) return false;
        }
    }
    return true;
}

std::vector<MoveStep> SolveMiddleEdges(State& cube)
{
    const int n = cube.Order();
    if (n % 2 == 0) return {};
    std::vector<MoveStep> out;
    const auto edges = BuildEdgeGeom();
    SolveMiddlesGeneral(cube, out, n, edges);
    return out;
}

constexpr std::array<uint8_t, 6> ToReductionColor{4, 1, 3, 0, 5, 2};

Sticker Facelet(int order, int face, int row, int col)
{
    Sticker sticker;
    const int last = order - 1;
    int x = 0, y = 0, z = 0, axis = 0, sign = 0;
    switch (face)
    {
    case 0:
        x = col;
        y = last;
        z = row;
        axis = 1;
        sign = 1;
        break; // U
    case 1:
        x = last;
        y = last - row;
        z = last - col;
        axis = 0;
        sign = 1;
        break; // R
    case 2:
        x = col;
        y = last - row;
        z = last;
        axis = 2;
        sign = 1;
        break; // F
    case 3:
        x = col;
        y = 0;
        z = last - row;
        axis = 1;
        sign = -1;
        break; // D
    case 4:
        x = 0;
        y = last - row;
        z = col;
        axis = 0;
        sign = -1;
        break; // L
    default:
        x = last - col;
        y = last - row;
        z = 0;
        axis = 2;
        sign = -1;
        break; // B
    }
    sticker.Position = {static_cast<int8_t>(x), static_cast<int8_t>(y), static_cast<int8_t>(z)};
    sticker.Axis = static_cast<uint8_t>(axis);
    sticker.Sign = static_cast<int8_t>(sign);
    return sticker;
}

State FromCube(const Cube& state)
{
    const int order = state.Order();
    State result(order);
    for (int face = 0; face < 6; ++face)
        for (int row = 0; row < order; ++row)
            for (int col = 0; col < order; ++col)
            {
                const uint8_t color = state.Colors()[state.Geometry().Index(Facelet(order, face, row, col))];
                result.SetSticker(face, row, col, ToReductionColor[color]);
            }
    return result;
}

std::vector<Burnside::Move> ToBurnsideMoves(const SliceMove& step, int order)
{
    const int face = static_cast<int>(step.Side);
    const int axis = face == 0 || face == 3 ? 1 : face == 1 || face == 4 ? 0 : 2;
    const bool positive = face == 0 || face == 1 || face == 2;
    const int clockwiseSign = face == 0 || face == 4 || face == 5 ? 1 : -1;
    int a = -1, b = -1;
    for (int tangent = 0; tangent < 3; ++tangent)
        if (tangent != axis)
        {
            if (a < 0) a = tangent;
            else b = tangent;
        }
    const int turns = step.Rotation == Turn::Half ? 2 : step.Rotation == Turn::CW ? clockwiseSign : -clockwiseSign;
    std::vector<Burnside::Move> result;
    for (int depth = step.OuterDepth; depth <= step.InnerDepth; ++depth)
        result.push_back({axis, positive ? order - 1 - depth : depth, a, b, turns});
    return result;
}

bool FixedCentersSolved(const Cube& state)
{
    const int order = state.Order(), cell = (order * order) / 2;
    for (int face = 0; face < 6; ++face)
        if (state.Colors()[face * order * order + cell] != face) return false;
    return true;
}

bool NormalizeFixedCenters(const Cube& state, Cube& normalized, std::vector<Burnside::Move>& prefix)
{
    if (state.Order() % 2 == 0 || FixedCentersSolved(state)) return true;
    struct Node
    {
        Cube State;
        std::vector<Burnside::Move> Path;
    };
    std::queue<Node> queue;
    queue.push({state, {}});
    std::unordered_set<std::string> visited;
    const int order = state.Order(), cell = order * order / 2;
    while (!queue.empty())
    {
        Node node = std::move(queue.front());
        queue.pop();
        std::string key;
        for (int face = 0; face < 6; ++face)
            key.push_back(static_cast<char>(node.State.Colors()[face * order * order + cell]));
        if (!visited.insert(key).second) continue;
        if (FixedCentersSolved(node.State))
        {
            normalized = std::move(node.State);
            prefix = std::move(node.Path);
            return true;
        }
        for (int axis = 0; axis < 3; ++axis)
        {
            int a = (axis + 1) % 3, b = (axis + 2) % 3;
            if (a > b) std::swap(a, b);
            Node next = node;
            for (int layer = 0; layer < order; ++layer)
            {
                const Burnside::Move move{axis, layer, a, b, 1};
                next.State.Apply(move);
                next.Path.push_back(move);
            }
            queue.push(std::move(next));
        }
    }
    return false;
}

bool CentersSelected(const State& cube, uint8_t faceMask)
{
    const int order = cube.Order();
    for (int face = 0; face < 6; ++face)
        if (faceMask & (1 << face))
            for (int row = 1; row < order - 1; ++row)
                for (int col = 1; col < order - 1; ++col)
                    if (cube.GetSticker(face, row, col) != face) return false;
    return true;
}

bool CrossWingsSelected(const State& cube, uint16_t edgeMask)
{
    const int n = cube.Order();
    for (int t = 1; t < n - 1; ++t)
    {
        if ((edgeMask & (1 << 4)) && (cube.GetSticker(3, 0, t) != 3 || cube.GetSticker(2, n - 1, t) != 2)) return false;
        if ((edgeMask & (1 << 5)) && (cube.GetSticker(3, t, n - 1) != 3 || cube.GetSticker(1, n - 1, t) != 1))
            return false;
        if ((edgeMask & (1 << 6)) && (cube.GetSticker(3, n - 1, n - 1 - t) != 3 || cube.GetSticker(5, n - 1, t) != 5))
            return false;
        if ((edgeMask & (1 << 7)) && (cube.GetSticker(3, n - 1 - t, 0) != 3 || cube.GetSticker(4, n - 1, t) != 4))
            return false;
    }
    return true;
}

SolveResult FinishAs3x3(State& state, const SolveOptions& options, std::vector<SliceMove>& moves)
{
    // Sample a paired wing instead of the fixed middle edge on odd orders.
    // The remaining middle-edge orbit is solved after the outer-face stage.
    constexpr std::array<uint8_t, 6> fromReductionColor{3, 1, 5, 2, 0, 4};
    Cube reduced(3);
    auto colors = reduced.Colors();
    const int sample[]{0, 1, state.Order() - 1};
    for (int face = 0; face < 6; ++face)
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
            {
                const auto color = row == 1 && col == 1 ? static_cast<uint8_t>(face)
                                                        : state.GetSticker(face, sample[row], sample[col]);
                colors[reduced.Geometry().Index(Facelet(3, face, row, col))] = fromReductionColor[color];
            }
    reduced.SetColors(colors);
    auto answer = SolveTwoPhase(reduced, options);
    if (!answer.Success()) return answer;
    for (Move& move : answer.Moves)
    {
        const bool positive = move.Layer == 2;
        const Face face = move.Axis == 0   ? (positive ? Face::R : Face::L)
                          : move.Axis == 1 ? (positive ? Face::U : Face::D)
                                           : (positive ? Face::F : Face::B);
        const int clockwiseSign = face == Face::U || face == Face::L || face == Face::B ? 1 : -1;
        const Turn turn = move.Turns == 2 ? Turn::Half : move.Turns == clockwiseSign ? Turn::CW : Turn::CCW;
        const SliceMove step{face, 0, 0, turn};
        ApplyMove(state, step);
        moves.push_back(step);
        if (positive) move.Layer = state.Order() - 1;
    }
    return answer;
}

} // namespace ReductionDetail

SolveResult SolveReduction(const Cube& state, const SolveOptions& options)
{
    using namespace ReductionDetail;
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    SolveResult result;
    result.Method = "NxN center/edge reduction";
    const auto finish = [&](SolveStatus status, const std::string& message) {
        result.Status = status;
        result.Message = message;
        result.Milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        return result;
    };
    const auto stopped = [&]() { return options.Cancel && options.Cancel->load(); };
    const auto timedOut = [&]() { return Clock::now() - start >= options.TimeLimit; };
    const auto report = [&](int stage) {
        if (options.ProgressStage) options.ProgressStage->store(stage, std::memory_order_relaxed);
    };
    if (stopped()) return finish(SolveStatus::Cancelled, "Cancelled");
    if (state.IsSolved()) return finish(SolveStatus::AlreadySolved, "Already solved");
    if (state.Order() < 4 || state.Order() > MaxOrder)
        return finish(SolveStatus::UnsupportedState, "Reduction supports orders 4 through 16");
    Cube normalized = state;
    std::vector<Move> prefix;
    if (!NormalizeFixedCenters(state, normalized, prefix))
        return finish(SolveStatus::UnsupportedState, "Invalid fixed-center orientation");
    State cube = FromCube(normalized);
    std::vector<SliceMove> raw;
    const auto append = [&](const std::vector<MoveStep>& steps) {
        for (const auto& step : steps) raw.insert(raw.end(), step.begin(), step.end());
    };
    report(1);
    append(SolveCenters(cube));
    if (!CentersReduced(cube)) return finish(SolveStatus::DepthLimit, "Could not solve the centers");
    if (stopped()) return finish(SolveStatus::Cancelled, "Cancelled");
    if (timedOut()) return finish(SolveStatus::BudgetExceeded, "Reduction time limit exceeded");
    report(2);
    append(SolveEdges(cube));
    if (!CentersReduced(cube) || !WingsReduced(cube) || DetectParity(cube) != ParityState::Valid)
        return finish(SolveStatus::DepthLimit, "Could not pair the edges");
    if (stopped()) return finish(SolveStatus::Cancelled, "Cancelled");
    if (timedOut()) return finish(SolveStatus::BudgetExceeded, "Reduction time limit exceeded");
    SolveOptions remaining = options;
    remaining.TimeLimit -= std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start);
    report(3);
    const auto last = FinishAs3x3(cube, remaining, raw);
    result.Visited = last.Visited;
    if (!last.Success()) return finish(last.Status, last.Message);
    report(4);
    append(SolveMiddleEdges(cube));
    if (!cube.IsSolved()) return finish(SolveStatus::DepthLimit, "Could not finish the reduced cube");
    if (stopped()) return finish(SolveStatus::Cancelled, "Cancelled");
    if (timedOut()) return finish(SolveStatus::BudgetExceeded, "Reduction time limit exceeded");
    report(5);
    auto optimized = OptimizeMoves(raw, state.Order());
    State replay = FromCube(normalized);
    for (const auto& step : optimized) ApplyMove(replay, step);
    if (!replay.IsSolved()) optimized = CollapseMoves(raw);
    result.Moves = std::move(prefix);
    for (const auto& step : optimized)
    {
        if (stopped()) return finish(SolveStatus::Cancelled, "Cancelled");
        if (timedOut()) return finish(SolveStatus::BudgetExceeded, "Reduction time limit exceeded");
        auto moves = ToBurnsideMoves(step, state.Order());
        result.Moves.insert(result.Moves.end(), moves.begin(), moves.end());
    }
    result.Moves = Reduce(result.Moves, state.Order());
    report(6);
    Cube verify = state;
    verify.Apply(result.Moves);
    if (!verify.IsSolved()) throw std::logic_error("Reduction solution verification failed");
    return finish(SolveStatus::Solved, "Verified NxN reduction solution");
}

} // namespace Burnside
