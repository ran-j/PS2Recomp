#ifndef PS2_GS_GPU_H
#define PS2_GS_GPU_H

#include "ps2_gs_rasterizer.h"
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

enum GSPrimType : uint8_t
{
    GS_PRIM_POINT = 0,
    GS_PRIM_LINE = 1,
    GS_PRIM_LINESTRIP = 2,
    GS_PRIM_TRIANGLE = 3,
    GS_PRIM_TRISTRIP = 4,
    GS_PRIM_TRIFAN = 5,
    GS_PRIM_SPRITE = 6,
};

enum GSPsm : uint8_t
{
    GS_PSM_CT32 = 0,
    GS_PSM_CT24 = 1,
    GS_PSM_CT16 = 2,
    GS_PSM_CT16S = 10,
    GS_PSM_T8 = 19,
    GS_PSM_T4 = 20,
    GS_PSM_T8H = 27,
    GS_PSM_T4HL = 36,
    GS_PSM_T4HH = 44,
    GS_PSM_Z32 = 48,
    GS_PSM_Z24 = 49,
    GS_PSM_Z16 = 50,
    GS_PSM_Z16S = 58,
};

enum GSGifFormat : uint8_t
{
    GIF_FMT_PACKED = 0,
    GIF_FMT_REGLIST = 1,
    GIF_FMT_IMAGE = 2,
    GIF_FMT_DISABLED = 3,
};

enum GSRegId : uint8_t
{
    GS_REG_PRIM = 0x00,
    GS_REG_RGBAQ = 0x01,
    GS_REG_ST = 0x02,
    GS_REG_UV = 0x03,
    GS_REG_XYZF2 = 0x04,
    GS_REG_XYZ2 = 0x05,
    GS_REG_TEX0_1 = 0x06,
    GS_REG_TEX0_2 = 0x07,
    GS_REG_CLAMP_1 = 0x08,
    GS_REG_CLAMP_2 = 0x09,
    GS_REG_FOG = 0x0A,
    GS_REG_XYZF3 = 0x0C,
    GS_REG_XYZ3 = 0x0D,
    GS_REG_AD = 0x0F,

    GS_REG_TEX1_1 = 0x14,
    GS_REG_TEX1_2 = 0x15,
    GS_REG_TEX2_1 = 0x16,
    GS_REG_TEX2_2 = 0x17,
    GS_REG_XYOFFSET_1 = 0x18,
    GS_REG_XYOFFSET_2 = 0x19,
    GS_REG_PRMODECONT = 0x1A,
    GS_REG_PRMODE = 0x1B,
    GS_REG_TEXCLUT = 0x1C,
    GS_REG_SCANMSK = 0x22,
    GS_REG_MIPTBP1_1 = 0x34,
    GS_REG_MIPTBP1_2 = 0x35,
    GS_REG_MIPTBP2_1 = 0x36,
    GS_REG_MIPTBP2_2 = 0x37,
    GS_REG_TEXA = 0x3B,
    GS_REG_FOGCOL = 0x3D,
    GS_REG_TEXFLUSH = 0x3F,
    GS_REG_SCISSOR_1 = 0x40,
    GS_REG_SCISSOR_2 = 0x41,
    GS_REG_ALPHA_1 = 0x42,
    GS_REG_ALPHA_2 = 0x43,
    GS_REG_DIMX = 0x44,
    GS_REG_DTHE = 0x45,
    GS_REG_COLCLAMP = 0x46,
    GS_REG_TEST_1 = 0x47,
    GS_REG_TEST_2 = 0x48,
    GS_REG_PABE = 0x49,
    GS_REG_FBA_1 = 0x4A,
    GS_REG_FBA_2 = 0x4B,
    GS_REG_FRAME_1 = 0x4C,
    GS_REG_FRAME_2 = 0x4D,
    GS_REG_ZBUF_1 = 0x4E,
    GS_REG_ZBUF_2 = 0x4F,
    GS_REG_BITBLTBUF = 0x50,
    GS_REG_TRXPOS = 0x51,
    GS_REG_TRXREG = 0x52,
    GS_REG_TRXDIR = 0x53,
    GS_REG_HWREG = 0x54,
    GS_REG_SIGNAL = 0x60,
    GS_REG_FINISH = 0x61,
    GS_REG_LABEL = 0x62,
};

