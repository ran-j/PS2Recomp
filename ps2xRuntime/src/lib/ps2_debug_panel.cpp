#include "ps2_debug_panel.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "ps2_log.h"

#include <unordered_set>
#include "Kernel/Syscalls/Helpers/State.h"
#include "Kernel/Stubs/CD.h"
#include "Kernel/Stubs/MemoryCard.h"
#include "Kernel/Stubs/Pad.h"
#include "runtime/ps2_iop.h"

#if defined(PS2X_ENABLE_DEBUG_UI) && !defined(PLATFORM_VITA)
#include "imgui.h"
#include "rlImGui.h"
#include "raylib.h"
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <exception>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace
{
#if defined(PS2X_ENABLE_DEBUG_UI) && !defined(PLATFORM_VITA)
    const char *threadStatusName(int status)
    {
        switch (status)
        {
        case THS_RUN:
            return "RUN";
        case THS_READY:
            return "READY";
        case THS_WAIT:
            return "WAIT";
        case THS_SUSPEND:
            return "SUSPEND";
        case THS_WAITSUSPEND:
            return "WAITSUSP";
        case THS_DORMANT:
            return "DORMANT";
        default:
            return "?";
        }
    }

    const char *waitTypeName(int waitType)
    {
        switch (waitType)
        {
        case TSW_NONE:
            return "NONE";
        case TSW_SLEEP:
            return "SLEEP";
        case TSW_SEMA:
            return "SEMA";
        case TSW_EVENT:
            return "EVENT";
        default:
            return "?";
        }
    }

    const char *iopRpcSidName(uint32_t sid)
    {
        switch (sid)
        {
        case IOP_SID_SNDDRV_COMMAND:
            return "SNDDRV command";
        case IOP_SID_SNDDRV_STATE:
            return "SNDDRV state";
        case IOP_SID_LIBSD:
            return "LIBSD";
        case IOP_SID_FATAL_FRAME_SDRDRV:
            return "Fatal Frame SDRDRV";
        case 0x80001300u:
            return "DBCMAN";
        default:
            return "";
        }
    }

    std::string pressedPadButtons(uint16_t activeLowButtons)
    {
        struct ButtonName
        {
            uint16_t mask;
            const char *name;
        };
        static constexpr ButtonName buttons[] = {
            {1u << 0, "Select"},
            {1u << 1, "L3"},
            {1u << 2, "R3"},
            {1u << 3, "Start"},
            {1u << 4, "Up"},
            {1u << 5, "Right"},
            {1u << 6, "Down"},
            {1u << 7, "Left"},
            {1u << 8, "L2"},
            {1u << 9, "R2"},
            {1u << 10, "L1"},
            {1u << 11, "R1"},
            {1u << 12, "Triangle"},
            {1u << 13, "Circle"},
            {1u << 14, "Cross"},
            {1u << 15, "Square"},
        };

        std::string out;
        for (const ButtonName &button : buttons)
        {
            if ((activeLowButtons & button.mask) == 0u)
            {
                if (!out.empty())
                {
                    out += ", ";
                }
                out += button.name;
            }
        }
        return out.empty() ? std::string("none") : out;
    }

    std::string rpcDebugFlags(uint32_t flags)
    {
        struct FlagName
        {
            uint32_t mask;
            const char *name;
        };
        static constexpr FlagName names[] = {
            {kSifRpcDebugFlagNowait, "nowait"},
            {kSifRpcDebugFlagHandledByHle, "hle"},
            {kSifRpcDebugFlagCallback, "callback"},
            {kSifRpcDebugFlagMissingClient, "bad-client"},
            {kSifRpcDebugFlagServerDispatch, "server"},
            {kSifRpcDebugFlagDtx, "dtx"},
        };

        std::string out;
        for (const FlagName &name : names)
        {
            if ((flags & name.mask) != 0u)
            {
                if (!out.empty())
                {
                    out += ",";
                }
                out += name.name;
            }
        }
        return out;
    }

    template <typename T>
    bool readGuestObjectNoThrow(uint8_t *rdram, uint32_t addr, T &out)
    {
        if (!rdram || addr == 0u)
        {
            return false;
        }

        uint8_t *ptr = getMemPtr(rdram, addr);
        if (!ptr)
        {
            return false;
        }

        std::memcpy(&out, ptr, sizeof(T));
        return true;
    }

    const char *primTypeName(GSPrimType type)
    {
        switch (type)
        {
        case GS_PRIM_POINT:
            return "POINT";
        case GS_PRIM_LINE:
            return "LINE";
        case GS_PRIM_LINESTRIP:
            return "LINE_STRIP";
        case GS_PRIM_TRIANGLE:
            return "TRI";
        case GS_PRIM_TRISTRIP:
            return "TRI_STRIP";
        case GS_PRIM_TRIFAN:
            return "TRI_FAN";
        case GS_PRIM_SPRITE:
            return "SPRITE";
        default:
            return "?";
        }
    }

    const char *gsDebugEventKindName(GSDebugEventKind kind)
    {
        switch (kind)
        {
        case GSDebugEventKind::GifTag:
            return "GIF";
        case GSDebugEventKind::Register:
            return "REG";
        case GSDebugEventKind::Draw:
            return "DRAW";
        case GSDebugEventKind::Transfer:
            return "XFER";
        case GSDebugEventKind::Present:
            return "PRESENT";
        default:
            return "?";
        }
    }

    const char *gifFormatName(uint8_t flg)
    {
        switch (flg)
        {
        case GIF_FMT_PACKED:
            return "PACKED";
        case GIF_FMT_REGLIST:
            return "REGLIST";
        case GIF_FMT_IMAGE:
            return "IMAGE";
        case GIF_FMT_DISABLED:
            return "DISABLED";
        default:
            return "?";
        }
    }

    const char *gsRegName(uint8_t reg)
    {
        switch (reg)
        {
        case GS_REG_PRIM:
            return "PRIM";
        case GS_REG_RGBAQ:
            return "RGBAQ";
        case GS_REG_ST:
            return "ST";
        case GS_REG_UV:
            return "UV";
        case GS_REG_XYZF2:
            return "XYZF2";
        case GS_REG_XYZ2:
            return "XYZ2";
        case GS_REG_TEX0_1:
            return "TEX0_1";
        case GS_REG_TEX0_2:
            return "TEX0_2";
        case GS_REG_CLAMP_1:
            return "CLAMP_1";
        case GS_REG_CLAMP_2:
            return "CLAMP_2";
        case GS_REG_XYZF3:
            return "XYZF3";
        case GS_REG_XYZ3:
            return "XYZ3";
        case GS_REG_TEX1_1:
            return "TEX1_1";
        case GS_REG_TEX1_2:
            return "TEX1_2";
        case GS_REG_TEX2_1:
            return "TEX2_1";
        case GS_REG_TEX2_2:
            return "TEX2_2";
        case GS_REG_XYOFFSET_1:
            return "XYOFFSET_1";
        case GS_REG_XYOFFSET_2:
            return "XYOFFSET_2";
        case GS_REG_PRMODECONT:
            return "PRMODECONT";
        case GS_REG_PRMODE:
            return "PRMODE";
        case GS_REG_TEXCLUT:
            return "TEXCLUT";
        case GS_REG_TEXA:
            return "TEXA";
        case GS_REG_SCISSOR_1:
            return "SCISSOR_1";
        case GS_REG_SCISSOR_2:
            return "SCISSOR_2";
        case GS_REG_ALPHA_1:
            return "ALPHA_1";
        case GS_REG_ALPHA_2:
            return "ALPHA_2";
        case GS_REG_TEST_1:
            return "TEST_1";
        case GS_REG_TEST_2:
            return "TEST_2";
        case GS_REG_FBA_1:
            return "FBA_1";
        case GS_REG_FBA_2:
            return "FBA_2";
        case GS_REG_FRAME_1:
            return "FRAME_1";
        case GS_REG_FRAME_2:
            return "FRAME_2";
        case GS_REG_ZBUF_1:
            return "ZBUF_1";
        case GS_REG_ZBUF_2:
            return "ZBUF_2";
        case GS_REG_BITBLTBUF:
            return "BITBLTBUF";
        case GS_REG_TRXPOS:
            return "TRXPOS";
        case GS_REG_TRXREG:
            return "TRXREG";
        case GS_REG_TRXDIR:
            return "TRXDIR";
        case GS_REG_HWREG:
            return "HWREG";
        case GS_REG_SIGNAL:
            return "SIGNAL";
        case GS_REG_FINISH:
            return "FINISH";
        case GS_REG_LABEL:
            return "LABEL";
        default:
            return "?";
        }
    }

    void textHex32(const char *label, uint32_t value)
    {
        ImGui::Text("%s: 0x%08X", label, value);
    }

    void textHex64(const char *label, uint64_t value)
    {
        ImGui::Text("%s: 0x%016llX", label, static_cast<unsigned long long>(value));
    }

    uint32_t readGuestU32NoThrow(uint8_t *rdram, uint32_t addr)
    {
        if (!rdram || addr == 0u)
        {
            return 0u;
        }

        uint8_t *ptr = getMemPtr(rdram, addr);
        if (!ptr)
        {
            return 0u;
        }

        uint32_t value = 0u;
        std::memcpy(&value, ptr, sizeof(value));
        return value;
    }

    std::string pathToString(const std::filesystem::path &path)
    {
        if (path.empty())
        {
            return std::string("<empty>");
        }
        return path.string();
    }

    void textPath(const char *label, const std::filesystem::path &path)
    {
        const std::string value = pathToString(path);
        ImGui::Text("%s: %s", label, value.c_str());
    }

    std::filesystem::path makeDebugDumpPath(const char *name, uint32_t frameIndex)
    {
        static uint32_t s_dumpCounter = 0u;
        std::filesystem::path dir = std::filesystem::current_path() / "ps2_debug_dumps";
        std::filesystem::create_directories(dir);

        char fileName[128]{};
        std::snprintf(fileName,
                      sizeof(fileName),
                      "%s_frame_%08X_%04u.txt",
                      name,
                      frameIndex,
                      ++s_dumpCounter);
        return dir / fileName;
    }

    void dumpGsContext(std::ostream &out, const char *label, const GSContext &ctx)
    {
        out << label << "\n";
        out << "  FRAME  fbp=" << ctx.frame.fbp
            << " fbw=" << static_cast<unsigned int>(ctx.frame.fbw)
            << " psm=0x" << std::hex << static_cast<unsigned int>(ctx.frame.psm)
            << " fbmsk=0x" << ctx.frame.fbmsk << std::dec << "\n";
        out << "  ZBUF   zbp=" << ctx.zbuf.zbp
            << " psm=0x" << std::hex << static_cast<unsigned int>(ctx.zbuf.psm) << std::dec
            << " zmask=" << (ctx.zbuf.zmask ? 1u : 0u) << "\n";
        out << "  SCISSOR x=" << ctx.scissor.x0 << ".." << ctx.scissor.x1
            << " y=" << ctx.scissor.y0 << ".." << ctx.scissor.y1 << "\n";
        out << "  TEX0   tbp0=" << ctx.tex0.tbp0
            << " tbw=" << static_cast<unsigned int>(ctx.tex0.tbw)
            << " psm=0x" << std::hex << static_cast<unsigned int>(ctx.tex0.psm) << std::dec
            << " size=" << (1u << ctx.tex0.tw) << "x" << (1u << ctx.tex0.th)
            << " tcc=" << static_cast<unsigned int>(ctx.tex0.tcc)
            << " tfx=" << static_cast<unsigned int>(ctx.tex0.tfx)
            << " cbp=" << ctx.tex0.cbp
            << " cpsm=0x" << std::hex << static_cast<unsigned int>(ctx.tex0.cpsm) << std::dec
            << " csm=" << static_cast<unsigned int>(ctx.tex0.csm)
            << " csa=" << static_cast<unsigned int>(ctx.tex0.csa)
            << " cld=" << static_cast<unsigned int>(ctx.tex0.cld) << "\n";
        out << "  XYOFFSET ofx=" << ctx.xyoffset.ofx << " ofy=" << ctx.xyoffset.ofy << "\n";
        out << "  TEST   0x" << std::hex << std::setw(16) << std::setfill('0') << ctx.test << std::setfill(' ') << std::dec
            << " ATE=" << ((ctx.test >> 0) & 1ull)
            << " ATST=" << ((ctx.test >> 1) & 7ull)
            << " AREF=0x" << std::hex << ((ctx.test >> 4) & 0xffull) << std::dec
            << " AFAIL=" << ((ctx.test >> 12) & 3ull)
            << " DATE=" << ((ctx.test >> 14) & 1ull)
            << " DATM=" << ((ctx.test >> 15) & 1ull)
            << " ZTE=" << ((ctx.test >> 16) & 1ull)
            << " ZTST=" << ((ctx.test >> 17) & 3ull) << "\n";
        out << "  ALPHA  0x" << std::hex << std::setw(16) << std::setfill('0') << ctx.alpha << std::setfill(' ') << std::dec << "\n";
        out << "  TEX1   0x" << std::hex << std::setw(16) << std::setfill('0') << ctx.tex1 << std::setfill(' ') << std::dec << "\n";
        out << "  CLAMP  0x" << std::hex << std::setw(16) << std::setfill('0') << ctx.clamp << std::setfill(' ') << std::dec << "\n";
        out << "  FBA    0x" << std::hex << std::setw(16) << std::setfill('0') << ctx.fba << std::setfill(' ') << std::dec << "\n";
    }

    bool writeGsDebugDump(const std::filesystem::path &path,
                          const GSRegisters &regs,
                          const GSDebugSnapshot &gs,
                          const std::vector<GSDebugHistoryEntry> &history,
                          bool currentFrameOnly,
                          bool drawsOnly,
                          uint32_t latestFrame,
                          std::string &error)
    {
        try
        {
            std::filesystem::create_directories(path.parent_path());
            std::ofstream out(path, std::ios::out | std::ios::trunc);
            if (!out)
            {
                error = "failed to open output file";
                return false;
            }

            out << "PS2 Runtime GS debug dump\n";
            out << "path: " << path.string() << "\n";
            out << "latestFrame: " << latestFrame << "\n";
            out << "filters: currentFrameOnly=" << (currentFrameOnly ? 1u : 0u)
                << " drawsOnly=" << (drawsOnly ? 1u : 0u) << "\n\n";

            out << "[GS private registers]\n";
            out << std::hex << std::setfill('0');
            out << "PMODE    0x" << std::setw(16) << regs.pmode << "\n";
            out << "SMODE2   0x" << std::setw(16) << regs.smode2 << "\n";
            out << "DISPFB1  0x" << std::setw(16) << regs.dispfb1 << "\n";
            out << "DISPLAY1 0x" << std::setw(16) << regs.display1 << "\n";
            out << "DISPFB2  0x" << std::setw(16) << regs.dispfb2 << "\n";
            out << "DISPLAY2 0x" << std::setw(16) << regs.display2 << "\n";
            out << "BGCOLOR  0x" << std::setw(16) << regs.bgcolor << "\n";
            out << "CSR      0x" << std::setw(16) << regs.csr << "\n";
            out << "IMR      0x" << std::setw(16) << regs.imr << "\n";
            out << "BUSDIR   0x" << std::setw(16) << regs.busdir << "\n";
            out << "SIGLBLID 0x" << std::setw(16) << regs.siglblid << "\n\n";
            out << std::dec << std::setfill(' ');

            out << "[GS draw state]\n";
            out << "PRIM type=" << primTypeName(gs.prim.type)
                << " iip=" << gs.prim.iip
                << " tme=" << gs.prim.tme
                << " fge=" << gs.prim.fge
                << " abe=" << gs.prim.abe
                << " aa1=" << gs.prim.aa1
                << " fst=" << gs.prim.fst
                << " ctxt=" << gs.prim.ctxt
                << " fix=" << gs.prim.fix << "\n";
            out << "TEXA ta0=0x" << std::hex << static_cast<unsigned int>(gs.texa.ta0) << std::dec
                << " aem=" << (gs.texa.aem ? 1u : 0u)
                << " ta1=0x" << std::hex << static_cast<unsigned int>(gs.texa.ta1) << std::dec
                << " TEXCLUT cbw=" << gs.texclut.cbw
                << " cou=" << gs.texclut.cou
                << " cov=" << gs.texclut.cov << "\n";
            out << "BITBLT sbp=" << gs.bitbltbuf.sbp
                << " sbw=" << gs.bitbltbuf.sbw
                << " spsm=0x" << std::hex << static_cast<unsigned int>(gs.bitbltbuf.spsm) << std::dec
                << " dbp=" << gs.bitbltbuf.dbp
                << " dbw=" << gs.bitbltbuf.dbw
                << " dpsm=0x" << std::hex << static_cast<unsigned int>(gs.bitbltbuf.dpsm) << std::dec << "\n";
            out << "TRXPOS ss=(" << gs.trxpos.ssax << "," << gs.trxpos.ssay << ")"
                << " ds=(" << gs.trxpos.dsax << "," << gs.trxpos.dsay << ")"
                << " dir=" << gs.trxpos.dir
                << " TRXREG=" << gs.trxreg.rrw << "x" << gs.trxreg.rrh
                << " TRXDIR=" << gs.trxdir
                << " copied=" << gs.transferCopiedPixels << "/" << gs.transferTotalPixels
                << " at=(" << gs.transferX << "," << gs.transferY << ")\n";
            out << "Host presentation has=" << (gs.hasHostPresentationFrame ? 1u : 0u)
                << " size=" << gs.hostPresentationWidth << "x" << gs.hostPresentationHeight
                << " displayFbp=" << gs.hostPresentationDisplayFbp
                << " sourceFbp=" << gs.hostPresentationSourceFbp
                << " preferred=" << (gs.hostPresentationUsedPreferred ? 1u : 0u)
                << " lastDisplayBaseBytes=0x" << std::hex << gs.lastDisplayBaseBytes << std::dec
                << " localToHostPending=" << gs.localToHostPendingBytes << "\n";
            out << "Preferred source has=" << (gs.hasPreferredDisplaySource ? 1u : 0u)
                << " frame fbp=" << gs.preferredDisplaySourceFrame.fbp
                << " fbw=" << gs.preferredDisplaySourceFrame.fbw
                << " psm=0x" << std::hex << static_cast<unsigned int>(gs.preferredDisplaySourceFrame.psm) << std::dec
                << " destFbp=" << gs.preferredDisplayDestFbp << "\n\n";

            out << "[GS context state]\n";
            dumpGsContext(out, "Context 0", gs.ctx[0]);
            dumpGsContext(out, "Context 1", gs.ctx[1]);
            out << "\n";

            out << "[GS history]\n";
            out << "seq\tframe\ttick\tkind\tprim\tctx\tframe\tzbuf\ttest\talpha\ttex0\txy\tz\ta\tdetail\tpresent\n";
            for (auto it = history.rbegin(); it != history.rend(); ++it)
            {
                const GSDebugHistoryEntry &row = *it;
                if (currentFrameOnly && row.frameIndex != latestFrame)
                {
                    continue;
                }
                if (drawsOnly && row.kind != GSDebugEventKind::Draw)
                {
                    continue;
                }

                out << row.seq << '\t'
                    << row.frameIndex << '\t'
                    << row.vsyncTick << '\t'
                    << gsDebugEventKindName(row.kind) << '\t'
                    << primTypeName(row.prim.type) << '\t'
                    << (row.prim.ctxt ? 1u : 0u) << '\t'
                    << "fbp=" << row.frame.fbp << "/fbw=" << static_cast<unsigned int>(row.frame.fbw) << "/psm=0x" << std::hex << static_cast<unsigned int>(row.frame.psm) << std::dec << '\t'
                    << "zbp=" << row.zbuf.zbp << "/psm=0x" << std::hex << static_cast<unsigned int>(row.zbuf.psm) << std::dec << "/m=" << (row.zbuf.zmask ? 1u : 0u) << '\t'
                    << "0x" << std::hex << row.test << std::dec << '\t'
                    << "0x" << std::hex << row.alpha << std::dec << '\t'
                    << "tbp=" << row.tex0.tbp0 << "/psm=0x" << std::hex << static_cast<unsigned int>(row.tex0.psm) << std::dec << "/size=" << (1u << row.tex0.tw) << "x" << (1u << row.tex0.th) << "/cbp=" << row.tex0.cbp << '\t';

                if (row.kind == GSDebugEventKind::Draw)
                {
                    out << row.xMin << ".." << row.xMax << "/" << row.yMin << ".." << row.yMax << '\t'
                        << row.zMin << ".." << row.zMax << '\t'
                        << static_cast<unsigned int>(row.aMin) << ".." << static_cast<unsigned int>(row.aMax) << '\t';
                }
                else
                {
                    out << "-\t-\t-\t";
                }

                if (row.kind == GSDebugEventKind::GifTag)
                {
                    out << gifFormatName(row.gifFlg) << " nloop=" << row.gifNloop << " nreg=" << row.gifNreg << " size=" << row.gifSizeBytes;
                }
                else if (row.kind == GSDebugEventKind::Register)
                {
                    out << gsRegName(row.reg) << "(0x" << std::hex << static_cast<unsigned int>(row.reg) << ")=0x" << row.regValue << std::dec;
                }
                else if (row.kind == GSDebugEventKind::Transfer)
                {
                    out << "TRXDIR=" << row.trxdir << " " << row.trxreg.rrw << "x" << row.trxreg.rrh
                        << " pix=" << row.transferPixels << " sbp=" << row.bitbltbuf.sbp << " dbp=" << row.bitbltbuf.dbp;
                }
                else
                {
                    out << "-";
                }
                out << '\t';
                if (row.kind == GSDebugEventKind::Present)
                {
                    out << row.width << "x" << row.height
                        << " display=" << row.displayFbp
                        << " source=" << row.sourceFbp
                        << " pref=" << (row.usedPreferred ? 1u : 0u);
                }
                else
                {
                    out << "-";
                }
                out << '\n';
            }

            return true;
        }
        catch (const std::exception &ex)
        {
            error = ex.what();
            return false;
        }
    }

    void drawGsContext(const char *label, const GSContext &ctx)
    {
        if (!ImGui::TreeNode(label))
        {
            return;
        }

        ImGui::Text("FRAME fbp=%u fbw=%u psm=0x%02X fbmsk=0x%08X",
                    ctx.frame.fbp,
                    ctx.frame.fbw,
                    static_cast<unsigned int>(ctx.frame.psm),
                    ctx.frame.fbmsk);
        ImGui::Text("ZBUF  zbp=%u psm=0x%02X zmask=%u",
                    ctx.zbuf.zbp,
                    static_cast<unsigned int>(ctx.zbuf.psm),
                    ctx.zbuf.zmask ? 1u : 0u);
        ImGui::Text("SCISSOR x=%u..%u y=%u..%u",
                    ctx.scissor.x0,
                    ctx.scissor.x1,
                    ctx.scissor.y0,
                    ctx.scissor.y1);
        ImGui::Text("TEX0 tbp0=%u tbw=%u psm=0x%02X tw=%u th=%u tcc=%u tfx=%u cbp=%u cpsm=0x%02X csm=%u csa=%u cld=%u",
                    ctx.tex0.tbp0,
                    static_cast<unsigned int>(ctx.tex0.tbw),
                    static_cast<unsigned int>(ctx.tex0.psm),
                    static_cast<unsigned int>(ctx.tex0.tw),
                    static_cast<unsigned int>(ctx.tex0.th),
                    static_cast<unsigned int>(ctx.tex0.tcc),
                    static_cast<unsigned int>(ctx.tex0.tfx),
                    ctx.tex0.cbp,
                    static_cast<unsigned int>(ctx.tex0.cpsm),
                    static_cast<unsigned int>(ctx.tex0.csm),
                    static_cast<unsigned int>(ctx.tex0.csa),
                    static_cast<unsigned int>(ctx.tex0.cld));
        ImGui::Text("XYOFFSET ofx=%u ofy=%u", ctx.xyoffset.ofx, ctx.xyoffset.ofy);
        textHex64("TEST", ctx.test);
        ImGui::SameLine();
        ImGui::Text("ATE=%llu ATST=%llu AREF=0x%02llX AFAIL=%llu DATE=%llu DATM=%llu ZTE=%llu ZTST=%llu",
                    (ctx.test >> 0) & 1ull,
                    (ctx.test >> 1) & 7ull,
                    (ctx.test >> 4) & 0xffull,
                    (ctx.test >> 12) & 3ull,
                    (ctx.test >> 14) & 1ull,
                    (ctx.test >> 15) & 1ull,
                    (ctx.test >> 16) & 1ull,
                    (ctx.test >> 17) & 3ull);
        textHex64("ALPHA", ctx.alpha);
        textHex64("TEX1", ctx.tex1);
        textHex64("CLAMP", ctx.clamp);
        textHex64("FBA", ctx.fba);
        ImGui::TreePop();
    }

    void drawHexDump(uint8_t *rdram, uint32_t startAddr, uint32_t bytes)
    {
        bytes = std::clamp<uint32_t>(bytes, 16u, 0x1000u);
        const uint32_t alignedStart = startAddr & ~0x0fu;
        for (uint32_t off = 0u; off < bytes; off += 16u)
        {
            const uint32_t addr = alignedStart + off;
            uint8_t *ptr = getMemPtr(rdram, addr);
            if (!ptr)
            {
                ImGui::Text("%08X: <invalid>", addr);
                continue;
            }

            char hex[16 * 3 + 1]{};
            char ascii[17]{};
            for (uint32_t i = 0u; i < 16u; ++i)
            {
                const uint8_t b = ptr[i];
                std::snprintf(hex + i * 3, sizeof(hex) - i * 3, "%02X ", b);
                ascii[i] = (b >= 32u && b < 127u) ? static_cast<char>(b) : '.';
            }
            ImGui::Text("%08X: %s |%s|", addr, hex, ascii);
        }
    }

    void drawCpuTab(PS2Runtime &runtime, bool showRegisters)
    {
        const uint32_t pc = runtime.m_debugPc.load(std::memory_order_relaxed);
        const uint32_t ra = runtime.m_debugRa.load(std::memory_order_relaxed);
        const uint32_t sp = runtime.m_debugSp.load(std::memory_order_relaxed);
        const uint32_t gp = runtime.m_debugGp.load(std::memory_order_relaxed);

        ImGui::Text("Runtime: %s", runtime.isStopRequested() ? "stop requested" : "running");
        ImGui::Text("Guest execution waiters: %u", runtime.guestExecutionWaiterCountForTesting());
        ImGui::Separator();
        textHex32("PC", pc);
        ImGui::SameLine();
        textHex32("RA", ra);
        textHex32("SP", sp);
        ImGui::SameLine();
        textHex32("GP", gp);

        if (ImGui::Button("Copy PC/RA/SP/GP"))
        {
            char buf[256]{};
            std::snprintf(buf, sizeof(buf), "pc=0x%08X ra=0x%08X sp=0x%08X gp=0x%08X", pc, ra, sp, gp);
            ImGui::SetClipboardText(buf);
        }

        ImGui::TextDisabled("full context snapshot is best-effort while guest is running");

        if (showRegisters)
        {
            const R5900Context &cpu = runtime.cpu();
            if (ImGui::BeginTable("gpr", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
            {
                ImGui::TableSetupColumn("Reg");
                ImGui::TableSetupColumn("Lo32");
                ImGui::TableSetupColumn("Hi32[1]");
                ImGui::TableSetupColumn("Hi64[2:3]");
                ImGui::TableHeadersRow();
                for (int i = 0; i < 32; ++i)
                {
                    const uint32_t w0 = static_cast<uint32_t>(_mm_extract_epi32(cpu.r[i], 0));
                    const uint32_t w1 = static_cast<uint32_t>(_mm_extract_epi32(cpu.r[i], 1));
                    const uint32_t w2 = static_cast<uint32_t>(_mm_extract_epi32(cpu.r[i], 2));
                    const uint32_t w3 = static_cast<uint32_t>(_mm_extract_epi32(cpu.r[i], 3));
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("r%d", i);
                    ImGui::TableNextColumn();
                    ImGui::Text("0x%08X", w0);
                    ImGui::TableNextColumn();
                    ImGui::Text("0x%08X", w1);
                    ImGui::TableNextColumn();
                    ImGui::Text("0x%08X_%08X", w3, w2);
                }
                ImGui::EndTable();
            }

            ImGui::SeparatorText("COP0");
            ImGui::Text("Status=0x%08X Cause=0x%08X EPC=0x%08X Count=0x%08X Compare=0x%08X BadVAddr=0x%08X",
                        cpu.cop0_status,
                        cpu.cop0_cause,
                        cpu.cop0_epc,
                        cpu.cop0_count,
                        cpu.cop0_compare,
                        cpu.cop0_badvaddr);
        }
    }

    void drawThreadsTab()
    {
        struct ThreadRow
        {
            int id = 0;
            uint32_t entry = 0;
            uint32_t stack = 0;
            uint32_t stackSize = 0;
            uint32_t gp = 0;
            uint32_t priority = 0;
            int status = 0;
            int waitType = 0;
            int waitId = 0;
            int currentPriority = 0;
            int wakeupCount = 0;
            int suspendCount = 0;
            uint32_t currentPc = 0u;
            bool terminated = false;
        };

        std::vector<ThreadRow> rows;
        {
            std::lock_guard<std::mutex> lock(g_thread_map_mutex);
            rows.reserve(g_threads.size());
            for (const auto &[id, ptr] : g_threads)
            {
                if (!ptr)
                {
                    continue;
                }
                ThreadRow row{};
                row.id = id;
                {
                    std::lock_guard<std::mutex> threadLock(ptr->m);
                    row.entry = ptr->entry;
                    row.stack = ptr->stack;
                    row.stackSize = ptr->stackSize;
                    row.gp = ptr->gp;
                    row.priority = ptr->priority;
                    row.currentPriority = ptr->currentPriority;
                    row.status = ptr->status;
                    row.waitType = ptr->waitType;
                    row.waitId = ptr->waitId;
                    row.wakeupCount = ptr->wakeupCount;
                    row.suspendCount = ptr->suspendCount;
                }
                row.currentPc = ptr->currentPc.load(std::memory_order_relaxed);
                row.terminated = ptr->terminated.load(std::memory_order_relaxed);
                rows.push_back(row);
            }
        }

        ImGui::Text("Threads: %zu activeThreads=%d", rows.size(), g_activeThreads.load(std::memory_order_relaxed));
        if (ImGui::BeginTable("threads", 12, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0, 320)))
        {
            ImGui::TableSetupColumn("ID");
            ImGui::TableSetupColumn("Status");
            ImGui::TableSetupColumn("Wait");
            ImGui::TableSetupColumn("WaitId");
            ImGui::TableSetupColumn("PC");
            ImGui::TableSetupColumn("Entry");
            ImGui::TableSetupColumn("Stack");
            ImGui::TableSetupColumn("GP");
            ImGui::TableSetupColumn("Prio");
            ImGui::TableSetupColumn("Wake");
            ImGui::TableSetupColumn("Susp");
            ImGui::TableSetupColumn("Term");
            ImGui::TableHeadersRow();
            for (const ThreadRow &row : rows)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", row.id);
                ImGui::TableNextColumn();
                ImGui::Text("%s", threadStatusName(row.status));
                ImGui::TableNextColumn();
                ImGui::Text("%s", waitTypeName(row.waitType));
                ImGui::TableNextColumn();
                ImGui::Text("%d", row.waitId);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", row.currentPc);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", row.entry);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X/%u", row.stack, row.stackSize);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", row.gp);
                ImGui::TableNextColumn();
                ImGui::Text("%d/%u", row.currentPriority, row.priority);
                ImGui::TableNextColumn();
                ImGui::Text("%d", row.wakeupCount);
                ImGui::TableNextColumn();
                ImGui::Text("%d", row.suspendCount);
                ImGui::TableNextColumn();
                ImGui::Text("%u", row.terminated ? 1u : 0u);
            }
            ImGui::EndTable();
        }
    }

    void drawKernelTab()
    {
        ImGui::SeparatorText("Semaphores");
        {
            std::lock_guard<std::mutex> lock(g_sema_map_mutex);
            if (ImGui::BeginTable("semas", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
            {
                ImGui::TableSetupColumn("ID");
                ImGui::TableSetupColumn("Count");
                ImGui::TableSetupColumn("Max");
                ImGui::TableSetupColumn("Init");
                ImGui::TableSetupColumn("Waiters");
                ImGui::TableSetupColumn("Attr");
                ImGui::TableSetupColumn("Deleted");
                ImGui::TableHeadersRow();
                for (const auto &[id, sema] : g_semas)
                {
                    if (!sema)
                        continue;
                    std::lock_guard<std::mutex> semaLock(sema->m);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", id);
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", sema->count);
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", sema->maxCount);
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", sema->initCount);
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", sema->waiters);
                    ImGui::TableNextColumn();
                    ImGui::Text("0x%08X", sema->attr);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", sema->deleted ? 1u : 0u);
                }
                ImGui::EndTable();
            }
        }

        ImGui::SeparatorText("Event flags");
        {
            std::lock_guard<std::mutex> lock(g_event_flag_map_mutex);
            if (ImGui::BeginTable("evf", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
            {
                ImGui::TableSetupColumn("ID");
                ImGui::TableSetupColumn("Bits");
                ImGui::TableSetupColumn("Init");
                ImGui::TableSetupColumn("Waiters");
                ImGui::TableSetupColumn("Attr");
                ImGui::TableSetupColumn("Deleted");
                ImGui::TableHeadersRow();
                for (const auto &[id, evf] : g_eventFlags)
                {
                    if (!evf)
                        continue;
                    std::lock_guard<std::mutex> evfLock(evf->m);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", id);
                    ImGui::TableNextColumn();
                    ImGui::Text("0x%08X", evf->bits);
                    ImGui::TableNextColumn();
                    ImGui::Text("0x%08X", evf->initBits);
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", evf->waiters);
                    ImGui::TableNextColumn();
                    ImGui::Text("0x%08X", evf->attr);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", evf->deleted ? 1u : 0u);
                }
                ImGui::EndTable();
            }
        }
    }

    void drawIopTab(PS2Runtime &runtime)
    {
        uint8_t *rdram = runtime.memory().getRDRAM();

        struct ModuleRow
        {
            int32_t id = 0;
            std::string path;
            std::string pathKey;
            uint32_t refCount = 0;
            bool loaded = false;
        };

        std::vector<ModuleRow> modules;
        uint32_t moduleLogCount = 0;
        int32_t nextModuleId = 0;
        {
            std::lock_guard<std::mutex> lock(g_sif_module_mutex);
            modules.reserve(g_sif_modules_by_id.size());
            for (const auto &[id, record] : g_sif_modules_by_id)
            {
                ModuleRow row{};
                row.id = id;
                row.path = record.path;
                row.pathKey = record.pathKey;
                row.refCount = record.refCount;
                row.loaded = record.loaded;
                modules.push_back(std::move(row));
            }
            moduleLogCount = g_sif_module_log_count;
            nextModuleId = g_next_sif_module_id;
        }
        std::sort(modules.begin(), modules.end(), [](const ModuleRow &a, const ModuleRow &b)
                  { return a.id < b.id; });

        struct RpcServerRow
        {
            uint32_t sid = 0;
            uint32_t sdPtr = 0;
            bool readable = false;
            t_SifRpcServerData sd{};
        };

        struct RpcClientRow
        {
            uint32_t clientPtr = 0;
            bool busy = false;
            uint32_t lastRpc = 0;
            uint32_t sid = 0;
            bool readable = false;
            t_SifRpcClientData cd{};
        };

        std::vector<RpcServerRow> servers;
        std::vector<RpcClientRow> clients;
        bool rpcInitialized = false;
        uint32_t rpcNextId = 0;
        uint32_t rpcPacketIndex = 0;
        uint32_t rpcServerIndex = 0;
        uint32_t rpcActiveQueue = 0;
        SoundDriverRpcState soundState{};
        {
            std::lock_guard<std::mutex> lock(g_rpc_mutex);
            rpcInitialized = g_rpc_initialized;
            rpcNextId = g_rpc_next_id;
            rpcPacketIndex = g_rpc_packet_index;
            rpcServerIndex = g_rpc_server_index;
            rpcActiveQueue = g_rpc_active_queue;
            soundState = g_soundDriverRpcState;

            servers.reserve(g_rpc_servers.size());
            for (const auto &[sid, state] : g_rpc_servers)
            {
                RpcServerRow row{};
                row.sid = sid;
                row.sdPtr = state.sd_ptr;
                row.readable = readGuestObjectNoThrow(rdram, state.sd_ptr, row.sd);
                servers.push_back(row);
            }

            clients.reserve(g_rpc_clients.size());
            for (const auto &[clientPtr, state] : g_rpc_clients)
            {
                RpcClientRow row{};
                row.clientPtr = clientPtr;
                row.busy = state.busy;
                row.lastRpc = state.last_rpc;
                row.sid = state.sid;
                row.readable = readGuestObjectNoThrow(rdram, clientPtr, row.cd);
                clients.push_back(row);
            }
        }
        std::sort(servers.begin(), servers.end(), [](const RpcServerRow &a, const RpcServerRow &b)
                  { return a.sid < b.sid; });
        std::sort(clients.begin(), clients.end(), [](const RpcClientRow &a, const RpcClientRow &b)
                  { return a.clientPtr < b.clientPtr; });

        PS2DtxCompatLayout dtxLayout{};
        size_t dtxRemoteCount = 0;
        size_t dtxTransferCount = 0;
        size_t dtxSjxCount = 0;
        size_t dtxRnaCount = 0;
        size_t dtxSjrmtCount = 0;
        uint32_t dtxNextUrpcObj = 0;
        {
            std::lock_guard<std::mutex> lock(g_dtx_rpc_mutex);
            dtxLayout = g_dtxCompatLayout;
            dtxRemoteCount = g_dtx_remote_by_id.size();
            dtxTransferCount = g_dtx_transfer_by_id.size();
            dtxSjxCount = g_dtx_sjx_by_handle.size();
            dtxRnaCount = g_dtx_ps2rna_by_handle.size();
            dtxSjrmtCount = g_dtx_sjrmt_by_handle.size();
            dtxNextUrpcObj = g_dtx_next_urpc_obj;
        }

        auto hasServer = [&](uint32_t sid)
        {
            return std::any_of(servers.begin(), servers.end(), [sid](const RpcServerRow &row)
                               { return row.sid == sid; });
        };

        ImGui::SeparatorText("IOP / SIF modules");
        ImGui::Text("Tracked modules: %zu loaded=%zu nextId=%d moduleLogs=%u",
                    modules.size(),
                    static_cast<size_t>(std::count_if(modules.begin(), modules.end(), [](const ModuleRow &row)
                                                      { return row.loaded; })),
                    nextModuleId,
                    moduleLogCount);
        ImGui::TextDisabled("This table is fed by SifLoadModule/SifLoadModuleBuffer/SifStopModule tracking.");

        if (ImGui::BeginTable("iop_modules", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0, 180)))
        {
            ImGui::TableSetupColumn("ID");
            ImGui::TableSetupColumn("Loaded");
            ImGui::TableSetupColumn("Refs");
            ImGui::TableSetupColumn("Key");
            ImGui::TableSetupColumn("Path");
            ImGui::TableHeadersRow();
            for (const ModuleRow &row : modules)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", row.id);
                ImGui::TableNextColumn();
                ImGui::Text("%s", row.loaded ? "yes" : "no");
                ImGui::TableNextColumn();
                ImGui::Text("%u", row.refCount);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(row.pathKey.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(row.path.c_str());
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Known HLE IOP RPC services");
        if (ImGui::BeginTable("iop_hle_services", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
        {
            struct ServiceRow
            {
                const char *name;
                uint32_t sid;
                bool dynamic;
            };
            const ServiceRow services[] = {
                {"SNDDRV command", IOP_SID_SNDDRV_COMMAND, false},
                {"SNDDRV state", IOP_SID_SNDDRV_STATE, false},
                {"LIBSD", IOP_SID_LIBSD, false},
                {"Fatal Frame SDRDRV", IOP_SID_FATAL_FRAME_SDRDRV, false},
                {"DBCMAN", 0x80001300u, false},
                {"DTX compat", dtxLayout.rpcSid, true},
            };

            ImGui::TableSetupColumn("Service");
            ImGui::TableSetupColumn("SID");
            ImGui::TableSetupColumn("Configured");
            ImGui::TableSetupColumn("EE server registered");
            ImGui::TableHeadersRow();
            for (const ServiceRow &service : services)
            {
                if (service.dynamic && service.sid == 0u)
                {
                    continue;
                }
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(service.name);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", service.sid);
                ImGui::TableNextColumn();
                ImGui::Text("%s", (!service.dynamic || service.sid != 0u) ? "yes" : "no");
                ImGui::TableNextColumn();
                ImGui::Text("%s", hasServer(service.sid) ? "yes" : "no");
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("SIF RPC state");
        ImGui::Text("initialized=%u servers=%zu clients=%zu nextId=%u packetIndex=%u serverIndex=%u activeQueue=0x%08X",
                    rpcInitialized ? 1u : 0u,
                    servers.size(),
                    clients.size(),
                    rpcNextId,
                    rpcPacketIndex,
                    rpcServerIndex,
                    rpcActiveQueue);

        if (ImGui::BeginTable("rpc_servers", 10, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0, 180)))
        {
            ImGui::TableSetupColumn("SID");
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("sd*");
            ImGui::TableSetupColumn("func");
            ImGui::TableSetupColumn("buf");
            ImGui::TableSetupColumn("size");
            ImGui::TableSetupColumn("cfunc");
            ImGui::TableSetupColumn("queue");
            ImGui::TableSetupColumn("recv");
            ImGui::TableSetupColumn("rpc#");
            ImGui::TableHeadersRow();
            for (const RpcServerRow &row : servers)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", row.sid);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(iopRpcSidName(row.sid));
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", row.sdPtr);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", row.readable ? row.sd.func : 0u);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", row.readable ? row.sd.buf : 0u);
                ImGui::TableNextColumn();
                ImGui::Text("%d", row.readable ? row.sd.size : 0);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", row.readable ? row.sd.cfunc : 0u);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", row.readable ? row.sd.base : 0u);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X/%d", row.readable ? row.sd.recvbuf : 0u, row.readable ? row.sd.rsize : 0);
                ImGui::TableNextColumn();
                ImGui::Text("%d", row.readable ? row.sd.rpc_number : 0);
            }
            ImGui::EndTable();
        }

        if (ImGui::BeginTable("rpc_clients", 9, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0, 160)))
        {
            ImGui::TableSetupColumn("client*");
            ImGui::TableSetupColumn("SID");
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Busy");
            ImGui::TableSetupColumn("LastRpc");
            ImGui::TableSetupColumn("cmd");
            ImGui::TableSetupColumn("buf");
            ImGui::TableSetupColumn("cbuf");
            ImGui::TableSetupColumn("server*");
            ImGui::TableHeadersRow();
            for (const RpcClientRow &row : clients)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", row.clientPtr);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", row.sid);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(iopRpcSidName(row.sid));
                ImGui::TableNextColumn();
                ImGui::Text("%u", row.busy ? 1u : 0u);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", row.lastRpc);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", row.readable ? row.cd.command : 0u);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", row.readable ? row.cd.buf : 0u);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", row.readable ? row.cd.cbuf : 0u);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", row.readable ? row.cd.server : 0u);
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Sound driver / DTX compat");
        ImGui::Text("SoundDriver initialized=%u owner=0x%p storage=0x%08X/%u status=0x%08X addrTable=0x%08X hd=0x%08X sq=0x%08X data=0x%08X",
                    soundState.initialized ? 1u : 0u,
                    reinterpret_cast<void *>(soundState.ownerRuntime),
                    soundState.storageBaseAddr,
                    soundState.storageSize,
                    soundState.statusAddr,
                    soundState.addrTableAddr,
                    soundState.hdBaseAddr,
                    soundState.sqBaseAddr,
                    soundState.dataBaseAddr);
        ImGui::Text("DTX configured=%u sid=0x%08X obj=[0x%08X,0x%08X) stride=0x%X fnTable=0x%08X objTable=0x%08X dispatcher=0x%08X nextObj=0x%08X",
                    dtxLayout.isConfigured() ? 1u : 0u,
                    dtxLayout.rpcSid,
                    dtxLayout.urpcObjBase,
                    dtxLayout.urpcObjLimit,
                    dtxLayout.urpcObjStride,
                    dtxLayout.urpcFnTableBase,
                    dtxLayout.urpcObjTableBase,
                    dtxLayout.dispatcherFuncAddr,
                    dtxNextUrpcObj);
        ImGui::Text("DTX states: remote=%zu transfer=%zu sjx=%zu ps2rna=%zu sjrmt=%zu",
                    dtxRemoteCount,
                    dtxTransferCount,
                    dtxSjxCount,
                    dtxRnaCount,
                    dtxSjrmtCount);
    }

    void drawRpcHistoryTab()
    {
        std::vector<SifRpcDebugEvent> events;
        uint64_t nextSeq = 0;
        {
            std::lock_guard<std::mutex> lock(g_rpc_mutex);
            nextSeq = g_sif_rpc_debug_next_seq;
            events.reserve(kSifRpcDebugHistoryCount);
            for (size_t i = 0; i < kSifRpcDebugHistoryCount; ++i)
            {
                const SifRpcDebugEvent &event = g_sif_rpc_debug_history[i];
                if (event.seq != 0u)
                {
                    events.push_back(event);
                }
            }
        }
        std::sort(events.begin(), events.end(), [](const SifRpcDebugEvent &a, const SifRpcDebugEvent &b)
                  { return a.seq > b.seq; });

        ImGui::Text("RPC events: %zu captured, nextSeq=%llu", events.size(), static_cast<unsigned long long>(nextSeq));
        ImGui::TextDisabled("Ring buffer fed by SifInitRpc/SifBindRpc/SifRegisterRpc/SifCallRpc.");

        static bool onlyCalls = false;
        static bool hideBindNoise = false;
        ImGui::Checkbox("Only CallRpc", &onlyCalls);
        ImGui::SameLine();
        ImGui::Checkbox("Hide bind/register/init", &hideBindNoise);

        if (ImGui::BeginTable("rpc_history", 20,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                              ImVec2(0, 360)))
        {
            ImGui::TableSetupColumn("Seq");
            ImGui::TableSetupColumn("Op");
            ImGui::TableSetupColumn("TID");
            ImGui::TableSetupColumn("PC");
            ImGui::TableSetupColumn("RA");
            ImGui::TableSetupColumn("SID");
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Rpc#");
            ImGui::TableSetupColumn("Client");
            ImGui::TableSetupColumn("Server");
            ImGui::TableSetupColumn("Send");
            ImGui::TableSetupColumn("SSize");
            ImGui::TableSetupColumn("Recv");
            ImGui::TableSetupColumn("RSize");
            ImGui::TableSetupColumn("ResultPtr");
            ImGui::TableSetupColumn("Mode");
            ImGui::TableSetupColumn("EndFunc");
            ImGui::TableSetupColumn("EndParam");
            ImGui::TableSetupColumn("Sema");
            ImGui::TableSetupColumn("Flags");
            ImGui::TableHeadersRow();

            for (const SifRpcDebugEvent &event : events)
            {
                const char *op = event.op ? event.op : "";
                if (onlyCalls && std::strcmp(op, "CallRpc") != 0)
                {
                    continue;
                }
                if (hideBindNoise && std::strcmp(op, "CallRpc") != 0)
                {
                    continue;
                }

                const std::string flags = rpcDebugFlags(event.flags);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%llu", static_cast<unsigned long long>(event.seq));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(op);
                ImGui::TableNextColumn();
                ImGui::Text("%u", event.threadId);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", event.pc);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", event.ra);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", event.sid);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(iopRpcSidName(event.sid));
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", event.rpcNum);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", event.clientPtr);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", event.serverPtr);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", event.sendBuf);
                ImGui::TableNextColumn();
                ImGui::Text("%u", event.sendSize);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", event.recvBuf);
                ImGui::TableNextColumn();
                ImGui::Text("%u", event.recvSize);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", event.resultPtr);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", event.mode);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", event.endFunc);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", event.endParam);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", event.semaId);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(flags.c_str());
            }
            ImGui::EndTable();
        }
    }

    void drawPadBytes(const uint8_t *data, size_t size)
    {
        char bytes[3 * 32 + 1]{};
        const size_t count = std::min<size_t>(size, 32u);
        for (size_t i = 0; i < count; ++i)
        {
            std::snprintf(bytes + (i * 3), sizeof(bytes) - (i * 3), "%02X ", data[i]);
        }
        ImGui::TextUnformatted(bytes);
    }

    void drawPadTab()
    {
        const ps2_stubs::PadDebugSnapshot snapshot = ps2_stubs::getPadDebugSnapshot();

        ImGui::SeparatorText("PAD global state");
        ImGui::Text("override=%u readLogCount=%d", snapshot.overrideEnabled ? 1u : 0u, snapshot.readLogCount);
        ImGui::SameLine();
        ImGui::Text("override buttons=0x%04X lx=%u ly=%u rx=%u ry=%u",
                    snapshot.overrideButtons,
                    snapshot.overrideLx,
                    snapshot.overrideLy,
                    snapshot.overrideRx,
                    snapshot.overrideRy);
        ImGui::Text("override pressed: %s", pressedPadButtons(snapshot.overrideButtons).c_str());

        if (ImGui::BeginTable("pad_ports", 15, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
        {
            ImGui::TableSetupColumn("Port");
            ImGui::TableSetupColumn("Slot");
            ImGui::TableSetupColumn("Open");
            ImGui::TableSetupColumn("Mode");
            ImGui::TableSetupColumn("Pressure");
            ImGui::TableSetupColumn("Req");
            ImGui::TableSetupColumn("DMA");
            ImGui::TableSetupColumn("Read#");
            ImGui::TableSetupColumn("ReadAddr");
            ImGui::TableSetupColumn("Source");
            ImGui::TableSetupColumn("Buttons raw");
            ImGui::TableSetupColumn("Pressed");
            ImGui::TableSetupColumn("LX/LY");
            ImGui::TableSetupColumn("RX/RY");
            ImGui::TableSetupColumn("Mask");
            ImGui::TableHeadersRow();

            for (size_t port = 0; port < ps2_stubs::kPadDebugPortCount; ++port)
            {
                for (size_t slot = 0; slot < ps2_stubs::kPadDebugSlotCount; ++slot)
                {
                    const ps2_stubs::PadDebugPortSnapshot &row = snapshot.ports[port][slot];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%zu", port);
                    ImGui::TableNextColumn();
                    ImGui::Text("%zu", slot);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", row.open ? 1u : 0u);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(row.analogMode ? "analog" : "digital");
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", row.pressureEnabled ? 1u : 0u);
                    ImGui::TableNextColumn();
                    ImGui::Text("0x%08X", row.reqState);
                    ImGui::TableNextColumn();
                    ImGui::Text("0x%08X", row.dmaAddr);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u", row.readCount);
                    ImGui::TableNextColumn();
                    ImGui::Text("0x%08X", row.lastReadDataAddr);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(row.lastUsedOverride ? "override" : (row.lastUsedBackend ? "backend" : "fallback"));
                    ImGui::TableNextColumn();
                    ImGui::Text("0x%04X", row.lastButtons);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(pressedPadButtons(row.lastButtons).c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%u/%u", row.lx, row.ly);
                    ImGui::TableNextColumn();
                    ImGui::Text("%u/%u", row.rx, row.ry);
                    ImGui::TableNextColumn();
                    ImGui::Text("0x%04X", row.buttonMask);
                }
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Last scePadRead bytes");
        for (size_t port = 0; port < ps2_stubs::kPadDebugPortCount; ++port)
        {
            for (size_t slot = 0; slot < ps2_stubs::kPadDebugSlotCount; ++slot)
            {
                const ps2_stubs::PadDebugPortSnapshot &row = snapshot.ports[port][slot];
                ImGui::Text("port=%zu slot=%zu ok=%u data:", port, slot, row.lastReadOk ? 1u : 0u);
                ImGui::SameLine();
                drawPadBytes(row.lastData, ps2_stubs::kPadDebugDataSize);
            }
        }

        ImGui::SeparatorText("PS2 active-low button layout");
        ImGui::TextUnformatted("byte[2:3] raw is active-low: 0 means pressed, 1 means released.");
        ImGui::TextUnformatted("bits: Select,L3,R3,Start,Up,Right,Down,Left,L2,R2,L1,R1,Triangle,Circle,Cross,Square");
    }

    void drawGsTab(PS2Runtime &runtime)
    {
        GSRegisters &regs = runtime.memory().gs();
        const GSDebugSnapshot gs = runtime.gs().getDebugSnapshot();

        ImGui::SeparatorText("GS private registers");
        if (ImGui::BeginTable("gspriv", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
        {
            auto row64 = [](const char *name, uint64_t value)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(name);
                ImGui::TableNextColumn();
                ImGui::Text("0x%016llX", static_cast<unsigned long long>(value));
                ImGui::TableNextColumn();
                ImGui::Text("%llu", static_cast<unsigned long long>(value));
                ImGui::TableNextColumn();
                ImGui::Text("lo=0x%08X hi=0x%08X", static_cast<uint32_t>(value), static_cast<uint32_t>(value >> 32));
            };
            ImGui::TableSetupColumn("Reg");
            ImGui::TableSetupColumn("Hex64");
            ImGui::TableSetupColumn("Dec");
            ImGui::TableSetupColumn("Split");
            ImGui::TableHeadersRow();
            row64("PMODE", regs.pmode);
            row64("SMODE2", regs.smode2);
            row64("DISPFB1", regs.dispfb1);
            row64("DISPLAY1", regs.display1);
            row64("DISPFB2", regs.dispfb2);
            row64("DISPLAY2", regs.display2);
            row64("BGCOLOR", regs.bgcolor);
            row64("CSR", regs.csr);
            row64("IMR", regs.imr);
            row64("BUSDIR", regs.busdir);
            row64("SIGLBLID", regs.siglblid);
            ImGui::EndTable();
        }

        ImGui::SeparatorText("GS draw state");
        ImGui::Text("PRIM type=%s iip=%u tme=%u fge=%u abe=%u aa1=%u fst=%u ctxt=%u fix=%u",
                    primTypeName(gs.prim.type), gs.prim.iip, gs.prim.tme, gs.prim.fge, gs.prim.abe,
                    gs.prim.aa1, gs.prim.fst, gs.prim.ctxt, gs.prim.fix);
        ImGui::Text("TEXA ta0=0x%02X aem=%u ta1=0x%02X | TEXCLUT cbw=%u cou=%u cov=%u",
                    gs.texa.ta0, gs.texa.aem ? 1u : 0u, gs.texa.ta1, gs.texclut.cbw, gs.texclut.cou, gs.texclut.cov);
        ImGui::Text("BITBLT sbp=%u sbw=%u spsm=0x%02X dbp=%u dbw=%u dpsm=0x%02X",
                    gs.bitbltbuf.sbp, gs.bitbltbuf.sbw, gs.bitbltbuf.spsm, gs.bitbltbuf.dbp, gs.bitbltbuf.dbw, gs.bitbltbuf.dpsm);
        ImGui::Text("TRXPOS ss=(%u,%u) ds=(%u,%u) dir=%u | TRXREG %ux%u TRXDIR=%u copied=%u/%u at=(%u,%u)",
                    gs.trxpos.ssax, gs.trxpos.ssay, gs.trxpos.dsax, gs.trxpos.dsay, gs.trxpos.dir,
                    gs.trxreg.rrw, gs.trxreg.rrh, gs.trxdir, gs.transferCopiedPixels, gs.transferTotalPixels,
                    gs.transferX, gs.transferY);
        ImGui::Text("Host presentation: has=%u size=%ux%u displayFbp=%u sourceFbp=%u preferred=%u lastDisplayBaseBytes=0x%08X localToHostPending=%zu",
                    gs.hasHostPresentationFrame ? 1u : 0u,
                    gs.hostPresentationWidth,
                    gs.hostPresentationHeight,
                    gs.hostPresentationDisplayFbp,
                    gs.hostPresentationSourceFbp,
                    gs.hostPresentationUsedPreferred ? 1u : 0u,
                    gs.lastDisplayBaseBytes,
                    gs.localToHostPendingBytes);
        ImGui::Text("Preferred source: has=%u frame fbp=%u fbw=%u psm=0x%02X destFbp=%u",
                    gs.hasPreferredDisplaySource ? 1u : 0u,
                    gs.preferredDisplaySourceFrame.fbp,
                    gs.preferredDisplaySourceFrame.fbw,
                    gs.preferredDisplaySourceFrame.psm,
                    gs.preferredDisplayDestFbp);

        drawGsContext("Context 0", gs.ctx[0]);
        drawGsContext("Context 1", gs.ctx[1]);

        ImGui::SeparatorText("GS history");
        bool gsHistoryPaused = runtime.gs().isDebugHistoryPaused();
        std::vector<GSDebugHistoryEntry> history = runtime.gs().getDebugHistory();
        uint32_t latestFrame = 0u;
        if (!history.empty())
        {
            latestFrame = history.back().frameIndex;
        }

        static bool s_gsHistoryCurrentFrameOnly = false;
        static bool s_gsHistoryDrawsOnly = false;
        static std::string s_lastGsDumpPath;
        static std::string s_lastGsDumpError;

        ImGui::Text("Ring entries: %zu / 512%s", history.size(), gsHistoryPaused ? " (paused)" : "");
        ImGui::SameLine();
        if (ImGui::Button("Clear GS history"))
        {
            runtime.gs().clearDebugHistory();
            history.clear();
            latestFrame = 0u;
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Pause capture", &gsHistoryPaused))
        {
            runtime.gs().setDebugHistoryPaused(gsHistoryPaused);
        }
        ImGui::SameLine();
        if (ImGui::Button("Dump GS TXT"))
        {
            s_lastGsDumpError.clear();
            const std::filesystem::path dumpPath = makeDebugDumpPath("gs_history", latestFrame);
            if (writeGsDebugDump(dumpPath, regs, gs, history, s_gsHistoryCurrentFrameOnly, s_gsHistoryDrawsOnly, latestFrame, s_lastGsDumpError))
            {
                s_lastGsDumpPath = dumpPath.string();
                ImGui::SetClipboardText(s_lastGsDumpPath.c_str());
            }
            else
            {
                s_lastGsDumpPath.clear();
            }
        }
        if (gsHistoryPaused)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("history capture stopped");
        }

        ImGui::Checkbox("Current frame only", &s_gsHistoryCurrentFrameOnly);
        ImGui::SameLine();
        ImGui::Checkbox("Draws only", &s_gsHistoryDrawsOnly);
        if (!s_lastGsDumpPath.empty())
        {
            ImGui::TextDisabled("Last dump copied to clipboard: %s", s_lastGsDumpPath.c_str());
        }
        if (!s_lastGsDumpError.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Dump failed: %s", s_lastGsDumpError.c_str());
        }

        if (ImGui::BeginTable("gs_history", 16, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX, ImVec2(0, 260)))
        {
            ImGui::TableSetupColumn("Seq");
            ImGui::TableSetupColumn("Frame");
            ImGui::TableSetupColumn("Tick");
            ImGui::TableSetupColumn("Kind");
            ImGui::TableSetupColumn("Prim");
            ImGui::TableSetupColumn("Ctx");
            ImGui::TableSetupColumn("FBP/PSM");
            ImGui::TableSetupColumn("ZBP/PSM");
            ImGui::TableSetupColumn("TEST");
            ImGui::TableSetupColumn("ALPHA");
            ImGui::TableSetupColumn("TEX0");
            ImGui::TableSetupColumn("XY");
            ImGui::TableSetupColumn("Z");
            ImGui::TableSetupColumn("A");
            ImGui::TableSetupColumn("GIF/REG/XFER");
            ImGui::TableSetupColumn("Present");
            ImGui::TableHeadersRow();

            for (auto it = history.rbegin(); it != history.rend(); ++it)
            {
                const GSDebugHistoryEntry &row = *it;
                if (s_gsHistoryCurrentFrameOnly && row.frameIndex != latestFrame)
                {
                    continue;
                }
                if (s_gsHistoryDrawsOnly && row.kind != GSDebugEventKind::Draw)
                {
                    continue;
                }

                const uint64_t test = row.test;
                const uint64_t alpha = row.alpha;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%llu", static_cast<unsigned long long>(row.seq));
                ImGui::TableNextColumn();
                ImGui::Text("%u", row.frameIndex);
                ImGui::TableNextColumn();
                ImGui::Text("%llu", static_cast<unsigned long long>(row.vsyncTick));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(gsDebugEventKindName(row.kind));
                ImGui::TableNextColumn();
                ImGui::Text("%s", primTypeName(row.prim.type));
                ImGui::TableNextColumn();
                ImGui::Text("%u", row.prim.ctxt ? 1u : 0u);
                ImGui::TableNextColumn();
                ImGui::Text("%u/%u/0x%02X", row.frame.fbp, row.frame.fbw, row.frame.psm);
                ImGui::TableNextColumn();
                ImGui::Text("%u/0x%02X m=%u", row.zbuf.zbp, row.zbuf.psm, row.zbuf.zmask ? 1u : 0u);
                ImGui::TableNextColumn();
                ImGui::Text("ATE=%llu ATST=%llu AREF=%02llX AFAIL=%llu ZTE=%llu ZTST=%llu",
                            (test >> 0) & 1ull,
                            (test >> 1) & 7ull,
                            (test >> 4) & 0xffull,
                            (test >> 12) & 3ull,
                            (test >> 16) & 1ull,
                            (test >> 17) & 3ull);
                ImGui::TableNextColumn();
                ImGui::Text("0x%016llX", static_cast<unsigned long long>(alpha));
                ImGui::TableNextColumn();
                ImGui::Text("tbp=%u psm=0x%02X %ux%u cbp=%u cpsm=0x%02X",
                            row.tex0.tbp0,
                            row.tex0.psm,
                            1u << row.tex0.tw,
                            1u << row.tex0.th,
                            row.tex0.cbp,
                            row.tex0.cpsm);
                ImGui::TableNextColumn();
                if (row.kind == GSDebugEventKind::Draw)
                    ImGui::Text("%.1f..%.1f / %.1f..%.1f", row.xMin, row.xMax, row.yMin, row.yMax);
                else
                    ImGui::TextUnformatted("-");
                ImGui::TableNextColumn();
                if (row.kind == GSDebugEventKind::Draw)
                    ImGui::Text("%.0f..%.0f", row.zMin, row.zMax);
                else
                    ImGui::TextUnformatted("-");
                ImGui::TableNextColumn();
                if (row.kind == GSDebugEventKind::Draw)
                    ImGui::Text("%u..%u", row.aMin, row.aMax);
                else
                    ImGui::TextUnformatted("-");
                ImGui::TableNextColumn();
                if (row.kind == GSDebugEventKind::GifTag)
                {
                    ImGui::Text("%s nloop=%u nreg=%u size=%u", gifFormatName(row.gifFlg), row.gifNloop, row.gifNreg, row.gifSizeBytes);
                }
                else if (row.kind == GSDebugEventKind::Register)
                {
                    ImGui::Text("%s(0x%02X)=0x%016llX", gsRegName(row.reg), row.reg, static_cast<unsigned long long>(row.regValue));
                }
                else if (row.kind == GSDebugEventKind::Transfer)
                {
                    ImGui::Text("TRXDIR=%u %ux%u pix=%u sbp=%u dbp=%u", row.trxdir, row.trxreg.rrw, row.trxreg.rrh, row.transferPixels, row.bitbltbuf.sbp, row.bitbltbuf.dbp);
                }
                else
                {
                    ImGui::TextUnformatted("-");
                }
                ImGui::TableNextColumn();
                if (row.kind == GSDebugEventKind::Present)
                {
                    ImGui::Text("%ux%u display=%u source=%u pref=%u", row.width, row.height, row.displayFbp, row.sourceFbp, row.usedPreferred ? 1u : 0u);
                }
                else
                {
                    ImGui::TextUnformatted("-");
                }
            }
            ImGui::EndTable();
        }
    }

    void drawFileCdTab(PS2Runtime &runtime)
    {
        (void)runtime;

        const PS2Runtime::IoPaths &ioPaths = PS2Runtime::getIoPaths();
        ImGui::SeparatorText("Runtime IO paths");
        textPath("ELF", ioPaths.elfPath);
        textPath("ELF dir", ioPaths.elfDirectory);
        textPath("Host root", ioPaths.hostRoot);
        textPath("CD root", ioPaths.cdRoot);
        textPath("CD image", ioPaths.cdImage);
        textPath("MC root", ioPaths.mcRoot);

        bool pathsInitialized = false;
        std::filesystem::path hostBase;
        std::filesystem::path cdromBase;
        std::filesystem::path hostCwd;
        std::filesystem::path cdromCwd;
        std::string cwdDevice;
        {
            std::lock_guard<std::mutex> lock(g_ps2_path_mutex);
            pathsInitialized = g_ps2_paths_initialized;
            hostBase = g_host_base;
            cdromBase = g_cdrom_base;
            hostCwd = g_host_cwd;
            cdromCwd = g_cdrom_cwd;
            cwdDevice = g_ps2_cwd_device;
        }

        ImGui::SeparatorText("PS2 path resolver");
        ImGui::Text("initialized=%u cwdDevice=%s", pathsInitialized ? 1u : 0u, cwdDevice.c_str());
        textPath("host base", hostBase);
        textPath("cdrom base", cdromBase);
        textPath("host cwd", hostCwd);
        textPath("cdrom cwd", cdromCwd);

        struct FdRow
        {
            int fd = 0;
            FILE *file = nullptr;
        };

        std::vector<FdRow> fds;
        {
            std::lock_guard<std::mutex> lock(g_fd_mutex);
            fds.reserve(g_fileDescriptors.size());
            for (const auto &[fd, file] : g_fileDescriptors)
            {
                fds.push_back(FdRow{fd, file});
            }
        }
        std::sort(fds.begin(), fds.end(), [](const FdRow &a, const FdRow &b)
                  { return a.fd < b.fd; });

        ImGui::SeparatorText("FileIO descriptors");
        ImGui::Text("Open host FILE* descriptors: %zu", fds.size());
        if (ImGui::BeginTable("fileio_fds", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable, ImVec2(0, 90)))
        {
            ImGui::TableSetupColumn("FD");
            ImGui::TableSetupColumn("FILE*");
            ImGui::TableHeadersRow();
            for (const FdRow &row : fds)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", row.fd);
                ImGui::TableNextColumn();
                ImGui::Text("%p", static_cast<void *>(row.file));
            }
            ImGui::EndTable();
        }

        const ps2_stubs::CdDebugSnapshot cd = ps2_stubs::getCdDebugSnapshot();
        ImGui::SeparatorText("CDVD / sceCd state");
        ImGui::Text("initialized=%u lastError=%d mode=0x%08X streamingLbn=0x%08X endLbn=0x%08X nextPseudoLbn=0x%08X",
                    cd.initialized ? 1u : 0u,
                    cd.lastError,
                    cd.mode,
                    cd.streamingLbn,
                    cd.streamingEndLbn,
                    cd.nextPseudoLbn);
        ImGui::Text("imageValid=%u imageSize=%llu leafIndexBuilt=%u leafIndex=%zu looseIndex=%zu registeredFiles=%zu",
                    cd.imageSizeValid ? 1u : 0u,
                    static_cast<unsigned long long>(cd.imageSizeBytes),
                    cd.leafIndexBuilt ? 1u : 0u,
                    cd.leafIndexCount,
                    cd.loosePathIndexCount,
                    cd.files.size());
        textPath("CD resolved root", cd.cdRoot);
        textPath("CD image path", cd.cdImage);
        textPath("CD image size path", cd.imageSizePath);
        textPath("CD leaf index root", cd.leafIndexRoot);

        if (ImGui::BeginTable("cd_files", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0, 180)))
        {
            ImGui::TableSetupColumn("Key");
            ImGui::TableSetupColumn("Base LBN");
            ImGui::TableSetupColumn("End LBN");
            ImGui::TableSetupColumn("Sectors");
            ImGui::TableSetupColumn("Bytes");
            ImGui::TableSetupColumn("Host path");
            ImGui::TableHeadersRow();
            for (const ps2_stubs::CdDebugFileEntry &row : cd.files)
            {
                const uint32_t endLbn = row.baseLbn + row.sectors;
                const std::string hostPath = pathToString(row.hostPath);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(row.key.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", row.baseLbn);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", endLbn);
                ImGui::TableNextColumn();
                ImGui::Text("%u", row.sectors);
                ImGui::TableNextColumn();
                ImGui::Text("%u", row.sizeBytes);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(hostPath.c_str());
            }
            ImGui::EndTable();
        }

        const ps2_stubs::MemoryCardDebugSnapshot mc = ps2_stubs::getMemoryCardDebugSnapshot();
        ImGui::SeparatorText("Memory card state");
        ImGui::Text("lastCmd=0x%X lastResult=%d nextFd=%d cvCursor=%d openFiles=%zu",
                    mc.lastCmd,
                    mc.lastResult,
                    mc.nextFd,
                    mc.cvFileCursor,
                    mc.openFiles.size());

        if (ImGui::BeginTable("mc_ports", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
        {
            ImGui::TableSetupColumn("Port");
            ImGui::TableSetupColumn("Formatted");
            ImGui::TableSetupColumn("Current dir");
            ImGui::TableSetupColumn("Host root");
            ImGui::TableHeadersRow();
            for (const ps2_stubs::MemoryCardDebugPort &port : mc.ports)
            {
                const std::string root = pathToString(port.rootPath);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", port.port);
                ImGui::TableNextColumn();
                ImGui::Text("%u", port.formatted ? 1u : 0u);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(port.currentDir.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(root.c_str());
            }
            ImGui::EndTable();
        }

        if (ImGui::BeginTable("mc_open_files", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0, 120)))
        {
            ImGui::TableSetupColumn("FD");
            ImGui::TableSetupColumn("Port");
            ImGui::TableSetupColumn("Host path");
            ImGui::TableHeadersRow();
            for (const ps2_stubs::MemoryCardDebugOpenFile &row : mc.openFiles)
            {
                const std::string path = pathToString(row.hostPath);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", row.fd);
                ImGui::TableNextColumn();
                ImGui::Text("%d", row.port);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(path.c_str());
            }
            ImGui::EndTable();
        }
    }

    void drawIoTab(PS2Runtime &runtime)
    {
        PS2Memory &mem = runtime.memory();
        ImGui::SeparatorText("DMA/VIF/GIF IO registers");
        struct RegRow
        {
            const char *name;
            uint32_t addr;
        };
        static constexpr RegRow rows[] = {
            {"D_STAT", 0x1000e010u},
            {"VIF0_STAT", 0x10003800u},
            {"VIF0_FBRST", 0x10003810u},
            {"VIF0_ERR", 0x10003830u},
            {"VIF1_STAT", 0x10003c00u},
            {"VIF1_FBRST", 0x10003c10u},
            {"VIF1_ERR", 0x10003c30u},
            {"VIF0_CHCR", 0x10008000u},
            {"VIF0_MADR", 0x10008010u},
            {"VIF0_QWC", 0x10008020u},
            {"VIF0_TADR", 0x10008030u},
            {"VIF1_CHCR", 0x10009000u},
            {"VIF1_MADR", 0x10009010u},
            {"VIF1_QWC", 0x10009020u},
            {"VIF1_TADR", 0x10009030u},
            {"GIF_CHCR", 0x1000a000u},
            {"GIF_MADR", 0x1000a010u},
            {"GIF_QWC", 0x1000a020u},
            {"GIF_TADR", 0x1000a030u},
            {"IPU_FROM_CHCR", 0x1000b000u},
            {"IPU_TO_CHCR", 0x1000b400u},
        };

        if (ImGui::BeginTable("ioregs", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
        {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Addr");
            ImGui::TableSetupColumn("Value");
            ImGui::TableHeadersRow();
            for (const auto &row : rows)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(row.name);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", row.addr);
                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", mem.readIORegister(row.addr));
            }
            ImGui::EndTable();
        }
    }

    bool writeRuntimeLogDump(const std::filesystem::path &path,
                             const std::vector<ps2_log::RuntimeLogEntry> &entries,
                             ImGuiTextFilter *filter,
                             std::string &error)
    {
        try
        {
            std::ofstream out(path, std::ios::out | std::ios::trunc);
            if (!out.is_open())
            {
                error = "failed to open dump path";
                return false;
            }

            out << "PS2 runtime log dump\n";
            out << "entries=" << entries.size() << "\n";
            out << "filter=" << (filter && filter->IsActive() ? filter->InputBuf : "<none>") << "\n";
            out << "ps2_log_txt=" << ps2_log::log_path() << "\n";
            out << "\n";
            out << "seq\ttext\n";

            for (const ps2_log::RuntimeLogEntry &entry : entries)
            {
                if (filter && filter->IsActive() && !filter->PassFilter(entry.text.c_str()))
                {
                    continue;
                }
                out << entry.seq << '\t' << entry.text;
                if (entry.text.empty() || entry.text.back() != '\n')
                {
                    out << '\n';
                }
            }
            return true;
        }
        catch (const std::exception &e)
        {
            error = e.what();
            return false;
        }
    }

    void drawLogsTab()
    {
        static ImGuiTextFilter s_logFilter;
        static bool s_autoScroll = true;
        static bool s_wrapText = false;
        static bool s_latestFirst = false;
        static std::string s_lastLogDumpPath;
        static std::string s_lastLogDumpError;

        bool paused = ps2_log::is_runtime_log_paused();
        std::vector<ps2_log::RuntimeLogEntry> entries = ps2_log::snapshot_runtime_log_entries();

        ImGui::SeparatorText("Runtime log ring buffer");
        ImGui::Text("Entries: %zu / %zu%s", entries.size(), ps2_log::kMaxRuntimeLogEntries, paused ? " (paused)" : "");
        ImGui::TextDisabled("Captures RUNTIME_LOG(...) calls. Direct std::cerr/std::cout writes are still console-only unless routed through RUNTIME_LOG.");
        ImGui::TextDisabled("Aggressive call trace file: %s", ps2_log::log_path().c_str());

        if (ImGui::Button("Clear logs"))
        {
            ps2_log::clear_runtime_log_entries();
            entries.clear();
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Pause capture", &paused))
        {
            ps2_log::set_runtime_log_paused(paused);
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &s_autoScroll);
        ImGui::SameLine();
        ImGui::Checkbox("Wrap", &s_wrapText);
        ImGui::SameLine();
        ImGui::Checkbox("Latest first", &s_latestFirst);

        s_logFilter.Draw("Filter", 260.0f);
        ImGui::SameLine();
        if (ImGui::Button("Copy visible"))
        {
            std::ostringstream out;
            auto appendEntry = [&](const ps2_log::RuntimeLogEntry &entry)
            {
                if (s_logFilter.IsActive() && !s_logFilter.PassFilter(entry.text.c_str()))
                {
                    return;
                }
                out << entry.seq << '\t' << entry.text;
                if (entry.text.empty() || entry.text.back() != '\n')
                {
                    out << '\n';
                }
            };

            if (s_latestFirst)
            {
                for (auto it = entries.rbegin(); it != entries.rend(); ++it)
                {
                    appendEntry(*it);
                }
            }
            else
            {
                for (const ps2_log::RuntimeLogEntry &entry : entries)
                {
                    appendEntry(entry);
                }
            }
            const std::string text = out.str();
            ImGui::SetClipboardText(text.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Dump logs TXT"))
        {
            s_lastLogDumpError.clear();
            const std::filesystem::path dumpPath = makeDebugDumpPath("runtime_log", 0u);
            if (writeRuntimeLogDump(dumpPath, entries, &s_logFilter, s_lastLogDumpError))
            {
                s_lastLogDumpPath = dumpPath.string();
                ImGui::SetClipboardText(s_lastLogDumpPath.c_str());
            }
            else
            {
                s_lastLogDumpPath.clear();
            }
        }

        if (!s_lastLogDumpPath.empty())
        {
            ImGui::TextDisabled("Last dump: %s", s_lastLogDumpPath.c_str());
        }
        if (!s_lastLogDumpError.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Dump failed: %s", s_lastLogDumpError.c_str());
        }

        ImGui::Separator();
        const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX;
        if (ImGui::BeginTable("runtime_logs", 2, tableFlags, ImVec2(0, 430)))
        {
            ImGui::TableSetupColumn("Seq", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Text", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            auto drawEntry = [&](const ps2_log::RuntimeLogEntry &entry)
            {
                if (s_logFilter.IsActive() && !s_logFilter.PassFilter(entry.text.c_str()))
                {
                    return;
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%llu", static_cast<unsigned long long>(entry.seq));
                ImGui::TableNextColumn();
                if (s_wrapText)
                {
                    ImGui::TextWrapped("%s", entry.text.c_str());
                }
                else
                {
                    ImGui::TextUnformatted(entry.text.c_str());
                }
            };

            if (s_latestFirst)
            {
                for (auto it = entries.rbegin(); it != entries.rend(); ++it)
                {
                    drawEntry(*it);
                }
            }
            else
            {
                for (const ps2_log::RuntimeLogEntry &entry : entries)
                {
                    drawEntry(entry);
                }
            }

            if (s_autoScroll && !s_latestFirst && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f)
            {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndTable();
        }
    }

#endif
}

void PS2DebugPanel::initialize()
{
#if defined(PS2X_ENABLE_DEBUG_UI) && !defined(PLATFORM_VITA)
    if (!m_initialized)
    {
        rlImGuiSetup(true);
        m_initialized = true;
    }
#endif
}

void PS2DebugPanel::shutdown()
{
#if defined(PS2X_ENABLE_DEBUG_UI) && !defined(PLATFORM_VITA)
    if (m_initialized)
    {
        rlImGuiShutdown();
        m_initialized = false;
    }
#endif
}

void PS2DebugPanel::draw(PS2Runtime &runtime)
{
#if defined(PS2X_ENABLE_DEBUG_UI) && !defined(PLATFORM_VITA)
    if (!m_initialized)
    {
        return;
    }

    if (IsKeyPressed(KEY_F1))
    {
        toggleVisible();
    }

    if (!m_visible)
    {
        return;
    }

    rlImGuiBegin();

    ImGui::SetNextWindowSize(ImVec2(780.0f, 620.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Runtime Debugger", &m_visible, ImGuiWindowFlags_MenuBar))
    {
        if (ImGui::BeginMenuBar())
        {
            ImGui::MenuItem("Registers", nullptr, &m_showRegisters);
            ImGui::EndMenuBar();
        }

        if (ImGui::BeginTabBar("debug-tabs"))
        {
            if (ImGui::BeginTabItem("CPU"))
            {
                drawCpuTab(runtime, m_showRegisters);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Threads"))
            {
                drawThreadsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Kernel"))
            {
                drawKernelTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("IOP/SIF"))
            {
                drawIopTab(runtime);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("RPC History"))
            {
                drawRpcHistoryTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("PAD"))
            {
                drawPadTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("File/CD"))
            {
                drawFileCdTab(runtime);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("GS"))
            {
                drawGsTab(runtime);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("DMA/IO"))
            {
                drawIoTab(runtime);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Logs"))
            {
                drawLogsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Memory"))
            {
                uint32_t bytes = m_memoryBytes;
                ImGui::InputScalar("Address", ImGuiDataType_U32, &m_memoryAddress, nullptr, nullptr, "%08X", ImGuiInputTextFlags_CharsHexadecimal);
                ImGui::InputScalar("Bytes", ImGuiDataType_U32, &bytes, nullptr, nullptr, "%u");
                m_memoryBytes = std::clamp<unsigned int>(bytes, 16u, 0x1000u);
                ImGui::SameLine();
                if (ImGui::Button("PC"))
                    m_memoryAddress = runtime.m_debugPc.load(std::memory_order_relaxed);
                ImGui::SameLine();
                if (ImGui::Button("SP"))
                    m_memoryAddress = runtime.m_debugSp.load(std::memory_order_relaxed);
                ImGui::SameLine();
                if (ImGui::Button("GP"))
                    m_memoryAddress = runtime.m_debugGp.load(std::memory_order_relaxed);
                ImGui::Separator();
                drawHexDump(runtime.memory().getRDRAM(), m_memoryAddress, m_memoryBytes);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    rlImGuiEnd();
#else
    (void)runtime;
#endif
}
