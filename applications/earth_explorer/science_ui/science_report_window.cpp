#include "science_report_window.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr float OUTER_MARGIN = 12.0f;
constexpr float DEFAULT_WIDTH = 900.0f;
constexpr float DEFAULT_HEIGHT = 680.0f;
constexpr float MINIMUM_WIDTH = 720.0f;
constexpr float MINIMUM_HEIGHT = 520.0f;
constexpr float MAXIMUM_WIDTH = 1200.0f;
constexpr float MAXIMUM_HEIGHT = 900.0f;
constexpr std::size_t MAX_SHELF_REPORTS = 3;

float clamped(float value, float low, float high)
{
    if (high < low) return low;
    return std::max(low, std::min(value, high));
}
}

void ScienceReportWindowModel::setViewport(
    float width, float height, float leftInset, float topInset,
    float bottomInset)
{
    _viewportWidth = std::max(1.0f, width);
    _viewportHeight = std::max(1.0f, height);
    _leftInset = std::max(0.0f, leftInset);
    _topInset = std::max(0.0f, topInset);
    _bottomInset = std::max(0.0f, bottomInset);
    for (auto& item : _windows) fit(item.second);
}

ScienceReportWindowState& ScienceReportWindowModel::ensure(
    const std::string& artifactId)
{
    auto found = _windows.find(artifactId);
    if (found != _windows.end()) return found->second;
    ScienceReportWindowState state;
    state.artifactId = artifactId;
    const auto inserted = _windows.emplace(artifactId, std::move(state));
    fit(inserted.first->second);
    return inserted.first->second;
}

void ScienceReportWindowModel::fit(ScienceReportWindowState& state)
{
    const float availableWidth = std::max(
        1.0f, _viewportWidth - _leftInset - 2.0f * OUTER_MARGIN);
    const float availableHeight = std::max(
        1.0f, _viewportHeight - _topInset - _bottomInset -
            2.0f * OUTER_MARGIN);
    const bool compact = _viewportWidth <= 1100.0f ||
                         _viewportHeight <= 640.0f;
    const float desiredWidth = compact ? 680.0f : DEFAULT_WIDTH;
    const float desiredHeight = compact ? 500.0f : DEFAULT_HEIGHT;
    const float minimumWidth = std::min(MINIMUM_WIDTH, availableWidth);
    const float minimumHeight = std::min(MINIMUM_HEIGHT, availableHeight);
    const float maximumWidth = std::min(MAXIMUM_WIDTH, availableWidth);
    const float maximumHeight = std::min(MAXIMUM_HEIGHT, availableHeight);
    if (!(state.size.x() > 0.0f) || state.size.x() == DEFAULT_WIDTH)
        state.size.x() = desiredWidth;
    if (!(state.size.y() > 0.0f) || state.size.y() == DEFAULT_HEIGHT)
        state.size.y() = desiredHeight;
    state.size.x() = clamped(state.size.x(), minimumWidth, maximumWidth);
    state.size.y() = clamped(state.size.y(), minimumHeight, maximumHeight);
    const float minimumX = _leftInset + OUTER_MARGIN;
    const float minimumY = _topInset + OUTER_MARGIN;
    const float maximumX = _viewportWidth - OUTER_MARGIN - state.size.x();
    const float maximumY = _viewportHeight - _bottomInset -
        OUTER_MARGIN - state.size.y();
    if (state.position.x() == 0.0f && state.position.y() == 0.0f)
    {
        state.position.x() = minimumX +
            std::max(0.0f, (availableWidth - state.size.x()) * 0.5f);
        state.position.y() = minimumY +
            std::max(0.0f, (availableHeight - state.size.y()) * 0.5f);
    }
    state.position.x() = clamped(state.position.x(), minimumX, maximumX);
    state.position.y() = clamped(state.position.y(), minimumY, maximumY);
}

bool ScienceReportWindowModel::openReady(const std::string& artifactId)
{
    if (artifactId.empty() || _removed.count(artifactId) ||
        _autoOpened.count(artifactId))
        return false;
    _autoOpened.insert(artifactId);
    ScienceReportWindowState& state = ensure(artifactId);
    state.visible = true;
    state.minimized = false;
    fit(state);
    removeFromShelf(artifactId);
    _activeArtifactId = artifactId;
    return true;
}

