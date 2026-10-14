#pragma once
#include "runtime/gs/gs_backend.h"

struct GSRegisters;

std::unique_ptr<GSRasterBackend> CreateParallelGSBackend(GSRegisters &registers);
