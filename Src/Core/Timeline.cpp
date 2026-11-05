#include "Timeline.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Burnside
{
void Timeline::Append(const std::vector<Move>& moves, MoveSource source, bool play)
{
    if (Busy()) throw std::logic_error("Cannot branch during an active animation");
    for (const auto& m : moves) m_Cube.Geometry().Validate(m);
    m_Entries.resize(m_Cursor);
    for (const auto& m : moves) m_Entries.push_back({m, source});
    m_Target = play ? m_Entries.size() : m_Cursor;
}
ManualAction Timeline::ApplyManual(const Move& move)
{
    m_Cube.Geometry().Validate(move);
    if (m_Active)
    {
        const size_t index = m_Direction > 0 ? m_Cursor : m_Cursor-1;
        const size_t boundary = static_cast<size_t>(static_cast<ptrdiff_t>(m_Cursor)+m_Direction);
        if (m_Entries[index].Source != MoveSource::Manual || (m_Target != m_Cursor && m_Target != boundary)) return ManualAction::Ignored;
        if (!m_Returning && move == m_Active->Inverse())
        {
            m_Returning = true; m_Target = m_Cursor;
            return m_Direction > 0 ? ManualAction::Undo : ManualAction::Redo;
        }
        if (m_Returning && move == *m_Active)
        {
            m_Returning = false; m_Target = boundary;
            return m_Direction > 0 ? ManualAction::Redo : ManualAction::Undo;
        }
        return ManualAction::Ignored;
    }
    if (Busy())
    {
        const int direction=m_Target>m_Cursor ? 1 : -1;
        const size_t index=direction>0 ? m_Cursor : m_Cursor-1;
        const size_t boundary=static_cast<size_t>(static_cast<ptrdiff_t>(m_Cursor)+direction);
        const Move pending=direction>0 ? m_Entries[index].Rotation : m_Entries[index].Rotation.Inverse();
        if (m_Target==boundary && m_Entries[index].Source==MoveSource::Manual && move==pending.Inverse())
        {
            m_Target=m_Cursor;
            return direction>0 ? ManualAction::Undo : ManualAction::Redo;
        }
        return ManualAction::Ignored;
    }
    if (m_Cursor > 0 && move == m_Entries[m_Cursor-1].Rotation.Inverse())
    { Seek(m_Cursor-1); return ManualAction::Undo; }
    if (m_Cursor < m_Entries.size() && move == m_Entries[m_Cursor].Rotation)
    { Seek(m_Cursor+1); return ManualAction::Redo; }
    Append({move},MoveSource::Manual);
    return ManualAction::Appended;
}
void Timeline::Seek(size_t cursor)
{
    if (cursor > m_Entries.size()) throw std::out_of_range("Timeline cursor");
    m_Target = cursor; m_ClearAtOrigin = false; m_Returning = false;
}
void Timeline::ResetImmediate()
{
    m_Cube.Reset();
    m_Entries.clear();
    m_Cursor=0; m_Target=0; m_Active.reset(); m_Progress=0;
    m_Direction=1; m_ClearAtOrigin=false; m_Returning=false;
}
void Timeline::ResetAnimated() { m_Target = 0; m_ClearAtOrigin = true; }
void Timeline::StopAtNextBoundary()
{
    m_Target = m_Active && !m_Returning ? static_cast<size_t>(static_cast<ptrdiff_t>(m_Cursor) + m_Direction) : m_Cursor;
    m_ClearAtOrigin = false;
}
void Timeline::Tick(float seconds, float secondsPerQuarterTurn, float scrambleSpeed)
{
    if (!std::isfinite(seconds) || seconds < 0 || !std::isfinite(secondsPerQuarterTurn) || secondsPerQuarterTurn <= 0 ||
        !std::isfinite(scrambleSpeed) || scrambleSpeed <= 0)
        throw std::invalid_argument("Invalid animation time");
    while (seconds > 0 && Busy())
    {
        if (!m_Active)
        {
            m_Direction = m_Target > m_Cursor ? 1 : -1;
            m_Active = m_Direction > 0 ? m_Entries[m_Cursor].Rotation : m_Entries[m_Cursor - 1].Rotation.Inverse();
            m_Progress = 0;
            m_Returning = false;
        }
        const size_t index=m_Direction>0 ? m_Cursor : m_Cursor-1;
        const float speed=m_Entries[index].Source==MoveSource::Scramble ? scrambleSpeed : 1.0f;
        const float duration = secondsPerQuarterTurn * static_cast<float>(std::abs(m_Active->Turns))/speed;
        const float remaining = m_Returning ? m_Progress : 1.0f-m_Progress;
        const float consumed = std::min(seconds, remaining * duration);
        seconds -= consumed; m_Progress += (m_Returning ? -1.0f : 1.0f)*consumed/duration;
        if (m_Returning && m_Progress <= 1e-6f)
        {
            m_Active.reset(); m_Progress = 0; m_Returning = false;
        }
        else if (!m_Returning && m_Progress >= 1.0f - 1e-6f)
        {
            m_Cube.Apply(*m_Active);
            m_Cursor = static_cast<size_t>(static_cast<ptrdiff_t>(m_Cursor) + m_Direction);
            m_Active.reset(); m_Progress = 0;
        }
        else break;
    }
    if (m_ClearAtOrigin && !Busy() && m_Cursor == 0)
    {
        m_Entries.clear(); m_Target = 0; m_ClearAtOrigin = false;
    }
}
std::vector<Move> Timeline::Certificate() const
{
    std::vector<Move> result;
    for (size_t i = 0; i < m_Cursor; ++i) result.push_back(m_Entries[i].Rotation);
    return result;
}
}
