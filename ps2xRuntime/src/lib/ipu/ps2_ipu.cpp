// EE IPU core: register file, bitstream FIFO, command state machine and the
// intra decode path. See ps2_ipu.h for the register/command contract.
#include "ps2_ipu.h"
#include "ps2_ipu_tables.h"
#include "ps2_log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace
{
    using namespace ps2ipu;

    constexpr uint32_t kCmdBclr = 0u;
    constexpr uint32_t kCmdIdec = 1u;
    constexpr uint32_t kCmdBdec = 2u;
    constexpr uint32_t kCmdVdec = 3u;
    constexpr uint32_t kCmdFdec = 4u;
    constexpr uint32_t kCmdSetiq = 5u;
    constexpr uint32_t kCmdSetvq = 6u;
    constexpr uint32_t kCmdSetth = 9u;

    // IDP (16-17), AS (20), IVF (21), QST (22), MP1 (23), PCT (24-26).
    constexpr uint32_t kCtrlWritableMask = 0x07F30000u;
    constexpr uint32_t kCtrlRst = 1u << 30;

    constexpr uint32_t kBusyWriteLogLimit = 8u;
    constexpr uint32_t kUnimplementedLogLimit = 32u;
    constexpr uint32_t kIdecErrorLogLimit = 16u;

    const char *const kCmdNames[16] = {
        "BCLR", "IDEC", "BDEC", "VDEC", "FDEC", "SETIQ", "SETVQ", "CSC",
        "PACK", "SETTH", "CMD_A", "CMD_B", "CMD_C", "CMD_D", "CMD_E", "CMD_F"};

    struct VlcEntry
    {
        int16_t value = 0;
        uint8_t len = 0; // 0: no code with this prefix
    };

    template <unsigned Bits>
    struct VlcLut
    {
        std::array<VlcEntry, (size_t(1) << Bits)> entries{};
        void add(uint32_t code, unsigned len, int16_t value)
        {
            const unsigned shift = Bits - len;
            for (uint32_t i = code << shift; i < ((code + 1u) << shift); ++i)
                entries[i] = VlcEntry{value, static_cast<uint8_t>(len)};
        }
        const VlcEntry &operator[](uint32_t prefix) const { return entries[prefix]; }
    };

    constexpr int16_t kMbaEscapeValue = -1;
    constexpr int16_t kMbaStuffingValue = -2;
    constexpr int16_t kAcEscapeValue = kRunLevelCount;
    constexpr int16_t kAcEobValue = kRunLevelCount + 1;

    struct Tables
    {
        VlcLut<11> mba;
        VlcLut<10> dcLuma;
        VlcLut<10> dcChroma;
        VlcLut<16> ac[2];  // [intra_vlc_format]
        double idct[8][8]; // [x][u] = C(u)/2 * cos((2x+1)u*pi/16)
        uint8_t inverseZigzag[64];

        Tables()
        {
            for (int i = 0; i < 33; ++i)
                mba.add(kMbaIncrement[i].code, kMbaIncrement[i].len, static_cast<int16_t>(i + 1));
            mba.add(kMbaEscape.code, kMbaEscape.len, kMbaEscapeValue);
            mba.add(kMbaStuffing.code, kMbaStuffing.len, kMbaStuffingValue);
            for (int s = 0; s < 12; ++s)
            {
                dcLuma.add(kDcSizeLuma[s].code, kDcSizeLuma[s].len, static_cast<int16_t>(s));
                dcChroma.add(kDcSizeChroma[s].code, kDcSizeChroma[s].len, static_cast<int16_t>(s));
            }
            for (int i = 0; i < kRunLevelCount; ++i)
            {
                ac[0].add(kDctTableZero[i].code, kDctTableZero[i].len, static_cast<int16_t>(i));
                ac[1].add(kDctTableOne[i].code, kDctTableOne[i].len, static_cast<int16_t>(i));
            }
            ac[0].add(kDctEscape.code, kDctEscape.len, kAcEscapeValue);
            ac[1].add(kDctEscape.code, kDctEscape.len, kAcEscapeValue);
            ac[0].add(kDctEobTableZero.code, kDctEobTableZero.len, kAcEobValue);
            ac[1].add(kDctEobTableOne.code, kDctEobTableOne.len, kAcEobValue);

            const double pi = 3.14159265358979323846;
            for (int x = 0; x < 8; ++x)
                for (int u = 0; u < 8; ++u)
                    idct[x][u] = (u == 0 ? std::sqrt(0.5) : 1.0) * 0.5 * std::cos((2 * x + 1) * u * pi / 16.0);
            for (int i = 0; i < 64; ++i)
                inverseZigzag[kZigzagScan[i]] = static_cast<uint8_t>(i);
        }
    };

    const Tables &tables()
    {
        static const Tables t;
        return t;
    }

    inline int saturateCoefficient(int v)
    {
        return std::clamp(v, -2048, 2047);
    }

    inline uint8_t clampByte(int v)
    {
        return static_cast<uint8_t>(std::clamp(v, 0, 255));
    }

    // 8x8 inverse DCT (ISO/IEC 13818-2 A.1 definition, separable, double
    // precision, round to nearest, output saturated to [-256, 255] and then
    // to the 8-bit sample range of an intra block).
    void inverseDctToSamples(const int32_t (&f)[64], uint8_t *dst, int stride)
    {
        bool acZero = true;
        for (int i = 1; i < 64 && acZero; ++i)
            acZero = (f[i] == 0);
        if (acZero)
        {
            // f(x,y) = F(0,0) * C(0)^2 / 4 = F(0,0) / 8, rounded.
            const uint8_t v = clampByte(std::clamp((f[0] + 4) >> 3, -256, 255));
            for (int y = 0; y < 8; ++y)
                std::memset(dst + y * stride, v, 8);
            return;
        }
        const auto &k = tables().idct;
        double rows[8][8];
        uint32_t nonZeroRows = 0;
        for (int v = 0; v < 8; ++v)
        {
            const int32_t *in = f + v * 8;
            bool any = false;
            for (int u = 0; u < 8; ++u)
                any = any || in[u] != 0;
            if (!any)
                continue;
            nonZeroRows |= 1u << v;
            for (int x = 0; x < 8; ++x)
            {
                double s = 0.0;
                for (int u = 0; u < 8; ++u)
                    s += k[x][u] * in[u];
                rows[v][x] = s;
            }
        }
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
            {
                double s = 0.0;
                for (int v = 0; v < 8; ++v)
                    if (nonZeroRows & (1u << v))
                        s += k[y][v] * rows[v][x];
                const int r = std::clamp(static_cast<int>(std::floor(s + 0.5)), -256, 255);
                dst[y * stride + x] = clampByte(r);
            }
    }

    // IPU colour space conversion, YCbCr 4:2:0 macroblock -> RGB32, with the
    // IPU's integer method (BT.601 studio swing; coefficients
    // 0x95/0xCC/-0x68/-0x32/0x102 in 1/64ths, final (x+1)>>1) as in PCSX2's
    // reference yuv2rgb (pcsx2/IPU/yuv2rgb.cpp). Alpha is 0x80; the SETTH
    // thresholds and IDEC SGN are applied as PCSX2's ipu_csc does.
    void convertMacroblockRgb32(const uint8_t *y, const uint8_t *cb, const uint8_t *cr, uint8_t *out,
                                uint16_t th0, uint16_t th1, bool sgn)
    {
        for (int py = 0; py < 16; ++py)
            for (int px = 0; px < 16; ++px)
            {
                const int c = (py >> 1) * 8 + (px >> 1);
                const int cbv = static_cast<int>(cb[c]) - 128;
                const int crv = static_cast<int>(cr[c]) - 128;
                const int lum = (0x95 * std::max(0, static_cast<int>(y[py * 16 + px]) - 16)) >> 6;
                const int rcr = (0xCC * crv) >> 6;
                const int gcr = (-0x68 * crv) >> 6;
                const int gcb = (-0x32 * cbv) >> 6;
                const int bcb = (0x102 * cbv) >> 6;
                uint8_t *p = out + (py * 16 + px) * 4;
                p[0] = clampByte((lum + rcr + 1) >> 1);
                p[1] = clampByte((lum + gcr + gcb + 1) >> 1);
                p[2] = clampByte((lum + bcb + 1) >> 1);
                p[3] = 0x80;
                if (th0 > 0)
                {
                    if (p[0] < th0 && p[1] < th0 && p[2] < th0)
                        std::memset(p, 0, 4);
                    else if (p[0] < th1 && p[1] < th1 && p[2] < th1)
                        p[3] = 0x40;
                }
                else if (th1 > 0 && p[0] < th1 && p[1] < th1 && p[2] < th1)
                {
                    p[3] = 0x40;
                }
                if (sgn)
                {
                    p[0] ^= 0x80;
                    p[1] ^= 0x80;
                    p[2] ^= 0x80;
                }
            }
    }
} // namespace

