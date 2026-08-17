include(CheckIPOSupported)

check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)

# ps2_runtime.h unconditionally includes <smmintrin.h> and the recompiler emits
# SSE4.1-only intrinsics (_mm_blendv_ps and friends) for the COP2/FPU select
# idioms, so SSE4.1 is a hard requirement of the codebase rather than a tuning
# knob. MSVC enables it implicitly (its intrinsics are not gated by a target
# feature), but GCC and Clang default to the plain x86-64 baseline, which is
# SSE2 -- so on any stock Linux or macOS toolchain those translation units fail
# to compile with "always_inline function ... requires target feature 'sse4.1'".
#
# This must not live in EnableFastReleaseMode: that is only applied for Release
# and RelWithDebInfo, whereas the requirement applies to every configuration.
function(EnableX86SimdBaseline TargetName)
    if(MSVC)
        return()
    endif()
    if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i[3-6]86|x86)$")
        return()
    endif()
    target_compile_options(${TargetName} PUBLIC -msse4.1)
endfunction()

function(EnableFastReleaseMode TargetName)
    message("> Enabling optimization for: ${TargetName}")
    if(MSVC)
        target_compile_options(${TargetName} PRIVATE
            $<$<CONFIG:Release>:
                /O2 # speed
                /Ob2 # inline aggressively
                /Oi # intrinsics
                /GL # whole program opt
                /Gy # function-level linking
                /Gw # global data in COMDAT
                /GF # string pooling
                /Zc:inline # remove unreferenced inline
                /fp:fast # fast math (graphics friendly)
                /DNDEBUG
                /arch:AVX2 # Advanced Vector Extensions 2
                /GS- # Disable Buffer Security Check (faster)
                /Qspectre- # Disable Spectre mitigations (faster)
            >
        )

        if(TARGET ${TargetName})
            target_link_options(${TargetName} PRIVATE
                $<$<CONFIG:Release>:
                    /LTCG # link-time code generation
                    /OPT:REF # remove unreferenced
                    /OPT:ICF # fold identical COMDATs
                >
            )
        endif()
    endif()

    if(IPO_SUPPORTED)
        set_property(TARGET ${TargetName} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    else()
        message(WARNING "Interprocedural optimization not supported: ${ipo_error}")
    endif()
endfunction()