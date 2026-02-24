#include "ps2_gs_gpu.h"
#include "ps2_gs_common.h"
#include "ps2_gs_psmt4.h"
#include "ps2_memory.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
static inline uint64_t loadLE64(const uint8_t *p)
{
    uint64_t v;
    std::memcpy(&v, p, 8);
    return v;
}
}

using namespace GSInternal;

GS::GS()
{
    reset();
}

void GS::init(uint8_t *vram, uint32_t vramSize, GSRegisters *privRegs)
{
    m_vram = vram;
    m_vramSize = vramSize;
    m_privRegs = privRegs;
    reset();
}

void GS::reset()
{
    std::memset(m_ctx, 0, sizeof(m_ctx));
    m_prim = {};
    m_curR = 0x80; m_curG = 0x80; m_curB = 0x80; m_curA = 0x80;
    m_curQ = 1.0f;
    m_curS = 0.0f; m_curT = 0.0f;
    m_curU = 0; m_curV = 0;
    m_curFog = 0;
    m_prmodecont = true;
    m_bitbltbuf = {};
    m_trxpos = {};
    m_trxreg = {};
    m_trxdir = 3;
    m_hwregX = 0;
    m_hwregY = 0;
    m_vtxCount = 0;
    m_vtxIndex = 0;
    m_localToHostBuffer.clear();
    m_localToHostReadPos = 0;

    for (int i = 0; i < 2; ++i)
    {
        m_ctx[i].frame.fbw = 10;
        m_ctx[i].scissor = {0, 639, 0, 447};
        m_ctx[i].xyoffset = {0, 0};
    }
}

GSContext &GS::activeContext()
{
    return m_ctx[m_prim.ctxt ? 1 : 0];
}

void GS::snapshotVRAM()
{
    if (!m_vram || m_vramSize == 0) return;
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    m_displaySnapshot.resize(m_vramSize);
    std::memcpy(m_displaySnapshot.data(), m_vram, m_vramSize);
}

const uint8_t *GS::lockDisplaySnapshot(uint32_t &outSize)
{
    m_snapshotMutex.lock();
    if (m_displaySnapshot.empty())
    {
        outSize = 0;
        return nullptr;
    }
    outSize = static_cast<uint32_t>(m_displaySnapshot.size());
    return m_displaySnapshot.data();
}

void GS::unlockDisplaySnapshot()
{
    m_snapshotMutex.unlock();
}

uint32_t GS::getLastDisplayBaseBytes() const
{
    return m_lastDisplayBaseBytes;
}

void GS::refreshDisplaySnapshot()
{
    snapshotVRAM();
}

void GS::processGIFPacket(const uint8_t *data, uint32_t sizeBytes)
{
    if (!data || sizeBytes < 16 || !m_vram)
        return;

    if (sizeBytes >= 16)
    {
        const uint64_t tagLo = loadLE64(data);
        const uint8_t flg = static_cast<uint8_t>((tagLo >> 58) & 0x3);
        if (flg == GIF_FMT_PACKED)
        {
            m_hwregX = 0;
            m_hwregY = 0;
        }
    }

    uint32_t offset = 0;
    while (offset + 16 <= sizeBytes)
    {
        uint64_t tagLo = loadLE64(data + offset);
        uint64_t tagHi = loadLE64(data + offset + 8);
        offset += 16;

        m_curQ = 1.0f;

        uint32_t nloop = static_cast<uint32_t>(tagLo & 0x7FFF);
        uint8_t flg = static_cast<uint8_t>((tagLo >> 58) & 0x3);
        uint32_t nreg = static_cast<uint32_t>((tagLo >> 60) & 0xF);
        if (nreg == 0) nreg = 16;

        bool pre = ((tagLo >> 46) & 1) != 0;
        if (pre)
        {
            writeRegister(GS_REG_PRIM, (tagLo >> 47) & 0x7FF);
        }

        uint8_t regs[16];
        for (uint32_t i = 0; i < nreg; ++i)
            regs[i] = static_cast<uint8_t>((tagHi >> (i * 4)) & 0xF);

        if (flg == GIF_FMT_PACKED)
        {
            for (uint32_t loop = 0; loop < nloop; ++loop)
            {
                for (uint32_t r = 0; r < nreg; ++r)
                {
                    if (offset + 16 > sizeBytes)
                        return;
                    uint64_t lo = loadLE64(data + offset);
                    uint64_t hi = loadLE64(data + offset + 8);
                    offset += 16;
                    writeRegisterPacked(regs[r], lo, hi);
                }
            }
        }
        else if (flg == GIF_FMT_REGLIST)
        {
            for (uint32_t loop = 0; loop < nloop; ++loop)
            {
                for (uint32_t r = 0; r < nreg; ++r)
                {
                    if (offset + 8 > sizeBytes)
                        return;
                    writeRegister(regs[r], loadLE64(data + offset));
                    offset += 8;
                }
            }
            if ((nloop * nreg) & 1)
                offset += 8;
        }
        else if (flg == GIF_FMT_IMAGE)
        {
            uint32_t imageBytes = nloop * 16;
            if (offset + imageBytes > sizeBytes)
                imageBytes = sizeBytes - offset;
            processImageData(data + offset, imageBytes);
            offset += imageBytes;
        }
    }
}

