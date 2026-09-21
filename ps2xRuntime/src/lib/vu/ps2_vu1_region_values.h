#ifndef PS2_VU1_REGION_VALUES_H
#define PS2_VU1_REGION_VALUES_H

#include "ps2_vu1_detail.h"
#include <cstdint>
#include <cstdlib>

namespace ps2_vu_detail {

struct RegionFlag {
    uint32_t mac{}, status{}, extraSticky{}, clipValue{};
    bool fmac{}, clip{};
};

struct RegionUpperInputs {
    const float *left, *right, *accumulator;
    float scalarI, scalarQ;
    float fs(unsigned lane) const { return left[lane]; }
    float ft(unsigned lane) const { return right[lane]; }
    float acc(unsigned lane) const { return accumulator[lane]; }
    float i() const { return scalarI; }
    float q() const { return scalarQ; }
    bool compiledFastPath() const { return true; }
};

struct RegionUpperSink {
    float *result;
    RegionFlag &flags;
    uint32_t &workingClip;
    void vf(unsigned, unsigned mask, const float *value)
    {
        for (unsigned lane = 0; lane < 4; ++lane)
            if ((mask & (8u >> lane)) != 0u)
                result[lane] = value[lane];
    }
    void acc(unsigned mask, const float *value) { vf(0u, mask, value); }
    void fmac(uint32_t mac, uint32_t status, uint32_t sticky)
    {
        flags.mac = mac;
        flags.status = status;
        flags.extraSticky = sticky;
        flags.fmac = true;
    }
    void clip(uint32_t sixBits)
    {
        workingClip = ((workingClip << 6u) | (sixBits & 63u)) & 0xffffffu;
        flags.clipValue = workingClip;
        flags.clip = true;
    }
    void reserved(uint32_t) { std::abort(); }
};

struct RegionLowerInputs {
    const float *left;
    int32_t source, target;
    float fs(unsigned lane) const { return left[lane]; }
    int32_t viS() const { return source; }
    int32_t viT() const { return target; }
};

struct RegionLowerSink {
    float *result;
    int32_t &integer;
    PS2_VU_FORCE_INLINE void vf(unsigned, unsigned mask, const float *value)
    {
        for (unsigned lane = 0; lane < 4; ++lane)
            if ((mask & (8u >> lane)) != 0u)
                result[lane] = value[lane];
    }
    void vi(unsigned reg, int32_t value)
    {
        if (reg != 0u)
            integer = static_cast<int16_t>(value);
    }
};

}
#endif