struct Ps2Ipu::Cursor
{
    const Ps2Ipu &ipu;
    size_t pos;
    size_t limit;
    bool starved = false;

    uint32_t show(unsigned n)
    {
        if (pos + n > limit)
            starved = true;
        return ipu.peekBits(pos, n);
    }
    uint32_t get(unsigned n)
    {
        const uint32_t v = show(n);
        pos += n;
        return v;
    }
    int32_t getSigned(unsigned n)
    {
        const uint32_t v = get(n);
        return static_cast<int32_t>(v << (32u - n)) >> (32u - n);
    }
};

void Ps2Ipu::reset()
{
    softReset();
    m_ctrlConfig = 0;
    std::memset(m_intraQuant, 0, sizeof(m_intraQuant));
    std::memset(m_nonIntraQuant, 0, sizeof(m_nonIntraQuant));
    std::memset(m_vqClut, 0, sizeof(m_vqClut));
}

void Ps2Ipu::softReset()
{
    m_in.clear();
    m_bitPos = 0;
    m_out.clear();
    m_outHead = 0;
    m_cbp = 0;
    m_th0 = 0;
    m_th1 = 0;
    m_ecd = false;
    m_scd = false;
    m_top = 0;
    m_cmdData = 0;
    m_lastCode = 0xFFFFFFFFu;
    m_pending = false;
    m_pendingCmd = 0;
    m_busyUntilDrained = false;
}

