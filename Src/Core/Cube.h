#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Burnside
{
constexpr int MaxOrder = 16;
constexpr int AxisCount = 3;
constexpr int FaceCount = 6;
using Coordinates = std::array<int8_t, AxisCount>;

struct Move
{
    int Axis = 0;
    int Layer = 1;
    int A = 1;
    int B = 2;
    int Turns = 1;

    Move Inverse() const;
    bool SameSlice(const Move& other) const;
    bool operator==(const Move& other) const;
    std::string ToString(int order) const;
};

struct Sticker
{
    Coordinates Position{};
    uint8_t Axis = 0;
    int8_t Sign = 1;
};

class Layout
{
public:
    explicit Layout(int order);
    int Order() const { return m_Order; }
    size_t FaceSize() const { return m_FaceSize; }
    const std::vector<Sticker>& Stickers() const { return m_Stickers; }
    size_t Index(const Sticker& sticker) const;
    void Validate(const Move& move) const;
    size_t Destination(size_t source, const Move& move) const;
    std::vector<Move> Generators() const;

private:
    int m_Order;
    size_t m_FaceSize;
    std::vector<Sticker> m_Stickers;
};

class Cube
{
public:
    explicit Cube(int order = 3);
    explicit Cube(std::shared_ptr<const Layout> layout);
    int Order() const { return m_Layout->Order(); }
    const Layout& Geometry() const { return *m_Layout; }
    const std::vector<uint8_t>& Colors() const { return m_Colors; }
    void Apply(const Move& move);
    void Apply(const std::vector<Move>& moves);
    void Reset();
    bool IsSolved() const;
    size_t Misplaced() const;
    bool operator==(const Cube& other) const;
    std::string Key() const;
    // Color counts are validated; reachability is established by a solver or certificate.
    void SetColors(const std::vector<uint8_t>& colors);
    void SetKey(const std::string& key);

private:
    std::shared_ptr<const Layout> m_Layout;
    std::vector<uint8_t> m_Colors;
    std::vector<uint8_t> m_Scratch;
};

std::vector<Move> Scramble(int order, int count, uint32_t seed);
uint32_t TimeSeed();
std::vector<Move> Reduce(const std::vector<Move>& moves, int order);
std::vector<Move> Invert(const std::vector<Move>& moves);
void SaveState(const Cube& cube, const std::string& path);
Cube LoadState(const std::string& path);
struct MoveSequence { int Order = 3; std::vector<Move> Moves; };
void SaveMoves(const MoveSequence& sequence, const std::string& path);
MoveSequence LoadMoves(const std::string& path);
}
