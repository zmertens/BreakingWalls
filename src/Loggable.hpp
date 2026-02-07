#ifndef LOGGABLE_H
#define LOGGABLE_H

#include <string_view>

class Loggable
{
public:

    virtual ~Loggable() = default;
    virtual void log(std::string_view message, const char delimiter = '\n') noexcept = 0;
    virtual std::string_view view() const noexcept = 0;
};

#endif // LOGGABLE_H