size_t Ps2Ipu::availableBits() const
{
    const size_t total = m_in.size() * 8u;
    return total > m_bitPos ? total - m_bitPos : 0u;
}

uint32_t Ps2Ipu::peekBits(size_t bitPos, unsigned count) const
{
    const size_t byte = bitPos >> 3;
    const unsigned shift = static_cast<unsigned>(bitPos & 7u);
    uint64_t window = 0;
    for (size_t i = 0; i < 5; ++i)
    {
        window <<= 8;
        if (byte + i < m_in.size())
            window |= m_in[byte + i];
    }
    const uint64_t mask = (count >= 32u) ? 0xFFFFFFFFull : ((1ull << count) - 1ull);
    return static_cast<uint32_t>((window >> (40u - shift - count)) & mask);
}

void Ps2Ipu::copyBits(size_t bitPos, uint8_t *dst, size_t bytes) const
{
    for (size_t i = 0; i < bytes; ++i)
        dst[i] = static_cast<uint8_t>(peekBits(bitPos + i * 8u, 8));
}

bool Ps2Ipu::busy() const
{
    return m_pending || (m_busyUntilDrained && outputBytes() != 0);
}

void Ps2Ipu::compactInput()
{
    // Drop fully consumed quadwords once enough have accumulated; keeps the
    // FIFO quadword-aligned so BP stays the offset within the current QW.
    constexpr size_t kCompactBytes = 1u << 20;
    const size_t consumedQw = std::min(m_bitPos / 128u, m_in.size() / 16u);
    if (consumedQw * 16u < kCompactBytes)
        return;
    m_in.erase(m_in.begin(), m_in.begin() + static_cast<std::ptrdiff_t>(consumedQw * 16u));
    m_bitPos -= consumedQw * 128u;
}

