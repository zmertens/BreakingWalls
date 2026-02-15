#include "Animation.hpp"

#include <algorithm>

Animation::Animation(int frameWidth, int frameHeight, int frameCount,
                     float frameDuration, bool loop)
    : mFrameDuration(frameDuration), mLoop(loop)
{
    mFrames.reserve(frameCount);
    for (int i = 0; i < frameCount; ++i)
    {
        mFrames.push_back(AnimationRect{
            i * frameWidth, // left
            0,              // top (single row)
            frameWidth,
            frameHeight});
    }
}

void Animation::configure(int row, int frameWidth, int frameHeight,
                          int frameCount, float frameDuration)
{
    mFrames.clear();
    mFrames.reserve(frameCount);
    mFrameDuration = frameDuration;
    mCurrentFrame = 0;
    mAccumulatedTime = 0.0f;
    mEnded = false;
    mLastFrameTime.reset();

    // Column-wise indexing: go down columns first, then move to next column
    // For a sprite sheet, this means:
    // Frame 0: (col=0, row=0), Frame 1: (col=0, row=1), Frame 2: (col=0, row=2), ...
    // Then Frame N: (col=1, row=0), etc.

    // Calculate how many rows fit in the sprite sheet
    // Assuming standard 1024x1024 sheet with 128x128 tiles = 8 rows
    int tilesPerColumn = 8; // 1024 / 128 = 8 tiles vertically

    for (int i = 0; i < frameCount; ++i)
    {
        int col = i / tilesPerColumn;      // Which column (0, 1, 2, ...)
        int rowInCol = i % tilesPerColumn; // Which row within that column

        mFrames.push_back(AnimationRect{
            col * frameWidth,       // left (x position)
            rowInCol * frameHeight, // top (y position)
            frameWidth,
            frameHeight});
    }

    // Debug log
    if (!mFrames.empty())
    {
        // Logs can go here
    }
}

AnimationRect Animation::update()
{
    if (mFrames.empty())
    {
        return AnimationRect{};
    }

    auto currentTime = std::chrono::steady_clock::now();

    // Initialize time on first call
    if (!mLastFrameTime.has_value())
    {
        mLastFrameTime = currentTime;
        return mFrames[mCurrentFrame];
    }

    // Calculate delta time
    std::chrono::duration<float> deltaTime = currentTime - mLastFrameTime.value();
    mLastFrameTime = currentTime;

    mAccumulatedTime += deltaTime.count();

    // Check if we should advance to next frame
    if (mAccumulatedTime >= mFrameDuration)
    {
        mCurrentFrame++;
        mAccumulatedTime = 0.0f;

        if (mCurrentFrame >= static_cast<int>(mFrames.size()))
        {
            if (mLoop)
            {
                mCurrentFrame = 0;
            }
            else
            {
                mCurrentFrame = static_cast<int>(mFrames.size()) - 1;
                mEnded = true;
            }
        }
    }

    return mFrames[mCurrentFrame];
}

AnimationRect Animation::getCurrentFrame() const
{
    if (mFrames.empty())
    {
        return AnimationRect{};
    }
    return mFrames[mCurrentFrame];
}

void Animation::reset()
{
    mCurrentFrame = 0;
    mAccumulatedTime = 0.0f;
    mEnded = false;
    mLastFrameTime.reset();
}

void Animation::setCurrentFrame(int frame)
{
    if (!mFrames.empty())
    {
        mCurrentFrame = std::clamp(frame, 0, static_cast<int>(mFrames.size()) - 1);
    }
}

// ============================================================================
// CharacterAnimator Implementation
// ============================================================================

CharacterAnimator::CharacterAnimator(int characterIndex, int tileSize)
    : mCharacterIndex(characterIndex), mTileSize(tileSize)
{
    initialize(characterIndex);
}

void CharacterAnimator::initialize(int characterIndex)
{
    mCharacterIndex = characterIndex;

    // Configure initial animation (IDLE)
    // Each character has FRAMES_PER_CHARACTER frames (9), all in one row
    // The row is determined by the character index
    mCurrentAnimation.configure(
        mCharacterIndex,      // row
        mTileSize,            // frame width
        mTileSize,            // frame height
        FRAMES_PER_CHARACTER, // 9 frames
        DEFAULT_FRAME_DURATION);

    // Ensure we start at frame 0
    mCurrentAnimation.reset();

    mCurrentState = CharacterAnimState::IDLE;
}

AnimationRect CharacterAnimator::update()
{
    return mCurrentAnimation.update();
}

void CharacterAnimator::setState(CharacterAnimState state, bool resetIfSame)
{
    if (state == mCurrentState && !resetIfSame)
    {
        return;
    }

    mCurrentState = state;

    // For this sprite sheet, each character has 9 frames in a single row
    // The animation uses different frame ranges for different states
    // Since all frames are in one row, we use frame indices within the row

    // Calculate base row from character index
    int baseRow = mCharacterIndex;

    // Different states could use different subsets of the 9 frames
    // For simplicity, we'll use all 9 frames for each state
    // In a more complex system, you might define frame ranges per state
    float duration = DEFAULT_FRAME_DURATION / mSpeedMultiplier;

    switch (state)
    {
    case CharacterAnimState::IDLE:
        duration = 0.2f / mSpeedMultiplier; // Slower for idle
        mCurrentAnimation.setLoop(true);
        break;

    case CharacterAnimState::WALK_FORWARD:
    case CharacterAnimState::WALK_BACKWARD:
    case CharacterAnimState::WALK_LEFT:
    case CharacterAnimState::WALK_RIGHT:
        duration = 0.1f / mSpeedMultiplier;
        mCurrentAnimation.setLoop(true);
        break;

    case CharacterAnimState::JUMP:
        duration = 0.15f / mSpeedMultiplier;
        mCurrentAnimation.setLoop(false);
        break;

    case CharacterAnimState::FALL:
        duration = 0.12f / mSpeedMultiplier;
        mCurrentAnimation.setLoop(true);
        break;

    case CharacterAnimState::ATTACK:
        duration = 0.08f / mSpeedMultiplier; // Fast attack
        mCurrentAnimation.setLoop(false);
        break;

    case CharacterAnimState::DEATH:
        duration = 0.15f / mSpeedMultiplier;
        mCurrentAnimation.setLoop(false);
        break;
    }

    // Reconfigure animation with new timing
    mCurrentAnimation.configure(
        baseRow,
        mTileSize,
        mTileSize,
        FRAMES_PER_CHARACTER,
        duration);
}

AnimationRect CharacterAnimator::getCurrentFrame() const
{
    return mCurrentAnimation.getCurrentFrame();
}

bool CharacterAnimator::isFinished() const
{
    return mCurrentAnimation.isFinished();
}
