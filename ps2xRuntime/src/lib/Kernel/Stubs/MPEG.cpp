#include "Common.h"
#include "MPEG.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
}

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>

#include "Syscalls/Helpers/State.h"

namespace ps2_stubs
{
    namespace
    {
        struct MpegDecodedFrame
        {
            int width = 0;
            int height = 0;
            std::vector<uint8_t> rgba;
        };

        std::string ffmpegErrorString(int err)
        {
            std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
            if (av_strerror(err, buffer.data(), buffer.size()) < 0)
            {
                return "unknown FFmpeg error";
            }
            return std::string(buffer.data());
        }

        void configureFfmpegLogLevel()
        {
            static std::once_flag s_once;
            std::call_once(s_once, [] {
                av_log_set_level(ps2_log::agressive_logs_enabled ? AV_LOG_WARNING : AV_LOG_ERROR);
            });
        }

        class MpegFfmpegDecoder
        {
        public:
            MpegFfmpegDecoder() = default;

            ~MpegFfmpegDecoder()
            {
                reset();
            }

            MpegFfmpegDecoder(const MpegFfmpegDecoder &) = delete;
            MpegFfmpegDecoder &operator=(const MpegFfmpegDecoder &) = delete;

            bool feed(const uint8_t *data, size_t size, std::deque<MpegDecodedFrame> &frames)
            {
                if (!data || size == 0)
                {
                    return true;
                }

                if (!ensureInitialized())
                {
                    return false;
                }

                static uint32_t s_feedLogCount = 0u;
                const bool shouldLog = (s_feedLogCount < 32u);
                if (shouldLog)
                    ++s_feedLogCount;

                size_t totalParsed = 0u;
                size_t totalPacketsSent = 0u;
                size_t framesBefore = frames.size();

                const uint8_t *cursor = data;
                size_t remaining = size;
                while (remaining > 0)
                {
                    uint8_t *packetData = nullptr;
                    int packetSize = 0;
                    const int chunk = static_cast<int>(std::min<size_t>(
                        remaining, static_cast<size_t>(std::numeric_limits<int>::max())));
                    const int used = av_parser_parse2(
                        m_parser,
                        m_codecCtx,
                        &packetData,
                        &packetSize,
                        cursor,
                        chunk,
                        AV_NOPTS_VALUE,
                        AV_NOPTS_VALUE,
                        0);
                    if (used < 0)
                    {
                        std::cerr << "[MPEG] parser failed: " << ffmpegErrorString(used) << std::endl;
                        return false;
                    }
                    if (used == 0 && packetSize == 0)
                    {
                        break;
                    }

                    totalParsed += static_cast<size_t>(used);
                    cursor += used;
                    remaining -= static_cast<size_t>(used);

                    if (packetSize > 0)
                    {
                        ++totalPacketsSent;
                        if (!sendPacket(packetData, static_cast<size_t>(packetSize), frames))
                        {
                            return false;
                        }
                    }
                }

                if (shouldLog)
                {
                    PS2_IF_AGRESSIVE_LOGS({
                        std::cerr << "[MPEG:feed] inSize=" << size
                                  << " parsed=" << totalParsed
                                  << " packets=" << totalPacketsSent
                                  << " newFrames=" << (frames.size() - framesBefore)
                                  << " totalFrames=" << frames.size()
                                  << std::endl;
                    });
                }

                return true;
            }

            bool flush(std::deque<MpegDecodedFrame> &frames)
            {
                if (!m_initialized || m_drained)
                {
                    return true;
                }

                if (m_parser)
                {
                    uint8_t *packetData = nullptr;
                    int packetSize = 0;
                    const int used = av_parser_parse2(
                        m_parser,
                        m_codecCtx,
                        &packetData,
                        &packetSize,
                        nullptr,
                        0,
                        AV_NOPTS_VALUE,
                        AV_NOPTS_VALUE,
                        0);
                    (void)used;
                    if (packetSize > 0 && !sendPacket(packetData, static_cast<size_t>(packetSize), frames))
                    {
                        return false;
                    }
                }

                const int sendRet = avcodec_send_packet(m_codecCtx, nullptr);
                if (sendRet < 0 && sendRet != AVERROR_EOF)
                {
                    std::cerr << "[MPEG] decoder flush failed: " << ffmpegErrorString(sendRet) << std::endl;
                    return false;
                }

                const bool ok = receiveFrames(frames);
                m_drained = true;
                return ok;
            }

            void reset()
            {
                if (m_swsCtx)
                {
                    sws_freeContext(m_swsCtx);
                    m_swsCtx = nullptr;
                }
                if (m_frame)
                {
                    av_frame_free(&m_frame);
                }
                if (m_packet)
                {
                    av_packet_free(&m_packet);
                }
                if (m_codecCtx)
                {
                    avcodec_free_context(&m_codecCtx);
                }
                if (m_parser)
                {
                    av_parser_close(m_parser);
                    m_parser = nullptr;
                }

                m_swsWidth = 0;
                m_swsHeight = 0;
                m_swsFormat = AV_PIX_FMT_NONE;
                m_initialized = false;
                m_drained = false;
            }

        private:
            bool ensureInitialized()
            {
                if (m_initialized)
                {
                    return true;
                }

                configureFfmpegLogLevel();

                const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_MPEG2VIDEO);
                if (!codec)
                {
                    std::cerr << "[MPEG] FFmpeg MPEG-2 decoder not found." << std::endl;
                    return false;
                }

                m_parser = av_parser_init(AV_CODEC_ID_MPEG2VIDEO);
                if (!m_parser)
                {
                    std::cerr << "[MPEG] FFmpeg MPEG-video parser not found." << std::endl;
                    return false;
                }

                m_codecCtx = avcodec_alloc_context3(codec);
                m_frame = av_frame_alloc();
                m_packet = av_packet_alloc();
                if (!m_codecCtx || !m_frame || !m_packet)
                {
                    std::cerr << "[MPEG] failed to allocate FFmpeg decoder state." << std::endl;
                    reset();
                    return false;
                }

                m_codecCtx->thread_count = 1;
                m_codecCtx->skip_frame = AVDISCARD_NONKEY;
                m_codecCtx->err_recognition = 0;
                const int ret = avcodec_open2(m_codecCtx, codec, nullptr);
                if (ret < 0)
                {
                    std::cerr << "[MPEG] failed to open MPEG decoder: " << ffmpegErrorString(ret) << std::endl;
                    reset();
                    return false;
                }

                m_initialized = true;
                m_drained = false;
                m_seenKeyframe = false;
                return true;
            }

            bool sendPacket(const uint8_t *data, size_t size, std::deque<MpegDecodedFrame> &frames)
            {
                if (!data || size == 0)
                {
                    return true;
                }

                av_packet_unref(m_packet);
                const int allocRet = av_new_packet(m_packet, static_cast<int>(size));
                if (allocRet < 0)
                {
                    std::cerr << "[MPEG] failed to allocate packet: " << ffmpegErrorString(allocRet) << std::endl;
                    return false;
                }
                std::memcpy(m_packet->data, data, size);

                int ret = avcodec_send_packet(m_codecCtx, m_packet);
                if (ret == AVERROR(EAGAIN))
                {
                    if (!receiveFrames(frames))
                    {
                        av_packet_unref(m_packet);
                        return false;
                    }
                    ret = avcodec_send_packet(m_codecCtx, m_packet);
                }
                av_packet_unref(m_packet);
                if (ret < 0 && ret != AVERROR(EAGAIN))
                {
                    static uint32_t s_rejectedPacketLogCount = 0u;
                    if (s_rejectedPacketLogCount < 32u)
                    {
                        std::cerr << "[MPEG] decoder rejected packet, dropping: "
                                  << ffmpegErrorString(ret) << std::endl;
                        ++s_rejectedPacketLogCount;
                    }
                    return true;
                }

                return receiveFrames(frames);
            }

            bool receiveFrames(std::deque<MpegDecodedFrame> &frames)
            {
                while (true)
                {
                    const int ret = avcodec_receive_frame(m_codecCtx, m_frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                    {
                        return true;
                    }
                    if (ret < 0)
                    {
                        static uint32_t s_receiveErrorLogCount = 0u;
                        if (s_receiveErrorLogCount < 32u)
                        {
                            std::cerr << "[MPEG] decoder receive failed, dropping: "
                                      << ffmpegErrorString(ret) << std::endl;
                            ++s_receiveErrorLogCount;
                        }
                        return true;
                    }

                    if (!m_seenKeyframe)
                    {
                        m_seenKeyframe = true;
                        m_codecCtx->skip_frame = AVDISCARD_DEFAULT;
                    }

                    if (!convertFrame(frames))
                    {
                        av_frame_unref(m_frame);
                        return false;
                    }
                    av_frame_unref(m_frame);
                }
            }

            bool convertFrame(std::deque<MpegDecodedFrame> &frames)
            {
                const int width = m_frame->width;
                const int height = m_frame->height;
                const AVPixelFormat srcFormat = static_cast<AVPixelFormat>(m_frame->format);
                if (width <= 0 || height <= 0 || srcFormat == AV_PIX_FMT_NONE)
                {
                    return false;
                }

                if (!m_swsCtx ||
                    m_swsWidth != width ||
                    m_swsHeight != height ||
                    m_swsFormat != srcFormat)
                {
                    if (m_swsCtx)
                    {
                        sws_freeContext(m_swsCtx);
                        m_swsCtx = nullptr;
                    }
                    m_swsCtx = sws_getContext(
                        width,
                        height,
                        srcFormat,
                        width,
                        height,
                        AV_PIX_FMT_RGBA,
                        SWS_BILINEAR,
                        nullptr,
                        nullptr,
                        nullptr);
                    if (!m_swsCtx)
                    {
                        std::cerr << "[MPEG] failed to create FFmpeg scaler." << std::endl;
                        return false;
                    }
                    m_swsWidth = width;
                    m_swsHeight = height;
                    m_swsFormat = srcFormat;
                }

                MpegDecodedFrame decoded;
                decoded.width = width;
                decoded.height = height;
                decoded.rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);

                uint8_t *dstData[4] = {decoded.rgba.data(), nullptr, nullptr, nullptr};
                int dstLinesize[4] = {width * 4, 0, 0, 0};
                const int scaledRows = sws_scale(
                    m_swsCtx,
                    m_frame->data,
                    m_frame->linesize,
                    0,
                    height,
                    dstData,
                    dstLinesize);
                if (scaledRows <= 0)
                {
                    std::cerr << "[MPEG] FFmpeg scaler produced no rows." << std::endl;
                    return false;
                }

                frames.push_back(std::move(decoded));
                return true;
            }