void GS::writeRegisterPacked(uint8_t regDesc, uint64_t lo, uint64_t hi)
{
    switch (regDesc)
    {
    case 0x00:
        writeRegister(GS_REG_PRIM, lo & 0x7FF);
        break;
    case 0x01:
        m_curR = static_cast<uint8_t>(lo & 0xFF);
        m_curG = static_cast<uint8_t>((lo >> 32) & 0xFF);
        m_curB = static_cast<uint8_t>(hi & 0xFF);
        m_curA = static_cast<uint8_t>((hi >> 32) & 0xFF);
        break;
    case 0x02:
    {
        uint32_t sBits = static_cast<uint32_t>(lo & 0xFFFFFFFF);
        uint32_t tBits = static_cast<uint32_t>((lo >> 32) & 0xFFFFFFFF);
        uint32_t qBits = static_cast<uint32_t>(hi & 0xFFFFFFFF);
        std::memcpy(&m_curS, &sBits, 4);
        std::memcpy(&m_curT, &tBits, 4);
        std::memcpy(&m_curQ, &qBits, 4);
        if (m_curQ == 0.0f) m_curQ = 1.0f;
        break;
    }
    case 0x03:
        m_curU = static_cast<uint16_t>(lo & 0xFFFFu);
        m_curV = static_cast<uint16_t>((lo >> 32) & 0xFFFFu);
        break;
    case 0x04:
    {
        uint16_t x = static_cast<uint16_t>(lo & 0xFFFF);
        uint16_t y = static_cast<uint16_t>((lo >> 32) & 0xFFFF);
        uint32_t z = static_cast<uint32_t>((hi >> 4) & 0xFFFFFF);
        uint8_t f = static_cast<uint8_t>((hi >> 36) & 0xFF);
        bool adk = ((hi >> 47) & 1) != 0;
        GSVertex &vtx = m_vtxQueue[m_vtxCount % kMaxVerts];
        vtx.x = static_cast<float>(x) / 16.0f;
        vtx.y = static_cast<float>(y) / 16.0f;
        vtx.z = static_cast<float>(z);
        vtx.r = m_curR; vtx.g = m_curG; vtx.b = m_curB; vtx.a = m_curA;
        vtx.q = m_curQ; vtx.s = m_curS; vtx.t = m_curT;
        vtx.u = m_curU; vtx.v = m_curV; vtx.fog = f;
        vertexKick(!adk);
        break;
    }
    case 0x05:
    {
        uint16_t x = static_cast<uint16_t>(lo & 0xFFFF);
        uint16_t y = static_cast<uint16_t>((lo >> 32) & 0xFFFF);
        uint32_t z = static_cast<uint32_t>(hi & 0xFFFFFFFF);
        bool adk = ((hi >> 47) & 1) != 0;
        GSVertex &vtx = m_vtxQueue[m_vtxCount % kMaxVerts];
        vtx.x = static_cast<float>(x) / 16.0f;
        vtx.y = static_cast<float>(y) / 16.0f;
        vtx.z = static_cast<float>(z);
        vtx.r = m_curR; vtx.g = m_curG; vtx.b = m_curB; vtx.a = m_curA;
        vtx.q = m_curQ; vtx.s = m_curS; vtx.t = m_curT;
        vtx.u = m_curU; vtx.v = m_curV; vtx.fog = m_curFog;
        vertexKick(!adk);
        break;
    }
    case 0x0A:
        m_curFog = static_cast<uint8_t>((hi >> 36) & 0xFF);
        break;
    case 0x0C:
    {
        GSVertex &vtx = m_vtxQueue[m_vtxCount % kMaxVerts];
        vtx.x = static_cast<float>(lo & 0xFFFF) / 16.0f;
        vtx.y = static_cast<float>((lo >> 32) & 0xFFFF) / 16.0f;
        vtx.z = static_cast<float>((hi >> 4) & 0xFFFFFF);
        vtx.r = m_curR; vtx.g = m_curG; vtx.b = m_curB; vtx.a = m_curA;
        vtx.q = m_curQ; vtx.s = m_curS; vtx.t = m_curT;
        vtx.u = m_curU; vtx.v = m_curV;
        vtx.fog = static_cast<uint8_t>((hi >> 36) & 0xFF);
        vertexKick(false);
        break;
    }
    case 0x0D:
    {
        GSVertex &vtx = m_vtxQueue[m_vtxCount % kMaxVerts];
        vtx.x = static_cast<float>(lo & 0xFFFF) / 16.0f;
        vtx.y = static_cast<float>((lo >> 32) & 0xFFFF) / 16.0f;
        vtx.z = static_cast<float>(hi & 0xFFFFFFFF);
        vtx.r = m_curR; vtx.g = m_curG; vtx.b = m_curB; vtx.a = m_curA;
        vtx.q = m_curQ; vtx.s = m_curS; vtx.t = m_curT;
        vtx.u = m_curU; vtx.v = m_curV; vtx.fog = m_curFog;
        vertexKick(false);
        break;
    }
    case 0x0E:
    {
        uint8_t addr = static_cast<uint8_t>(hi & 0xFF);
        writeRegister(addr, lo);
        break;
    }
    case 0x0F:
        break;
    default:
        writeRegister(regDesc, lo);
        break;
    }
}

