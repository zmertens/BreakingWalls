#ifndef OPTIONS_HPP
#define OPTIONS_HPP

/// @brief Camera perspective mode
enum class CameraPerspective : unsigned int
{
    FIRST_PERSON = 0,
    THIRD_PERSON = 1
};

struct Options
{
    bool mAntiAliasing{ true };
    bool mEnableMusic{ true };
    bool mEnableSound{ true };
    bool mFullscreen{ false };
    bool mShowDebugOverlay{ false };
    bool mVsync{ true };

    float mMasterVolume{ 25.0f };
    float mMusicVolume{ 100.0f };
    float mSfxVolume{ 10.0f };

    // Camera settings
    CameraPerspective mCameraPerspective{ CameraPerspective::FIRST_PERSON };
    float mThirdPersonDistance{ 10.0f };    // Distance behind player in third person
    float mThirdPersonHeight{ 5.0f };       // Height above player in third person
};

#endif // OPTIONS_HPP
