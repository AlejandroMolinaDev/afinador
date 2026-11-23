# Shared compiler settings for NovaPitch targets.
#
# Notes:
#  - The MSVC runtime is forced to the static one (/MT) in the root CMakeLists
#    (CMAKE_MSVC_RUNTIME_LIBRARY) so FL Studio users on Windows don't need the
#    Visual C++ redistributable.
#  - macOS Universal Binary + deployment target are set before project().

function(novapitch_apply_compiler_settings target)
    if(MSVC)
        # /bigobj: signalsmith-linear/stretch are template-heavy
        target_compile_options(${target} PRIVATE /utf-8 /MP /bigobj)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()
