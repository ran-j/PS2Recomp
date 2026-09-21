    static constexpr auto plan = ps2_vu_detail::planRegion(decodedRegion);
    if constexpr (plan.eligible)
    {
        const auto tryRegion = [&]() PS2_VU_INLINE_LAMBDA {
            const uint64_t start = vu.m_cycle;
            if (!ps2_vu_detail::regionBudgetFits(start, budgetEnd, plan.cycles) ||
                vu.m_state.pc + sizeof...(words) * 8u > vu.m_cachedCodeSize || vu.m_stopRequested ||
                vu.m_state.branchPending || vu.m_state.ebit || vu.m_state.haltAfterDelaySlot ||
                vu.m_xgkick.active || vu.m_fdiv.valid || vu.m_activeStores || vu.m_activeAccWrites ||
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
                if (vu.m_flagPipeline[std::countr_zero(active)].readyCycle > start + 3u)
                    return false;
            const bool ready = [&]<size_t... indices>(std::index_sequence<indices...>) PS2_VU_INLINE_LAMBDA {
                return ((vu.calculatePairReadyCycleInline(decodedRegion[indices]) <= start + plan.issue[indices]) && ...);
            }(std::make_index_sequence<sizeof...(words)>{});
            if (!ready)
                return false;

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
                        if (plan.vfVisible[reg][lane] != reg)
                            vf(plan.vfVisible[reg][lane], lane);
                for (unsigned reg = 0; reg < 16u; ++reg)
                    if (plan.viVisible[reg] != reg)
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
            const float entryI = vu.m_state.i, entryQ = vu.m_state.q;
            std::array<std::array<float, 4>, sizeof...(words) * 2u> values{};
            std::array<int32_t, sizeof...(words)> integers{};
            std::array<ps2_vu_detail::RegionFlag, sizeof...(words)> flags{};
            uint32_t workingClip = vu.m_workingClip;
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
                    readI.template operator()<source.scalarI>(), entryQ};
                ps2_vu_detail::RegionUpperSink sink{values[index * 2u].data(), flags[index], workingClip};
                ps2_vu_detail::upper::computeUpper(decodedRegion[index].upper, input, sink);
                integers[index] = readVi.template operator()<source.viOld>();
                ps2_vu_detail::RegionLowerInputs lowerInput{lowerFs,
                    readVi.template operator()<source.viS>(), readVi.template operator()<source.viT>()};
                ps2_vu_detail::RegionLowerSink lowerSink{values[index * 2u + 1u].data(), integers[index]};
                if constexpr (!decodedRegion[index].iBit)
                    ps2_vu_detail::computeRegionLower(decodedRegion[index].lower, lowerInput, lowerSink,
                                                    vu.m_activeVuData, vu.m_activeVuDataSize);
            };
            [&]<size_t... indices>(std::index_sequence<indices...>) PS2_VU_INLINE_LAMBDA {
                (issue.template operator()<indices>(), ...);
            }(std::make_index_sequence<sizeof...(words)>{});

            const uint64_t sequenceBase = vu.m_nextWriteSequence;
            const uint64_t end = start + plan.cycles;
            constexpr unsigned last = sizeof...(words) - 1u;
            const int32_t branchOld = readVi.template operator()<plan.sources[last].viOld>();
            const auto publishVf = [&]<size_t reg, size_t lane>() PS2_VU_INLINE_LAMBDA {
                if constexpr (plan.vfVisible[reg][lane] != reg)
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
                    if constexpr (plan.viVisible[regs] != regs)
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
            vu.m_state.pc += sizeof...(words) * 8u;
            if (vu.m_state.pc == vu.m_cachedCodeSize)
                vu.m_state.pc = 0u;
            vu.m_compiledPairsExecuted += sizeof...(words);
            return true;
        };
        if (tryRegion())
        {
            if (ps2_vu_detail::profileRegions)
            {
                ps2_vu_detail::regionCounters.accepted[unit == Unit::VU1 ? 1u : 0u] += sizeof...(words);
                ps2_vu_detail::regionCounters.acceptedByLength[sizeof...(words)][unit == Unit::VU1 ? 1u : 0u] += sizeof...(words);
            }
            return false;
        }
    }