void GS::writeRegister(uint8_t regAddr, uint64_t value)
{
    switch (regAddr)
    {
    case GS_REG_PRIM:
    {
        m_prim.type = static_cast<GSPrimType>(value & 0x7);
        m_prim.iip = ((value >> 3) & 1) != 0;
        m_prim.tme = ((value >> 4) & 1) != 0;
        m_prim.fge = ((value >> 5) & 1) != 0;
        m_prim.abe = ((value >> 6) & 1) != 0;
        m_prim.aa1 = ((value >> 7) & 1) != 0;
        m_prim.fst = ((value >> 8) & 1) != 0;
        m_prim.ctxt = ((value >> 9) & 1) != 0;
        m_prim.fix = ((value >> 10) & 1) != 0;
        m_vtxCount = 0;
        m_vtxIndex = 0;
        break;
    }
    case GS_REG_RGBAQ:
    {
        m_curR = static_cast<uint8_t>(value & 0xFF);
        m_curG = static_cast<uint8_t>((value >> 8) & 0xFF);
        m_curB = static_cast<uint8_t>((value >> 16) & 0xFF);
        m_curA = static_cast<uint8_t>((value >> 24) & 0xFF);
        uint32_t qBits = static_cast<uint32_t>((value >> 32) & 0xFFFFFFFF);
        std::memcpy(&m_curQ, &qBits, 4);
        if (m_curQ == 0.0f) m_curQ = 1.0f;
        break;
    }
    case GS_REG_ST:
    {
        uint32_t sBits = static_cast<uint32_t>(value & 0xFFFFFFFF);
        uint32_t tBits = static_cast<uint32_t>((value >> 32) & 0xFFFFFFFF);
        std::memcpy(&m_curS, &sBits, 4);
        std::memcpy(&m_curT, &tBits, 4);
        break;
    }
    case GS_REG_UV:
    {
        m_curU = static_cast<uint16_t>(value & 0xFFFFu);
        m_curV = static_cast<uint16_t>((value >> 16) & 0xFFFFu);
        break;
    }
    case GS_REG_XYZF2:
    case GS_REG_XYZF3:
    {
        GSVertex &vtx = m_vtxQueue[m_vtxCount % kMaxVerts];
        vtx.x = static_cast<float>(value & 0xFFFF) / 16.0f;
        vtx.y = static_cast<float>((value >> 16) & 0xFFFF) / 16.0f;
        vtx.z = static_cast<float>((value >> 32) & 0xFFFFFF);
        vtx.fog = static_cast<uint8_t>((value >> 56) & 0xFF);
        vtx.r = m_curR; vtx.g = m_curG; vtx.b = m_curB; vtx.a = m_curA;
        vtx.q = m_curQ; vtx.s = m_curS; vtx.t = m_curT;
        vtx.u = m_curU; vtx.v = m_curV;
        vertexKick(regAddr == GS_REG_XYZF2);
        break;
    }
    case GS_REG_XYZ2:
    case GS_REG_XYZ3:
    {
        GSVertex &vtx = m_vtxQueue[m_vtxCount % kMaxVerts];
        vtx.x = static_cast<float>(value & 0xFFFF) / 16.0f;
        vtx.y = static_cast<float>((value >> 16) & 0xFFFF) / 16.0f;
        vtx.z = static_cast<float>((value >> 32) & 0xFFFFFFFF);
        vtx.r = m_curR; vtx.g = m_curG; vtx.b = m_curB; vtx.a = m_curA;
        vtx.q = m_curQ; vtx.s = m_curS; vtx.t = m_curT;
        vtx.u = m_curU; vtx.v = m_curV; vtx.fog = m_curFog;
        vertexKick(regAddr == GS_REG_XYZ2);
        break;
    }
    case GS_REG_TEX0_1:
    case GS_REG_TEX0_2:
    {
        int ci = (regAddr == GS_REG_TEX0_2) ? 1 : 0;
        auto &t = m_ctx[ci].tex0;
        t.tbp0 = static_cast<uint32_t>(value & 0x3FFF);
        t.tbw = static_cast<uint8_t>((value >> 14) & 0x3F);
        t.psm = static_cast<uint8_t>((value >> 20) & 0x3F);
        t.tw = static_cast<uint8_t>((value >> 26) & 0xF);
        t.th = static_cast<uint8_t>((value >> 30) & 0xF);
        t.tcc = static_cast<uint8_t>((value >> 34) & 0x1);
        t.tfx = static_cast<uint8_t>((value >> 35) & 0x3);
        t.cbp = static_cast<uint32_t>((value >> 37) & 0x3FFF);
        t.cpsm = static_cast<uint8_t>((value >> 51) & 0xF);
        t.csm = static_cast<uint8_t>((value >> 55) & 0x1);
        t.csa = static_cast<uint8_t>((value >> 56) & 0x1F);
        t.cld = static_cast<uint8_t>((value >> 61) & 0x7);
        break;
    }
    case GS_REG_CLAMP_1:
    case GS_REG_CLAMP_2:
    {
        int ci = (regAddr == GS_REG_CLAMP_2) ? 1 : 0;
        m_ctx[ci].clamp = value;
        break;
    }
    case GS_REG_FOG:
        m_curFog = static_cast<uint8_t>((value >> 56) & 0xFF);
        break;
    case GS_REG_TEX1_1:
    case GS_REG_TEX1_2:
    {
        int ci = (regAddr == GS_REG_TEX1_2) ? 1 : 0;
        m_ctx[ci].tex1 = value;
        break;
    }
    case GS_REG_TEX2_1:
    case GS_REG_TEX2_2:
        break;
    case GS_REG_XYOFFSET_1:
    case GS_REG_XYOFFSET_2:
    {
        int ci = (regAddr == GS_REG_XYOFFSET_2) ? 1 : 0;
        m_ctx[ci].xyoffset.ofx = static_cast<uint16_t>(value & 0xFFFF);
        m_ctx[ci].xyoffset.ofy = static_cast<uint16_t>((value >> 32) & 0xFFFF);
        break;
    }
    case GS_REG_PRMODECONT:
        m_prmodecont = (value & 1) != 0;
        break;
    case GS_REG_PRMODE:
        if (!m_prmodecont)
        {
            m_prim.iip = ((value >> 3) & 1) != 0;
            m_prim.tme = ((value >> 4) & 1) != 0;
            m_prim.fge = ((value >> 5) & 1) != 0;
            m_prim.abe = ((value >> 6) & 1) != 0;
            m_prim.aa1 = ((value >> 7) & 1) != 0;
            m_prim.fst = ((value >> 8) & 1) != 0;
            m_prim.ctxt = ((value >> 9) & 1) != 0;
            m_prim.fix = ((value >> 10) & 1) != 0;
        }
        break;
    case GS_REG_SCISSOR_1:
    case GS_REG_SCISSOR_2:
    {
        int ci = (regAddr == GS_REG_SCISSOR_2) ? 1 : 0;
        m_ctx[ci].scissor.x0 = static_cast<uint16_t>(value & 0x7FF);
        m_ctx[ci].scissor.x1 = static_cast<uint16_t>((value >> 16) & 0x7FF);
        m_ctx[ci].scissor.y0 = static_cast<uint16_t>((value >> 32) & 0x7FF);
        m_ctx[ci].scissor.y1 = static_cast<uint16_t>((value >> 48) & 0x7FF);
        break;
    }
    case GS_REG_ALPHA_1:
    case GS_REG_ALPHA_2:
    {
        int ci = (regAddr == GS_REG_ALPHA_2) ? 1 : 0;
        m_ctx[ci].alpha = value;
        break;
    }
    case GS_REG_TEST_1:
    case GS_REG_TEST_2:
    {
        int ci = (regAddr == GS_REG_TEST_2) ? 1 : 0;
        m_ctx[ci].test = value;
        break;
    }
    case GS_REG_FRAME_1:
    case GS_REG_FRAME_2:
    {
        int ci = (regAddr == GS_REG_FRAME_2) ? 1 : 0;
        m_ctx[ci].frame.fbp = static_cast<uint32_t>(value & 0x1FF);
        m_ctx[ci].frame.fbw = static_cast<uint32_t>((value >> 16) & 0x3F);
        m_ctx[ci].frame.psm = static_cast<uint8_t>((value >> 24) & 0x3F);
        m_ctx[ci].frame.fbmsk = static_cast<uint32_t>((value >> 32) & 0xFFFFFFFF);
        break;
    }
    case GS_REG_ZBUF_1:
    case GS_REG_ZBUF_2:
    {
        int ci = (regAddr == GS_REG_ZBUF_2) ? 1 : 0;
        m_ctx[ci].zbuf = value;
        break;
    }
    case GS_REG_FBA_1:
    case GS_REG_FBA_2:
    {
        int ci = (regAddr == GS_REG_FBA_2) ? 1 : 0;
        m_ctx[ci].fba = value;
        break;
    }
    case GS_REG_BITBLTBUF:
    {
        m_bitbltbuf.sbp = static_cast<uint32_t>(value & 0x3FFF);
        m_bitbltbuf.sbw = static_cast<uint8_t>((value >> 16) & 0x3F);
        m_bitbltbuf.spsm = static_cast<uint8_t>((value >> 24) & 0x3F);
        m_bitbltbuf.dbp = static_cast<uint32_t>((value >> 32) & 0x3FFF);
        m_bitbltbuf.dbw = static_cast<uint8_t>((value >> 48) & 0x3F);
        m_bitbltbuf.dpsm = static_cast<uint8_t>((value >> 56) & 0x3F);
        break;
    }
    case GS_REG_TRXPOS:
    {
        m_trxpos.ssax = static_cast<uint16_t>(value & 0x7FF);
        m_trxpos.ssay = static_cast<uint16_t>((value >> 16) & 0x7FF);
        m_trxpos.dsax = static_cast<uint16_t>((value >> 32) & 0x7FF);
        m_trxpos.dsay = static_cast<uint16_t>((value >> 48) & 0x7FF);
        m_trxpos.dir = static_cast<uint8_t>((value >> 59) & 0x3);
        break;
    }
    case GS_REG_TRXREG:
    {
        m_trxreg.rrw = static_cast<uint16_t>(value & 0xFFF);
        m_trxreg.rrh = static_cast<uint16_t>((value >> 32) & 0xFFF);
        break;
    }
    case GS_REG_TRXDIR:
    {
        m_trxdir = static_cast<uint32_t>(value & 0x3);
        m_hwregX = 0;
        m_hwregY = 0;

        if (m_trxdir == 2 && m_vram)
        {
            uint32_t sbp  = m_bitbltbuf.sbp;
            uint8_t  sbw  = m_bitbltbuf.sbw;
            uint8_t  spsm = m_bitbltbuf.spsm;
            uint32_t dbp  = m_bitbltbuf.dbp;
            uint8_t  dbw  = m_bitbltbuf.dbw;
            uint8_t  dpsm = m_bitbltbuf.dpsm;

            if (sbw == 0) sbw = 1;
            if (dbw == 0) dbw = 1;

            uint32_t srcBase  = sbp * 256u;
            uint32_t dstBase  = dbp * 256u;
            uint32_t srcBpp   = bitsPerPixel(spsm) / 8u;
            uint32_t dstBpp   = bitsPerPixel(dpsm) / 8u;
            if (srcBpp == 0) srcBpp = 4;
            if (dstBpp == 0) dstBpp = 4;
            uint32_t srcStride = static_cast<uint32_t>(sbw) * 64u * srcBpp;
            uint32_t dstStride = static_cast<uint32_t>(dbw) * 64u * dstBpp;
            uint32_t rrw = m_trxreg.rrw;
            uint32_t rrh = m_trxreg.rrh;
            uint32_t ssax = m_trxpos.ssax;
            uint32_t ssay = m_trxpos.ssay;
            uint32_t dsax = m_trxpos.dsax;
            uint32_t dsay = m_trxpos.dsay;
            uint32_t copyBpp = (srcBpp < dstBpp) ? srcBpp : dstBpp;
            uint32_t rowBytes = rrw * copyBpp;

            if (dstBase > srcBase)
            {
                for (int row = static_cast<int>(rrh) - 1; row >= 0; --row)
                {
                    uint32_t srcOff = srcBase + (ssay + row) * srcStride + ssax * srcBpp;
                    uint32_t dstOff = dstBase + (dsay + row) * dstStride + dsax * dstBpp;
                    if (srcOff + rowBytes <= m_vramSize && dstOff + rowBytes <= m_vramSize)
                        std::memmove(m_vram + dstOff, m_vram + srcOff, rowBytes);
                }
            }
            else
            {
                for (uint32_t row = 0; row < rrh; ++row)
                {
                    uint32_t srcOff = srcBase + (ssay + row) * srcStride + ssax * srcBpp;
                    uint32_t dstOff = dstBase + (dsay + row) * dstStride + dsax * dstBpp;
                    if (srcOff + rowBytes <= m_vramSize && dstOff + rowBytes <= m_vramSize)
                        std::memmove(m_vram + dstOff, m_vram + srcOff, rowBytes);
                }
            }

            if (sbp == 0u && (dbp == 0u || dbp == 0x20u) && rrw >= 640u && rrh >= 512u) {
                m_lastDisplayBaseBytes = (dbp == 0x20u) ? 8192u : 0u;
                snapshotVRAM();
            }
        }
        else if (m_trxdir == 1 && m_vram)
        {
            performLocalToHostToBuffer();
        }
        break;
    }
    case GS_REG_HWREG:
    {
        uint8_t buf[8];
        std::memcpy(buf, &value, 8);
        processImageData(buf, 8);
        break;
    }
    case GS_REG_TEXFLUSH:
    case GS_REG_TEXCLUT:
    case GS_REG_SCANMSK:
    case GS_REG_FOGCOL:
    case GS_REG_DIMX:
    case GS_REG_DTHE:
    case GS_REG_COLCLAMP:
    case GS_REG_PABE:
    case GS_REG_MIPTBP1_1:
    case GS_REG_MIPTBP1_2:
    case GS_REG_MIPTBP2_1:
    case GS_REG_MIPTBP2_2:
    case GS_REG_TEXA:
        break;
    case GS_REG_SIGNAL:
    {
        if (m_privRegs)
        {
            uint32_t id   = static_cast<uint32_t>(value & 0xFFFFFFFF);
            uint32_t mask = static_cast<uint32_t>(value >> 32);
            uint32_t lo   = static_cast<uint32_t>(m_privRegs->siglblid & 0xFFFFFFFF);
            lo = (lo & ~mask) | (id & mask);
            m_privRegs->siglblid = (m_privRegs->siglblid & 0xFFFFFFFF00000000ULL) | lo;
            m_privRegs->csr |= 0x1;
        }
        break;
    }
    case GS_REG_FINISH:
    {
        if (m_privRegs)
            m_privRegs->csr |= 0x2;
        break;
    }
    case GS_REG_LABEL:
    {
        if (m_privRegs)
        {
            uint32_t id   = static_cast<uint32_t>(value & 0xFFFFFFFF);
            uint32_t mask = static_cast<uint32_t>(value >> 32);
            uint32_t hi   = static_cast<uint32_t>(m_privRegs->siglblid >> 32);
            hi = (hi & ~mask) | (id & mask);
            m_privRegs->siglblid = (static_cast<uint64_t>(hi) << 32) | (m_privRegs->siglblid & 0xFFFFFFFF);
        }
        break;
    }
    case 0x59:
        if (m_privRegs)
            m_privRegs->dispfb1 = value;
        break;
    case 0x5a:
        if (m_privRegs)
            m_privRegs->display1 = value;
        break;
    case 0x5f:
        if (m_privRegs)
            m_privRegs->bgcolor = value;
        break;
    default:
        break;
    }
}

