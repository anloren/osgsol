#ifndef EARTH_LAYER_MANAGER_H
#define EARTH_LAYER_MANAGER_H

#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include <functional>
#include "marker_style.h"

// 统一图层注册表。P0 只管标注层；结构留好分组/类型/透明度，供后续 P1+ 扩展。
struct OverlayLayer
{
    enum Type { RasterTile, PointFeed, Grid };
    std::string id, displayName, group;
    std::string subtitle;        // 可选:显示在开关下方的小字(数据来源署名等)
    std::string maxDetailNote;   // 超原生最大缩放时角标里的分辨率提示(空=该层不显角标)
    Type type = RasterTile;
    bool enabled = false;
    bool hasOpacity = false;     // 栅格层有透明度滑块
    float opacity = 1.0f;
    bool needsKey = false;       // UI 标 key、缺 key 时灰显
    bool opaque = false;         // 不透明科学层(看不穿地形,如 gebco/ndvi/night);角标文案据此追加提示(Task 4)
    earthmark::MarkerShape shape = earthmark::MarkerShape::Circle;   // 图层目录 icon 形状(外观)
    osg::Vec4 iconColor = osg::Vec4(0.7f, 0.7f, 0.72f, 1.0f);        // 图层目录 icon 颜色(外观)
    // 应用回调：enabled/opacity 变化时被调用（P0 里标注层把它接到 LabelOpacity uniform）
    std::function<void(const OverlayLayer&)> apply;
    // 任务A(抓取失败 UI 可见):可空(=底图/标注等无抓取概念的层不接)。返回 0/1/2/3:
    // 0=未抓/idle、1=最近一次成功且有要素、2=最近一次失败(填 out 错误文案)、
    // 3=最近一次成功但 0 要素(Task 1,如当前无活跃飓风——非故障,UI 显中性提示)。
    // FeedLayer 源(registerFeedLayer)接它;UI(EarthControlUI)按返回值画指示。
    std::function<int(std::string&)> fetchStatus;
};

// 场景预设:一键切到一组图层组合(Task 4)。由 earth_main 在全部图层注册完成后注册,
// 那时 LayerManager 里的 id 才齐全;P1 后按军事/经济/太空等主题扩军。
struct Preset
{
    std::string name;                     // 按钮显示名(如 u8"灾害")
    std::vector<std::string> enabledIds;  // 预设开启的图层 id;其余(豁免层除外)全关
};

class LayerManager
{
public:
    OverlayLayer& add(const OverlayLayer& l) { _layers.push_back(l); return _layers.back(); }
    std::vector<OverlayLayer>& layers() { return _layers; }

    void addPreset(const Preset& p) { _presets.push_back(p); }
    const std::vector<Preset>& presets() const { return _presets; }

