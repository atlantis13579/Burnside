#pragma once
#include "Scene.h"

namespace Burnside
{
class CompletionEffect
{
public:
    static constexpr float Duration = 3.2f;
    bool Active() const { return m_Age >= 0; }
    float Age() const { return m_Age; }
    size_t TriggerCount() const { return m_Triggers; }
    void Reset(bool suppressUntilIdle = false)
    {
        m_Age=-1; m_Armed=false; m_Suppressed=suppressUntilIdle;
    }
    void Tick(bool solved, bool busy, float seconds)
    {
        if (Active()) { m_Age+=seconds; if (m_Age>=Duration) m_Age=-1; }
        if (m_Suppressed) { if (!busy) m_Suppressed=false; return; }
        if (!solved) { m_Armed=true; m_Age=-1; }
        if (busy) { m_Age=-1; return; }
        if (solved && m_Armed) { m_Age=0; m_Armed=false; ++m_Triggers; }
    }
private:
    float m_Age=-1;
    bool m_Armed=false, m_Suppressed=false;
    size_t m_Triggers=0;
};

void DrawFeedback(const SceneFrame& scene, const CompletionEffect& completion,
                  int width, int height, bool showHints);
}
