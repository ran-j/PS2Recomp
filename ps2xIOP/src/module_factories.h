#pragma once

#include "iop_service.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ps2x::iop::detail
{
    struct CriDtxBindings
    {
        std::string serviceName;
        uint32_t sid = 0u;
        uint32_t urpcObjectBase = 0u;
        uint32_t urpcObjectLimit = 0u;
        uint32_t urpcObjectStride = 0u;
        uint32_t urpcFunctionTableBase = 0u;
        uint32_t urpcObjectTableBase = 0u;
        uint32_t dispatcherFunctionAddress = 0u;
        uint32_t rpcServerPoolBase = 0u;
        uint32_t rpcServerStride = 0u;
    };

    enum class TsnddrvProtocolVariant
    {
        SndQueueV1,
    };

    struct TsnddrvGuestArena
    {
        uint32_t base = 0u;
        uint32_t limit = 0u;
        uint32_t statusAlignment = 0u;
        uint32_t tableAlignment = 0u;
        uint32_t storageAlignment = 0u;
        uint32_t hdBytes = 0u;
        uint32_t sqBytes = 0u;
        uint32_t dataBytes = 0u;
    };

    struct TsnddrvChecksumTables
    {
        uint32_t seAddress = 0u;
        uint32_t midiAddress = 0u;
    };

    struct TsnddrvCompletionRule
    {
        uint32_t eeFunction = 0u;
        bool suppressGuestCallback = false;
        bool signalCompletion = false;
        bool clearBusy = false;
    };

    struct TsnddrvBindings
    {
        std::string serviceName;
        TsnddrvProtocolVariant protocol = TsnddrvProtocolVariant::SndQueueV1;
        TsnddrvGuestArena arena;
        std::vector<TsnddrvChecksumTables> checksumCandidates;
        uint32_t busyFlagAddress = 0u;
        std::vector<TsnddrvCompletionRule> completionRules;

        // SID + subcommand (fno) numbers this title's sound driver speaks. Defaults match
        // the values every currently-known title uses; override per game if a title's
        // driver registers a different service id or subcommand numbering. SID 0 is a
        // legitimate service id here (unlike the fno fields below, these two are always
        // considered configured).
        uint32_t commandSid = 0x00000000u;        // service id carrying the submit-command-buffer subcommand
        uint32_t stateSid = 0x00000001u;           // service id carrying the status/addr-table queries
        uint32_t submitFunction = 0x00000000u;     // submit-command-buffer
        uint32_t getStatusFunction = 0x00000012u;  // return statusAddress
        uint32_t getAddrTableFunction = 0x00000013u; // return addressTableAddress

        // Optional extra subcommands some titles' drivers expose. 0 = unused/disabled.
        uint32_t streamOpenFunction = 0u;     // write streamReadyValue to streamStateAddress
        uint32_t channelConfigFunction = 0u;  // set channelAllocFlagTableAddress[channel]=1 (channel from send word 0 if <16)
        uint32_t stopFunction = 0u;           // write 1 to stopCompletionFlagAddress
        uint32_t benignStatusValue = 0xffffff9bu; // recv[0] written for an unrecognized fno on a served sid
        uint32_t streamStateAddress = 0u;
        uint32_t streamReadyValue = 0u;
        uint32_t channelAllocFlagTableAddress = 0u;
        uint32_t stopCompletionFlagAddress = 0u;
    };

    struct ClFileRpcLayout
    {
        uint32_t directLoadFunction = 0x01u;
        uint32_t getStatusFunction = 0x03u;
        uint32_t initializeFunction = 0x04u;
        uint32_t waitFunction = 0x05u;
        uint32_t getSizeFunction = 0x06u;
        uint32_t openFunction = 0x08u;
        uint32_t closeFunction = 0x09u;
        uint32_t readFunction = 0x0Au;
        uint32_t secondaryWaitFunction = 0x15u;
        uint32_t setRootFunction = 0x16u;
        uint32_t pathBytes = 0x100u;
        uint32_t directLoadSizeOffset = 0x100u;
        uint32_t directLoadDestinationOffset = 0x104u;
        uint32_t responseStatusOffset = 0u;
        uint32_t responseValueOffset = 4u;
        uint32_t responseClearBytes = 0x40u;
        uint32_t maximumReadBytes = 0x2000u;
        uint32_t loadResultQueued = 5u;
        uint32_t loadStatusFailed = 3u;
        uint32_t loadStatusComplete = 7u;
        uint32_t invalidHandleStatus = 9u;
        uint32_t firstLoadHandle = 0x00010000u;
        bool acknowledgeUnknownFunctions = true;
    };

    struct ClFileBindings
    {
        std::string serviceName;
        uint32_t sid = 0u;
        ClFileRpcLayout rpc;
    };

    // TODO This is for the lord of the rings better name for that one
    struct SoundUpdateStubBindings
    {
        std::string serviceName;
        uint32_t sid = 0u;
        uint32_t activeStreamCountOffset = 0u;
        uint32_t responseCounterOffset = 0u;
        bool zeroReceiveBuffer = true;
        bool signalNowaitCompletion = false;
        std::vector<uint32_t> suppressedCompletionCallbacks;
    };

    struct SdrdrvBindings
    {
        std::string serviceName;
        uint32_t sid = 0u;
        uint32_t imageHeaderAddress = 0u;
        uint32_t sectorSize = 0u;
        uint32_t statusOffset = 0u;
        uint32_t statusStride = 0u;
        uint32_t statusSlotMask = 0u;
        uint8_t completeValue = 0u;
        uint32_t initFunction = 0u;
        uint32_t submitFunction = 1u;
        uint32_t shutdownFunction = 2u;
        uint32_t headerCommand = 0x0Cu;
        uint32_t loadCommand = 0x0Eu;
        uint32_t commandBytes = 32u;
        uint32_t maxCommands = 32u;
        uint32_t lbnWord = 2u;
        uint32_t byteCountWord = 3u;
        uint32_t destinationWord = 4u;
        uint32_t destinationKindWord = 5u;
        uint32_t loadIdWord = 6u;
        uint32_t eeDestinationKind = 0u;
        bool fallbackBodyToCdImage = true;
        bool clearReceiveBeforeDispatch = true;
        bool completeFailedLoads = true;
        bool pretendNonEeLoadsComplete = true;
        uint32_t headerWarningLimit = 4u;
        uint32_t bodyWarningLimit = 8u;
        std::string imageHeaderLowerName;
        std::string imageHeaderUpperName;
        std::string imageBodyLowerName;
        std::string imageBodyUpperName;
    };

    std::unique_ptr<IopService> createDbcmanService(IopHost &host);
    std::unique_ptr<IopService> createLibSdService(IopHost &host);
    std::unique_ptr<IopService> createMcservService(IopHost &host);
    std::unique_ptr<IopService> createTsnddrvService(IopHost &host, TsnddrvBindings bindings);
    std::unique_ptr<IopService> createCriDtxService(IopHost &host, CriDtxBindings bindings);
    std::unique_ptr<IopService> createClFileService(IopHost &host, ClFileBindings bindings);
    std::unique_ptr<IopService> createSoundUpdateStubService(IopHost &host, SoundUpdateStubBindings bindings);
    std::unique_ptr<IopService> createSdrdrvService(IopHost &host, SdrdrvBindings bindings);
}