    // 应用预设:先全关、再开 enabledIds 列表,逐层触发 apply;未知预设名安全无操作,
    // 返回 false(调用方可据此告警,如 EARTH_PRESET 钩子)。
    // 豁免(不动):① u8"底图 / 标注" 组(判据与 EarthControlUI 图层树的常开组一致);
    // ② needsKey / 无 apply 回调的不可交互层(UI 里也是灰显,预设不该越过它们)。
    bool applyPreset(const std::string& name)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const Preset* preset = findPresetUnlocked(name);
        if (!preset) return false;
        for (size_t i = 0; i < _layers.size(); ++i)
            if (!exemptFromPreset(_layers[i])) _layers[i].enabled = false;
        for (size_t i = 0; i < preset->enabledIds.size(); ++i)
        {
            OverlayLayer* layer = findUnlocked(preset->enabledIds[i]);
            if (layer && !exemptFromPreset(*layer)) layer->enabled = true;
        }
        PendingCommand command;
        command.kind = PendingCommand::Preset;
        command.id = name;
        _pending.push_back(command);
        return true;
    }

    void setEnabled(const std::string& id, bool on)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        OverlayLayer* layer = findUnlocked(id);
        if (!layer) return;
        layer->enabled = on;
        PendingCommand command;
        command.kind = PendingCommand::Enable;
        command.id = id;
        command.enabled = on;
        _pending.push_back(command);
    }
    void setOpacity(const std::string& id, float value)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        OverlayLayer* layer = findUnlocked(id);
        if (!layer) return;
        layer->opacity = value;
        PendingCommand command;
        command.kind = PendingCommand::Opacity;
        command.id = id;
        command.opacity = value;
        _pending.push_back(command);
    }
    bool setSubtitle(const std::string& id, const std::string& value)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        OverlayLayer* layer = findUnlocked(id);
        if (!layer) return false;
        layer->subtitle = value;
        return true;
    }
    void setExclusiveGroupEnabled(const std::vector<std::string>& ids,
                                  const std::string& activeId)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (size_t i = 0; i < ids.size(); ++i)
        {
            OverlayLayer* layer = findUnlocked(ids[i]);
            if (layer) layer->enabled = (ids[i] == activeId);
        }
    }
    float firstEnabledOpacity(const std::vector<std::string>& ids) const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        for (size_t i = 0; i < ids.size(); ++i)
        {
            for (size_t j = 0; j < _layers.size(); ++j)
                if (_layers[j].id == ids[i] && _layers[j].enabled)
                    return _layers[j].opacity;
        }
        return 0.0f;
    }
    size_t drainPending()
    {
        size_t count = 0;
        for (;;)
        {
            PendingCommand command;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                if (_pending.empty()) break;
                command = _pending.front();
                _pending.pop_front();
            }
            if (command.kind == PendingCommand::Preset)
                applyPresetNow(command.id);
            else
                applyLayerCommandNow(command);
            ++count;
        }
        return count;
    }
    OverlayLayer* find(const std::string& id)
    {
        for (size_t i = 0; i < _layers.size(); ++i)
            if (_layers[i].id == id) return &_layers[i];
        return nullptr;
    }
    std::vector<OverlayLayer> layersSnapshot() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _layers;
    }
    std::vector<Preset> presetsSnapshot() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _presets;
    }
    // T8 状态带:最近一次成功应用的预设名(未应用过 = 空串;applyPreset 未命中不改写)。
    std::string lastAppliedPreset() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _lastAppliedPreset;
    }
private:
    struct PendingCommand
    {
        enum Kind { Enable, Opacity, Preset } kind = Enable;
        std::string id;
        bool enabled = false;
        float opacity = 1.0f;
    };

    OverlayLayer* findUnlocked(const std::string& id)
    {
        for (size_t i = 0; i < _layers.size(); ++i)
            if (_layers[i].id == id) return &_layers[i];
        return nullptr;
    }
    const Preset* findPresetUnlocked(const std::string& name) const
    {
        for (size_t i = 0; i < _presets.size(); ++i)
            if (_presets[i].name == name) return &_presets[i];
        return nullptr;
    }
    void applyLayerCommandNow(const PendingCommand& command)
    {
        OverlayLayer layerCopy;
        std::function<void(const OverlayLayer&)> apply;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            OverlayLayer* layer = findUnlocked(command.id);
            if (!layer) return;
            if (command.kind == PendingCommand::Enable)
                layer->enabled = command.enabled;
            else
                layer->opacity = command.opacity;
            layerCopy = *layer;
            apply = layer->apply;
        }
        if (apply) apply(layerCopy);
    }
    void applyPresetNow(const std::string& name)
    {
        std::vector<PendingCommand> commands;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            const Preset* preset = findPresetUnlocked(name);
            if (!preset) return;
            _lastAppliedPreset = name;
            for (size_t i = 0; i < _layers.size(); ++i)
            {
                if (exemptFromPreset(_layers[i])) continue;
                PendingCommand command;
                command.kind = PendingCommand::Enable;
                command.id = _layers[i].id;
                command.enabled = false;
                commands.push_back(command);
            }
            for (size_t i = 0; i < preset->enabledIds.size(); ++i)
            {
                OverlayLayer* layer = findUnlocked(preset->enabledIds[i]);
                if (!layer || exemptFromPreset(*layer)) continue;
                PendingCommand command;
                command.kind = PendingCommand::Enable;
                command.id = layer->id;
                command.enabled = true;
                commands.push_back(command);
            }
        }
        for (size_t i = 0; i < commands.size(); ++i)
            applyLayerCommandNow(commands[i]);
    }
    static bool exemptFromPreset(const OverlayLayer& l)
    { return l.group == u8"底图 / 标注" || l.needsKey || !l.apply; }

    mutable std::mutex _mutex;
    std::deque<PendingCommand> _pending;
    std::vector<OverlayLayer> _layers;
    std::vector<Preset> _presets;
    std::string _lastAppliedPreset;
};

#endif
