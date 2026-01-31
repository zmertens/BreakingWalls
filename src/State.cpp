#include "State.hpp"

#include "StateStack.hpp"

#include <SDL3/SDL.h>

State::Context::Context(RenderWindow& window, FontManager& fonts,
    ShaderManager& shaders,
    TextureManager& textures, Player& player)
    : window{ &window }
    , fonts{ &fonts }
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