            AVCodecParserContext *m_parser = nullptr;
            AVCodecContext *m_codecCtx = nullptr;
            AVFrame *m_frame = nullptr;
            AVPacket *m_packet = nullptr;
            SwsContext *m_swsCtx = nullptr;
            int m_swsWidth = 0;
            int m_swsHeight = 0;
            AVPixelFormat m_swsFormat = AV_PIX_FMT_NONE;
            bool m_initialized = false;
            bool m_drained = false;
            bool m_seenKeyframe = false;
        };

        struct MpegRegisteredCallback
        {
            uint32_t type = 0u;
            uint32_t streamId = 0u;
            uint32_t func = 0u;
            uint32_t data = 0u;
            uint32_t handle = 0u;
            bool stream = false;
        };

        struct MpegPlaybackState
        {
            uint32_t picturesServed = 0u;
            uint32_t width = 320u;
            uint32_t height = 240u;
            uint32_t decodeMode = 0u;
            uint32_t imageBufferAddr = 0u;
            bool sawInput = false;
            bool streamEnded = false;
            bool decoderFailed = false;
            uint64_t cdStreamGeneration = 0u;
            bool noFrameStallArmed = false;
            std::chrono::steady_clock::time_point noFrameStallStart{};
            uint32_t consecutiveEmptyGetPicture = 0u;
            bool waitingForVideoSequenceHeader = true;
            std::vector<uint8_t> videoSequenceSyncBuffer;
            std::vector<uint8_t> pssBuffer;
            std::vector<uint32_t> pssGuestAddrs;
            std::deque<MpegDecodedFrame> decodedFrames;
            bool hasLastFrame = false;
            MpegDecodedFrame lastFrame;
            std::unique_ptr<MpegFfmpegDecoder> decoder;
        };

        struct MpegStreamCallbackEvent
        {
            uint32_t mpegAddr = 0u;
            uint32_t streamType = 0u;
            uint32_t dataAddr = 0u;
            uint32_t len = 0u;
            uint64_t pts = 0xFFFFFFFFFFFFFFFFull;
            uint64_t dts = 0xFFFFFFFFFFFFFFFFull;
            std::vector<MpegRegisteredCallback> callbacks;
        };

        struct MpegStubState
        {
            bool initialized = false;
            uint32_t nextCallbackHandle = 1u;
            uint64_t cdStreamGeneration = 0u;
            bool currentCdStreamEofSeen = false;
            uint32_t feedEsTraceCount = 0u;
            uint32_t demuxPssTraceCount = 0u;
            uint32_t demuxRingTraceCount = 0u;
            uint32_t getPictureWaitTraceCount = 0u;
            uint32_t pictureTraceCount = 0u;
            uint32_t isEndTraceCount = 0u;
            std::unordered_map<uint32_t, std::vector<MpegRegisteredCallback>> callbacksByMpeg;
            std::unordered_map<uint32_t, MpegPlaybackState> playbackByMpeg;
        };

        std::mutex g_mpeg_stub_mutex;
        std::condition_variable g_mpeg_cv;
        MpegStubState g_mpeg_stub_state;

        // TODO this resolution should follow runtime resolution
        constexpr uint32_t kStubMovieWidth = 320u;
        constexpr uint32_t kStubMovieHeight = 240u;
        constexpr uint32_t kMpegStrM2V = 0u;
        constexpr uint32_t kMpegStrPCM = 1u;
        constexpr uint32_t kMpegStrADPCM = 2u;
        constexpr uint8_t kMpegPackHeader = 0xBAu;
        constexpr uint8_t kMpegSystemHeader = 0xBBu;
        constexpr uint8_t kMpegProgramEnd = 0xB9u;
        constexpr uint8_t kMpegSequenceEnd = 0xB7u;
        constexpr uint8_t kMpegPrivateStream1 = 0xBDu;
        constexpr size_t kStartCodeNotFound = std::numeric_limits<size_t>::max();
        constexpr uint32_t kMpegCallbackDataSize = 0x20u;
        constexpr uint32_t kMpegCallbackMaxSteps = 0x4000u;
        constexpr std::chrono::milliseconds kMpegGetPictureNoFrameWaitTimeout{64};
        constexpr std::chrono::milliseconds kMpegNoFrameEndTimeout{500};
        constexpr uint32_t kMpegMaxConsecutiveEmptyGetPicture = 60u;

        uint32_t align16(uint32_t value)
        {
            return (value + 15u) & ~15u;
        }

        uint32_t readStackArg(uint8_t *rdram, R5900Context *ctx, uint32_t offset)
        {
            if (!rdram || !ctx)
            {
                return 0u;
            }
            return FAST_READ32(getRegU32(ctx, 29) + offset);
        }

        uint32_t readAbiArg4(uint8_t *rdram, R5900Context *ctx)
        {
            const uint32_t regArg = getRegU32(ctx, 8);
            if (regArg != 0u)
            {
                return regArg;
            }
            return readStackArg(rdram, ctx, 0x10u);
        }

        MpegPlaybackState &getPlaybackState(uint32_t mpegAddr)
        {
            return g_mpeg_stub_state.playbackByMpeg[mpegAddr];
        }

        MpegPlaybackState makeFreshPlaybackState()
        {
            MpegPlaybackState playback{};
            playback.cdStreamGeneration = g_mpeg_stub_state.cdStreamGeneration;
            return playback;
        }

        MpegPlaybackState makeFreshPlaybackStatePreservingConfig(const MpegPlaybackState &oldPlayback)
        {
            MpegPlaybackState playback = makeFreshPlaybackState();
            playback.decodeMode = oldPlayback.decodeMode;
            playback.imageBufferAddr = oldPlayback.imageBufferAddr;
            playback.width = oldPlayback.width;
            playback.height = oldPlayback.height;
            return playback;
        }