void Ps2Ipu::pushInput(const uint8_t *data, size_t size)
{
    if (data && size != 0)
        m_in.insert(m_in.end(), data, data + size);
}

size_t Ps2Ipu::drainOutput(uint8_t *dst, size_t maxBytes)
{
    const size_t n = std::min(maxBytes, outputBytes());
    std::memcpy(dst, m_out.data() + m_outHead, n);
    m_outHead += n;
    if (m_outHead == m_out.size())
    {
        m_out.clear();
        m_outHead = 0;
        m_busyUntilDrained = false;
    }
    return n;
}

uint32_t Ps2Ipu::readControl() const
{
    const size_t totalQw = m_in.size() / 16u;
    const size_t consumedQw = std::min(m_bitPos / 128u, totalQw);
    const size_t remainingQw = totalQw - consumedQw;
    const size_t fp = std::min<size_t>(remainingQw, 2u);
    const uint32_t ifc = static_cast<uint32_t>(std::min<size_t>(remainingQw - fp, 8u));
    const uint32_t ofc = static_cast<uint32_t>(std::min<size_t>(outputBytes() / 16u, 8u));
    uint32_t v = m_ctrlConfig & kCtrlWritableMask;
    v |= ifc;
    v |= ofc << 4;
    v |= (m_cbp & 0x3Fu) << 8;
    v |= m_ecd ? (1u << 14) : 0u;
    v |= m_scd ? (1u << 15) : 0u;
    v |= busy() ? (1u << 31) : 0u;
    return v;
}

uint32_t Ps2Ipu::readBp() const
{
    const size_t totalQw = m_in.size() / 16u;
    const size_t consumedQw = std::min(m_bitPos / 128u, totalQw);
    const size_t remainingQw = totalQw - consumedQw;
    const size_t fp = std::min<size_t>(remainingQw, 2u);
    const size_t ifc = std::min<size_t>(remainingQw - fp, 8u);
    return static_cast<uint32_t>((m_bitPos & 0x7Fu) | (ifc << 8) | (fp << 16));
}

uint64_t Ps2Ipu::readTop() const
{
    // Bit 63 means fewer than 32 bits are queued (PS2Tek: FMV libraries
    // poll it to wait for data), not that a command is running.
    if (availableBits() >= 32u)
        return peekBits(m_bitPos, 32);
    return (1ull << 63) | m_top;
}

uint64_t Ps2Ipu::readCommand()
{
    // DATA holds the FDEC/VDEC result; after any other command a read
    // returns the next 32 bitstream bits (PS2Tek; PCSX2 ipuRead32).
    if (m_lastCode != kCmdFdec && m_lastCode != kCmdVdec && availableBits() >= 32u)
        m_cmdData = peekBits(m_bitPos, 32);
    return static_cast<uint64_t>(m_cmdData) | (busy() ? (1ull << 63) : 0ull);
}

void Ps2Ipu::writeControl(uint32_t value)
{
    m_ctrlConfig = value & kCtrlWritableMask;
    if (value & kCtrlRst)
        softReset();
}

