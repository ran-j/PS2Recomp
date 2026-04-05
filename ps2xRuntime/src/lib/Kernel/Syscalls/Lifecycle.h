#pragma once

#include "ps2_syscalls.h"

namespace ps2_syscalls
{
    void notifyRuntimeStop();
    void joinAllGuestHostThreads();
    void detachAllGuestHostThreads();
}
