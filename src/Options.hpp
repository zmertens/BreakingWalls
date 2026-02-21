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

    // Motion blur score brackets
    int mMotionBlurBracket1Points{300};
    int mMotionBlurBracket2Points{500};
    int mMotionBlurBracket3Points{800};
    int mMotionBlurBracket4Points{1200};
    float mMotionBlurBracket1Boost{0.10f};
    float mMotionBlurBracket2Boost{0.18f};
    float mMotionBlurBracket3Boost{0.28f};
    float mMotionBlurBracket4Boost{0.38f};
};

#endif // OPTIONS_HPP
