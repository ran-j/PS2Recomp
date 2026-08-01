#pragma once
#include <exception>

// Must not be in an anonymous namespace — ps2_scheduler.cpp must catch the same type.
struct ThreadExitException final : public std::exception
{
    const char* what() const noexcept override
    {
        return "PS2 Thread Exit";
    }
};
