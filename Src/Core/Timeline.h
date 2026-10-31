#pragma once
#include "Cube.h"
#include <optional>

namespace Burnside
{
enum class MoveSource { Manual, Scramble, Solver };
struct Entry { Move Rotation; MoveSource Source = MoveSource::Manual; };
enum class ManualAction { Appended, Undo, Redo, Ignored };

class Timeline
{
public:
    explicit Timeline(int order = 3) : m_Cube(order) {}
    const Cube& State() const { return m_Cube; }
    const std::vector<Entry>& Entries() const { return m_Entries; }
    size_t Cursor() const { return m_Cursor; }
    size_t Target() const { return m_Target; }
    size_t HistoryCount() const { return m_Cursor + (m_Active && m_Direction > 0 ? 1 : 0); }
    bool Busy() const { return m_Active.has_value() || m_Cursor != m_Target; }
    const std::optional<Move>& ActiveMove() const { return m_Active; }
    float Progress() const { return m_Progress; }
    void Append(const std::vector<Move>& moves, MoveSource source, bool play = true);
    ManualAction ApplyManual(const Move& move);
    void Seek(size_t cursor);
    void Tick(float seconds, float secondsPerQuarterTurn);
    void ResetAnimated();
    void StopAtNextBoundary();
    std::vector<Move> Certificate() const;

private:
    Cube m_Cube;
    std::vector<Entry> m_Entries;
    size_t m_Cursor = 0;
    size_t m_Target = 0;
    std::optional<Move> m_Active;
    float m_Progress = 0;
    int m_Direction = 1;
    bool m_ClearAtOrigin = false;
    bool m_Returning = false;
};
}