void Ps2Ipu::writeCommand(uint32_t value)
{
    const uint32_t code = value >> 28;
    if (busy() && m_busyWritesReported < kBusyWriteLogLimit)
    {
        ++m_busyWritesReported;
        RUNTIME_ERROR("[IPU] " << kCmdNames[code] << " written while "
                               << (m_pending ? kCmdNames[m_pendingCmd >> 28] : "IDEC output drain")
                               << " is still busy; the previous command is abandoned" << std::endl);
    }
    m_ecd = false;
    m_scd = false;
    m_pending = false;
    m_busyUntilDrained = false;
    m_lastCode = code;

    switch (code)
    {
    case kCmdBclr:
        m_in.clear();
        m_bitPos = value & 0x7Fu;
        return;
    case kCmdSetth:
        m_th0 = static_cast<uint16_t>(value & 0x1FFu);
        m_th1 = static_cast<uint16_t>((value >> 16) & 0x1FFu);
        return;
    case kCmdIdec:
    case kCmdBdec:
    case kCmdVdec:
    case kCmdFdec:
    case kCmdSetiq:
        m_bitPos += value & 0x3Fu; // FB: bits skipped before the command runs
        break;
    default:
        break;
    }
    m_pending = true;
    m_pendingCmd = value;
    runPending();
}

void Ps2Ipu::runPending()
{
    if (!m_pending)
        return;
    const uint32_t code = m_pendingCmd >> 28;
    bool done = false;
    switch (code)
    {
    case kCmdFdec:
        if (availableBits() >= 32u)
        {
            m_cmdData = peekBits(m_bitPos, 32);
            m_top = m_cmdData;
            done = true;
        }
        break;
    case kCmdSetiq:
        if (availableBits() >= 512u)
        {
            const bool nonIntra = ((m_pendingCmd >> 27) & 1u) != 0u;
            copyBits(m_bitPos, nonIntra ? m_nonIntraQuant : m_intraQuant, 64);
            m_bitPos += 512u;
            done = true;
        }
        break;
    case kCmdSetvq:
        if (availableBits() >= 256u)
        {
            copyBits(m_bitPos, m_vqClut, sizeof(m_vqClut));
            m_bitPos += 256u;
            done = true;
        }
        break;
    case kCmdIdec:
        done = runIdec();
        break;
    default:
        // BDEC, VDEC, CSC, PACK and the reserved codes: flag an error
        // instead of faking a result.
        if (m_unimplementedReported < kUnimplementedLogLimit)
        {
            ++m_unimplementedReported;
            RUNTIME_ERROR("[IPU:unimplemented] " << kCmdNames[code] << " data=0x" << std::hex
                                                 << (m_pendingCmd & 0x0FFFFFFFu) << std::dec
                                                 << " -> ECD set, no output" << std::endl);
        }
        m_ecd = true;
        done = true;
        break;
    }
    if (done)
    {
        m_pending = false;
        compactInput();
    }
}

