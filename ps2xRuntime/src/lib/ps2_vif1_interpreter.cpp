// Based on Blackline Interactive implementation
#include "ps2_memory.h"
#include <cstring>
#include <iostream>

enum VIFCmd : uint8_t
{
    VIF_NOP = 0x00,
    VIF_STCYCL = 0x01,
    VIF_OFFSET = 0x02,
    VIF_BASE = 0x03,
    VIF_ITOP = 0x04,
    VIF_STMOD = 0x05,
    VIF_MSKPATH3 = 0x06,
    VIF_MARK = 0x07,
    VIF_FLUSHE = 0x10,
    VIF_FLUSH = 0x11,
    VIF_FLUSHA = 0x13,
    VIF_MSCAL = 0x14,
    VIF_MSCALF = 0x15,
    VIF_MSCNT = 0x17,
    VIF_STMASK = 0x20,
    VIF_STROW = 0x30,
    VIF_STCOL = 0x31,
    VIF_MPG = 0x4A,
    VIF_DIRECT = 0x50,
    VIF_DIRECTHL = 0x51,
};

void PS2Memory::processVIF1Data(uint32_t srcPhys, uint32_t sizeBytes)
{
    if (!m_rdram || !m_gsVRAM || sizeBytes == 0u)
        return;
    if (srcPhys >= PS2_RAM_SIZE)
        return;

    const uint64_t requestedEnd = static_cast<uint64_t>(srcPhys) + static_cast<uint64_t>(sizeBytes);
    if (requestedEnd > static_cast<uint64_t>(PS2_RAM_SIZE))
        sizeBytes = PS2_RAM_SIZE - srcPhys;

    processVIF1Data(m_rdram + srcPhys, sizeBytes);
}

void PS2Memory::processVIF1Data(const uint8_t *data, uint32_t sizeBytes)
{
    if (!data || !m_gsVRAM || sizeBytes == 0u)
        return;

    uint32_t pos = 0;

    while (pos + 4 <= sizeBytes)
    {
        uint32_t cmd;
        memcpy(&cmd, data + pos, 4);
        pos += 4;

        uint8_t opcode = (cmd >> 24) & 0x7F;
        uint16_t imm = cmd & 0xFFFF;
        uint8_t num = (cmd >> 16) & 0xFF;

        if (opcode == VIF_NOP)
        {
            continue;
        }
        else if (opcode == VIF_STCYCL)
        {
            vif1_regs.cycle = imm;
            continue;
        }
        else if (opcode == VIF_OFFSET)
        {
            vif1_regs.ofst = imm & 0x3FFu;
            continue;
        }
        else if (opcode == VIF_BASE)
        {
            vif1_regs.base = imm & 0x3FFu;
            continue;
        }
        else if (opcode == VIF_ITOP)
        {
            vif1_regs.itop = imm & 0x3FFu;
            continue;
        }
        else if (opcode == VIF_STMOD)
        {
            vif1_regs.mode = imm & 3u;
            continue;
        }
        else if (opcode == VIF_MSKPATH3)
        {
            m_path3Masked = (imm & 1u) != 0;
            continue;
        }
        else if (opcode == VIF_MARK)
        {
            continue;
        }
        else if (opcode == VIF_FLUSHE || opcode == VIF_FLUSH || opcode == VIF_FLUSHA)
        {
            continue;
        }
        else if (opcode == VIF_MSCAL || opcode == VIF_MSCALF)
        {
            uint32_t startPC = (uint32_t)imm * 8u;
            if (m_vu1MscalCallback)
                m_vu1MscalCallback(startPC, vif1_regs.itop);
            continue;
        }
        else if (opcode == VIF_MSCNT)
        {
            continue;
        }
        else if (opcode == VIF_STMASK)
        {
            pos += 4;
            if (pos > sizeBytes)
                break;
            continue;
        }
        else if (opcode == VIF_STROW)
        {
            pos += 16;
            if (pos > sizeBytes)
                break;
            continue;
        }
        else if (opcode == VIF_STCOL)
        {
            pos += 16;
            if (pos > sizeBytes)
                break;
            continue;
        }
        else if (opcode == VIF_MPG)
        {
            uint32_t destAddr = (uint32_t)imm * 8u;
            uint32_t mpgBytes = (uint32_t)num * 8u;
            mpgBytes = (mpgBytes + 15) & ~15u;
            if (m_vu1Code && destAddr < PS2_VU1_CODE_SIZE && mpgBytes > 0)
            {
                uint32_t copyBytes = mpgBytes;
                if (destAddr + copyBytes > PS2_VU1_CODE_SIZE)
                    copyBytes = PS2_VU1_CODE_SIZE - destAddr;
                if (pos + copyBytes <= sizeBytes)
                    std::memcpy(m_vu1Code + destAddr, data + pos, copyBytes);
            }
            pos += mpgBytes;
            if (pos > sizeBytes)
                break;
            continue;
        }
        else if (opcode == VIF_DIRECT || opcode == VIF_DIRECTHL)
        {
            uint32_t qwCount = imm;
            if (qwCount == 0)
                qwCount = 65536;
            const uint32_t availableQw = (sizeBytes - pos) / 16u;
            const bool truncated = qwCount > availableQw;
            if (qwCount > availableQw)
                qwCount = availableQw;

            if (qwCount > 0)
            {
                submitGifPacket(GifPathId::Path2, data + pos, qwCount * 16);
            }

            pos += qwCount * 16;
            if (truncated)
            {
                pos = sizeBytes;
                break;
            }
            continue;
        }
        else if ((opcode & 0x60) == 0x60)
        {
            uint8_t vn = (opcode >> 2) & 0x3;
            uint8_t vl = opcode & 0x3;
            int components = vn + 1;
            int bitsPerComponent = 32;
            switch (vl)
            {
            case 0: bitsPerComponent = 32; break;
            case 1: bitsPerComponent = 16; break;
            case 2: bitsPerComponent = 8; break;
            case 3: bitsPerComponent = (vn == 3) ? 4 : 16; break;
            default: break;
            }
            int bitsPerVector = (vl == 3 && vn == 3) ? 16 : (components * bitsPerComponent);
            uint32_t bytesPerVector = (bitsPerVector + 7) / 8;
            uint32_t totalBytes = (uint32_t)num * bytesPerVector;
            totalBytes = (totalBytes + 3) & ~3u;

            uint32_t vuAddr = (uint32_t)imm & 0x3FFu;
            if (m_vu1Data && totalBytes > 0 && pos + totalBytes <= sizeBytes)
            {
                if (bytesPerVector == 16 && vuAddr * 16u < PS2_VU1_DATA_SIZE)
                {
                    for (uint32_t i = 0; i < num; ++i)
                    {
                        uint32_t destOff = ((vuAddr + i) & 0x3FFu) * 16u;
                        if (destOff + 16 <= PS2_VU1_DATA_SIZE)
                            std::memcpy(m_vu1Data + destOff, data + pos + i * 16, 16);
                    }
                }
                else
                {
                    uint32_t destOff = vuAddr * 16u;
                    if (destOff < PS2_VU1_DATA_SIZE)
                    {
                        uint32_t copyBytes = totalBytes;
                        if (destOff + copyBytes > PS2_VU1_DATA_SIZE)
                            copyBytes = PS2_VU1_DATA_SIZE - destOff;
                        std::memcpy(m_vu1Data + destOff, data + pos, copyBytes);
                    }
                }
            }
            pos += totalBytes;

            if (pos > sizeBytes)
                break;
            continue;
        }
        else
        {
            continue;
        }
    }
}
