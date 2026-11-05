#include "Cube.h"
#include <algorithm>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <atomic>
#include <chrono>

namespace Burnside
{
namespace
{
int Mod4(int n) { return (n % 4 + 4) % 4; }
}

Move Move::Inverse() const { Move m = *this; m.Turns = -m.Turns; return m; }
bool Move::SameSlice(const Move& m) const
{
    return Axis == m.Axis && Layer == m.Layer && A == m.A && B == m.B;
}
bool Move::operator==(const Move& m) const { return SameSlice(m) && Turns == m.Turns; }
std::string Move::ToString(int order) const
{
    std::ostringstream s;
    s << "XYZ"[Axis] << '[' << Layer+1 << '/' << order << "] " << (Turns > 0 ? "+" : "") << Turns*90 << " deg";
    return s.str();
}

Layout::Layout(int order) : m_Order(order), m_FaceSize(static_cast<size_t>(order)*order)
{
    if (order < 2 || order > MaxOrder) throw std::invalid_argument("Cube order must be 2..16");
    m_Stickers.reserve(FaceCount*m_FaceSize);
    for (int axis = 0; axis < AxisCount; ++axis)
        for (int sign : {-1, 1})
            for (size_t cell = 0; cell < m_FaceSize; ++cell)
            {
                Sticker s;
                s.Axis = static_cast<uint8_t>(axis);
                s.Sign = static_cast<int8_t>(sign);
                size_t digits = cell;
                for (int d = 0; d < AxisCount; ++d)
                {
                    if (d == axis) s.Position[d] = static_cast<int8_t>(sign > 0 ? order-1 : 0);
                    else { s.Position[d] = static_cast<int8_t>(digits % order); digits /= order; }
                }
                m_Stickers.push_back(s);
            }
}

size_t Layout::Index(const Sticker& s) const
{
    size_t cell = 0, weight = 1;
    for (int d = 0; d < AxisCount; ++d)
        if (d != s.Axis) { cell += s.Position[d] * weight; weight *= m_Order; }
    return (2 * s.Axis + (s.Sign > 0 ? 1 : 0)) * m_FaceSize + cell;
}

void Layout::Validate(const Move& m) const
{
    if (m.Axis < 0 || m.Axis >= AxisCount || m.Layer < 0 || m.Layer >= m_Order ||
        m.A < 0 || m.A >= AxisCount || m.B < 0 || m.B >= AxisCount ||
        m.Turns == 0 || m.Turns < -2 || m.Turns > 2)
        throw std::invalid_argument("Invalid move coordinates or turn count");
    if (m.A >= m.B || m.A == m.Axis || m.B == m.Axis)
        throw std::invalid_argument("Rotation plane must be ordered and tangent to the slice");
}

size_t Layout::Destination(size_t source, const Move& m) const
{
    Sticker s = m_Stickers[source];
    if (s.Position[m.Axis] != m.Layer) return source;
    for (int q = 0; q < Mod4(m.Turns); ++q)
    {
        const int8_t old = s.Position[m.A];
        s.Position[m.A] = static_cast<int8_t>(m_Order-1-s.Position[m.B]);
        s.Position[m.B] = old;
        if (s.Axis == m.A) s.Axis = static_cast<uint8_t>(m.B);
        else if (s.Axis == m.B) { s.Axis = static_cast<uint8_t>(m.A); s.Sign = -s.Sign; }
    }
    return Index(s);
}

std::vector<Move> Layout::Generators() const
{
    std::vector<Move> result;
    for (int f = 0; f < AxisCount; ++f)
        for (int layer = 0; layer < m_Order; ++layer)
        {
            for (int a = 0; a < AxisCount; ++a)
                for (int b = a + 1; b < AxisCount; ++b)
                    if (a != f && b != f)
                        for (int turn : {-1, 1}) result.push_back({f, layer, a, b, turn});
        }
    return result;
}

Cube::Cube(int order) : Cube(std::make_shared<Layout>(order)) {}
Cube::Cube(std::shared_ptr<const Layout> layout) : m_Layout(std::move(layout))
{
    if (!m_Layout) throw std::invalid_argument("Missing layout");
    m_Colors.resize(m_Layout->Stickers().size()); m_Scratch.resize(m_Colors.size()); Reset();
}
void Cube::Reset()
{
    for (size_t i = 0; i < m_Colors.size(); ++i) m_Colors[i] = static_cast<uint8_t>(i / m_Layout->FaceSize());
}
void Cube::Apply(const Move& m)
{
    m_Layout->Validate(m);
    for (size_t i = 0; i < m_Colors.size(); ++i) m_Scratch[m_Layout->Destination(i, m)] = m_Colors[i];
    m_Colors.swap(m_Scratch);
}
void Cube::Apply(const std::vector<Move>& moves) { for (const Move& m : moves) Apply(m); }
size_t Cube::Misplaced() const
{
    size_t count = 0;
    for (size_t i = 0; i < m_Colors.size(); ++i) count += m_Colors[i] != i / m_Layout->FaceSize();
    return count;
}
bool Cube::IsSolved() const { return Misplaced() == 0; }
bool Cube::operator==(const Cube& c) const { return Order() == c.Order() && m_Colors == c.m_Colors; }
std::string Cube::Key() const { return std::string(m_Colors.begin(), m_Colors.end()); }
void Cube::SetColors(const std::vector<uint8_t>& colors)
{
    if (colors.size() != m_Colors.size()) throw std::invalid_argument("Wrong sticker count");
    std::array<size_t, FaceCount> counts{};
    for (uint8_t c : colors)
    {
        if (c >= FaceCount) throw std::invalid_argument("Invalid sticker color");
        ++counts[c];
    }
    for (int c = 0; c < FaceCount; ++c)
        if (counts[c] != m_Layout->FaceSize()) throw std::invalid_argument("Unbalanced sticker colors");
    m_Colors = colors;
}
void Cube::SetKey(const std::string& key) { SetColors(std::vector<uint8_t>(key.begin(), key.end())); }

uint32_t TimeSeed()
{
    static std::atomic<uint64_t> previous{0};
    const auto now=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    uint64_t last=previous.load(), stamp=0;
    do { stamp=std::max(now,last+1); } while (!previous.compare_exchange_weak(last,stamp));
    return static_cast<uint32_t>(stamp^(stamp>>32));
}
std::vector<Move> Scramble(int order, int count, uint32_t seed)
{
    if (count < 0 || count > 100000) throw std::invalid_argument("Scramble count must be 0..100000");
    Layout layout(order);
    const auto generators = layout.Generators();
    std::mt19937 random(seed);
    std::uniform_int_distribution<size_t> pick(0, generators.size() - 1);
    std::vector<Move> moves;
    while (moves.size() < static_cast<size_t>(count))
    {
        Move m = generators[pick(random)];
        if (!moves.empty() && m.SameSlice(moves.back())) continue;
        moves.push_back(m);
    }
    return moves;
}

std::vector<Move> Reduce(const std::vector<Move>& moves, int order)
{
    Layout layout(order);
    std::vector<Move> result;
    for (Move m : moves)
    {
        layout.Validate(m);
        // Parallel slices in the same plane commute; combine across those slices.
        size_t j = result.size();
        while (j > 0)
        {
            const Move& prev = result[j - 1];
            if (prev.SameSlice(m))
            {
                m.Turns = Mod4(m.Turns + prev.Turns);
                result.erase(result.begin() + static_cast<ptrdiff_t>(j - 1));
                break;
            }
            if (prev.Axis != m.Axis || prev.A != m.A || prev.B != m.B) break;
            --j;
        }
        if (m.Turns == 3) m.Turns = -1;
        if (m.Turns) result.push_back(m);
    }
    return result;
}
std::vector<Move> Invert(const std::vector<Move>& moves)
{
    std::vector<Move> result;
    for (auto i = moves.rbegin(); i != moves.rend(); ++i) result.push_back(i->Inverse());
    return result;
}
void SaveState(const Cube& cube, const std::string& path)
{
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Cannot write state: " + path);
    out << "BURNSIDE_STATE 2 " << cube.Order() << '\n';
    for (uint8_t c : cube.Colors()) out << static_cast<int>(c) << ' ';
    out << '\n';
    if (!out) throw std::runtime_error("State write failed");
}
Cube LoadState(const std::string& path)
{
    std::ifstream in(path);
    std::string magic; int version = 0, dim = 0;
    if (!(in >> magic >> version >> dim) || magic != "BURNSIDE_STATE" || (version != 1 && version != 2))
        throw std::runtime_error("Invalid Burnside state header");
    if (version == 1 && dim != 3) throw std::runtime_error("Legacy high-dimensional states are not NxNxN cube states");
    Cube cube(dim);
    std::vector<uint8_t> colors(cube.Colors().size());
    for (uint8_t& c : colors)
    {
        int value = 0;
        if (!(in >> value) || value < 0 || value >= FaceCount) throw std::runtime_error("Invalid or truncated state");
        c = static_cast<uint8_t>(value);
    }
    std::string extra;
    if (in >> extra) throw std::runtime_error("Trailing state data");
    cube.SetColors(colors); return cube;
}
void SaveMoves(const MoveSequence& sequence, const std::string& path)
{
    Layout layout(sequence.Order);
    for (const Move& m : sequence.Moves) layout.Validate(m);
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Cannot write sequence: " + path);
    out << "BURNSIDE_MOVES 2 " << sequence.Order << '\n';
    for (const Move& m : sequence.Moves) out << m.Axis << ' ' << m.Layer << ' ' << m.A << ' ' << m.B << ' ' << m.Turns << '\n';
    if (!out) throw std::runtime_error("Sequence write failed");
}
MoveSequence LoadMoves(const std::string& path)
{
    std::ifstream in(path);
    MoveSequence sequence;
    std::string magic; int version = 0;
    if (!(in >> magic >> version >> sequence.Order) || magic != "BURNSIDE_MOVES" || (version != 1 && version != 2))
        throw std::runtime_error("Invalid Burnside moves header");
    if (version == 1 && sequence.Order != 3) throw std::runtime_error("Legacy high-dimensional moves are not NxNxN cube moves");
    Layout layout(sequence.Order);
    in >> std::ws;
    while (in.peek() != std::char_traits<char>::eof())
    {
        Move move;
        if (!(in >> move.Axis >> move.Layer >> move.A >> move.B >> move.Turns)) throw std::runtime_error("Truncated or invalid move");
        if (version == 1) ++move.Layer;
        layout.Validate(move); sequence.Moves.push_back(move);
        if (sequence.Moves.size() > 100000) throw std::runtime_error("Sequence exceeds 100000 moves");
        in >> std::ws;
    }
    return sequence;
}
}
