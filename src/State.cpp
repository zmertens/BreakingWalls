#include "State.hpp"

#include "ResourceIdentifiers.hpp"
#include "StateStack.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <numeric>

State::State(StateStack &stack, Context context)
    : mStack{&stack}, mContext{context}
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

StateStack &State::getStack() const noexcept
{
    return *mStack;
}

/// @brief
/// @param message
/// @param delimiter '\n'
void State::log(std::string_view message, const char delimiter) noexcept
{
    auto nextLine = std::string{message} + delimiter;
    mLogs.emplace_back(nextLine);
}

std::string_view State::view() const noexcept
{
    mLogViewBuffer.clear();

    size_t totalSize = 0;
    for (const auto &line : mLogs)
    {
        totalSize += line.size();
    }

    mLogViewBuffer.reserve(totalSize);
    for (const auto &line : mLogs)
    {
        mLogViewBuffer += line;
    }

    return std::string_view{mLogViewBuffer};
}

std::string_view State::consumeView() noexcept
{
    mLogViewBuffer.clear();

    size_t totalSize = 0;
    for (const auto &line : mLogs)
    {
        totalSize += line.size();
    }

    mLogViewBuffer.reserve(totalSize);
    for (const auto &line : mLogs)
    {
        mLogViewBuffer += line;
    }

    mLogs.clear();
    return std::string_view{mLogViewBuffer};
}
