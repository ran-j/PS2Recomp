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
    // UNPACK range: 0x60-0x6F (V4-32..V4-5)
};

namespace
{
    static int g_vifLogCount = 0;
    static uint32_t g_vifDirectCount = 0;
    static uint32_t g_vifUnpackCount = 0;
    static uint32_t g_vifTotalCmds = 0;
} // namespace

void PS2Memory::processVIF1Data(uint32_t srcPhys, uint32_t sizeBytes)
{
    if (!m_rdram || !m_gsVRAM || sizeBytes == 0u)
        return;
    if (srcPhys >= PS2_RAM_SIZE)
        return;

    const uint64_t requestedEnd = static_cast<uint64_t>(srcPhys) + static_cast<uint64_t>(sizeBytes);
    if (requestedEnd > static_cast<uint64_t>(PS2_RAM_SIZE))
        sizeBytes = PS2_RAM_SIZE - srcPhys;

    const uint8_t *data = m_rdram + srcPhys;
    uint32_t pos = 0; // byte offset

    while (pos + 4 <= sizeBytes)
    {
        // Read VIF command word (32 bits)
        uint32_t cmd;
        memcpy(&cmd, data + pos, 4);
        pos += 4;

        uint8_t opcode = (cmd >> 24) & 0x7F; // bits 30:24
        // bool irq = (cmd >> 31) & 1;        // bit 31: interrupt
        uint16_t imm = cmd & 0xFFFF;      // bits 15:0 (IMMEDIATE)
        uint8_t num = (cmd >> 16) & 0xFF; // bits 23:16 (NUM)

        g_vifTotalCmds++;

        if (opcode == VIF_NOP)
        {
            // No operation
            continue;
        }
        else if (opcode == VIF_STCYCL)
        {
            // Set write cycle: CL in bits 7:0, WL in bits 15:8
            // Used with UNPACK - store for later
            continue;
        }
        else if (opcode == VIF_OFFSET)
        {
            // Set double-buffer offset
            continue;
        }
        else if (opcode == VIF_BASE)
        {
            // Set double-buffer base
            continue;
        }
        else if (opcode == VIF_ITOP)
        {
            // Set ITOP register
            continue;
        }
        else if (opcode == VIF_STMOD)
        {
            // Set decompression mode
            continue;
        }
        else if (opcode == VIF_MSKPATH3)
        {
            // Mask/unmask GIF PATH3
            continue;
        }
        else if (opcode == VIF_MARK)
        {
            // Set MARK register
            continue;
        }
        else if (opcode == VIF_FLUSHE || opcode == VIF_FLUSH || opcode == VIF_FLUSHA)
        {
            // Wait for pipeline flush - no-op in software
            continue;
        }
        else if (opcode == VIF_MSCAL || opcode == VIF_MSCALF)
        {
            // Start VU1 microprogram at address IMM - skip (no VU1 emu)
            continue;
        }
        else if (opcode == VIF_MSCNT)
        {
            // Continue VU1 execution - skip
            continue;
        }
        else if (opcode == VIF_STMASK)
        {
            // Next QW contains write mask - skip 4 bytes
            pos += 4;
            if (pos > sizeBytes)
                break;
            continue;
        }
        else if (opcode == VIF_STROW)
        {
            // Next 4 words (16 bytes) = fill row registers
            pos += 16;
            if (pos > sizeBytes)
                break;
            continue;
        }
        else if (opcode == VIF_STCOL)
        {
            // Next 4 words (16 bytes) = fill column registers
            pos += 16;
            if (pos > sizeBytes)
                break;
            continue;
        }
        else if (opcode == VIF_MPG)
        {
            // Upload microprogram to VU1: NUM*8 bytes of data follow
            uint32_t mpgBytes = (uint32_t)num * 8;
            // Align to QW
            mpgBytes = (mpgBytes + 15) & ~15u;
            pos += mpgBytes;
            if (pos > sizeBytes)
                break;
            continue;
        }
        else if (opcode == VIF_DIRECT || opcode == VIF_DIRECTHL)
        {
            // IMM = number of 128-bit quadwords of GIF data following
            uint32_t qwCount = imm;
            if (qwCount == 0)
                qwCount = 65536; // 0 means 65536
            const uint32_t availableQw = (sizeBytes - pos) / 16u;
            const bool truncated = qwCount > availableQw;
            if (qwCount > availableQw)
            {
                qwCount = availableQw;
            }

            if (qwCount > 0)
            {
                // The GIF data starts at current position in the source buffer
                // processGIFPacket expects a physical RAM address
                uint32_t gifPhysAddr = srcPhys + pos;
                processGIFPacket(gifPhysAddr, qwCount);
                g_vifDirectCount++;
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
            // UNPACK commands (0x60-0x7F)
            // Format: VN in bits 25:24, VL in bits 27:26
            // NUM = number of vectors, IMM = VU addr
            // Skip the data payload
            uint8_t vn = (opcode >> 2) & 0x3; // 0=S, 1=V2, 2=V3, 3=V4
            uint8_t vl = opcode & 0x3;        // 0=32, 1=16, 2=8, 3=5

            // Calculate component count and size
            int components = vn + 1;
            int bitsPerComponent;
            switch (vl)
            {
            case 0:
                bitsPerComponent = 32;
                break;
            case 1:
                bitsPerComponent = 16;
                break;
            case 2:
                bitsPerComponent = 8;
                break;
            case 3:
                bitsPerComponent = 16;
                break; // V4-5 is special (4x16 packed)
            default:
                bitsPerComponent = 32;
                break;
            }

            // Total bits per vector
            int bitsPerVector;
            if (vl == 3 && vn == 3)
            {
                // V4-5: 4 components × 4-bit nibbles = 16 bits per vector.
                bitsPerVector = 16;
            }
            else
            {
                bitsPerVector = components * bitsPerComponent;
            }

            uint32_t bytesPerVector = (bitsPerVector + 7) / 8;
            uint32_t totalBytes = (uint32_t)num * bytesPerVector;
            // Align to 32-bit word boundary
            totalBytes = (totalBytes + 3) & ~3u;

            pos += totalBytes;
            g_vifUnpackCount++;

            if (pos > sizeBytes)
                break;
            continue;
        }
        else
        {
            // Unknown VIF command - try to continue
            if (g_vifLogCount < 10)
            {
                std::cerr << "[VIF1] Unknown opcode 0x" << std::hex << (int)opcode
                          << " at offset 0x" << (pos - 4) << std::dec << std::endl;
                g_vifLogCount++;
            }
            continue;
        }
    }

    static uint32_t s_logInterval = 0;
    if (++s_logInterval >= 100)
    {
        if (g_vifLogCount < 50)
        {
            std::cerr << "[VIF1] stats: total_cmds=" << g_vifTotalCmds
                      << " direct=" << g_vifDirectCount
                      << " unpack=" << g_vifUnpackCount << std::endl;
            g_vifLogCount++;
        }
        s_logInterval = 0;
    }
}