void GS::vertexKick(bool drawing)
{
    ++m_vtxCount;
    ++m_vtxIndex;

    if (!drawing)
        return;

    int needed = 0;
    switch (m_prim.type)
    {
    case GS_PRIM_POINT:      needed = 1; break;
    case GS_PRIM_LINE:        needed = 2; break;
    case GS_PRIM_LINESTRIP:   needed = 2; break;
    case GS_PRIM_TRIANGLE:    needed = 3; break;
    case GS_PRIM_TRISTRIP:    needed = 3; break;
    case GS_PRIM_TRIFAN:      needed = 3; break;
    case GS_PRIM_SPRITE:      needed = 2; break;
    default: return;
    }

    if (m_vtxCount < needed)
        return;

    m_rasterizer.drawPrimitive(this);

    switch (m_prim.type)
    {
    case GS_PRIM_LINE:
    case GS_PRIM_TRIANGLE:
    case GS_PRIM_SPRITE:
    case GS_PRIM_POINT:
        m_vtxCount = 0;
        break;
    case GS_PRIM_LINESTRIP:
        m_vtxQueue[0] = m_vtxQueue[1];
        m_vtxCount = 1;
        break;
    case GS_PRIM_TRISTRIP:
        m_vtxQueue[0] = m_vtxQueue[1];
        m_vtxQueue[1] = m_vtxQueue[2];
        m_vtxCount = 2;
        break;
    case GS_PRIM_TRIFAN:
        m_vtxQueue[1] = m_vtxQueue[2];
        m_vtxCount = 2;
        break;
    default:
        m_vtxCount = 0;
        break;
    }
}

