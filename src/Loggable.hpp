<<<<<<< HEAD
#ifndef LOGGABLE_HPP
#define LOGGABLE_HPP

#include <string_view>

class Loggable
{
public:
    virtual ~Loggable() = default;
    virtual void log(std::string_view message, const char delimiter = '\n') noexcept = 0;
    virtual std::string_view view() const noexcept = 0;
    virtual std::string_view consumeView() noexcept = 0;
};

#endif // LOGGABLE_HPP
=======
#ifndef LOGGABLE_HPP
#define LOGGABLE_HPP

class Loggable
{
public:
    virtual ~Loggable() = default;

    virtual bool isLoggable(const bool newCondition = false) const noexcept = 0;
};

#endif // LOGGABLE_HPP
>>>>>>> d3122ee0e58222ba762f9edf23a88344c9a14b0d