bool ScienceReportWindowModel::reopen(const std::string& artifactId)
{
    auto found = _windows.find(artifactId);
    if (found == _windows.end() || _removed.count(artifactId)) return false;
    found->second.visible = true;
    found->second.minimized = false;
    fit(found->second);
    removeFromShelf(artifactId);
    _activeArtifactId = artifactId;
    return true;
}

bool ScienceReportWindowModel::minimize(const std::string& artifactId)
{
    auto found = _windows.find(artifactId);
    if (found == _windows.end() || !found->second.visible) return false;
    found->second.visible = false;
    found->second.minimized = true;
    removeFromShelf(artifactId);
    _shelf.push_back(artifactId);
    if (_shelf.size() > MAX_SHELF_REPORTS)
    {
        const std::string evicted = _shelf.front();
        _shelf.erase(_shelf.begin());
        auto old = _windows.find(evicted);
        if (old != _windows.end()) old->second.minimized = false;
    }
    if (_activeArtifactId == artifactId) _activeArtifactId.clear();
    return true;
}

bool ScienceReportWindowModel::close(const std::string& artifactId)
{
    auto found = _windows.find(artifactId);
    if (found == _windows.end()) return false;
    found->second.visible = false;
    found->second.minimized = false;
    removeFromShelf(artifactId);
    if (_activeArtifactId == artifactId) _activeArtifactId.clear();
    return true;
}

bool ScienceReportWindowModel::remove(const std::string& artifactId)
{
    const bool existed = _windows.erase(artifactId) != 0;
    _removed.insert(artifactId);
    removeFromShelf(artifactId);
    if (_activeArtifactId == artifactId) _activeArtifactId.clear();
    return existed;
}

bool ScienceReportWindowModel::drag(
    const std::string& artifactId, const osg::Vec2f& delta)
{
    auto found = _windows.find(artifactId);
    if (found == _windows.end() || !found->second.visible) return false;
    found->second.position += delta;
    fit(found->second);
    return true;
}

bool ScienceReportWindowModel::resize(
    const std::string& artifactId, ScienceReportResizeEdge edge,
    const osg::Vec2f& delta)
{
    auto found = _windows.find(artifactId);
    if (found == _windows.end() || !found->second.visible) return false;
    if (edge == ScienceReportResizeEdge::Right ||
        edge == ScienceReportResizeEdge::BottomRight)
        found->second.size.x() += delta.x();
    if (edge == ScienceReportResizeEdge::Bottom ||
        edge == ScienceReportResizeEdge::BottomRight)
        found->second.size.y() += delta.y();
    fit(found->second);
    return true;
}

bool ScienceReportWindowModel::setSection(
    const std::string& artifactId, const std::string& section)
{
    auto found = _windows.find(artifactId);
    if (found == _windows.end() || section.empty()) return false;
    found->second.activeSection = section;
    return true;
}

bool ScienceReportWindowModel::selectMetric(
    const std::string& artifactId, const std::string& metricId)
{
    auto found = _windows.find(artifactId);
    if (found == _windows.end() || metricId.empty()) return false;
    found->second.selectedMetric = metricId;
    return true;
}

bool ScienceReportWindowModel::selectYear(
    const std::string& artifactId, int year)
{
    auto found = _windows.find(artifactId);
    if (found == _windows.end() || year <= 0) return false;
    found->second.selectedYear = year;
    return true;
}

const ScienceReportWindowState* ScienceReportWindowModel::find(
    const std::string& artifactId) const
{
    const auto found = _windows.find(artifactId);
    return found == _windows.end() ? nullptr : &found->second;
}

const ScienceReportWindowState* ScienceReportWindowModel::active() const
{
    return _activeArtifactId.empty() ? nullptr : find(_activeArtifactId);
}

void ScienceReportWindowModel::removeFromShelf(const std::string& artifactId)
{
    _shelf.erase(std::remove(_shelf.begin(), _shelf.end(), artifactId),
                 _shelf.end());
}
