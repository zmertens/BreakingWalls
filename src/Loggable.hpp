#ifndef LOGGABLE_HPP
#define LOGGABLE_HPP

class Loggable
{
public:
    virtual ~Loggable() = default;

    virtual bool isLoggable(const bool newCondition = false) const noexcept = 0;
};

#endif // LOGGABLE_HPP
