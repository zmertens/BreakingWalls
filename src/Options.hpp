#ifndef OPTIONS_HPP
#define OPTIONS_HPP

struct Options
{
    bool mAntiAliasing{ true };
    bool mEnableMusic{ true };
    bool mEnableSound{ true };
    bool mFullscreen{ false };
    bool mShowDebugOverlay{ false };
    bool mVsync{ true };

    float mMasterVolume{ 100.0f };
    float mMusicVolume{ 80.0f };
    float mSfxVolume{ 90.0f };
};

#endif // OPTIONS_HPP
