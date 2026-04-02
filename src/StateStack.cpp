#include "StateStack.hpp"

#include <glad/glad.h>

#include <SFML/Window/Event.hpp>
#include <iostream>

#include <stdexcept>

StateStack::StateStack(State::Context context)
    : mStack(), mPendingList(), mContext(context), mFactories()
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

    applyPendingChanges();
}

void StateStack::draw() const noexcept
{
    if (mStack.empty())
    {
        return;
    }

    // Draw from bottom to top so overlay states (pause/menu) render last
    for (auto it = mStack.cbegin(); it != mStack.cend(); ++it)
    {
        (*it)->draw();
    }
}

void StateStack::handleEvent(const sf::Event &event) noexcept
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
    for (const PendingChange &change : mPendingList)
    {
        switch (change.action)
        {
        case Action::PUSH:
            try
            {
                mStack.push_back(createState(change.stateID));
            }
            catch (const std::exception &e)
            {
                std::cerr << "StateStack::applyPendingChanges - Failed to create state: " << e.what() << "\n";
            }
            break;
        case Action::POP:
            if (!mStack.empty())
            {
                mStack.pop_back();
            }
            else
            {
                std::cerr << "StateStack::applyPendingChanges - Attempted to pop from empty stack\n";
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
    : action(action), stateID(stateID)
{
}
