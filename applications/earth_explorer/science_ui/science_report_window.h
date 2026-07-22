#ifndef EARTH_SCIENCE_UI_REPORT_WINDOW_H
#define EARTH_SCIENCE_UI_REPORT_WINDOW_H

#include <osg/Vec2>

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct ScienceReportWindowState
{
    std::string artifactId;
    osg::Vec2f position{0.0f, 0.0f};
    osg::Vec2f size{900.0f, 680.0f};
    bool visible = false;
    bool minimized = false;
    std::string activeSection{"overview"};
    std::string selectedMetric;
    int selectedYear = 0;
};

enum class ScienceReportResizeEdge
{
    Right,
    Bottom,
    BottomRight,
};

class ScienceReportWindowModel
{
public:
    void setViewport(float width, float height,
                     float leftInset = 380.0f,
                     float topInset = 52.0f,
                     float bottomInset = 116.0f);
    bool openReady(const std::string& artifactId);
    bool reopen(const std::string& artifactId);
    bool minimize(const std::string& artifactId);
    bool close(const std::string& artifactId);
    bool remove(const std::string& artifactId);
    bool drag(const std::string& artifactId, const osg::Vec2f& delta);
    bool resize(const std::string& artifactId,
                ScienceReportResizeEdge edge,
                const osg::Vec2f& delta);
    bool setSection(const std::string& artifactId,
                    const std::string& section);
    bool selectMetric(const std::string& artifactId,
                      const std::string& metricId);
    bool selectYear(const std::string& artifactId, int year);

    const ScienceReportWindowState* find(
        const std::string& artifactId) const;
    const ScienceReportWindowState* active() const;
    const std::vector<std::string>& shelf() const { return _shelf; }
    std::size_t size() const { return _windows.size(); }

private:
    ScienceReportWindowState& ensure(const std::string& artifactId);
    void fit(ScienceReportWindowState& state);
    void removeFromShelf(const std::string& artifactId);

    float _viewportWidth = 1440.0f;
    float _viewportHeight = 900.0f;
    float _leftInset = 380.0f;
    float _topInset = 52.0f;
    float _bottomInset = 116.0f;
    std::string _activeArtifactId;
    std::unordered_map<std::string, ScienceReportWindowState> _windows;
    std::unordered_set<std::string> _autoOpened;
    std::unordered_set<std::string> _removed;
    std::vector<std::string> _shelf;
};

#endif
