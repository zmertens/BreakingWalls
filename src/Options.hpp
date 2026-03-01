#ifndef OPTIONS_HPP
#define OPTIONS_HPP

#include <optional>

struct Options final
{
    // Getter methods with defaults matching initial values
    [[nodiscard]] bool getAntiAliasing() const noexcept { return mAntiAliasing.value_or(true); }
    [[nodiscard]] bool getEnableMusic() const noexcept { return mEnableMusic.value_or(true); }
    [[nodiscard]] bool getEnableSound() const noexcept { return mEnableSound.value_or(true); }
    [[nodiscard]] bool getFullscreen() const noexcept { return mFullscreen.value_or(false); }
    [[nodiscard]] bool getShowDebugOverlay() const noexcept { return mShowDebugOverlay.value_or(true); }
    [[nodiscard]] bool getVsync() const noexcept { return mVsync.value_or(true); }

    [[nodiscard]] float getMasterVolume() const noexcept { return mMasterVolume.value_or(25.0f); }
    [[nodiscard]] float getMusicVolume() const noexcept { return mMusicVolume.value_or(100.0f); }
    [[nodiscard]] float getSfxVolume() const noexcept { return mSfxVolume.value_or(10.0f); }

    [[nodiscard]] bool getArcadeModeEnabled() const noexcept { return mArcadeModeEnabled.value_or(true); }
    [[nodiscard]] float getRunnerSpeed() const noexcept { return mRunnerSpeed.value_or(30.0f); }
    [[nodiscard]] float getRunnerStrafeLimit() const noexcept { return mRunnerStrafeLimit.value_or(35.0f); }
    [[nodiscard]] int getRunnerStartingPoints() const noexcept { return mRunnerStartingPoints.value_or(100); }
    [[nodiscard]] int getRunnerPickupMinValue() const noexcept { return mRunnerPickupMinValue.value_or(-25); }
    [[nodiscard]] int getRunnerPickupMaxValue() const noexcept { return mRunnerPickupMaxValue.value_or(40); }
    [[nodiscard]] float getRunnerPickupSpacing() const noexcept { return mRunnerPickupSpacing.value_or(18.0f); }
    [[nodiscard]] int getRunnerObstaclePenalty() const noexcept { return mRunnerObstaclePenalty.value_or(25); }
    [[nodiscard]] float getRunnerCollisionCooldown() const noexcept { return mRunnerCollisionCooldown.value_or(0.40f); }

    [[nodiscard]] int getMotionBlurBracket1Points() const noexcept { return mMotionBlurBracket1Points.value_or(300); }
    [[nodiscard]] int getMotionBlurBracket2Points() const noexcept { return mMotionBlurBracket2Points.value_or(500); }
    [[nodiscard]] int getMotionBlurBracket3Points() const noexcept { return mMotionBlurBracket3Points.value_or(800); }
    [[nodiscard]] int getMotionBlurBracket4Points() const noexcept { return mMotionBlurBracket4Points.value_or(1200); }
    [[nodiscard]] float getMotionBlurBracket1Boost() const noexcept { return mMotionBlurBracket1Boost.value_or(0.10f); }
    [[nodiscard]] float getMotionBlurBracket2Boost() const noexcept { return mMotionBlurBracket2Boost.value_or(0.18f); }
    [[nodiscard]] float getMotionBlurBracket3Boost() const noexcept { return mMotionBlurBracket3Boost.value_or(0.28f); }
    [[nodiscard]] float getMotionBlurBracket4Boost() const noexcept { return mMotionBlurBracket4Boost.value_or(0.38f); }

    // Builder methods returning reference for fluent interface
    Options &withAntiAliasing(bool value)
    {
        mAntiAliasing = value;
        return *this;
    }

    Options &withEnableMusic(bool value)
    {
        mEnableMusic = value;
        return *this;
    }

    Options &withEnableSound(bool value)
    {
        mEnableSound = value;
        return *this;
    }

    Options &withFullscreen(bool value)
    {
        mFullscreen = value;
        return *this;
    }

