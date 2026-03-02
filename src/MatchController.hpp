<<<<<<< HEAD
#ifndef MATCH_CONTROLLER_HPP
#define MATCH_CONTROLLER_HPP

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <random>

struct MatchWorldSnapshot
{
    glm::vec3 localPlayerPosition{0.0f};
    float runnerSpeed{30.0f};
    float strafeLimit{30.0f};
    float elapsedSeconds{0.0f};
};

struct PlayerCommand
{
    float targetZ{0.0f};
    float forwardScale{1.0f};
    bool jump{false};
};

class IPlayerController
{
public:
    virtual ~IPlayerController() = default;

    virtual PlayerCommand sample(const MatchWorldSnapshot &world,
                                 const glm::vec3 &currentPosition,
                                 float dt) = 0;
};

class LaneAIController final : public IPlayerController
{
public:
    explicit LaneAIController(std::uint32_t seed = 1337u)
        : mRng(seed)
    {
    }

    PlayerCommand sample(const MatchWorldSnapshot &world,
                         const glm::vec3 &currentPosition,
                         float dt) override
    {
        mLaneSwitchTimer = std::max(0.0f, mLaneSwitchTimer - dt);
        if (mLaneSwitchTimer <= 0.0f)
        {
            std::uniform_real_distribution<float> laneDist(-0.85f, 0.85f);
            std::uniform_real_distribution<float> holdDist(0.65f, 1.65f);
            mDesiredLane = laneDist(mRng) * world.strafeLimit;
            mLaneSwitchTimer = holdDist(mRng);
        }

        const float microSway = std::sin(world.elapsedSeconds * (2.1f + mPhaseBias) + mPhaseOffset) * (0.06f * world.strafeLimit);
        const float target = std::clamp(mDesiredLane + microSway, -world.strafeLimit, world.strafeLimit);

        std::uniform_real_distribution<float> speedJitter(-0.08f, 0.08f);
        const float forwardScale = std::clamp(1.0f + speedJitter(mRng), 0.82f, 1.14f);

        PlayerCommand command;
        command.targetZ = target;
        command.forwardScale = forwardScale;
        command.jump = false;
        return command;
    }

private:
    std::mt19937 mRng;
    float mDesiredLane{0.0f};
    float mLaneSwitchTimer{0.0f};
    float mPhaseOffset{0.73f};
    float mPhaseBias{0.17f};
};

#endif // MATCH_CONTROLLER_HPP
=======
#ifndef MATCH_CONTROLLER_HPP
#define MATCH_CONTROLLER_HPP

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <random>

struct MatchWorldSnapshot
{
    glm::vec3 localPlayerPosition{0.0f};
    float runnerSpeed{30.0f};
    float strafeLimit{30.0f};
    float elapsedSeconds{0.0f};
};

struct PlayerCommand
{
    float targetZ{0.0f};
    float forwardScale{1.0f};
    bool jump{false};
};

class IPlayerController
{
public:
    virtual ~IPlayerController() = default;

    virtual PlayerCommand sample(const MatchWorldSnapshot &world,
                                 const glm::vec3 &currentPosition,
                                 float dt) = 0;
};

class LaneAIController final : public IPlayerController
{
public:
    explicit LaneAIController(std::uint32_t seed = 1337u)
        : mRng(seed)
    {
    }

    PlayerCommand sample(const MatchWorldSnapshot &world,
                         const glm::vec3 &currentPosition,
                         float dt) override
    {
        mLaneSwitchTimer = std::max(0.0f, mLaneSwitchTimer - dt);
        if (mLaneSwitchTimer <= 0.0f)
        {
            std::uniform_real_distribution<float> laneDist(-0.85f, 0.85f);
            std::uniform_real_distribution<float> holdDist(0.65f, 1.65f);
            mDesiredLane = laneDist(mRng) * world.strafeLimit;
            mLaneSwitchTimer = holdDist(mRng);
        }

        const float microSway = std::sin(world.elapsedSeconds * (2.1f + mPhaseBias) + mPhaseOffset) * (0.06f * world.strafeLimit);
        const float target = std::clamp(mDesiredLane + microSway, -world.strafeLimit, world.strafeLimit);

        std::uniform_real_distribution<float> speedJitter(-0.08f, 0.08f);
        const float forwardScale = std::clamp(1.0f + speedJitter(mRng), 0.82f, 1.14f);

        PlayerCommand command;
        command.targetZ = target;
        command.forwardScale = forwardScale;
        command.jump = false;
        return command;
    }

private:
    std::mt19937 mRng;
    float mDesiredLane{0.0f};
    float mLaneSwitchTimer{0.0f};
    float mPhaseOffset{0.73f};
    float mPhaseBias{0.17f};
};

#endif // MATCH_CONTROLLER_HPP
>>>>>>> d3122ee0e58222ba762f9edf23a88344c9a14b0d
