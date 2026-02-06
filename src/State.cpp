#include "State.hpp"

#include "StateStack.hpp"

#include <cstdarg>
#include <cstdio>

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

/// @brief 
/// @param message 
/// @param delim "\n" by default, but can be customized if needed
void State::log(std::string_view formatStr, ...) noexcept
{
    va_list args;
    va_start(args, formatStr);
    
    // First, determine the required buffer size
    va_list argsCopy;
    va_copy(argsCopy, args);
    int size = std::vsnprintf(nullptr, 0, formatStr.data(), argsCopy);
    va_end(argsCopy);
    
    if (size > 0)
    {
        std::string buffer(static_cast<size_t>(size) + 1, '\0');
        std::vsnprintf(buffer.data(), buffer.size(), formatStr.data(), args);
        // Remove null terminator from string
        buffer.resize(static_cast<size_t>(size));
        mLogs += buffer;
    }
    
    va_end(args);
}

std::string_view State::view() const noexcept
{
    return mLogs;
}
