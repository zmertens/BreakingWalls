#include "StateStack.hpp"

#if defined(BREAKING_WALLS_DEBUG)
#include <SDL3/SDL_log.h>
#endif

#include <stdexcept>

StateStack::StateStack(State::Context context)
    : mStack()
      , mPendingList()
      , mContext(context)
      , mFactories()
{
}

void StateStack::update(float dt, unsigned int subSteps) noexcept
{
    // Process existing states (skip if empty)
    if (!mStack.empty())
    {
        for (auto it = mStack.rbegin(); it != mStack.rend(); ++it)
        {
            if (!(*it)->update(dt, subSteps))
            {
                break;
            }
        }
    }

    // ALWAYS apply pending changes, even if stack is currently empty
    // This is crucial for initial state pushes during startup
    applyPendingChanges();
}

void StateStack::draw() const noexcept
{
    if (mStack.empty())
    {
#if defined(BREAKING_WALLS_DEBUG)
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "StateStack::draw called on empty stack");
#endif
        return;
    }

    // Draw from the first opaque state to the top
    for (auto it = mStack.rbegin(); it != mStack.rend(); ++it)
    {
        (*it)->draw();
    }
}

void StateStack::handleEvent(const SDL_Event& event) noexcept
{
    // Process events for existing states (skip if empty)
    if (!mStack.empty())
    {
        for (auto it = mStack.rbegin(); it != mStack.rend(); ++it)
        {
            if (!(*it)->handleEvent(event))
            {
                break;
            }
        }
    }

    // ALWAYS apply pending changes, even if stack is currently empty
    // This ensures state transitions from events are processed
    applyPendingChanges();
}

void StateStack::pushState(States::ID stateID)
{
    mPendingList.emplace_back(Action::PUSH, stateID);
}

void StateStack::popState()
{
    mPendingList.emplace_back(Action::POP);
}

void StateStack::clearStates()
{
    mPendingList.emplace_back(Action::CLEAR);
}

bool StateStack::isEmpty() const noexcept
{
    return mStack.empty();
}

State::Ptr StateStack::createState(States::ID stateID)
{
    if (auto found = mFactories.find(stateID); found != mFactories.cend())
    {
        return found->second();
    }

    throw std::runtime_error("StateStack::createState - No factory found for state ID");
}

void StateStack::applyPendingChanges()
{
    for (const PendingChange& change : mPendingList)
    {
        switch (change.action)
        {
        case Action::PUSH:
            mStack.push_back(createState(change.stateID));
            break;
        case Action::POP:
            if (!mStack.empty())
            {
                mStack.pop_back();
            }
            else
            {
#if defined(BREAKING_WALLS_DEBUG)
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, 
                           "StateStack::applyPendingChanges - Attempted to pop from empty stack");
#endif
            }
            break;
        case Action::CLEAR:
            mStack.clear();
            break;
        }
    }

    mPendingList.clear();
}

StateStack::PendingChange::PendingChange(Action action, States::ID stateID)
    : action(action)
      , stateID(stateID)
{
}
