#include "MiniTest.h"
#include "ps2x/iop/ps2_path.h"
#include "../../ps2xIOP/tests/iop_compat_test_support.h"

void register_ps2_iop_tests()
{
    MiniTest::Case("PS2IopSubsystem", [](TestCase &tc)
    {
        tc.Run("PS2 path parsing is shared and normalizes ISO/module names", [](TestCase &t)
        {
            const ps2x::iop::ParsedPs2Path cd = ps2x::iop::parsePs2Path("CDROM0:\\MODULES\\LIBSD.IRX;1");
            t.Equals(cd.device, ps2x::iop::Ps2PathDevice::Cdrom,
                     "device names should be case-insensitive");
            t.Equals(cd.path, std::string("MODULES/LIBSD.IRX"),
                     "separators and ISO version suffixes should normalize once");
            t.Equals(ps2x::iop::ps2PathLeafKey(cd), std::string("libsd"),
                     "module lookup should use a normalized IRX leaf key");

            const ps2x::iop::ParsedPs2Path rom = ps2x::iop::parsePs2Path("rom0:ROMVER");
            t.Equals(rom.device, ps2x::iop::Ps2PathDevice::Rom0,
                     "ROM0 should remain a distinct virtual device");
            t.IsFalse(static_cast<bool>(ps2x::iop::parsePs2Path("unknown0:file.irx")),
                      "unsupported devices must not fall through to cdrom0");
        });

        tc.Run("HLE services activate only after a recognized module load", [](TestCase &t)
        {
            iop_test::Host host;
            ps2x::iop::IopSubsystem subsystem(host);
            t.IsFalse(subsystem.canBindRpc(0x80000701u),
                      "LIBSD RPC must not exist before LIBSD is loaded");

            const ps2x::iop::ModuleLoadResult unknown = subsystem.loadModule("rom0:NOT_A_REAL_MODULE");
            t.IsTrue(unknown.handled, "the module manager should return a real load result");
            t.IsTrue(unknown.moduleId < 0, "unknown ROM modules must fail instead of receiving fake IDs");

            const ps2x::iop::ModuleLoadResult loaded = subsystem.loadModule("rom0:LIBSD");
            t.IsTrue(loaded.moduleId > 0, "a registered no-BIOS HLE module should load");
            t.IsTrue(subsystem.canBindRpc(0x80000701u),
                     "loading LIBSD should activate its HLE RPC endpoint");

            ps2x::iop::RpcRequest request{};
            request.sid = 0x80000701u;
            request.function = 3u;
            t.IsTrue(subsystem.handleRpc(request).handled,
                     "the activated LIBSD service should handle its RPC");
            t.Equals(host.audioCalls, 1u, "the RPC should reach the HLE audio contract");

            int32_t stopResult = -1;
            t.IsTrue(subsystem.stopModule(loaded.moduleId, &stopResult),
                     "an HLE module should have a real stoppable lifecycle");
            t.Equals(stopResult, 0, "stopping an HLE module should report success");
            t.IsFalse(subsystem.canBindRpc(0x80000701u),
                      "stopping LIBSD should deactivate its RPC endpoint");
        });

        tc.Run("cdvdman ROM service serves the cdvdfsv RPC family", [](TestCase &t)
        {
            iop_test::Host host;
            ps2x::iop::IopSubsystem subsystem(host);

            // cdvdman/cdvdfsv register their EE-facing RPC servers from the IOP
            // ROM at boot, so the endpoints are bindable without any module
            // load. Games bind these in libcdvd's init loop and poll
            // client->server until a descriptor appears.
            constexpr uint32_t kCcmdSid = 0x80000592u;    // async command server
            constexpr uint32_t kScmdSid = 0x80000593u;    // sync command server
            constexpr uint32_t kStcmdSid = 0x80000595u;   // CD streaming server
            constexpr uint32_t kSearchSid = 0x8000059Au;  // file search server
            t.IsTrue(subsystem.canBindRpc(kCcmdSid), "Ccmd must be bindable without loading modules");
            t.IsTrue(subsystem.canBindRpc(kScmdSid), "Scmd must be bindable without loading modules");
            t.IsTrue(subsystem.canBindRpc(kStcmdSid), "Stcmd must be bindable without loading modules");
            t.IsTrue(subsystem.canBindRpc(kSearchSid), "the search server must be bindable without loading modules");

            // SLUS_215.97 (MOH:EA) spins in its libcdvd init until the Scmd
            // bind reports a server, then issues SifCallRpc(scmd, 0x27, ...) with
            // a 12-byte receive buffer whose first word must read as success.
            host.fill(0x800u, 12u, 0xCCu);
            ps2x::iop::RpcRequest scmdRequest{};
            scmdRequest.sid = kScmdSid;
            scmdRequest.function = 0x27u;
            scmdRequest.receive = {0x800u, 12u};
            const ps2x::iop::RpcResult scmdResult = subsystem.handleRpc(scmdRequest);
            t.IsTrue(scmdResult.handled, "Scmd requests must be claimed by the cdvdman HLE");
            t.Equals(scmdResult.resultAddress, scmdRequest.receive.address,
                     "the reply must be delivered from the receive buffer");
            t.Equals(host.word(0x800u), 1u,
                     "Scmd 0x27 must report success in its first result word: SLUS_215.97's libcdvd "
                     "polls SifCallRpc(scmd, 0x27) in a loop that only exits on a nonzero first word");

            // Unknown sync commands must still be answered (neutral ready /
            // no-error state) so boot proceeds, and must be logged for diagnosis.
            host.fill(0x800u, 16u, 0xCCu);
            ps2x::iop::RpcRequest unknown = iop_test::request(kScmdSid, 0x42u);
            const ps2x::iop::RpcResult unknownResult = subsystem.handleRpc(unknown);
            t.IsTrue(unknownResult.handled, "unknown Scmd commands must still be answered");
            t.Equals(host.word(0x800u), 0u, "unknown Scmd commands must report a neutral success state");
            t.Equals(iop_test::metric(subsystem, "cdvdman", "unknown_rpc_logs"), 1u,
                     "unknown Scmd commands should be logged");
        });

        tc.Run("unknown SID remains unhandled without a loaded IRX", [](TestCase &t)
        {
            iop_test::Host host;
            ps2x::iop::IopSubsystem subsystem(host);

            ps2x::iop::RpcRequest request{};
            request.sid = 0xDEADC0DEu;
            request.function = 0x99u;
            const ps2x::iop::RpcResult result = subsystem.handleRpc(request);
            t.IsFalse(result.handled, "an unknown SID should not be claimed by the IOP subsystem");
            t.Equals(result.resultAddress, 0u, "an unknown SID should not return a guest result address");
            t.IsFalse(result.signalNowaitCompletion, "an unknown SID should not signal nowait completion");
            t.Equals(result.callbackPolicy, ps2x::iop::CallbackPolicy::RuntimeDefault,
                     "an unknown SID should preserve runtime callback handling");

        });

    });
}
