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