        uint16_t readBe16(const uint8_t *p)
        {
            return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8u) |
                                         static_cast<uint16_t>(p[1]));
        }

        bool isVideoStreamId(uint8_t streamId)
        {
            return streamId >= 0xE0u && streamId <= 0xEFu;
        }

        bool isAudioStreamId(uint8_t streamId)
        {
            return streamId == kMpegPrivateStream1 || (streamId >= 0xC0u && streamId <= 0xDFu);
        }

        bool isLengthPrefixedHeader(uint8_t streamId)
        {
            switch (streamId)
            {
            case kMpegSystemHeader:
            case 0xBCu: // program_stream_map
            case 0xBEu: // padding_stream
            case 0xBFu: // private_stream_2
            case 0xF0u: // ECM
            case 0xF1u: // EMM
            case 0xF2u: // DSMCC
            case 0xF8u: // ITU-T H.222.1 type E
            case 0xFFu: // program_stream_directory
                return true;
            default:
                return false;
            }
        }

        size_t findStartCode(const std::vector<uint8_t> &buffer, size_t from)
        {
            if (buffer.size() < 4 || from >= buffer.size() - 3u)
            {
                return kStartCodeNotFound;
            }

            for (size_t i = from; i + 3u < buffer.size(); ++i)
            {
                if (buffer[i] == 0x00u && buffer[i + 1u] == 0x00u && buffer[i + 2u] == 0x01u)
                {
                    return i;
                }
            }
            return kStartCodeNotFound;
        }

        bool containsMpegSequenceEnd(const uint8_t *data, size_t size)
        {
            if (!data || size < 4)
            {
                return false;
            }

            for (size_t i = 0; i + 3u < size; ++i)
            {
                if (data[i] == 0x00u &&
                    data[i + 1u] == 0x00u &&
                    data[i + 2u] == 0x01u &&
                    data[i + 3u] == kMpegSequenceEnd)
                {
                    return true;
                }
            }
            return false;
        }

        size_t findMpegSequenceHeader(const uint8_t *data, size_t size)
        {
            if (!data || size < 7u)
            {
                return kStartCodeNotFound;
            }

            for (size_t i = 0; i + 6u < size; ++i)
            {
                if (data[i] == 0x00u &&
                    data[i + 1u] == 0x00u &&
                    data[i + 2u] == 0x01u &&
                    data[i + 3u] == 0xB3u)
                {
                    const uint32_t width = (static_cast<uint32_t>(data[i + 4u]) << 4u) |
                                           (static_cast<uint32_t>(data[i + 5u]) >> 4u);
                    const uint32_t height = ((static_cast<uint32_t>(data[i + 5u]) & 0x0Fu) << 8u) |
                                            static_cast<uint32_t>(data[i + 6u]);
                    if (width != 0u && height != 0u && width <= 4096u && height <= 4096u)
                    {
                        return i;
                    }
                }
            }
            return kStartCodeNotFound;
        }

        size_t parsePesPayloadOffset(const uint8_t *packet, size_t packetSize)
        {
            if (!packet || packetSize <= 6u)
            {
                return packetSize;
            }

            size_t pos = 6u;
            if (packetSize >= 9u && (packet[pos] & 0xC0u) == 0x80u)
            {
                return std::min(packetSize, 9u + static_cast<size_t>(packet[pos + 2u]));
            }

            while (pos < packetSize && packet[pos] == 0xFFu)
            {
                ++pos;
            }

            if (pos + 1u < packetSize && (packet[pos] & 0xC0u) == 0x40u)
            {
                pos += 2u;
            }

            if (pos >= packetSize)
            {
                return packetSize;
            }

            const uint8_t flags = packet[pos];
            if ((flags & 0xF0u) == 0x20u)
            {
                pos += 5u;
            }
            else if ((flags & 0xF0u) == 0x30u)
            {
                pos += 10u;
            }
            else if (flags == 0x0Fu)
            {
                pos += 1u;
            }

            return std::min(packetSize, pos);
        }

        void flushDecoderIfEnded(MpegPlaybackState &playback)
        {
            if (playback.streamEnded && playback.decoder)
            {
                playback.decoder->flush(playback.decodedFrames);
            }
        }

        void clearNoFrameStall(MpegPlaybackState &playback)
        {
            playback.noFrameStallArmed = false;
            playback.noFrameStallStart = {};
        }

        void finishPlaybackStream(uint32_t mpegAddr, MpegPlaybackState &playback);

        bool maybeFinishNoFrameStall(uint32_t mpegAddr, MpegPlaybackState &playback)
        {
            if (playback.streamEnded || playback.decoderFailed || !playback.decodedFrames.empty())
            {
                clearNoFrameStall(playback);
                playback.consecutiveEmptyGetPicture = 0u;
                return false;
            }

            if (!playback.sawInput || playback.picturesServed == 0u)
            {
                return false;
            }

            const auto now = std::chrono::steady_clock::now();
            const bool cdStreamEofSeen = g_mpeg_stub_state.currentCdStreamEofSeen;

            bool stallByNoFrame = false;
            if (cdStreamEofSeen)
            {
                if (!playback.noFrameStallArmed)
                {
                    playback.noFrameStallArmed = true;
                    playback.noFrameStallStart = now;
                }
                else if (now - playback.noFrameStallStart >= kMpegNoFrameEndTimeout)
                {
                    stallByNoFrame = true;
                }
            }
            else
            {
                clearNoFrameStall(playback);
            }

            const bool stallByConsecutive =
                cdStreamEofSeen && (playback.consecutiveEmptyGetPicture >= kMpegMaxConsecutiveEmptyGetPicture);

            if (!stallByNoFrame && !stallByConsecutive)
            {
                return false;
            }

            finishPlaybackStream(mpegAddr, playback);

            static uint32_t s_noFrameEofLogCount = 0u;
            if (s_noFrameEofLogCount < 16u)
            {
                PS2_IF_AGRESSIVE_LOGS({
                    std::cerr << "[MPEG:no-frame-eof] mpeg=0x" << std::hex << mpegAddr
                              << std::dec << " served=" << playback.picturesServed
                              << " sawInput=" << playback.sawInput
                              << " cdEof=" << cdStreamEofSeen
                              << " reason="
                              << (stallByNoFrame ? "no-frame" : "consecutive")
                              << " consecutiveEmpty=" << playback.consecutiveEmptyGetPicture
                              << std::endl;
                });
                ++s_noFrameEofLogCount;
            }
            return true;
        }

        void feedElementaryStream(MpegPlaybackState &playback, const uint8_t *data, size_t size)
        {
            if (!data || size == 0)
            {
                return;
            }

            const uint32_t feedEsIdx = g_mpeg_stub_state.feedEsTraceCount++;
            if (feedEsIdx < 32u)
            {
                PS2_IF_AGRESSIVE_LOGS({
                    char hexBuf[16] = {};
                    for (size_t i = 0; i < std::min<size_t>(4u, size); ++i)
                    {
                        ::snprintf(hexBuf + i * 2, 3, "%02x", data[i]);
                    }
                    std::cerr << "[MPEG:feedES] #" << feedEsIdx
                              << " size=" << size
                              << " first4=" << hexBuf
                              << " decoderFailed=" << playback.decoderFailed
                              << " waitSeq=" << playback.waitingForVideoSequenceHeader
                              << std::endl;
                });
            }

            playback.sawInput = true;
            if (playback.waitingForVideoSequenceHeader)
            {
                playback.videoSequenceSyncBuffer.insert(
                    playback.videoSequenceSyncBuffer.end(),
                    data,
                    data + size);

                constexpr size_t kMaxVideoSequenceSyncBytes = 2u * 1024u * 1024u;
                if (playback.videoSequenceSyncBuffer.size() > kMaxVideoSequenceSyncBytes)
                {
                    const size_t keepFrom = playback.videoSequenceSyncBuffer.size() - 3u;
                    playback.videoSequenceSyncBuffer.erase(
                        playback.videoSequenceSyncBuffer.begin(),
                        playback.videoSequenceSyncBuffer.begin() + static_cast<std::ptrdiff_t>(keepFrom));
                }

                const size_t sequenceHeader = findMpegSequenceHeader(
                    playback.videoSequenceSyncBuffer.data(),
                    playback.videoSequenceSyncBuffer.size());
                if (sequenceHeader == kStartCodeNotFound)
                {
                    return;
                }

                if (sequenceHeader != 0u)
                {
                    playback.videoSequenceSyncBuffer.erase(
                        playback.videoSequenceSyncBuffer.begin(),
                        playback.videoSequenceSyncBuffer.begin() + static_cast<std::ptrdiff_t>(sequenceHeader));
                }

                data = playback.videoSequenceSyncBuffer.data();
                size = playback.videoSequenceSyncBuffer.size();
                playback.waitingForVideoSequenceHeader = false;
                playback.decoderFailed = false;
                playback.decoder.reset();
                playback.decodedFrames.clear();
            }

            if (containsMpegSequenceEnd(data, size))
            {
                playback.streamEnded = true;
                playback.cdStreamGeneration = g_mpeg_stub_state.cdStreamGeneration;
            }

            if (!playback.decoder)
            {
                playback.decoder = std::make_unique<MpegFfmpegDecoder>();
            }

            if (!playback.decoder->feed(data, size, playback.decodedFrames))
            {
                playback.decoder.reset();
                playback.waitingForVideoSequenceHeader = true;
                playback.videoSequenceSyncBuffer.clear();
                playback.decoderFailed = false;
                return;
            }

            playback.videoSequenceSyncBuffer.clear();
            flushDecoderIfEnded(playback);
            if (!playback.decodedFrames.empty())
            {
                clearNoFrameStall(playback);
            }
        }

        void erasePssPrefix(MpegPlaybackState &playback, size_t count)
        {
            std::vector<uint8_t> &buffer = playback.pssBuffer;
            std::vector<uint32_t> &guestAddrs = playback.pssGuestAddrs;
            const size_t clamped = std::min(count, buffer.size());
            if (clamped == 0u)
            {
                return;
            }

            buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(clamped));
            if (guestAddrs.size() >= clamped)
            {
                guestAddrs.erase(guestAddrs.begin(), guestAddrs.begin() + static_cast<std::ptrdiff_t>(clamped));
            }
            else
            {
                guestAddrs.clear();
            }
        }

        std::vector<MpegRegisteredCallback> matchingStreamCallbacks(uint32_t mpegAddr, uint32_t streamType)
        {
            std::vector<MpegRegisteredCallback> out;
            auto it = g_mpeg_stub_state.callbacksByMpeg.find(mpegAddr);
            if (it == g_mpeg_stub_state.callbacksByMpeg.end())
            {
                return out;
            }

            for (const MpegRegisteredCallback &callback : it->second)
            {
                if (callback.stream && callback.type == streamType)
                {
                    out.push_back(callback);
                }
            }
            return out;
        }

        void queueStreamCallbackEvent(uint32_t mpegAddr,
                                      uint32_t streamType,
                                      uint32_t dataAddr,
                                      uint32_t len,
                                      std::vector<MpegStreamCallbackEvent> &callbackEvents)
        {
            MpegStreamCallbackEvent event{};
            event.mpegAddr = mpegAddr;
            event.streamType = streamType;
            event.dataAddr = dataAddr;
            event.len = len;
            event.callbacks = matchingStreamCallbacks(mpegAddr, streamType);
            if (!event.callbacks.empty())
            {
                callbackEvents.push_back(std::move(event));
            }
        }

        void processPssBuffer(uint32_t mpegAddr,
                              MpegPlaybackState &playback,
                              std::vector<MpegStreamCallbackEvent> &callbackEvents,
                              bool finalChunk = false)
        {
            std::vector<uint8_t> &buffer = playback.pssBuffer;

            while (true)
            {
                if (playback.streamEnded)
                {
                    erasePssPrefix(playback, buffer.size());
                    return;
                }

                const size_t start = findStartCode(buffer, 0u);
                if (start == kStartCodeNotFound)
                {
                    if (finalChunk)
                    {
                        erasePssPrefix(playback, buffer.size());
                        return;
                    }
                    if (buffer.size() > 3u)
                    {
                        erasePssPrefix(playback, buffer.size() - 3u);
                    }
                    return;
                }

                if (start > 0u)
                {
                    erasePssPrefix(playback, start);
                }

                if (buffer.size() < 4u)
                {
                    return;
                }

                const uint8_t streamId = buffer[3u];

                if (streamId == kMpegProgramEnd)
                {
                    playback.streamEnded = true;
                    playback.cdStreamGeneration = g_mpeg_stub_state.cdStreamGeneration;
                    flushDecoderIfEnded(playback);
                    erasePssPrefix(playback, buffer.size());
                    return;
                }

                if (streamId == kMpegPackHeader)
                {
                    if (buffer.size() < 12u)
                    {
                        if (finalChunk)
                        {
                            erasePssPrefix(playback, buffer.size());
                        }
                        return;
                    }

                    size_t packSize = 12u;
                    if ((buffer[4u] & 0xC0u) == 0x40u)
                    {
                        if (buffer.size() < 14u)
                        {
                            if (finalChunk)
                            {
                                erasePssPrefix(playback, buffer.size());
                            }
                            return;
                        }
                        packSize = 14u + static_cast<size_t>(buffer[13u] & 0x07u);
                    }
                    if (buffer.size() < packSize)
                    {
                        if (finalChunk)
                        {
                            erasePssPrefix(playback, buffer.size());
                        }
                        return;
                    }
                    erasePssPrefix(playback, packSize);
                    continue;
                }

                if (buffer.size() < 6u)
                {
                    if (finalChunk)
                    {
                        erasePssPrefix(playback, buffer.size());
                    }
                    return;
                }

                const uint16_t packetLength = readBe16(buffer.data() + 4u);
                if (isLengthPrefixedHeader(streamId))
                {
                    const size_t packetEnd = 6u + static_cast<size_t>(packetLength);
                    if (buffer.size() < packetEnd)
                    {
                        if (finalChunk)
                        {
                            erasePssPrefix(playback, buffer.size());
                        }
                        return;
                    }
                    erasePssPrefix(playback, packetEnd);
                    continue;
                }

                size_t packetEnd = 0u;
                if (packetLength != 0u)
                {
                    packetEnd = 6u + static_cast<size_t>(packetLength);
                    if (buffer.size() < packetEnd)
                    {
                        if (!finalChunk)
                        {
                            return;
                        }
                        packetEnd = buffer.size();
                    }
                }
                else
                {
                    const size_t next = findStartCode(buffer, 6u);
                    if (next == kStartCodeNotFound)
                    {
                        if (!finalChunk)
                        {
                            return;
                        }
                        packetEnd = buffer.size();
                    }
                    else
                    {
                        packetEnd = next;
                    }
                }

                if (isVideoStreamId(streamId))
                {
                    const size_t payloadStart = parsePesPayloadOffset(buffer.data(), packetEnd);
                    if (payloadStart < packetEnd)
                    {
                        if (payloadStart < playback.pssGuestAddrs.size())
                        {
                            queueStreamCallbackEvent(
                                mpegAddr,
                                kMpegStrM2V,
                                playback.pssGuestAddrs[payloadStart],
                                static_cast<uint32_t>(packetEnd - payloadStart),
                                callbackEvents);
                        }
                        feedElementaryStream(
                            playback,
                            buffer.data() + payloadStart,
                            packetEnd - payloadStart);
                    }
                }
                else if (isAudioStreamId(streamId))
                {
                    const size_t payloadStart = parsePesPayloadOffset(buffer.data(), packetEnd);
                    if (payloadStart < packetEnd && payloadStart < playback.pssGuestAddrs.size())
                    {
                        queueStreamCallbackEvent(
                            mpegAddr,
                            kMpegStrPCM,
                            playback.pssGuestAddrs[payloadStart],
                            static_cast<uint32_t>(packetEnd - payloadStart),
                            callbackEvents);
                        queueStreamCallbackEvent(
                            mpegAddr,
                            kMpegStrADPCM,
                            playback.pssGuestAddrs[payloadStart],
                            static_cast<uint32_t>(packetEnd - payloadStart),
                            callbackEvents);
                    }
                }

                erasePssPrefix(playback, packetEnd);
            }
        }

        void finishPlaybackStream(uint32_t mpegAddr, MpegPlaybackState &playback)
        {
            std::vector<MpegStreamCallbackEvent> ignoredCallbacks;
            processPssBuffer(mpegAddr, playback, ignoredCallbacks, true);
            playback.streamEnded = true;
            playback.cdStreamGeneration = g_mpeg_stub_state.cdStreamGeneration;
            flushDecoderIfEnded(playback);
        }

        void appendPssBytes(uint32_t mpegAddr,
                            MpegPlaybackState &playback,
                            const uint8_t *data,
                            size_t size,
                            uint32_t guestAddr,
                            std::vector<MpegStreamCallbackEvent> &callbackEvents)
        {
            if (!data || size == 0)
            {
                return;
            }

            if (playback.sawInput && playback.cdStreamGeneration != g_mpeg_stub_state.cdStreamGeneration)
            {
                playback = makeFreshPlaybackState();
            }

            if (playback.streamEnded)
            {
                if (playback.cdStreamGeneration == g_mpeg_stub_state.cdStreamGeneration)
                {
                    playback.sawInput = true;
                    return;
                }

                playback = makeFreshPlaybackState();
            }

            if (!playback.sawInput)
            {
                playback.cdStreamGeneration = g_mpeg_stub_state.cdStreamGeneration;
            }
            playback.sawInput = true;

            playback.pssBuffer.insert(playback.pssBuffer.end(), data, data + size);
            playback.pssGuestAddrs.reserve(playback.pssGuestAddrs.size() + size);
            for (size_t i = 0; i < size; ++i)
            {
                playback.pssGuestAddrs.push_back(guestAddr + static_cast<uint32_t>(i));
            }
            processPssBuffer(mpegAddr, playback, callbackEvents);
            if (!playback.decodedFrames.empty())
            {
                clearNoFrameStall(playback);
            }
        }

        size_t appendGuestBytes(uint32_t mpegAddr,
                                MpegPlaybackState &playback,
                                const uint8_t *rdram,
                                uint32_t addr,
                                size_t size,
                                std::vector<MpegStreamCallbackEvent> &callbackEvents)
        {
            size_t copied = 0u;
            while (copied < size)
            {
                const uint32_t curAddr = addr + static_cast<uint32_t>(copied);
                const uint32_t offset = curAddr & PS2_RAM_MASK;
                size_t chunk = std::min<size_t>(size - copied, PS2_RAM_SIZE - offset);
                if (chunk == 0u)
                {
                    break;
                }

                const uint8_t *src = getConstMemPtr(rdram, curAddr);
                if (!src)
                {
                    break;
                }

                appendPssBytes(
                    mpegAddr,
                    playback,
                    src,
                    chunk,
                    curAddr,
                    callbackEvents);
                copied += chunk;
            }
            return copied;
        }

        size_t appendGuestRingBytes(uint32_t mpegAddr,
                                    MpegPlaybackState &playback,
                                    const uint8_t *rdram,
                                    uint32_t dataAddr,
                                    uint32_t byteCount,
                                    uint32_t ringBaseAddr,
                                    uint32_t ringSize,
                                    std::vector<MpegStreamCallbackEvent> &callbackEvents)
        {
            if (byteCount == 0u)
            {
                return 0u;
            }

            const uint32_t base = ringBaseAddr & PS2_RAM_MASK;
            const uint32_t data = dataAddr & PS2_RAM_MASK;
            if (ringBaseAddr != 0u && ringSize != 0u && ringSize <= PS2_RAM_SIZE)
            {
                const uint32_t ringOffset = (data - base) & PS2_RAM_MASK;
                if (ringOffset < ringSize)
                {
                    const uint32_t first = std::min<uint32_t>(byteCount, ringSize - ringOffset);
                    size_t copied = appendGuestBytes(
                        mpegAddr,
                        playback,
                        rdram,
                        dataAddr,
                        first,
                        callbackEvents);
                    if (copied < first)
                    {
                        return copied;
                    }

                    const uint32_t remaining = byteCount - first;
                    if (remaining != 0u)
                    {
                        copied += appendGuestBytes(
                            mpegAddr,
                            playback,
                            rdram,
                            ringBaseAddr,
                            remaining,
                            callbackEvents);
                    }
                    return copied;
                }
            }

            return appendGuestBytes(mpegAddr, playback, rdram, dataAddr, byteCount, callbackEvents);
        }

        bool writeMpegCallbackData(uint8_t *rdram, uint32_t addr, const MpegStreamCallbackEvent &event)
        {
            if (!rdram || addr == 0u)
            {
                return false;
            }

            uint8_t *data = getMemPtr(rdram, addr);
            if (!data)
            {
                return false;
            }

            std::memset(data, 0, kMpegCallbackDataSize);
            *reinterpret_cast<uint32_t *>(data + 0x00u) = event.streamType;
            *reinterpret_cast<uint32_t *>(data + 0x08u) = event.dataAddr;
            *reinterpret_cast<uint32_t *>(data + 0x0Cu) = event.len;
            *reinterpret_cast<uint64_t *>(data + 0x10u) = event.pts;
            *reinterpret_cast<uint64_t *>(data + 0x18u) = event.dts;
            return true;
        }

        void dispatchGuestStreamCallback(uint8_t *rdram,
                                         R5900Context *callerCtx,
                                         PS2Runtime *runtime,
                                         const MpegStreamCallbackEvent &event,
                                         const MpegRegisteredCallback &callback)
        {
            if (!rdram || !callerCtx || !runtime || callback.func == 0u || !runtime->hasFunction(callback.func))
            {
                return;
            }

            thread_local PS2Runtime *s_callbackStackRuntime = nullptr;
            thread_local uint32_t s_callbackStackTop = 0u;
            if (s_callbackStackRuntime != runtime || s_callbackStackTop == 0u)
            {
                constexpr uint32_t kCallbackStackSize = 0x4000u;
                s_callbackStackRuntime = runtime;
                s_callbackStackTop = runtime->reserveAsyncCallbackStack(kCallbackStackSize, 16u);
            }

            const uint32_t cbDataAddr = runtime->guestMalloc(kMpegCallbackDataSize, 16u);
            if (cbDataAddr == 0u)
            {
                return;
            }
            if (!writeMpegCallbackData(rdram, cbDataAddr, event))
            {
                runtime->guestFree(cbDataAddr);
                return;
            }

            R5900Context callbackCtx = *callerCtx;
            SET_GPR_U32(&callbackCtx, 4, event.mpegAddr);
            SET_GPR_U32(&callbackCtx, 5, cbDataAddr);
            SET_GPR_U32(&callbackCtx, 6, callback.data);
            SET_GPR_U32(&callbackCtx, 7, 0u);
            SET_GPR_U32(&callbackCtx, 29, (s_callbackStackTop != 0u) ? s_callbackStackTop : (PS2_RAM_SIZE - 0x10u));
            SET_GPR_U32(&callbackCtx, 31, 0u);
            callbackCtx.pc = callback.func;

            uint32_t steps = 0u;
            while (callbackCtx.pc != 0u && !runtime->isStopRequested() && steps < kMpegCallbackMaxSteps)
            {
                if (!runtime->hasFunction(callbackCtx.pc))
                {
                    static uint32_t badPcLogCount = 0u;
                    if (badPcLogCount < 16u)
                    {
                        std::cerr << "[MPEG:callback:bad-pc] cb=0x" << std::hex << callback.func
                                  << " pc=0x" << callbackCtx.pc
                                  << " ra=0x" << getRegU32(&callbackCtx, 31)
                                  << std::dec << std::endl;
                        ++badPcLogCount;
                    }
                    break;
                }

                PS2Runtime::RecompiledFunction step = runtime->lookupFunction(callbackCtx.pc);
                if (!step)
                {
                    break;
                }

                {
                    PS2Runtime::GuestExecutionScope guestExecution(runtime);
                    step(rdram, &callbackCtx, runtime);
                }
                ++steps;
            }

            if (steps >= kMpegCallbackMaxSteps)
            {
                static uint32_t stepLimitLogCount = 0u;
                if (stepLimitLogCount < 16u)
                {
                    std::cerr << "[MPEG:callback:step-limit] cb=0x" << std::hex << callback.func
                              << " pc=0x" << callbackCtx.pc << std::dec << std::endl;
                    ++stepLimitLogCount;
                }
            }

            runtime->guestFree(cbDataAddr);
        }

        void dispatchStreamCallbacks(uint8_t *rdram,
                                     R5900Context *ctx,
                                     PS2Runtime *runtime,
                                     const std::vector<MpegStreamCallbackEvent> &events)
        {
            if (events.empty())
            {
                return;
            }

            for (const MpegStreamCallbackEvent &event : events)
            {
                for (const MpegRegisteredCallback &callback : event.callbacks)
                {
                    dispatchGuestStreamCallback(rdram, ctx, runtime, event, callback);
                }
            }
        }

        void dispatchStreamCallbacksUnlocked(uint8_t *rdram,
                                             R5900Context *ctx,
                                             PS2Runtime *runtime,
                                             const std::vector<MpegStreamCallbackEvent> &events)
        {
            if (events.empty())
            {
                return;
            }

            PS2Runtime::GuestExecutionReleaseScope releaseGuestExecution(runtime);
            dispatchStreamCallbacks(rdram, ctx, runtime, events);
        }

        void writeBlankMpegFrame(uint8_t *rdram, uint32_t destAddr, uint32_t width, uint32_t height)
        {
            if (!rdram || destAddr == 0u)
            {
                return;
            }

            const uint32_t outWidth = align16(width == 0u ? kStubMovieWidth : width);
            const uint32_t outHeight = align16(height == 0u ? kStubMovieHeight : height);
            const uint32_t macroblockColumns = outWidth / 16u;
            for (uint32_t mbx = 0u; mbx < macroblockColumns; ++mbx)
            {
                const size_t stripOffset =
                    static_cast<size_t>(mbx) * static_cast<size_t>(outHeight) * 16u * 4u;
                for (uint32_t y = 0u; y < outHeight; ++y)
                {
                    uint8_t *dst = getMemPtr(
                        rdram,
                        destAddr + static_cast<uint32_t>(stripOffset + static_cast<size_t>(y) * 16u * 4u));
                    if (!dst)
                    {
                        continue;
                    }
                    for (uint32_t x = 0u; x < 16u; ++x)
                    {
                        dst[x * 4u + 0u] = 0u;
                        dst[x * 4u + 1u] = 0u;
                        dst[x * 4u + 2u] = 0u;
                        dst[x * 4u + 3u] = 0x80u;
                    }
                }
            }
        }

        void writeDecodedFrameToGuest(uint8_t *rdram, uint32_t destAddr, const MpegDecodedFrame &frame)
        {
            if (!rdram || destAddr == 0u || frame.rgba.empty() || frame.width <= 0 || frame.height <= 0)
            {
                return;
            }

            const uint32_t width = static_cast<uint32_t>(frame.width);
            const uint32_t height = static_cast<uint32_t>(frame.height);
            const uint32_t outWidth = align16(width);
            const uint32_t outHeight = align16(height);
            const uint32_t macroblockColumns = outWidth / 16u;

            for (uint32_t mbx = 0u; mbx < macroblockColumns; ++mbx)
            {
                const size_t stripOffset =
                    static_cast<size_t>(mbx) * static_cast<size_t>(outHeight) * 16u * 4u;
                for (uint32_t y = 0u; y < outHeight; ++y)
                {
                    uint8_t *dst = getMemPtr(
                        rdram,
                        destAddr + static_cast<uint32_t>(stripOffset + static_cast<size_t>(y) * 16u * 4u));
                    if (!dst)
                    {
                        continue;
                    }

                    for (uint32_t x = 0u; x < 16u; ++x)
                    {
                        const uint32_t srcX = mbx * 16u + x;
                        const uint8_t *src = nullptr;
                        if (srcX < width && y < height)
                        {
                            src = frame.rgba.data() +
                                  (static_cast<size_t>(y) * static_cast<size_t>(width) + srcX) * 4u;
                        }

                        if (src)
                        {
                            dst[x * 4u + 0u] = src[0u];
                            dst[x * 4u + 1u] = src[1u];
                            dst[x * 4u + 2u] = src[2u];
                            dst[x * 4u + 3u] = 0x80u;
                        }
                        else
                        {
                            dst[x * 4u + 0u] = 0u;
                            dst[x * 4u + 1u] = 0u;
                            dst[x * 4u + 2u] = 0u;
                            dst[x * 4u + 3u] = 0x80u;
                        }
                    }
                }
            }
        }

        void resetMpegStubStateUnlocked()
        {
            g_mpeg_stub_state.initialized = false;
            g_mpeg_stub_state.nextCallbackHandle = 1u;
            g_mpeg_stub_state.cdStreamGeneration = 0u;
            g_mpeg_stub_state.currentCdStreamEofSeen = false;
            g_mpeg_stub_state.feedEsTraceCount = 0u;
            g_mpeg_stub_state.demuxPssTraceCount = 0u;
            g_mpeg_stub_state.demuxRingTraceCount = 0u;
            g_mpeg_stub_state.getPictureWaitTraceCount = 0u;
            g_mpeg_stub_state.pictureTraceCount = 0u;
            g_mpeg_stub_state.isEndTraceCount = 0u;
            g_mpeg_stub_state.callbacksByMpeg.clear();
            g_mpeg_stub_state.playbackByMpeg.clear();
        }
    }

    void resetMpegStubState()
    {
        std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
        resetMpegStubStateUnlocked();
        g_mpeg_cv.notify_all();
    }

    void notifyMpegCdStreamStart()
    {
        std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
        ++g_mpeg_stub_state.cdStreamGeneration;
        g_mpeg_stub_state.currentCdStreamEofSeen = false;
        g_mpeg_stub_state.feedEsTraceCount = 0u;
        g_mpeg_stub_state.demuxPssTraceCount = 0u;
        g_mpeg_stub_state.demuxRingTraceCount = 0u;
        g_mpeg_stub_state.getPictureWaitTraceCount = 0u;
        g_mpeg_stub_state.pictureTraceCount = 0u;
        g_mpeg_stub_state.isEndTraceCount = 0u;

        for (auto &[mpegAddr, playback] : g_mpeg_stub_state.playbackByMpeg)
        {
            playback = makeFreshPlaybackStatePreservingConfig(playback);
        }
        PS2_IF_AGRESSIVE_LOGS({
            std::cerr << "[MPEG:CdStreamStart] generation=" << g_mpeg_stub_state.cdStreamGeneration
                      << " reopened=" << g_mpeg_stub_state.playbackByMpeg.size() << std::endl;
        });
        g_mpeg_cv.notify_all();
    }

    void notifyMpegCdStreamEof()
    {
        std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
        g_mpeg_stub_state.currentCdStreamEofSeen = true;
        bool changed = false;
        for (auto &[mpegAddr, playback] : g_mpeg_stub_state.playbackByMpeg)
        {
            if (!playback.sawInput || playback.streamEnded)
            {
                continue;
            }

            finishPlaybackStream(mpegAddr, playback);
            changed = true;
        }

        if (changed)
        {
            static uint32_t s_eofLogCount = 0u;
            if (s_eofLogCount < 8u)
            {
                PS2_IF_AGRESSIVE_LOGS({
                    std::cerr << "[MPEG:CdStreamEof] finalized active MPEG playback" << std::endl;
                });
                ++s_eofLogCount;
            }
            g_mpeg_cv.notify_all();
        }
    }

    void sceMpegFlush(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;

        const uint32_t mpegAddr = getRegU32(ctx, 4);
        std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
        MpegPlaybackState &playback = getPlaybackState(mpegAddr);
        if (playback.decoder)
        {
            playback.decoder->flush(playback.decodedFrames);
        }
        g_mpeg_cv.notify_all();
        setReturnS32(ctx, 0);
    }

    void sceMpegAddBs(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)runtime;

        const uint32_t mpegAddr = getRegU32(ctx, 4);
        const uint32_t dataAddr = getRegU32(ctx, 5);
        const uint32_t byteCount = getRegU32(ctx, 6);

        std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
        MpegPlaybackState &playback = getPlaybackState(mpegAddr);
        size_t copied = 0u;
        while (copied < byteCount)
        {
            const uint32_t curAddr = dataAddr + static_cast<uint32_t>(copied);
            const uint32_t offset = curAddr & PS2_RAM_MASK;
            const size_t chunk = std::min<size_t>(static_cast<size_t>(byteCount) - copied, PS2_RAM_SIZE - offset);
            const uint8_t *src = getConstMemPtr(rdram, curAddr);
            if (!src || chunk == 0u)
            {
                break;
            }
            feedElementaryStream(playback, src, chunk);
            copied += chunk;
        }

        g_mpeg_cv.notify_all();
        setReturnS32(ctx, static_cast<int32_t>(copied));
    }

    void sceMpegAddCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;

        const uint32_t mpegAddr = getRegU32(ctx, 4);
        const uint32_t callbackType = getRegU32(ctx, 5);
        const uint32_t callbackFunc = getRegU32(ctx, 6);
        const uint32_t callbackData = getRegU32(ctx, 7);

        std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
        g_mpeg_stub_state.initialized = true;
        (void)getPlaybackState(mpegAddr);

        const uint32_t handle = g_mpeg_stub_state.nextCallbackHandle++;
        g_mpeg_stub_state.callbacksByMpeg[mpegAddr].push_back(
            MpegRegisteredCallback{callbackType, 0u, callbackFunc, callbackData, handle, false});

        setReturnU32(ctx, handle);
    }

    void sceMpegAddStrCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)runtime;
        const uint32_t mpegAddr = getRegU32(ctx, 4);
        const uint32_t streamType = getRegU32(ctx, 5);
        const uint32_t streamId = getRegU32(ctx, 6);
        const uint32_t callbackFunc = getRegU32(ctx, 7);
        const uint32_t callbackData = readAbiArg4(rdram, ctx);

        std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
        g_mpeg_stub_state.initialized = true;
        (void)getPlaybackState(mpegAddr);
        const uint32_t handle = g_mpeg_stub_state.nextCallbackHandle++;
        g_mpeg_stub_state.callbacksByMpeg[mpegAddr].push_back(
            MpegRegisteredCallback{streamType, streamId, callbackFunc, callbackData, handle, true});
        setReturnU32(ctx, 0u);
    }

    void sceMpegClearRefBuff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)ctx;
        (void)runtime;
        static const uint32_t kRefGlobalAddrs[] = {
            0x171800u, 0x17180Cu, 0x171818u, 0x171804u, 0x171810u, 0x17181Cu};
        for (uint32_t addr : kRefGlobalAddrs)
        {
            uint8_t *p = getMemPtr(rdram, addr);
            if (!p)
                continue;
            uint32_t ptr = *reinterpret_cast<uint32_t *>(p);
            if (ptr != 0u)
            {
                uint8_t *q = getMemPtr(rdram, ptr + 0x28u);
                if (q)
                    *reinterpret_cast<uint32_t *>(q) = 0u;
            }
        }
        setReturnU32(ctx, 1u);
    }

    static void mpegGuestWrite32(uint8_t *rdram, uint32_t addr, uint32_t value)
    {
        if (uint8_t *p = getMemPtr(rdram, addr))
            *reinterpret_cast<uint32_t *>(p) = value;
    }
    static void mpegGuestWrite64(uint8_t *rdram, uint32_t addr, uint64_t value)
    {
        if (uint8_t *p = getMemPtr(rdram, addr))
        {
            *reinterpret_cast<uint32_t *>(p) = static_cast<uint32_t>(value);
            *reinterpret_cast<uint32_t *>(p + 4) = static_cast<uint32_t>(value >> 32);
        }
    }

    void sceMpegCreate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t param_1 = getRegU32(ctx, 4); // a0
        const uint32_t param_2 = getRegU32(ctx, 5); // a1
        const uint32_t param_3 = getRegU32(ctx, 6); // a2

        const uint32_t uVar3 = (param_2 + 3u) & 0xFFFFFFFCu;
        const int32_t iVar2_signed = static_cast<int32_t>(param_3) - static_cast<int32_t>(uVar3 - param_2);

        if (iVar2_signed <= 0x117)
        {
            setReturnU32(ctx, 0u);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
            getPlaybackState(param_1) = makeFreshPlaybackState();
        }

        const uint32_t puVar4 = uVar3 + 0x108u;
        const uint32_t innerSize = static_cast<uint32_t>(iVar2_signed) - 0x118u;

        mpegGuestWrite32(rdram, param_1 + 0x40, uVar3);

        const uint32_t a1_init = uVar3 + 0x118u;
        mpegGuestWrite32(rdram, puVar4 + 0x0, a1_init);
        mpegGuestWrite32(rdram, puVar4 + 0x4, innerSize);
        mpegGuestWrite32(rdram, puVar4 + 0x8, a1_init);
        mpegGuestWrite32(rdram, puVar4 + 0xC, a1_init);

        const uint32_t allocResult = runtime ? runtime->guestMalloc(0x600, 8u) : (uVar3 + 0x200u);
        mpegGuestWrite32(rdram, uVar3 + 0x44, allocResult);

        // param_1[0..2] = 0; param_1[4..0xe] = 0xffffffff/0 as per decompilation
        mpegGuestWrite32(rdram, param_1 + 0x00, 0);
        mpegGuestWrite32(rdram, param_1 + 0x04, 0);
        mpegGuestWrite32(rdram, param_1 + 0x08, 0);
        mpegGuestWrite64(rdram, param_1 + 0x10, 0xFFFFFFFFFFFFFFFFULL);
        mpegGuestWrite64(rdram, param_1 + 0x18, 0xFFFFFFFFFFFFFFFFULL);
        mpegGuestWrite64(rdram, param_1 + 0x20, 0);
        mpegGuestWrite64(rdram, param_1 + 0x28, 0xFFFFFFFFFFFFFFFFULL);
        mpegGuestWrite64(rdram, param_1 + 0x30, 0xFFFFFFFFFFFFFFFFULL);
        mpegGuestWrite64(rdram, param_1 + 0x38, 0);

        static const unsigned s_zeroOffsets[] = {
            0xB4, 0xB8, 0xBC, 0xC0, 0xC4, 0xC8, 0xCC, 0xD0, 0xD4, 0xD8, 0xDC, 0xE0, 0xE4, 0xE8, 0xF8,
            0x0C, 0x14, 0x2C, 0x34, 0x3C,
            0x48, 0xFC, 0x100, 0x104, 0x70, 0x90, 0xAC};
        for (unsigned off : s_zeroOffsets)
            mpegGuestWrite32(rdram, uVar3 + off, 0u);
        mpegGuestWrite64(rdram, uVar3 + 0x78, 0);
        mpegGuestWrite64(rdram, uVar3 + 0x88, 0);

        mpegGuestWrite64(rdram, uVar3 + 0xF0, 0xFFFFFFFFFFFFFFFFULL);
        mpegGuestWrite32(rdram, uVar3 + 0x1C, 0x1209F8u);
        mpegGuestWrite32(rdram, uVar3 + 0x24, 0x120A08u);
        mpegGuestWrite32(rdram, uVar3 + 0xB0, 1u);
        mpegGuestWrite32(rdram, uVar3 + 0x9C, 0xFFFFFFFFu);
        mpegGuestWrite32(rdram, uVar3 + 0x80, 0xFFFFFFFFu);
        mpegGuestWrite32(rdram, uVar3 + 0x94, 0xFFFFFFFFu);
        mpegGuestWrite32(rdram, uVar3 + 0x98, 0xFFFFFFFFu);

        mpegGuestWrite32(rdram, 0x1717BCu, param_1);

        static const uint32_t s_refValues[] = {
            0x171A50u, 0x171C58u, 0x171CC0u, 0x171D28u, 0x171D90u,
            0x171AB8u, 0x171B20u, 0x171B88u, 0x171BF0u};
        for (unsigned i = 0; i < 9u; ++i)
            mpegGuestWrite32(rdram, 0x171800u + i * 4u, s_refValues[i]);

        uint32_t setDynamicRet = a1_init;
        if (uint8_t *p = getMemPtr(rdram, puVar4 + 8))
            setDynamicRet = *reinterpret_cast<uint32_t *>(p);
        mpegGuestWrite32(rdram, puVar4 + 12, setDynamicRet);

        setReturnU32(ctx, setDynamicRet);
    }

    void sceMpegDelete(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;

        const uint32_t mpegAddr = getRegU32(ctx, 4);
        std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
        g_mpeg_stub_state.callbacksByMpeg.erase(mpegAddr);
        g_mpeg_stub_state.playbackByMpeg.erase(mpegAddr);
        setReturnU32(ctx, 0u);
    }

    void sceMpegDemuxPss(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t mpegAddr = getRegU32(ctx, 4);
        const uint32_t dataAddr = getRegU32(ctx, 5);
        const uint32_t byteCount = getRegU32(ctx, 6);

        std::vector<MpegStreamCallbackEvent> callbackEvents;
        size_t consumed = 0u;
        size_t decodedCount = 0u;
        uint32_t traceIdx = 0u;
        {
            PS2Runtime::GuestExecutionReleaseScope releaseGuestExecution(runtime);
            std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
            MpegPlaybackState &playback = getPlaybackState(mpegAddr);
            consumed = appendGuestBytes(mpegAddr, playback, rdram, dataAddr, byteCount, callbackEvents);
            decodedCount = playback.decodedFrames.size();
            traceIdx = g_mpeg_stub_state.demuxPssTraceCount++;
        }
        g_mpeg_cv.notify_all();

        if (traceIdx < 32u)
        {
            PS2_IF_AGRESSIVE_LOGS({
                std::cerr << "[MPEG:DemuxPss] mpeg=0x" << std::hex << mpegAddr
                          << " data=0x" << dataAddr << std::dec
                          << " bytes=" << byteCount
                          << " consumed=" << consumed
                          << " decoded=" << decodedCount
                          << " callbacks=" << callbackEvents.size()
                          << std::endl;
            });
        }

        dispatchStreamCallbacksUnlocked(rdram, ctx, runtime, callbackEvents);
        setReturnS32(ctx, static_cast<int32_t>(consumed));
    }

    void sceMpegDemuxPssRing(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        static std::atomic<uint32_t> s_demuxRingEntryCount{0u};
        const uint32_t entryIdx = s_demuxRingEntryCount.fetch_add(1u, std::memory_order_relaxed);
        if (entryIdx < 4u)
        {
            PS2_IF_AGRESSIVE_LOGS({
                std::cerr << "[MPEG:DemuxPssRing:ENTER] call #" << entryIdx
                          << " pc=0x" << std::hex << ctx->pc
                          << " ra=0x" << getRegU32(ctx, 31)
                          << std::dec << std::endl;
            });
        }

        const uint32_t mpegAddr = getRegU32(ctx, 4);
        const uint32_t dataAddr = getRegU32(ctx, 5);
        const uint32_t availableBytes = getRegU32(ctx, 6);
        const uint32_t ringBaseAddr = getRegU32(ctx, 7);
        const uint32_t ringSize = readAbiArg4(rdram, ctx);

        std::vector<MpegStreamCallbackEvent> callbackEvents;
        size_t consumed = 0u;
        size_t decodedCount = 0u;
        uint32_t traceIdx = 0u;
        {
            // This prevents an ABBA deadlock with sceMpegGetPicture on thread 5, need investigation on other games
            PS2Runtime::GuestExecutionReleaseScope releaseGuestExecution(runtime);
            std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
            MpegPlaybackState &playback = getPlaybackState(mpegAddr);
            consumed = appendGuestRingBytes(
                mpegAddr,
                playback,
                rdram,
                dataAddr,
                availableBytes,
                ringBaseAddr,
                ringSize,
                callbackEvents);
            decodedCount = playback.decodedFrames.size();
            traceIdx = g_mpeg_stub_state.demuxRingTraceCount++;
        }
        g_mpeg_cv.notify_all();

        if (traceIdx < 32u)
        {
            PS2_IF_AGRESSIVE_LOGS({
                std::cerr << "[MPEG:DemuxPssRing] mpeg=0x" << std::hex << mpegAddr
                          << " data=0x" << dataAddr
                          << " ring=0x" << ringBaseAddr << std::dec
                          << " avail=" << availableBytes
                          << " consumed=" << consumed
                          << " decoded=" << decodedCount
                          << " callbacks=" << callbackEvents.size()
                          << std::endl;
            });
        }

        dispatchStreamCallbacksUnlocked(rdram, ctx, runtime, callbackEvents);
        setReturnS32(ctx, static_cast<int32_t>(consumed));
    }

    void sceMpegDispCenterOffX(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        setReturnS32(ctx, 0);
    }

    void sceMpegDispCenterOffY(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        setReturnS32(ctx, 0);
    }

    void sceMpegDispHeight(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        const uint32_t mpegAddr = getRegU32(ctx, 4);
        std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
        setReturnU32(ctx, getPlaybackState(mpegAddr).height);
    }

    void sceMpegDispWidth(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        const uint32_t mpegAddr = getRegU32(ctx, 4);
        std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
        setReturnU32(ctx, getPlaybackState(mpegAddr).width);
    }

    void sceMpegGetDecodeMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        const uint32_t mpegAddr = getRegU32(ctx, 4);
        std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
        setReturnU32(ctx, getPlaybackState(mpegAddr).decodeMode);
    }

    void sceMpegGetPicture(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t mpegAddr = getRegU32(ctx, 4);
        const uint32_t imageAddr = getRegU32(ctx, 5);
        uint32_t width = kStubMovieWidth;
        uint32_t height = kStubMovieHeight;
        uint32_t frameCount = 0u;
        bool haveFrame = false;
        MpegDecodedFrame frame;
        {
            PS2Runtime::GuestExecutionReleaseScope releaseGuestExecution(runtime);
            std::unique_lock<std::mutex> lock(g_mpeg_stub_mutex);
            MpegPlaybackState &playback = getPlaybackState(mpegAddr);
            const uint64_t waitCdStreamGeneration = g_mpeg_stub_state.cdStreamGeneration;

            if (playback.decodedFrames.empty())
            {
                playback.consecutiveEmptyGetPicture++;
                if (g_mpeg_stub_state.getPictureWaitTraceCount < 32u)
                {
                    PS2_IF_AGRESSIVE_LOGS({
                        std::cerr << "[MPEG:GetPicture] waiting for frames, mpeg=0x" << std::hex << mpegAddr
                                  << std::dec << " ended=" << playback.streamEnded
                                  << " failed=" << playback.decoderFailed
                                  << " sawInput=" << playback.sawInput
                                  << " consec=" << playback.consecutiveEmptyGetPicture << std::endl;
                    });
                    ++g_mpeg_stub_state.getPictureWaitTraceCount;
                }
            }

            std::shared_ptr<ThreadInfo> currentThreadInfo = nullptr;
            {
                std::lock_guard<std::mutex> mapLock(g_thread_map_mutex);
                auto it = g_threads.find(g_currentThreadId);
                if (it != g_threads.end())
                    currentThreadInfo = it->second;
            }

            const auto noFrameWaitStart = std::chrono::steady_clock::now();
            while (runtime &&
                   g_mpeg_stub_state.playbackByMpeg.find(mpegAddr) != g_mpeg_stub_state.playbackByMpeg.end() &&
                   getPlaybackState(mpegAddr).decodedFrames.empty() &&
                   !getPlaybackState(mpegAddr).streamEnded &&
                   !getPlaybackState(mpegAddr).decoderFailed &&
                   g_mpeg_stub_state.cdStreamGeneration == waitCdStreamGeneration &&
                   !runtime->isStopRequested() &&
                   (!currentThreadInfo || !currentThreadInfo->terminated.load(std::memory_order_relaxed)))
            {
                g_mpeg_cv.wait_for(lock, std::chrono::milliseconds(8));

                auto playbackIt = g_mpeg_stub_state.playbackByMpeg.find(mpegAddr);
                if (playbackIt == g_mpeg_stub_state.playbackByMpeg.end())
                {
                    break;
                }

                MpegPlaybackState &waitPlayback = playbackIt->second;
                if (maybeFinishNoFrameStall(mpegAddr, waitPlayback))
                {
                    break;
                }

                if (!g_mpeg_stub_state.currentCdStreamEofSeen &&
                    std::chrono::steady_clock::now() - noFrameWaitStart >= kMpegGetPictureNoFrameWaitTimeout)
                {
                    static uint32_t s_noFrameYieldLogCount = 0u;
                    if (s_noFrameYieldLogCount < 16u)
                    {
                        PS2_IF_AGRESSIVE_LOGS({
                            std::cerr << "[MPEG:GetPicture:yield] mpeg=0x" << std::hex << mpegAddr
                                      << std::dec << " generation=" << g_mpeg_stub_state.cdStreamGeneration
                                      << " sawInput=" << waitPlayback.sawInput
                                      << " served=" << waitPlayback.picturesServed
                                      << " cdEof=" << g_mpeg_stub_state.currentCdStreamEofSeen
                                      << std::endl;
                        });
                        ++s_noFrameYieldLogCount;
                    }
                    break;
                }
            }

            if (g_mpeg_stub_state.playbackByMpeg.find(mpegAddr) == g_mpeg_stub_state.playbackByMpeg.end())
            {
                // The MPEG decoder was deleted while we were waiting.
                setReturnS32(ctx, -1);
                return;
            }

            if (!playback.decodedFrames.empty())
            {
                frame = std::move(playback.decodedFrames.front());
                playback.decodedFrames.pop_front();
                playback.width = static_cast<uint32_t>(frame.width);
                playback.height = static_cast<uint32_t>(frame.height);
                width = playback.width;
                height = playback.height;
                frameCount = playback.picturesServed;
                playback.picturesServed += 1u;
                playback.consecutiveEmptyGetPicture = 0u;
                playback.lastFrame = frame;
                playback.hasLastFrame = true;
                haveFrame = true;
                if (g_mpeg_stub_state.pictureTraceCount < 32u)
                {
                    PS2_IF_AGRESSIVE_LOGS({
                        std::cerr << "[MPEG:GetPicture:FRAME] mpeg=0x" << std::hex << mpegAddr
                                  << std::dec << " generation=" << g_mpeg_stub_state.cdStreamGeneration
                                  << " frame=" << frameCount
                                  << " queued=" << playback.decodedFrames.size()
                                  << " size=" << width << "x" << height << std::endl;
                    });
                    ++g_mpeg_stub_state.pictureTraceCount;
                }
                if (!playback.decodedFrames.empty())
                {
                    clearNoFrameStall(playback);
                }
            }
            else if (!g_mpeg_stub_state.currentCdStreamEofSeen &&
                     playback.sawInput &&
                     playback.hasLastFrame &&
                     playback.picturesServed > 0u &&
                     !playback.streamEnded &&
                     !playback.decoderFailed)
            {
                frame = playback.lastFrame;
                width = static_cast<uint32_t>(frame.width);
                height = static_cast<uint32_t>(frame.height);
                frameCount = playback.picturesServed;
                playback.picturesServed += 1u;
                playback.consecutiveEmptyGetPicture = 0u;
                haveFrame = true;

                static uint32_t s_duplicateFrameLogCount = 0u;
                if (s_duplicateFrameLogCount < 16u)
                {
                    PS2_IF_AGRESSIVE_LOGS({
                        std::cerr << "[MPEG:GetPicture:DUP] mpeg=0x" << std::hex << mpegAddr
                                  << std::dec << " generation=" << g_mpeg_stub_state.cdStreamGeneration
                                  << " frame=" << frameCount
                                  << " size=" << width << "x" << height << std::endl;
                    });
                    ++s_duplicateFrameLogCount;
                }
            }
            else
            {
                maybeFinishNoFrameStall(mpegAddr, playback);
                width = playback.width;
                height = playback.height;
                frameCount = playback.picturesServed;
            }
        }

        mpegGuestWrite32(rdram, mpegAddr + 0x00u, width);
        mpegGuestWrite32(rdram, mpegAddr + 0x04u, height);
        mpegGuestWrite32(rdram, mpegAddr + 0x08u, frameCount);

        if (uint8_t *base = getMemPtr(rdram, mpegAddr))
        {
            const uint32_t iVar1 = *reinterpret_cast<uint32_t *>(base + 0x40);
            if (uint8_t *inner = getMemPtr(rdram, iVar1))
            {
                *reinterpret_cast<uint32_t *>(inner + 0xb0) = 1;
                *reinterpret_cast<uint32_t *>(inner + 0xd8) = (getRegU32(ctx, 5) & 0x0FFFFFFFu) | 0x20000000u;
                *reinterpret_cast<uint32_t *>(inner + 0xe4) = getRegU32(ctx, 6);
                *reinterpret_cast<uint32_t *>(inner + 0xdc) = 0;
                *reinterpret_cast<uint32_t *>(inner + 0xe0) = 0;
            }
        }

        if (haveFrame)
        {
            writeDecodedFrameToGuest(rdram, imageAddr, frame);
        }
        else if (frameCount == 0u)
        {
            writeBlankMpegFrame(rdram, imageAddr, width, height);
        }

        setReturnS32(ctx, 0);
    }

    void sceMpegGetPictureRAW8(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegGetPictureRAW8", rdram, ctx, runtime);
    }

    void sceMpegGetPictureRAW8xy(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        TODO_NAMED("sceMpegGetPictureRAW8xy", rdram, ctx, runtime);
    }

    void sceMpegInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;

        std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
        const uint64_t cdStreamGeneration = g_mpeg_stub_state.cdStreamGeneration;
        const bool currentCdStreamEofSeen = g_mpeg_stub_state.currentCdStreamEofSeen;
        resetMpegStubStateUnlocked();
        g_mpeg_stub_state.initialized = true;
        g_mpeg_stub_state.cdStreamGeneration = cdStreamGeneration;
        g_mpeg_stub_state.currentCdStreamEofSeen = currentCdStreamEofSeen;
        g_mpeg_cv.notify_all();
        setReturnU32(ctx, 0u);
    }

    void sceMpegIsEnd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        // runtime used below for GuestExecutionReleaseScope
        const uint32_t mpegAddr = getRegU32(ctx, 4);

        PS2Runtime::GuestExecutionReleaseScope releaseGuestExecution(runtime);
        std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
        g_mpeg_stub_state.initialized = true;
        MpegPlaybackState &playback = getPlaybackState(mpegAddr);
        maybeFinishNoFrameStall(mpegAddr, playback);
        const bool ended = playback.streamEnded || (playback.decoderFailed && playback.sawInput);

        if (g_mpeg_stub_state.isEndTraceCount < 16u)
        {
            PS2_IF_AGRESSIVE_LOGS({
                std::cerr << "[MPEG:IsEnd] mpeg=0x" << std::hex << mpegAddr << std::dec
                          << " ended=" << ended
                          << " frames=" << playback.decodedFrames.size()
                          << " sawInput=" << playback.sawInput << std::endl;
            });
            ++g_mpeg_stub_state.isEndTraceCount;
        }

        setReturnS32(ctx, (ended && playback.decodedFrames.empty()) ? 1 : 0);
    }

    void sceMpegIsRefBuffEmpty(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        const uint32_t mpegAddr = getRegU32(ctx, 4);
        std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
        const MpegPlaybackState &playback = getPlaybackState(mpegAddr);
        setReturnS32(ctx, playback.decodedFrames.empty() ? 1 : 0);
    }

    void sceMpegReset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)runtime;
        const uint32_t param_1 = getRegU32(ctx, 4);
        {
            std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
            MpegPlaybackState &playback = getPlaybackState(param_1);
            MpegPlaybackState resetState = makeFreshPlaybackStatePreservingConfig(playback);
            if (playback.streamEnded || playback.decoderFailed)
            {
                resetState.sawInput = true;
                resetState.streamEnded = true;
                resetState.cdStreamGeneration = playback.cdStreamGeneration;
            }
            playback = std::move(resetState);
        }
        uint8_t *base = getMemPtr(rdram, param_1);
        if (!base)
        {
            return;
        }
        uint32_t inner = *reinterpret_cast<uint32_t *>(base + 0x40);
        if (inner == 0u)
            return;
        mpegGuestWrite32(rdram, param_1 + 0x00u, 0u);
        mpegGuestWrite32(rdram, param_1 + 0x04u, 0u);
        mpegGuestWrite32(rdram, param_1 + 0x08u, 0u);
        mpegGuestWrite32(rdram, inner + 0x00, 0u);
        mpegGuestWrite32(rdram, inner + 0x04, 0u);
        mpegGuestWrite32(rdram, inner + 0x08, 0u);
        mpegGuestWrite32(rdram, param_1 + 0x08, 0u);
        mpegGuestWrite32(rdram, inner + 0x80, 0xFFFFFFFFu);
        mpegGuestWrite32(rdram, inner + 0xAC, 0u);
        mpegGuestWrite32(rdram, 0x171904u, 0u);
    }

    void sceMpegResetDefaultPtsGap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        setReturnS32(ctx, 0);
    }

    void sceMpegSetDecodeMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        const uint32_t mpegAddr = getRegU32(ctx, 4);
        const uint32_t mode = getRegU32(ctx, 5);
        std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
        getPlaybackState(mpegAddr).decodeMode = mode;
        setReturnS32(ctx, 0);
    }

    void sceMpegSetDefaultPtsGap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        setReturnS32(ctx, 0);
    }

    void sceMpegSetImageBuff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        const uint32_t mpegAddr = getRegU32(ctx, 4);
        const uint32_t imageBufferAddr = getRegU32(ctx, 5);
        std::lock_guard<std::mutex> lock(g_mpeg_stub_mutex);
        getPlaybackState(mpegAddr).imageBufferAddr = imageBufferAddr;
        setReturnS32(ctx, 0);
    }
}
