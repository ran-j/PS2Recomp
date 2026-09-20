include(CheckIPOSupported)

check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)

function(EnableFastReleaseMode TargetName)
    cmake_parse_arguments(RELEASE_MODE "NO_IPO" "" "" ${ARGN})
    message("> Enabling optimization for: ${TargetName}")
    if(MSVC)
        target_compile_options(${TargetName} PRIVATE
            $<$<CONFIG:Release>:
                /O2 # speed
                /Ob2 # inline aggressively
                /Oi # intrinsics
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

        if(NOT RELEASE_MODE_NO_IPO)
            target_compile_options(${TargetName} PRIVATE
                $<$<CONFIG:Release>:/GL>
            )
        endif()

        if(TARGET ${TargetName})
            target_link_options(${TargetName} PRIVATE
                $<$<CONFIG:Release>:
                    /OPT:REF # remove unreferenced
                    /OPT:ICF # fold identical COMDATs
                >
            )
            if(NOT RELEASE_MODE_NO_IPO)
                target_link_options(${TargetName} PRIVATE
                    $<$<CONFIG:Release>:/LTCG>
                )
            endif()
        endif()
    endif()

    if(NOT RELEASE_MODE_NO_IPO)
        if(IPO_SUPPORTED)
            set_property(TARGET ${TargetName} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        else()
            message(WARNING "Interprocedural optimization not supported: ${ipo_error}")
        endif()
    endif()
endfunction()
