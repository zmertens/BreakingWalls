#include "State.hpp"

#include "ResourceIdentifiers.hpp"
#include "StateStack.hpp"

#include <cstdarg>
#include <cstdio>

State::Context::Context(RenderWindow& window,
    FontManager& fonts,
    LevelsManager& levels,
    MusicManager& music,
    OptionsManager& options,
    SoundBufferManager& soundBuffers,
    SoundPlayer& sounds,
    ShaderManager& shaders,
    TextureManager& textures, Player& player)
    : window{ &window }
    , fonts{ &fonts }
    , levels{ &levels }
    , music{ &music }
    , options{ &options }
    , soundBuffers{ &soundBuffers }
    , sounds{ &sounds }
    , shaders{ &shaders }
    , textures{ &textures }
    , player{ &player }
{

}

State::State(StateStack& stack, Context context)
    : mStack{ &stack }
    , mContext{ context }
{
}

void State::requestStackPush(States::ID stateID)
{
    mStack->pushState(stateID);
}

void State::requestStackPop()
{
    mStack->popState();
}

void State::requestStateClear()
{
    mStack->clearStates();
}

State::Context State::getContext() const noexcept
{
    return mContext;
}

StateStack& State::getStack() const noexcept
{
    return *mStack;
}

/// @brief 
/// @param message
/// @param delimiter '\n'
void State::log(std::string_view message, const char delimiter) const noexcept
{
    mLogs += std::string{ message } + delimiter;
}

std::string_view State::view() const noexcept
{
    return mLogs;
}
