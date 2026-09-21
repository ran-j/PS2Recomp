{
    static constexpr auto loop = ps2_vu_detail::planCountedLoop(decodedRegion);
    static constexpr auto plan = loop.body;
    static constexpr bool hasDiv = plan.pendingQ != 0u;
    if constexpr (loop.eligible)
    {
        const auto tryLoop = [&]() PS2_VU_INLINE_LAMBDA {
            uint64_t start = vu.m_cycle;
            const uint32_t startPC = vu.m_state.pc;
            uint64_t sequenceBase = vu.m_nextWriteSequence;
            uint64_t iterations = 0u;
            bool branchTaken = false, everBranched = false;
            if (ps2_vu_detail::disableCountedLoops || !ps2_vu_detail::regionBudgetFits(start, budgetEnd, plan.cycles) ||
                vu.m_state.pc + sizeof...(words) * 8u > vu.m_cachedCodeSize || vu.m_stopRequested ||
                vu.m_state.branchPending || vu.m_state.ebit || vu.m_state.haltAfterDelaySlot ||
                vu.m_xgkick.active || (!hasDiv && vu.m_fdiv.valid) || vu.m_activeStores || vu.m_activeAccWrites ||
                !vu.m_activeVuData || vu.m_activeVuDataSize < 16u)
                return false;
            const uint64_t generation = unit == Unit::VU1
                ? vu.m_activeMemory->getVU1CodeGeneration() : vu.m_activeMemory->getVU0CodeGeneration();
            if (generation != vu.m_cachedCodeGeneration)
                return false;
            for (const auto &entry : vu.m_efu)
                if (entry.valid)
                    return false;
            for (uint32_t active = vu.m_activeFlags; active != 0u; active &= active - 1u)
                if (vu.m_flagPipeline[std::countr_zero(active)].readyCycle > start + 3u ||
                    (hasDiv && vu.m_fdiv.valid && vu.m_flagPipeline[std::countr_zero(active)].writesSticky))
                    return false;
            const bool ready = [&]<size_t... indices>(std::index_sequence<indices...>) PS2_VU_INLINE_LAMBDA {
                return ((vu.calculatePairReadyCycleInline(decodedRegion[indices]) <= start + plan.issue[indices]) && ...);
            }(std::make_index_sequence<sizeof...(words)>{});
            if (!ready)
                return false;

            std::array<uint32_t, 4> incomingClip{};
            if constexpr (plan.readsClip)
            {
                incomingClip[0] = vu.m_state.clip;
                for (unsigned offset = 1u; offset < 4u; ++offset)
                {
                    incomingClip[offset] = incomingClip[offset - 1u];
                    const uint64_t cycle = start + offset;
                    for (uint32_t active = vu.m_activeFlags & (3u << (2u * (cycle & 3u)));
                         active != 0u; active &= active - 1u)
                    {
                        const auto &entry = vu.m_flagPipeline[std::countr_zero(active)];
                        if (entry.readyCycle <= cycle && entry.writesClip)
                            incomingClip[offset] = entry.clip;
                    }
                }
            }
            ps2_vu_detail::retireRegionInputs(vu.m_state, start, vu.m_flagPipeline,
                vu.m_vfWritePipeline, vu.m_viWritePipeline, vu.m_vfLatestWrite,
                vu.m_viLatestWrite, vu.m_activeFlags, vu.m_activeVfWrites, vu.m_activeViWrites,
                plan.firstVfWrite, plan.firstViWrite);
            static constexpr auto entryReads = [] {
                struct Reads {
                    std::array<uint8_t, 33> vf{};
                    std::array<bool, 16> vi{};
                } reads;
                const auto vf = [&](uint16_t source, unsigned lane) {
                    if (source < 33u)
                        reads.vf[source] |= static_cast<uint8_t>(1u << lane);
                };
                const auto vi = [&](uint16_t source) {
                    if (source < 16u)
                        reads.vi[source] = true;
                };
                for (const auto &source : plan.sources)
                {
                    for (unsigned lane = 0; lane < 4u; ++lane)
                    {
                        vf(source.fs[lane], lane);
                        vf(source.ft[lane], lane);
                        vf(source.acc[lane], lane);
                        vf(source.lowerFs[lane], lane);
                        vf(source.lowerFt[lane], lane);
                    }
                    vi(source.viOld);
                    vi(source.viS);
                    vi(source.viT);
                }
                for (unsigned reg = 0; reg < 32u; ++reg)
                    for (unsigned lane = 0; lane < 4u; ++lane)
                        if ((plan.vfVisible[reg][lane] != reg || plan.vfLatest[reg][lane] != 0u))
                            vf(plan.vfVisible[reg][lane], lane);
                for (unsigned reg = 0; reg < 16u; ++reg)
                    if ((plan.viVisible[reg] != reg || plan.viLatest[reg] != 0u))
                        vi(plan.viVisible[reg]);
                for (unsigned lane = 0; lane < 4u; ++lane)
                    if (plan.accVisible[lane] != 32u)
                        vf(plan.accVisible[lane], lane);
                return reads;
            }();
            std::array<std::array<float, 4>, 33> entryVf;
            std::array<int32_t, 16> entryVi;
            // Snapshot every referenced entry operand before byte stores can alias state.
            [&]<size_t... indices>(std::index_sequence<indices...>) PS2_VU_INLINE_LAMBDA {
                (([&]() PS2_VU_INLINE_LAMBDA {
                    constexpr unsigned reg = indices / 4u, lane = indices % 4u;
                    if constexpr ((entryReads.vf[reg] & (1u << lane)) != 0u)
                    {
                        if constexpr (reg < 32u)
                            entryVf[reg][lane] = vu.m_state.vf[reg][lane];
                        else
                            entryVf[reg][lane] = vu.m_state.acc[lane];
                    }
                }()), ...);
            }(std::make_index_sequence<132u>{});
            [&]<size_t... regs>(std::index_sequence<regs...>) PS2_VU_INLINE_LAMBDA {
                (([&]() PS2_VU_INLINE_LAMBDA {
                    if constexpr (entryReads.vi[regs])
                        entryVi[regs] = vu.m_state.vi[regs];
                }()), ...);
            }(std::make_index_sequence<16u>{});
            float entryI = vu.m_state.i;
            float entryQ = vu.m_state.q;
            float entryPendingQ = vu.m_fdiv.value;
            uint64_t entryPendingReady = vu.m_fdiv.readyCycle;
            uint32_t entryPendingDi = vu.m_fdiv.statusDi;
            bool entryPending = vu.m_fdiv.valid;
            struct DivValue { float value{}; uint32_t statusDi{}; };
            std::array<DivValue, sizeof...(words)> divisions{};
            std::array<std::array<float, 4>, sizeof...(words) * 2u> values{};
            std::array<int32_t, sizeof...(words)> integers{};
            std::array<ps2_vu_detail::RegionFlag, sizeof...(words)> flags{};
            uint32_t workingClip = vu.m_workingClip;
            uint32_t workingMac = vu.m_state.mac, workingStatus = vu.m_state.status;
            uint32_t visibleClip = vu.m_state.clip;
            auto *const loopData = vu.m_activeVuData;
            const uint32_t loopDataSize = vu.m_activeVuDataSize;
            const auto readVf = [&]<uint16_t source, unsigned lane>() PS2_VU_INLINE_LAMBDA {
                if constexpr (source == ps2_vu_detail::regionConstantZero)
                    return lane == 3u ? 1.0f : 0.0f;
                else if constexpr (source < 32u)
                {
                    static_assert((entryReads.vf[source] & (1u << lane)) != 0u);
                    return entryVf[source][lane];
                }
                else if constexpr (source == 32u)
                {
                    static_assert((entryReads.vf[32u] & (1u << lane)) != 0u);
                    return entryVf[32u][lane];
                }
                else
                    return values[source - 33u][lane];
            };
            const auto readVi = [&]<uint16_t source>() PS2_VU_INLINE_LAMBDA {
                if constexpr (source == ps2_vu_detail::regionConstantZero)
                    return int32_t{0};
                else if constexpr (source < 16u)
                {
                    static_assert(entryReads.vi[source]);
                    return entryVi[source];
                }
                else
                    return integers[source - 16u];
            };
            const auto readI = [&]<uint16_t source>() PS2_VU_INLINE_LAMBDA {
                if constexpr (source == 0u)
                    return entryI;
                else
                    return ps2_vu_detail::upper::normalizeOperand(
                        std::bit_cast<float>(decodedRegion[source - 1u].lower));
            };
            const auto readQ = [&]<uint16_t source, uint16_t offset>() PS2_VU_INLINE_LAMBDA {
                if constexpr (source != 0u)
                    return divisions[source - 1u].value;
                else if constexpr (hasDiv)
                    return entryPending && entryPendingReady <= start + offset ? entryPendingQ : entryQ;
                else
                    return entryQ;
            };
            const auto readClip = [&]<size_t index>() PS2_VU_INLINE_LAMBDA {
                if constexpr (!decodedRegion[index].lowerUsage.readsClip)
                    return uint32_t{0};
                else if constexpr (plan.sources[index].clip != 0u)
                    return flags[plan.sources[index].clip - 1u].clipValue;
                else
                    return incomingClip[std::min<unsigned>(plan.issue[index], 3u)];
            };
            const auto issue = [&]<size_t index>() PS2_VU_INLINE_LAMBDA {
                static constexpr auto source = plan.sources[index];
                float fs[4], ft[4], acc[4], lowerFs[4];
                [&]<size_t... lanes>(std::index_sequence<lanes...>) PS2_VU_INLINE_LAMBDA {
                    ((fs[lanes] = readVf.template operator()<source.fs[lanes], lanes>(),
                      ft[lanes] = readVf.template operator()<source.ft[lanes], lanes>(),
                      acc[lanes] = readVf.template operator()<source.acc[lanes], lanes>(),
                      lowerFs[lanes] = readVf.template operator()<source.lowerFs[lanes], lanes>(),
                      values[index * 2u + 1u][lanes] = readVf.template operator()<source.lowerFt[lanes], lanes>()), ...);
                }(std::make_index_sequence<4u>{});
                ps2_vu_detail::RegionUpperInputs input{fs, ft, acc,
                    readI.template operator()<source.scalarI>(),
                    readQ.template operator()<source.scalarQ, plan.issue[index]>()};
                flags[index] = {};
                ps2_vu_detail::RegionUpperSink sink{values[index * 2u].data(), flags[index], workingClip};
                ps2_vu_detail::upper::computeUpper(decodedRegion[index].upper, input, sink);
                integers[index] = readVi.template operator()<source.viOld>();
                ps2_vu_detail::RegionLowerInputs lowerInput{lowerFs,
                    readVi.template operator()<source.viS>(), readVi.template operator()<source.viT>(),
                    readClip.template operator()<index>()};
                ps2_vu_detail::RegionLowerSink lowerSink{values[index * 2u + 1u].data(), integers[index]};
                if constexpr (index == sizeof...(words) - 2u)
                {
                    const bool equal = static_cast<int16_t>(readVi.template operator()<loop.branchS>()) ==
                                       static_cast<int16_t>(readVi.template operator()<loop.branchT>());
                    branchTaken = (decodedRegion[index].lower >> 25u) == 0x28u ? equal : !equal;
                }
                else if constexpr (plan.qReady[index] != 0u)
                {
                    constexpr uint32_t instruction = decodedRegion[index].lower;
                    const float num = vu.normalizeOperand(lowerFs[(instruction >> 21u) & 3u]);
                    const float den = vu.normalizeOperand(values[index * 2u + 1u][(instruction >> 23u) & 3u]);
                    auto &division = divisions[index];
                    division.statusDi = 0u;
                    if (den == 0.0f)
                    {
                        division.statusDi = num == 0.0f ? 0x10u : 0x20u;
                        division.value = std::signbit(num) != std::signbit(den)
                            ? -std::numeric_limits<float>::max() : std::numeric_limits<float>::max();
                    }
                    else
                        division.value = num / den;
                    uint32_t ignoredFlags = 0u;
                    division.value = vu.normalizeResult(division.value, ignoredFlags);
                }
                else if constexpr (!decodedRegion[index].iBit)
                    ps2_vu_detail::computeRegionLower(decodedRegion[index].lower, lowerInput, lowerSink,
                                                    loopData, loopDataSize);
            };
            const auto retireDivStatus = [&]<bool includePending>() PS2_VU_INLINE_LAMBDA {
                const auto apply = [&](uint32_t statusDi) PS2_VU_INLINE_LAMBDA {
                    const uint32_t current = statusDi & 0x30u;
                    workingStatus = (workingStatus & 0xfcfu) | current | (current << 6u);
                };
                if constexpr (hasDiv)
                {
                    if (entryPending && entryPendingReady <= start + plan.cycles)
                        apply(entryPendingDi);
                    [&]<size_t... indices>(std::index_sequence<indices...>) PS2_VU_INLINE_LAMBDA {
                        (([&]() PS2_VU_INLINE_LAMBDA {
                            if constexpr (plan.qReady[indices] != 0u &&
                                (includePending || plan.qReady[indices] <= plan.cycles))
                                apply(divisions[indices].statusDi);
                        }()), ...);
                    }(std::make_index_sequence<sizeof...(words)>{});
                }
            };
            for (;;)
            {
            [&]<size_t... indices>(std::index_sequence<indices...>) PS2_VU_INLINE_LAMBDA {
                (issue.template operator()<indices>(), ...);
            }(std::make_index_sequence<sizeof...(words)>{});

            ++iterations;
            everBranched |= branchTaken;
            const uint64_t end = start + plan.cycles;
            const uint64_t currentGeneration = unit == Unit::VU1
                ? vu.m_activeMemory->getVU1CodeGeneration() : vu.m_activeMemory->getVU0CodeGeneration();
            if (branchTaken && ps2_vu_detail::regionBudgetFits(end, budgetEnd, plan.cycles) &&
                !vu.m_stopRequested && currentGeneration == vu.m_cachedCodeGeneration)
            {
                retireDivStatus.template operator()<true>();
                if constexpr (plan.readsClip)
                {
                    // The last three CLIP results can cross the loop backedge.
                    const uint32_t entryClip = incomingClip[3];
                    for (unsigned offset = 0u; offset < 4u; ++offset)
                    {
                        uint32_t clip = entryClip;
                        for (unsigned index = 0u; index < sizeof...(words); ++index)
                            if (flags[index].clip && plan.issue[index] + 4u <= plan.cycles + offset)
                                clip = flags[index].clipValue;
                        incomingClip[offset] = clip;
                    }
                }
                // Prior-iteration flags retire before this iteration can publish any new ones.
                for (const auto &flag : flags)
                {
                    if (flag.fmac)
                    {
                        workingMac = flag.mac;
                        const uint32_t current = flag.status & 15u;
                        workingStatus = (workingStatus & 0xff0u) | current | ((current | flag.extraSticky) << 6u);
                    }
                    if (flag.clip)
                        visibleClip = flag.clipValue;
                }
                [&]<size_t... indices>(std::index_sequence<indices...>) PS2_VU_INLINE_LAMBDA {
                    (([&]() PS2_VU_INLINE_LAMBDA {
                        constexpr unsigned reg = indices / 4u, lane = indices % 4u;
                        if constexpr ((entryReads.vf[reg] & (1u << lane)) != 0u)
                        {
                            if constexpr (reg < 32u)
                                entryVf[reg][lane] = readVf.template operator()<loop.vfNext[reg][lane], lane>();
                            else
                                entryVf[reg][lane] = readVf.template operator()<plan.accVisible[lane], lane>();
                        }
                    }()), ...);
                }(std::make_index_sequence<132u>{});
                [&]<size_t... regs>(std::index_sequence<regs...>) PS2_VU_INLINE_LAMBDA {
                    (([&]() PS2_VU_INLINE_LAMBDA {
                        if constexpr (entryReads.vi[regs])
                            entryVi[regs] = readVi.template operator()<loop.viNext[regs]>();
                    }()), ...);
                }(std::make_index_sequence<16u>{});
                if constexpr (plan.scalarI != 0u)
                    entryI = readI.template operator()<plan.scalarI>();
                if constexpr (hasDiv)
                {
                    entryQ = readQ.template operator()<plan.scalarQ, plan.cycles>();
                    constexpr unsigned lastDiv = plan.pendingQ - 1u;
                    entryPending = plan.qReady[lastDiv] > plan.cycles;
                    entryPendingQ = divisions[lastDiv].value;
                    entryPendingDi = divisions[lastDiv].statusDi;
                    entryPendingReady = start + plan.qReady[lastDiv];
                }
                sequenceBase += plan.sequences;
                start = end;
                continue;
            }
            retireDivStatus.template operator()<false>();
            if constexpr (hasDiv)
            {
                vu.m_state.q = readQ.template operator()<plan.scalarQ, plan.cycles>();
                vu.m_fdiv = {};
                constexpr unsigned lastDiv = plan.pendingQ - 1u;
                if constexpr (plan.qReady[lastDiv] > plan.cycles)
                {
                    vu.m_fdiv.valid = true;
                    vu.m_fdiv.readyCycle = start + plan.qReady[lastDiv];
                    vu.m_fdiv.value = divisions[lastDiv].value;
                    vu.m_fdiv.statusDi = divisions[lastDiv].statusDi;
                }
            }
            vu.m_state.mac = workingMac;
            vu.m_state.status = workingStatus;
            vu.m_state.clip = visibleClip;
            constexpr unsigned last = sizeof...(words) - 1u;
            const int32_t branchOld = readVi.template operator()<plan.sources[last].viOld>();
            const auto publishVf = [&]<size_t reg, size_t lane>() PS2_VU_INLINE_LAMBDA {
                if constexpr ((plan.vfVisible[reg][lane] != reg || plan.vfLatest[reg][lane] != 0u))
                    vu.m_state.vf[reg][lane] = readVf.template operator()<plan.vfVisible[reg][lane], lane>();
                if constexpr (plan.vfLatest[reg][lane] != 0u)
                {
                    vu.m_vfReady[reg][lane] = start + plan.vfReady[reg][lane];
                    vu.m_vfLatestWrite[reg][lane] = sequenceBase + plan.vfLatest[reg][lane];
                }
            };
            [&]<size_t... indices>(std::index_sequence<indices...>) PS2_VU_INLINE_LAMBDA {
                (publishVf.template operator()<indices / 4u, indices % 4u>(), ...);
            }(std::make_index_sequence<128u>{});
            [&]<size_t... regs>(std::index_sequence<regs...>) PS2_VU_INLINE_LAMBDA {
                (([&]() PS2_VU_INLINE_LAMBDA {
                    if constexpr ((plan.viVisible[regs] != regs || plan.viLatest[regs] != 0u))
                        vu.m_state.vi[regs] = readVi.template operator()<plan.viVisible[regs]>();
                    if constexpr (plan.viLatest[regs] != 0u)
                    {
                        vu.m_viReady[regs] = start + plan.viReady[regs];
                        vu.m_viLatestWrite[regs] = sequenceBase + plan.viLatest[regs];
                    }
                }()), ...);
            }(std::make_index_sequence<16u>{});
            [&]<size_t... lanes>(std::index_sequence<lanes...>) PS2_VU_INLINE_LAMBDA {
                (([&]() PS2_VU_INLINE_LAMBDA {
                    if constexpr (plan.accVisible[lanes] != 32u)
                        vu.m_state.acc[lanes] = readVf.template operator()<plan.accVisible[lanes], lanes>();
                    if constexpr (plan.accReady[lanes] != 0u)
                        vu.m_accReady[lanes] = start + plan.accReady[lanes];
                }()), ...);
            }(std::make_index_sequence<4u>{});
            const auto pendingVf = [&]<ps2_vu_detail::RegionWrite write>() PS2_VU_INLINE_LAMBDA {
                if constexpr (write.mask != 0u && write.ready > plan.cycles)
                {
                    auto *entry = allocateScheduledEntry(vu.m_vfWritePipeline, vu.m_activeVfWrites, start + write.ready);
                    *entry = {};
                    entry->valid = true;
                    entry->readyCycle = start + write.ready;
                    entry->sequence = sequenceBase + write.sequence;
                    entry->reg = write.reg;
                    entry->laneMask = write.mask;
                    entry->value = values[write.value - 33u];
                }
            };
            const auto publishTail = [&]<size_t index>() PS2_VU_INLINE_LAMBDA {
                pendingVf.template operator()<plan.upper[index]>();
                pendingVf.template operator()<plan.lower[index]>();
                static constexpr auto viWrite = plan.vi[index];
                if constexpr (viWrite.mask != 0u && viWrite.ready > plan.cycles)
                {
                    auto *entry = allocateScheduledEntry(vu.m_viWritePipeline, vu.m_activeViWrites, start + viWrite.ready);
                    *entry = {};
                    entry->valid = true;
                    entry->readyCycle = start + viWrite.ready;
                    entry->sequence = sequenceBase + viWrite.sequence;
                    entry->reg = viWrite.reg;
                    entry->value = integers[index];
                }
                const auto &flag = flags[index];
                if (flag.fmac || flag.clip)
                {
                    if constexpr (plan.issue[index] + 4u <= plan.cycles)
                    {
                        if (flag.fmac)
                        {
                            vu.m_state.mac = flag.mac;
                            const uint32_t current = flag.status & 15u;
                            vu.m_state.status = (vu.m_state.status & 0xff0u) | current | ((current | flag.extraSticky) << 6u);
                        }
                        if (flag.clip)
                            vu.m_state.clip = flag.clipValue;
                    }
                    else
                    {
                        auto *entry = allocateScheduledEntry(vu.m_flagPipeline, vu.m_activeFlags, start + plan.issue[index] + 4u);
                        *entry = {};
                        entry->valid = true;
                        entry->issueCycle = start + plan.issue[index];
                        entry->readyCycle = entry->issueCycle + 4u;
                        entry->mac = flag.mac;
                        entry->status = flag.status;
                        entry->extraSticky = flag.extraSticky;
                        entry->clip = flag.clipValue;
                        entry->writesMac = entry->writesStatus = flag.fmac;
                        entry->writesClip = flag.clip;
                    }
                }
            };
            [&]<size_t... indices>(std::index_sequence<indices...>) PS2_VU_INLINE_LAMBDA {
                (publishTail.template operator()<indices>(), ...);
            }(std::make_index_sequence<sizeof...(words)>{});
            vu.m_viBranchBackupValid = false;
            if constexpr (plan.vi[last].mask != 0u && decodedRegion[last].lowerUsage.delaysNextBranchRead)
                vu.recordViWriteForBranch(plan.vi[last].reg, branchOld);
            vu.m_nextWriteSequence = sequenceBase + plan.sequences;
            vu.m_workingClip = workingClip;
            if constexpr (plan.scalarI != 0u)
                vu.m_state.i = readI.template operator()<plan.scalarI>();
            vu.m_currentUpperInstruction = decodedRegion[last].upper;
            vu.m_cycle = vu.m_state.cycles = end;
            if (everBranched)
            {
                vu.m_state.branchTarget = startPC;
                vu.m_state.branchDelay = 0u;
            }
            vu.m_state.pc = branchTaken ? startPC : startPC + sizeof...(words) * 8u;
            if (vu.m_state.pc == vu.m_cachedCodeSize)
                vu.m_state.pc = 0u;
            vu.m_compiledPairsExecuted += sizeof...(words) * iterations;
            if (ps2_vu_detail::profileCountedLoops)
                ps2_vu_detail::countedLoopPairs += sizeof...(words) * iterations;
            return true;
            }
        };
        if (tryLoop())
            return false;
    }
}
