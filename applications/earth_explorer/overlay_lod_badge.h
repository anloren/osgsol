#ifndef EARTH_OVERLAY_LOD_BADGE_H
#define EARTH_OVERLAY_LOD_BADGE_H

namespace earthui
{
    // "已达最大细节"角标是否可见(纯函数,便于单测):
    // - lastStretchFrame:引擎最近一次做 OVERLAY 超缩放拉伸的帧号(0 = 从未);
    // - curFrame:当前帧号;debounceFrames:去抖窗(帧数),防止边界帧抖动导致角标闪烁;
    // - hasActiveNote:当前有激活叠加层且它带 maxDetailNote 文案;dismissed:用户已手动关闭。
    // - shownFrames(Task 4):角标本轮已连续可见的帧数;autoDismissFrames:自消失阈值(帧数)。
    // 可见 ⟺ 近期(curFrame - lastStretchFrame < 去抖窗)确有拉伸 且 有文案 且 未被关
    //        且 尚未到自消失阈值(shownFrames < autoDismissFrames)。
    inline bool overlayMaxDetailBadgeVisible(unsigned int curFrame, unsigned int lastStretchFrame,
                                             unsigned int debounceFrames, bool hasActiveNote, bool dismissed,
                                             unsigned int shownFrames, unsigned int autoDismissFrames)
    {
        if (dismissed || !hasActiveNote) return false;
        if (lastStretchFrame == 0u || curFrame < lastStretchFrame) return false;
        if ((curFrame - lastStretchFrame) >= debounceFrames) return false;
        return shownFrames < autoDismissFrames;
    }
}

#endif
