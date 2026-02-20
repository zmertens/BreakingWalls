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

    // Arcade endless-runner gameplay
    bool mArcadeModeEnabled{true};
    float mRunnerSpeed{30.0f};
    float mRunnerStrafeLimit{35.0f};
    int mRunnerStartingPoints{100};
    int mRunnerPickupMinValue{-25};
    int mRunnerPickupMaxValue{40};
    float mRunnerPickupSpacing{18.0f};
    int mRunnerObstaclePenalty{25};
    float mRunnerCollisionCooldown{0.40f};
};

#endif // OPTIONS_HPP
