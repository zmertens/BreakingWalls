#ifndef ANIMATION_HPP
#define ANIMATION_HPP

#include <vector>
#include <chrono>
#include <optional>
#include <cstdint>

#include <glm/glm.hpp>

/// @brief Rectangle representing a frame in a sprite sheet
struct AnimationRect
{
    int left{ 0 };
    int top{ 0 };
    int width{ 0 };
    int height{ 0 };
};

/// @brief Animation state for sprite sheet animations
/// @details Handles frame timing, looping, and frame selection
class Animation
{
public:
    /// @brief Default constructor
    Animation() = default;

    /// @brief Constructor with frame configuration
    /// @param frameWidth Width of each frame in pixels
    /// @param frameHeight Height of each frame in pixels
    /// @param frameCount Number of frames in the animation
    /// @param frameDuration Duration of each frame in seconds
    /// @param loop Whether the animation should loop
    explicit Animation(int frameWidth, int frameHeight, int frameCount,
        float frameDuration = 0.1f, bool loop = true);

    /// @brief Configure animation from sprite sheet row
    /// @param row Row index in the sprite sheet (0-based)
    /// @param frameWidth Width of each frame
    /// @param frameHeight Height of each frame
    /// @param frameCount Number of frames in this animation
    /// @param frameDuration Duration per frame in seconds
    void configure(int row, int frameWidth, int frameHeight,
        int frameCount, float frameDuration = 0.1f);

    /// @brief Update animation and get current frame rect
    /// @return Current frame rectangle
    AnimationRect update();

    /// @brief Get current frame without updating
    /// @return Current frame rectangle
    [[nodiscard]] AnimationRect getCurrentFrame() const;

    /// @brief Reset animation to first frame
    void reset();

    /// @brief Check if animation has completed (non-looping only)
    [[nodiscard]] bool isFinished() const noexcept { return mEnded && !mLoop; }

    /// @brief Check if animation is looping
    [[nodiscard]] bool isLooping() const noexcept { return mLoop; }

    /// @brief Set looping behavior
    void setLoop(bool loop) noexcept { mLoop = loop; }

    /// @brief Get current frame index
    [[nodiscard]] int getCurrentFrameIndex() const noexcept { return mCurrentFrame; }

    /// @brief Set frame duration
    void setFrameDuration(float duration) noexcept { mFrameDuration = duration; }

    /// @brief Get frame duration
    [[nodiscard]] float getFrameDuration() const noexcept { return mFrameDuration; }

    /// @brief Set current frame directly
    void setCurrentFrame(int frame);

    /// @brief Get total frame count
    [[nodiscard]] int getFrameCount() const noexcept { return static_cast<int>(mFrames.size()); }

private:
    std::vector<AnimationRect> mFrames;
    int mCurrentFrame{ 0 };
    float mFrameDuration{ 0.1f };
    float mAccumulatedTime{ 0.0f };
    bool mLoop{ true };
    bool mEnded{ false };

    std::optional<std::chrono::steady_clock::time_point> mLastFrameTime;
};

/// @brief Character animation state enumeration
enum class CharacterAnimState : std::uint8_t
{
    IDLE = 0,
    WALK_FORWARD = 1,
    WALK_BACKWARD = 2,
    WALK_LEFT = 3,
    WALK_RIGHT = 4,
    JUMP = 5,
    FALL = 6,
    ATTACK = 7,
    DEATH = 8
};

/// @brief Character animator managing multiple animations
/// @details Manages animations for a character sprite sheet where each character
/// has 9 frames per row, and each row represents a different animation state
class CharacterAnimator
{
public:
    /// @brief Sprite sheet configuration constants
    static constexpr int TILE_SIZE = 128;
    static constexpr int FRAMES_PER_CHARACTER = 9;
    static constexpr float DEFAULT_FRAME_DURATION = 0.1f;

    /// @brief Default constructor
    CharacterAnimator() = default;

    /// @brief Constructor with character index
    /// @param characterIndex Index of character in sprite sheet (row = characterIndex)
    /// @param tileSize Size of each tile (default 128x128)
    explicit CharacterAnimator(int characterIndex, int tileSize = TILE_SIZE);

    /// @brief Initialize with character index
    /// @param characterIndex Index of character in sprite sheet
    void initialize(int characterIndex);

    /// @brief Update current animation
    /// @return Current frame rectangle
    AnimationRect update();

    /// @brief Set current animation state
    /// @param state Animation state to play
    /// @param resetIfSame If true, reset animation even if same state
    void setState(CharacterAnimState state, bool resetIfSame = false);

    /// @brief Get current animation state
    [[nodiscard]] CharacterAnimState getState() const noexcept { return mCurrentState; }

    /// @brief Get current frame rectangle
    [[nodiscard]] AnimationRect getCurrentFrame() const;

    /// @brief Check if current animation has finished
    [[nodiscard]] bool isFinished() const;

    /// @brief Get character index
    [[nodiscard]] int getCharacterIndex() const noexcept { return mCharacterIndex; }

    /// @brief Set animation speed multiplier
    void setSpeedMultiplier(float multiplier) noexcept { mSpeedMultiplier = multiplier; }

    /// @brief Get animation speed multiplier
    [[nodiscard]] float getSpeedMultiplier() const noexcept { return mSpeedMultiplier; }

    /// @brief Get the 3D position for rendering
    [[nodiscard]] glm::vec3 getPosition() const noexcept { return mPosition; }

    /// @brief Set the 3D position
    void setPosition(const glm::vec3& position) noexcept { mPosition = position; }

    /// @brief Get rotation (facing direction)
    [[nodiscard]] float getRotation() const noexcept { return mRotation; }

    /// @brief Set rotation
    void setRotation(float rotation) noexcept { mRotation = rotation; }

private:
    Animation mCurrentAnimation;
    CharacterAnimState mCurrentState{ CharacterAnimState::IDLE };
    int mCharacterIndex{ 0 };
    int mTileSize{ TILE_SIZE };
    float mSpeedMultiplier{ 1.0f };

    // 3D positioning for rendering
    glm::vec3 mPosition{ 0.0f };
    float mRotation{ 0.0f };  // Yaw rotation in degrees
};

#endif // ANIMATION_HPP
