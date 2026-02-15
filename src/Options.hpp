#ifndef OPTIONS_HPP
#define OPTIONS_HPP

struct Options
{
    bool mAntiAliasing{true};
    bool mEnableMusic{true};
    bool mEnableSound{true};
    bool mFullscreen{false};
    bool mShowDebugOverlay{false};
    bool mVsync{true};

    float mMasterVolume{25.0f};
    float mMusicVolume{100.0f};
    float mSfxVolume{10.0f};
};

#endif // OPTIONS_HPP
