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
    [[nodiscard]] float getRenderQuality() const noexcept { return mRenderQuality.value_or(1.0f); }
    [[nodiscard]] float getSfxVolume() const noexcept { return mSfxVolume.value_or(10.0f); }

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

    Options &withRenderQuality(float value)
    {
        mRenderQuality = value;
        return *this;
    }

    Options &withSfxVolume(float value)
    {
        mSfxVolume = value;
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
    std::optional<float> mRenderQuality;
    std::optional<float> mSfxVolume;
}; // Options struct

#endif // OPTIONS_HPP