void GS::processImageData(const uint8_t *data, uint32_t sizeBytes)
{
    if (m_trxdir != 0 || !m_vram)
        return;

    uint32_t dbp = m_bitbltbuf.dbp;
    uint8_t dbw = m_bitbltbuf.dbw;
    uint8_t dpsm = m_bitbltbuf.dpsm;

    if (dbw == 0) dbw = 1;
    uint32_t base = dbp * 256u;
    uint32_t bpp = bitsPerPixel(dpsm);
    uint32_t stridePixels = static_cast<uint32_t>(dbw) * 64u;

    uint32_t rrw = m_trxreg.rrw;
    uint32_t rrh = m_trxreg.rrh;
    uint32_t dsax = m_trxpos.dsax;
    uint32_t dsay = m_trxpos.dsay;

    if (bpp == 4)
    {
        uint32_t rowBytes = (rrw + 1u) / 2u;
        if (rowBytes == 0) rowBytes = 1;
        uint32_t widthBlocks = (dbw != 0) ? static_cast<uint32_t>(dbw) : 1u;
        for (uint32_t y = 0; y < rrh && (y * rowBytes) < sizeBytes; ++y)
        {
            uint32_t srcRowOff = y * rowBytes;
            for (uint32_t x = 0; x < rrw && (srcRowOff + (x / 2u)) < sizeBytes; ++x)
            {
                uint32_t srcByte = data[srcRowOff + (x / 2u)];
                uint32_t nibble = (x & 1u) ? ((srcByte >> 4) & 0xFu) : (srcByte & 0xFu);

                uint32_t vx = dsax + x;
                uint32_t vy = dsay + y;
                uint32_t nibbleAddr = GSPSMT4::addrPSMT4(dbp, widthBlocks, vx, vy);
                uint32_t byteOff = nibbleAddr >> 1;

                if (byteOff < m_vramSize)
                {
                    int shift = static_cast<int>((nibbleAddr & 1u) << 2);
                    uint8_t &b = m_vram[byteOff];
                    b = static_cast<uint8_t>((b & (0xF0u >> shift)) | ((nibble & 0x0Fu) << shift));
                }
            }
        }
        m_hwregX = 0;
        m_hwregY = rrh;
    }
    else if (dpsm == GS_PSM_CT24 || dpsm == GS_PSM_Z24)
    {
        uint32_t storageBpp = 4;
        uint32_t transferBpp = 3;
        uint32_t storageStride = stridePixels * storageBpp;

        uint32_t offset = 0;
        while (offset < sizeBytes && m_hwregY < rrh)
        {
            uint32_t dstY = dsay + m_hwregY;
            uint32_t pixelsLeft = rrw - m_hwregX;
            uint32_t srcBytesLeft = pixelsLeft * transferBpp;
            uint32_t bytesAvail = sizeBytes - offset;
            uint32_t pixelsToCopy = pixelsLeft;
            if (srcBytesLeft > bytesAvail)
                pixelsToCopy = bytesAvail / transferBpp;

            if (pixelsToCopy == 0)
                break;

            uint32_t dstOff = base + dstY * storageStride + (dsax + m_hwregX) * storageBpp;
            if (dstOff + pixelsToCopy * storageBpp <= m_vramSize && pixelsToCopy > 0)
            {
                for (uint32_t p = 0; p < pixelsToCopy; ++p)
                {
                    m_vram[dstOff + p * 4 + 0] = data[offset + p * 3 + 0];
                    m_vram[dstOff + p * 4 + 1] = data[offset + p * 3 + 1];
                    m_vram[dstOff + p * 4 + 2] = data[offset + p * 3 + 2];
                    m_vram[dstOff + p * 4 + 3] = 0x80;
                }
            }

            offset += pixelsToCopy * transferBpp;
            m_hwregX += pixelsToCopy;
            if (m_hwregX >= rrw)
            {
                m_hwregX = 0;
                ++m_hwregY;
            }
        }
    }
    else
    {
        uint32_t bytesPerPixel = bpp / 8u;
        if (bytesPerPixel == 0) bytesPerPixel = 4;
        uint32_t strideBytes = stridePixels * bytesPerPixel;
        uint32_t rowBytes = rrw * bytesPerPixel;

        uint32_t offset = 0;
        while (offset < sizeBytes && m_hwregY < rrh)
        {
            uint32_t dstY = dsay + m_hwregY;
            uint32_t pixelsLeft = rrw - m_hwregX;
            uint32_t bytesLeft = pixelsLeft * bytesPerPixel;
            uint32_t bytesAvail = sizeBytes - offset;
            if (bytesLeft > bytesAvail)
                bytesLeft = (bytesAvail / bytesPerPixel) * bytesPerPixel;

            uint32_t dstOff = base + dstY * strideBytes + (dsax + m_hwregX) * bytesPerPixel;
            if (dstOff + bytesLeft <= m_vramSize && bytesLeft > 0)
                std::memcpy(m_vram + dstOff, data + offset, bytesLeft);

            uint32_t pixelsCopied = bytesLeft / bytesPerPixel;
            offset += bytesLeft;
            m_hwregX += pixelsCopied;
            if (m_hwregX >= rrw)
            {
                m_hwregX = 0;
                ++m_hwregY;
            }
        }
    }
}

