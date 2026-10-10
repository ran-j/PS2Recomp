file(READ "${parallel_gs_SOURCE_DIR}/gs/gs_interface.cpp" gs_interface_source)

function(patch_gs_transfer before after)
    string(FIND "${gs_interface_source}" "${before}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "parallel-gs transfer patch does not match the pinned source")
    endif()
    string(REPLACE "${before}" "${after}" gs_interface_source "${gs_interface_source}")
    set(gs_interface_source "${gs_interface_source}" PARENT_SCOPE)
endfunction()

patch_gs_transfer("get_bits_per_pixel(transfer_state.copy.bitbltbuf.desc.DPSM)) / 64;" "get_bits_per_pixel(transfer_state.copy.bitbltbuf.desc.DPSM) + 63) / 64;")
patch_gs_transfer("get_bits_per_pixel(transfer_state.copy.bitbltbuf.desc.SPSM)) / 8;" "get_bits_per_pixel(transfer_state.copy.bitbltbuf.desc.SPSM) + 7) / 8;")
patch_gs_transfer("transfer_state.fifo_readback.reserve(required_bytes);" "transfer_state.fifo_readback.reserve((required_bytes + 15) & ~15u);\n\t\tif (required_bytes) memset(transfer_state.fifo_readback.data(), 0, (required_bytes + 15) & ~15u);")
patch_gs_transfer("transfer_state.fifo_readback_128b_size = required_bytes / 16;" "transfer_state.fifo_readback_128b_size = (required_bytes + 15) / 16;")

set(patched_interface "${parallel_gs_BINARY_DIR}/gs_interface_transfer_tails.cpp")
file(CONFIGURE OUTPUT "${patched_interface}" CONTENT "${gs_interface_source}" @ONLY)

get_target_property(gs_sources parallel-gs SOURCES)
list(REMOVE_ITEM gs_sources gs_interface.cpp)
set_property(TARGET parallel-gs PROPERTY SOURCES "${gs_sources}")

target_sources(parallel-gs PRIVATE "${patched_interface}")
