#ifndef LOADING_STATE_HPP
#define LOADING_STATE_HPP

#include <string>
#include <string_view>
#include <unordered_map>

#include "State.hpp"

class StateStack;
union SDL_Event;
namespace Textures
{
    enum class ID : unsigned int;
}

class LoadingState : public State
{
public:
    explicit LoadingState(StateStack &stack, State::Context context, std::string_view resourcePath = "");

    void draw() const noexcept override;
    bool update(float dt, unsigned int subSteps) noexcept override;
    bool handleEvent(const SDL_Event &event) noexcept override;

    // Check if loading has completed
    bool isFinished() const noexcept;

private:
    void loadResources() noexcept;

    void loadTexturesFromWorkerRequests() const noexcept;

    void loadWindowIcon(const std::unordered_map<std::string, std::string> &resources) noexcept;

    void loadNoiseTexture2D() noexcept;

    void loadAudio() noexcept;
    void loadFonts() noexcept;
    void loadLevels() noexcept;
    void loadShaders() noexcept;

    void setCompletion(float percent) noexcept;

    bool mHasFinished;

    const std::string mResourcePath;
};

#endif // LOADING_STATE_HPP