struct GSVertex
{
    float x, y, z;
    uint8_t r, g, b, a;
    float q;
    float s, t;
    uint16_t u, v;
    uint8_t fog;
};

struct GSFrameReg
{
    uint32_t fbp;
    uint32_t fbw;
    uint8_t psm;
    uint32_t fbmsk;
};

struct GSScissorReg
{
    uint16_t x0, x1, y0, y1;
};

struct GSTex0Reg
{
    uint32_t tbp0;
    uint8_t tbw;
    uint8_t psm;
    uint8_t tw;
    uint8_t th;
    uint8_t tcc;
    uint8_t tfx;
    uint32_t cbp;
    uint8_t cpsm;
    uint8_t csm;
    uint8_t csa;
    uint8_t cld;
};

struct GSXYOffsetReg
{
    uint16_t ofx;
    uint16_t ofy;
};

struct GSContext
{
    GSFrameReg frame;
    GSScissorReg scissor;
    GSTex0Reg tex0;
    GSXYOffsetReg xyoffset;
    uint64_t zbuf;
    uint64_t tex1;
    uint64_t clamp;
    uint64_t alpha;
    uint64_t test;
    uint64_t fba;
};

struct GSPrimReg
{
    GSPrimType type;
    bool iip;
    bool tme;
    bool fge;
    bool abe;
    bool aa1;
    bool fst;
    bool ctxt;
    bool fix;
};

struct GSBitBltBuf
{
    uint32_t sbp;
    uint8_t sbw;
    uint8_t spsm;
    uint32_t dbp;
    uint8_t dbw;
    uint8_t dpsm;
};

struct GSTrxPos
{
    uint16_t ssax, ssay;
    uint16_t dsax, dsay;
    uint8_t dir;
};

struct GSTrxReg
{
    uint16_t rrw, rrh;
};

class GSRasterizer;

class GS
{
    friend class GSRasterizer;

public:
    GS();
    ~GS() = default;

    void init(uint8_t *vram, uint32_t vramSize, struct GSRegisters *privRegs = nullptr);
    void reset();

    void processGIFPacket(const uint8_t *data, uint32_t sizeBytes);
    void writeRegister(uint8_t regAddr, uint64_t value);

    const uint8_t *lockDisplaySnapshot(uint32_t &outSize);
    void unlockDisplaySnapshot();
    uint32_t getLastDisplayBaseBytes() const;

    uint32_t consumeLocalToHostBytes(uint8_t *dst, uint32_t maxBytes);

    void refreshDisplaySnapshot();

private:
    void snapshotVRAM();
    void writeRegisterPacked(uint8_t regDesc, uint64_t lo, uint64_t hi);
    void vertexKick(bool drawing);

    void processImageData(const uint8_t *data, uint32_t sizeBytes);
    void performLocalToHostToBuffer();

    GSContext &activeContext();

    uint8_t *m_vram = nullptr;
    uint32_t m_vramSize = 0;
    struct GSRegisters *m_privRegs = nullptr;

    GSContext m_ctx[2];
    GSPrimReg m_prim{};

    uint8_t m_curR = 0x80, m_curG = 0x80, m_curB = 0x80, m_curA = 0x80;
    float m_curQ = 1.0f;
    float m_curS = 0.0f, m_curT = 0.0f;
    uint16_t m_curU = 0, m_curV = 0;
    uint8_t m_curFog = 0;

    bool m_prmodecont = true;

    GSBitBltBuf m_bitbltbuf{};
    GSTrxPos m_trxpos{};
    GSTrxReg m_trxreg{};
    uint32_t m_trxdir = 3;
    uint32_t m_hwregX = 0;
    uint32_t m_hwregY = 0;

    static constexpr int kMaxVerts = 6;
    GSVertex m_vtxQueue[kMaxVerts];
    int m_vtxCount = 0;
    int m_vtxIndex = 0;

    std::vector<uint8_t> m_displaySnapshot;
    std::mutex m_snapshotMutex;
    uint32_t m_lastDisplayBaseBytes = 0;

    std::vector<uint8_t> m_localToHostBuffer;
    size_t m_localToHostReadPos = 0;

    GSRasterizer m_rasterizer;
};

#endif
