#include "services/scan_service.h"

void ScanService::reset()
{
    m_recentDetections.clear();
    m_detectionWindow.clear();
    m_confirmed.clear();
}

bool ScanService::feedFrame(const Image& frame)
{
    std::string digits;
    if (!m_decoder.decode(frame, digits)) return false;

    m_detectionWindow.push_back(digits);
    ++m_recentDetections[digits];
    if (m_detectionWindow.size() > static_cast<std::size_t>(kWindowSize))
    {
        auto old = m_recentDetections.find(m_detectionWindow.front());
        if (old != m_recentDetections.end() && old->second > 0)
            --old->second;
        m_detectionWindow.erase(m_detectionWindow.begin());
    }

    if (m_recentDetections[digits] >= kRequired)
    {
        m_confirmed = digits;
        return true;
    }
    return false;
}
