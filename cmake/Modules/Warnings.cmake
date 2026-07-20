# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# note - excluding '-Wshadow' for now as it warns about constructor parameters shadowing class members,
#        which is fine in my opinion with modern compilers. -Wshadow=local gets around this, but AppleClang doesn't support it
function(set_project_warnings target)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:preprocessor)
    elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang|GNU")
        target_compile_options(${target} PRIVATE
            -Werror
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
#            -Wshadow=local
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wunused
        )
    endif()
endfunction()