bool Ps2Ipu::runIdec()
{
    const Tables &t = tables();
    const uint32_t cmd = m_pendingCmd;
    const bool dtd = ((cmd >> 24) & 1u) != 0u;
    const bool sgn = ((cmd >> 25) & 1u) != 0u;
    const bool ofm = ((cmd >> 27) & 1u) != 0u;
    const uint32_t qsc = (cmd >> 16) & 0x1Fu;
    const bool mpeg1 = ((m_ctrlConfig >> 23) & 1u) != 0u;
    const bool qst = ((m_ctrlConfig >> 22) & 1u) != 0u;
    const bool ivf = !mpeg1 && ((m_ctrlConfig >> 21) & 1u) != 0u;
    const bool alternateScan = ((m_ctrlConfig >> 20) & 1u) != 0u;
    const int idp = mpeg1 ? 0 : static_cast<int>((m_ctrlConfig >> 16) & 3u);

    if (ofm)
    {
        if (m_unimplementedReported < kUnimplementedLogLimit)
        {
            ++m_unimplementedReported;
            RUNTIME_ERROR("[IPU:unimplemented] IDEC OFM=1 (RGB16 output) -> ECD set, no output" << std::endl);
        }
        m_ecd = true;
        return true;
    }

    const uint8_t *scan = alternateScan ? kAlternateScan : kZigzagScan;
    // SETIQ matrices arrive in zigzag order (ISO/IEC 13818-2 downloads them
    // that way whatever the scan); weight each coefficient by its raster
    // frequency position, also under alternate scan. PCSX2 indexes the
    // matrix by scan position instead.
    uint8_t weight[64];
    for (int p = 0; p < 64; ++p)
        weight[p] = m_intraQuant[t.inverseZigzag[p]];
    const VlcLut<16> &acLut = t.ac[ivf ? 1 : 0];

    Cursor c{*this, m_bitPos, m_in.size() * 8u};
    // QSC is a quantiser_scale_code, mapped like the one in the stream and
    // like BDEC's (PCSX2 uses IDEC's QSC unmapped).
    int quantiserScale = qst ? kNonLinearQuantiserScale[qsc] : static_cast<int>(qsc << 1);
    const int dcReset = 128 << idp;
    const int dcShift = 3 - idp;
    int dcPred[3] = {dcReset, dcReset, dcReset};

    // Macroblocks are converted straight into the output FIFO and only
    // committed once the whole run has been parsed.
    const size_t outStart = m_out.size();
    const char *error = nullptr;
    uint64_t macroblocks = 0;
    alignas(16) uint8_t yPlane[256];
    alignas(16) uint8_t cbPlane[64];
    alignas(16) uint8_t crPlane[64];
    int32_t coef[64];

    while (!error)
    {
        // Table B.2, I-picture: '1' intra, '01' intra + quant.
        const uint32_t mbType = c.show(2);
        bool quant = false;
        if (mbType & 2u)
        {
            c.pos += 1;
        }
        else if (mbType == 1u)
        {
            c.pos += 2;
            quant = true;
        }
        else
        {
            error = "macroblock_type";
            break;
        }
        const bool fieldDct = dtd && c.get(1) != 0u;
        if (quant)
        {
            const uint32_t code = c.get(5);
            quantiserScale = qst ? kNonLinearQuantiserScale[code] : static_cast<int>(code << 1);
        }

        for (int block = 0; block < 6 && !error; ++block)
        {
            const int cc = block < 4 ? 0 : block - 3;
            std::memset(coef, 0, sizeof(coef));

            const VlcEntry &dc = (cc == 0 ? t.dcLuma : t.dcChroma)[c.show(10)];
            if (dc.len == 0)
            {
                error = "dct_dc_size";
                break;
            }
            c.pos += dc.len;
            int diff = 0;
            if (dc.value > 0)
            {
                diff = static_cast<int>(c.get(static_cast<unsigned>(dc.value)));
                if ((diff & (1 << (dc.value - 1))) == 0)
                    diff -= (1 << dc.value) - 1;
            }
            dcPred[cc] += diff;
            coef[0] = saturateCoefficient(dcPred[cc] << dcShift);
            int sum = coef[0];

            for (int i = 0;;)
            {
                const VlcEntry &e = acLut[c.show(16)];
                if (e.len == 0)
                {
                    error = "dct_coefficient";
                    break;
                }
                c.pos += e.len;
                if (e.value == kAcEobValue)
                    break;
                int run = 0;
                int level = 0;
                if (e.value == kAcEscapeValue)
                {
                    run = static_cast<int>(c.get(6));
                    if (!mpeg1)
                    {
                        level = c.getSigned(12);
                    }
                    else
                    {
                        level = c.getSigned(8);
                        if (level == 0)
                            level = static_cast<int>(c.get(8));
                        else if (level == -128)
                            level = static_cast<int>(c.get(8)) - 256;
                    }
                    if (level == 0)
                    {
                        error = "escape level 0";
                        break;
                    }
                }
                else
                {
                    run = kRunLevelRun[e.value];
                    level = kRunLevelLevel[e.value];
                    if (c.get(1))
                        level = -level;
                }
                i += run + 1;
                if (i > 63)
                {
                    error = "run past coefficient 63";
                    break;
                }
                const int pos = scan[i];
                // Intra: (2 * QF * W * quantiser_scale) / 32, truncated toward zero.
                int value = (std::abs(level) * quantiserScale * weight[pos]) >> 4;
                if (mpeg1 && value != 0 && (value & 1) == 0)
                    value -= 1; // MPEG-1 oddification toward zero
                value = saturateCoefficient(level < 0 ? -value : value);
                coef[pos] = value;
                sum += value;
            }
            if (error)
                break;
            if (!mpeg1 && (sum & 1) == 0)
                coef[63] ^= 1; // MPEG-2 mismatch control

            uint8_t *dst = nullptr;
            int stride = 8;
            if (cc == 0)
            {
                const int x0 = (block & 1) * 8;
                if (fieldDct)
                {
                    dst = yPlane + (block >> 1) * 16 + x0;
                    stride = 32;
                }
                else
                {
                    dst = yPlane + (block >> 1) * 128 + x0;
                    stride = 16;
                }
            }
            else
            {
                dst = cc == 1 ? cbPlane : crPlane;
            }
            inverseDctToSamples(coef, dst, stride);
        }
        if (error || c.starved)
            break;

        m_out.resize(m_out.size() + kMacroblockRgb32Bytes);
        convertMacroblockRgb32(yPlane, cbPlane, crPlane, m_out.data() + m_out.size() - kMacroblockRgb32Bytes, m_th0,
                               m_th1, sgn);
        ++macroblocks;

        // macroblock_address_increment (Table B.1). A run of 7+ zero bits
        // that is neither escape nor stuffing ends the macroblock run.
        int increment = 0;
        bool end = false;
        while (true)
        {
            const uint32_t prefix = c.show(11);
            if (prefix == kMbaEscape.code)
            {
                c.pos += 11;
                increment += 33;
                continue;
            }
            if (prefix == kMbaStuffing.code)
            {
                c.pos += 11;
                continue;
            }
            if (c.show(16) < 0x0300u)
            {
                end = true;
                break;
            }
            const VlcEntry &e = t.mba[prefix];
            if (e.len == 0 || e.value <= 0)
            {
                error = "macroblock_address_increment";
                break;
            }
            c.pos += e.len;
            increment += e.value;
            break;
        }
        if (end || error || c.starved)
            break;
        if (increment != 1)
        {
            // Skipped macroblocks reset the DC predictors (7.2.1); the IPU
            // emits nothing for them.
            dcPred[0] = dcPred[1] = dcPred[2] = dcReset;
        }
    }

    if (c.starved)
    {
        m_out.resize(outStart); // stall until more input arrives; nothing committed
        return false;
    }

    bool scd = false;
    bool ecd = false;
    uint32_t top = 0;
    if (!error)
    {
        // After the run: skip zero bytes up to the next start code prefix
        // (SCD) or flag ECD on anything else (PCSX2 mpeg2sliceIDEC).
        if (c.show(8) == 0u)
        {
            c.pos = (c.pos + 7u) & ~static_cast<size_t>(7u);
            while (true)
            {
                const uint32_t s = c.show(24);
                if (c.starved)
                    break;
                if (s != 0u)
                {
                    (s == 1u ? scd : ecd) = true;
                    break;
                }
                c.pos += 8;
            }
        }
        top = c.show(32);
        if (c.starved)
        {
            m_out.resize(outStart);
            return false;
        }
    }
    else
    {
        ecd = true;
        if (m_idecErrorsReported < kIdecErrorLogLimit)
        {
            ++m_idecErrorsReported;
            RUNTIME_ERROR("[IPU] IDEC: invalid " << error << " at macroblock " << macroblocks << " (bit " << c.pos
                                                 << " of " << m_in.size() * 8u << ") -> ECD" << std::endl);
        }
    }

    m_bitPos = c.pos;
    m_top = top;
    m_scd = scd;
    m_ecd = ecd;
    m_cbp = 0x3Fu;
    m_busyUntilDrained = m_out.size() != outStart;
    return true;
}
