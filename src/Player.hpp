#ifndef PLAYER_HPP
#define PLAYER_HPP

#include <cstdint>
#include <map>
#include <functional>

#include "Command.hpp"

union SDL_Event;

class CommandQueue;
class Entity;
class Camera;

class Player
{
public:
    enum class Action
    {
        // Camera movement actions
        MOVE_FORWARD,
        MOVE_BACKWARD,
        MOVE_LEFT,
        MOVE_RIGHT,
        MOVE_UP,
        MOVE_DOWN,
        // Camera rotation actions
        ROTATE_LEFT,
        ROTATE_RIGHT,
        ROTATE_UP,
        ROTATE_DOWN,
        // Special actions
        RESET_CAMERA,
        RESET_ACCUMULATION,
        ACTION_COUNT
    };

    explicit Player();

    void handleEvent(const SDL_Event& event, Camera& camera);
    void handleRealtimeInput(Camera& camera, float dt);

    void assignKey(Action action, std::uint32_t key);
    [[nodiscard]] std::uint32_t getAssignedKey(Action action) const;

    void onBeginContact(Entity* other) noexcept;
    void onEndContact(Entity* other) noexcept;

    void setGroundContact(bool contact);
    bool hasGroundContact() const;

    bool isActive() const noexcept;
    void setActive(bool active) noexcept;

private:
    void initializeActions();
    static bool isRealtimeAction(Action action);

    std::map<std::uint32_t, Action> mKeyBinding;
    std::map<Action, std::function<void(Camera&, float)>> mCameraActions;
    bool mIsActive;
    bool mIsOnGround;
};

#endif // PLAYER_HPP