    Options &withShowDebugOverlay(bool value)
    {
        mShowDebugOverlay = value;
        return *this;
    }

    Options &withVsync(bool value)
    {
        mVsync = value;
        return *this;
    }

    Options &withMasterVolume(float value)
    {
        mMasterVolume = value;
        return *this;
    }

    Options &withMusicVolume(float value)
    {
        mMusicVolume = value;
        return *this;
    }

    Options &withSfxVolume(float value)
    {
        mSfxVolume = value;
        return *this;
    }

    Options &withArcadeModeEnabled(bool value)
    {
        mArcadeModeEnabled = value;
        return *this;
    }

    Options &withRunnerSpeed(float value)
    {
        mRunnerSpeed = value;
        return *this;
    }

    Options &withRunnerStrafeLimit(float value)
    {
        mRunnerStrafeLimit = value;
        return *this;
    }

    Options &withRunnerStartingPoints(int value)
    {
        mRunnerStartingPoints = value;
        return *this;
    }

    Options &withRunnerPickupMinValue(int value)
    {
        mRunnerPickupMinValue = value;
        return *this;
    }

    Options &withRunnerPickupMaxValue(int value)
    {
        mRunnerPickupMaxValue = value;
        return *this;
    }

    Options &withRunnerPickupSpacing(float value)
    {
        mRunnerPickupSpacing = value;
        return *this;
    }

    Options &withRunnerObstaclePenalty(int value)
    {
        mRunnerObstaclePenalty = value;
        return *this;
    }

    Options &withRunnerCollisionCooldown(float value)
    {
        mRunnerCollisionCooldown = value;
        return *this;
    }

    Options &withMotionBlurBracket1Points(int value)
    {
        mMotionBlurBracket1Points = value;
        return *this;
    }

    Options &withMotionBlurBracket2Points(int value)
    {
        mMotionBlurBracket2Points = value;
        return *this;
    }

    Options &withMotionBlurBracket3Points(int value)
    {
        mMotionBlurBracket3Points = value;
        return *this;
    }

    Options &withMotionBlurBracket4Points(int value)
    {
        mMotionBlurBracket4Points = value;
        return *this;
    }

    Options &withMotionBlurBracket1Boost(float value)
    {
        mMotionBlurBracket1Boost = value;
        return *this;
    }

    Options &withMotionBlurBracket2Boost(float value)
    {
        mMotionBlurBracket2Boost = value;
        return *this;
    }

    Options &withMotionBlurBracket3Boost(float value)
    {
        mMotionBlurBracket3Boost = value;
        return *this;
    }

    Options &withMotionBlurBracket4Boost(float value)
    {
        mMotionBlurBracket4Boost = value;
        return *this;
    }
private:
    std::optional<bool> mAntiAliasing;
    std::optional<bool> mEnableMusic;
    std::optional<bool> mEnableSound;
    std::optional<bool> mFullscreen;
    std::optional<bool> mShowDebugOverlay;
    std::optional<bool> mVsync;

    std::optional<float> mMasterVolume;
    std::optional<float> mMusicVolume;
    std::optional<float> mSfxVolume;

    std::optional<bool> mArcadeModeEnabled;
    std::optional<float> mRunnerSpeed;
    std::optional<float> mRunnerStrafeLimit;
    std::optional<int> mRunnerStartingPoints;
    std::optional<int> mRunnerPickupMinValue;
    std::optional<int> mRunnerPickupMaxValue;
    std::optional<float> mRunnerPickupSpacing;
    std::optional<int> mRunnerObstaclePenalty;
    std::optional<float> mRunnerCollisionCooldown;

    std::optional<int> mMotionBlurBracket1Points;
    std::optional<int> mMotionBlurBracket2Points;
    std::optional<int> mMotionBlurBracket3Points;
    std::optional<int> mMotionBlurBracket4Points;
    std::optional<float> mMotionBlurBracket1Boost;
    std::optional<float> mMotionBlurBracket2Boost;
    std::optional<float> mMotionBlurBracket3Boost;
    std::optional<float> mMotionBlurBracket4Boost;
}; // Options struct

#endif // OPTIONS_HPP