void GS::performLocalToHostToBuffer()
{
    m_localToHostBuffer.clear();
    m_localToHostReadPos = 0;
    if (!m_vram)
        return;

    uint32_t sbp = m_bitbltbuf.sbp;
    uint8_t sbw = m_bitbltbuf.sbw;
    uint8_t spsm = m_bitbltbuf.spsm;

    if (sbw == 0) sbw = 1;
    uint32_t base = sbp * 256u;
    uint32_t bpp = bitsPerPixel(spsm);
    uint32_t stridePixels = static_cast<uint32_t>(sbw) * 64u;

    uint32_t rrw = m_trxreg.rrw;
    uint32_t rrh = m_trxreg.rrh;
    uint32_t ssax = m_trxpos.ssax;
    uint32_t ssay = m_trxpos.ssay;

    if (bpp == 4)
    {
        uint32_t rowBytes = (rrw + 1u) / 2u;
        if (rowBytes == 0) rowBytes = 1;
        m_localToHostBuffer.reserve(rowBytes * rrh);
        uint32_t widthBlocks = static_cast<uint32_t>(sbw);
        for (uint32_t y = 0; y < rrh; ++y)
        {
            for (uint32_t x = 0; x < rrw; ++x)
            {
                uint32_t vx = ssax + x;
                uint32_t vy = ssay + y;
                uint32_t nibbleAddr = GSPSMT4::addrPSMT4(sbp, widthBlocks, vx, vy);
                uint32_t byteOff = nibbleAddr >> 1;
                uint8_t nibble = 0;
                if (byteOff < m_vramSize)
                {
                    int shift = static_cast<int>((nibbleAddr & 1u) << 2);
                    nibble = static_cast<uint8_t>((m_vram[byteOff] >> shift) & 0x0Fu);
                }
                if (x & 1u)
                    m_localToHostBuffer.back() = static_cast<uint8_t>((m_localToHostBuffer.back() & 0x0Fu) | (nibble << 4));
                else
                    m_localToHostBuffer.push_back(nibble);
            }
        }
    }
    else if (spsm == GS_PSM_CT24 || spsm == GS_PSM_Z24)
    {
        uint32_t storageBpp = 4;
        uint32_t transferBpp = 3;
        uint32_t storageStride = stridePixels * storageBpp;
        m_localToHostBuffer.reserve(rrw * rrh * transferBpp);

        for (uint32_t y = 0; y < rrh; ++y)
        {
            for (uint32_t x = 0; x < rrw; ++x)
            {
                uint32_t srcOff = base + (ssay + y) * storageStride + (ssax + x) * storageBpp;
                if (srcOff + 4 <= m_vramSize)
                {
                    m_localToHostBuffer.push_back(m_vram[srcOff + 0]);
                    m_localToHostBuffer.push_back(m_vram[srcOff + 1]);
                    m_localToHostBuffer.push_back(m_vram[srcOff + 2]);
                }
            }
        }
    }
    else
    {
        uint32_t bytesPerPixel = bpp / 8u;
        if (bytesPerPixel == 0) bytesPerPixel = 4;
        uint32_t strideBytes = stridePixels * bytesPerPixel;
        uint32_t rowBytes = rrw * bytesPerPixel;
        m_localToHostBuffer.reserve(rowBytes * rrh);

        for (uint32_t y = 0; y < rrh; ++y)
        {
            uint32_t srcOff = base + (ssay + y) * strideBytes + ssax * bytesPerPixel;
            if (srcOff + rowBytes <= m_vramSize)
            {
                for (uint32_t i = 0; i < rowBytes; ++i)
                    m_localToHostBuffer.push_back(m_vram[srcOff + i]);
            }
        }
    }
}

uint32_t GS::consumeLocalToHostBytes(uint8_t *dst, uint32_t maxBytes)
{
    if (!dst || maxBytes == 0)
        return 0;
    size_t avail = m_localToHostBuffer.size() - m_localToHostReadPos;
    if (avail == 0)
        return 0;
    size_t toCopy = (avail < maxBytes) ? avail : static_cast<size_t>(maxBytes);
    std::memcpy(dst, m_localToHostBuffer.data() + m_localToHostReadPos, toCopy);
    m_localToHostReadPos += toCopy;
    return static_cast<uint32_t>(toCopy);
}
