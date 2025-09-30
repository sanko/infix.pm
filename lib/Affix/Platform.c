/**
 * @file Platform.c
 * @brief Exports platform-specific constants to the Affix::Platform Perl package.
 *
 * @details This file is responsible for providing Perl code with essential,
 * compile-time information about the C data model and the underlying FFI
 * library on the current platform.
 *
 * It exports two main categories of information:
 * 1.  **Infix Library Info:** The version number, OS, Architecture, and ABI as
 *     detected by the `infix.h` header. This is useful for debugging and
 *     conditional logic in Perl.
 * 2.  **C Type Layout:** The `sizeof` and `alignof` values for all fundamental
 *     C types. The Perl code uses this information to correctly calculate the
 *     size, alignment, and member offsets when generating signature strings for
 *     complex C structs.
 */

#include "../Affix.h"
#include <stdbool.h>

// Define ALIGNOF_ macro using the modern C11 _Alignof for consistency.
#define ALIGNOF_(t) _Alignof(t)

// Helper macro to reduce boilerplate when exporting boolean flags.
#ifdef FFI_DEBUG_ENABLED  // Use a generic check that infix might provide
#define EXPORT_PLATFORM_BOOL(name, macro) register_constant("Affix::Platform", name, boolSV(true))
#else
#define EXPORT_PLATFORM_BOOL(name, macro) register_constant("Affix::Platform", name, boolSV(false))
#endif


// XS Functions

/**
 * @brief (Internal) Calculates the padding needed to align an offset.
 *
 * @details This function is exposed to Perl as
 *          `Affix::Platform::padding_needed_for`. It's a utility for Perl code
 *          that might need to manually calculate struct layouts.
 *
 * @param offset The current offset.
 * @param alignment The desired alignment boundary.
 * @return The number of bytes of padding required.
 */
IV padding_needed_for(IV offset, IV alignment) {
    if (alignment == 0) {
        return 0;
    }
    IV misalignment = offset % alignment;
    if (misalignment != 0) {
        // Round up to the next multiple of alignment.
        return alignment - misalignment;
    }
    return 0;  // Already aligned.
}

XS_INTERNAL(Affix_Platform_padding) {
    dXSARGS;
    if (items != 2) {
        croak_xs_usage(cv, "$offset, $alignment");
    }
    XSRETURN_IV(padding_needed_for(SvIV(ST(0)), SvIV(ST(1))));
}


// XS Boot Section

/**
 * @brief Initializes the Affix::Platform package and exports constants.
 */
void boot_Affix_Platform(pTHX_ CV * cv) {
    PERL_UNUSED_VAR(cv);

    (void)newXSproto_portable("Affix::Platform::padding_needed_for", Affix_Platform_padding, __FILE__, "$$");

    // Infix Library Information
    register_constant(
        "Affix::Platform", "INFIX_Version", Perl_newSVpvf(aTHX_ "%d.%d.%d", INFIX_MAJOR, INFIX_MINOR, INFIX_PATCH));
    register_constant("Affix::Platform", "INFIX_Major", newSViv(INFIX_MAJOR));
    register_constant("Affix::Platform", "INFIX_Minor", newSViv(INFIX_MINOR));
    register_constant("Affix::Platform", "INFIX_Patch", newSViv(INFIX_PATCH));

    // Determine platform strings using infix's detection macros
    const char * os =
#if defined(FFI_OS_LINUX)
        "Linux";
#elif defined(FFI_OS_WINDOWS)
        "Windows";
#elif defined(FFI_OS_MACOS)
        "macOS";
#elif defined(FFI_OS_IOS)
        "iOS";
#elif defined(FFI_OS_ANDROID)
        "Android";
#elif defined(FFI_OS_TERMUX)
        "Termux";
#elif defined(FFI_OS_FREEBSD)
        "FreeBSD";
#elif defined(FFI_OS_OPENBSD)
        "OpenBSD";
#elif defined(FFI_OS_NETBSD)
        "NetBSD";
#elif defined(FFI_OS_DRAGONFLY)
        "DragonFly BSD"
#elif defined(FFI_OS_SOLARIS)
        "Solaris"
#elif defined(FFI_OS_HAIKU)
        "Haiku"
#else
        "Unknown";
#endif

    const char * architecture =
#if defined(FFI_ARCH_X64)
        "x86_64";
#elif defined(FFI_ARCH_AARCH64)
        "ARM64";
#else
        "Unknown";
#endif

    const char * compiler =
#if defined(FFI_COMPILER_CLANG)
        "Clang";
#elif defined(FFI_COMPILER_GCC)
        "GCC";
#elif defined(FFI_COMPILER_MSVC)
        "MSVC";
#else
            "Unknown";
#endif

    const char * abi =
#if defined(FFI_ABI_WINDOWS_X64)
        "Windows x64";
#elif defined(FFI_ABI_SYSV_X64)
        "System V AMD64";
#elif defined(FFI_ABI_AAPCS64)
        "AAPCS64";
#else
        "Unknown";
#endif

    register_constant("Affix::Platform", "OS", newSVpv(os, 0));
    register_constant("Affix::Platform", "Architecture", newSVpv(architecture, 0));
    register_constant("Affix::Platform", "Compiler", newSVpv(compiler, 0));
    register_constant("Affix::Platform", "ABI", newSVpv(abi, 0));

    // Export boolean flags for easier checking in Perl
    EXPORT_PLATFORM_BOOL("Linux", FFI_OS_LINUX);
    EXPORT_PLATFORM_BOOL("Windows", FFI_OS_WINDOWS);
    EXPORT_PLATFORM_BOOL("macOS", FFI_OS_MACOS);
    EXPORT_PLATFORM_BOOL("iOS", FFI_OS_IOS);  // Ha!
    EXPORT_PLATFORM_BOOL("Android", FFI_OS_ANDROID);
    EXPORT_PLATFORM_BOOL("FreeBSD", FFI_OS_FREEBSD);
    EXPORT_PLATFORM_BOOL("OpenBSD", FFI_OS_OPENBSD);
    EXPORT_PLATFORM_BOOL("NetBSD", FFI_OS_NETBSD);
    EXPORT_PLATFORM_BOOL("DragonFlyBSD", FFI_OS_DRAGONFLY);
    EXPORT_PLATFORM_BOOL("Solaris", FFI_OS_SOLARIS);
    EXPORT_PLATFORM_BOOL("Haiku", FFI_OS_HAIKU);

    // 64bit
    EXPORT_PLATFORM_BOOL("ARCH_x86_64", FFI_ARCH_X64);
    EXPORT_PLATFORM_BOOL("ARCH_ARM64", FFI_ARCH_AARCH64);
    // 32bit (no support... yet?)
    EXPORT_PLATFORM_BOOL("ARCH_x86", FFI_ARCH_X86);
    EXPORT_PLATFORM_BOOL("ARCH_ARM", FFI_ARCH_ARM);

    /* TODO:
     * Application Binary Interface (ABI):
     * - FFI_ABI_WINDOWS_X64:  Microsoft x64 Calling Convention
     * - FFI_ABI_SYSV_X64:     System V AMD64 ABI
     * - FFI_ABI_AAPCS64:      ARM 64-bit Procedure Call Standard
     *
     * Compiler:
     * - FFI_COMPILER_MSVC:    Microsoft Visual C++
     * - FFI_COMPILER_CLANG:   Clang
     * - FFI_COMPILER_GCC:     GNU Compiler Collection
     * - FFI_COMPILER_INTEL:   Intel C/C++ Compiler
     * - FFI_COMPILER_IBM:     IBM XL C/C++
     * - FFI_COMPILER_NFI:     Unknown compiler
     *
     * Environment:
     * - FFI_ENV_POSIX:         Defined for POSIX-compliant systems (macOS, Linux, BSDs, etc.)
     * - FFI_ENV_MSYS:         MSYS/MSYS2 build environment
     * - FFI_ENV_CYGWIN:       Cygwin environment
     * - FFI_ENV_MINGW:        MinGW/MinGW-w64 compilers
     * - FFI_ENV_TERMUX:       Termux running on Android or Chrome OS
     *
     */

    // SIZEOF Constants
    // These are essential for the Perl side to generate correct struct signatures.
    export_constant("Affix::Platform", "SIZEOF_BOOL", "sizeof", sizeof(bool));
    export_constant("Affix::Platform", "SIZEOF_CHAR", "sizeof", sizeof(char));
    export_constant("Affix::Platform", "SIZEOF_SCHAR", "sizeof", sizeof(signed char));
    export_constant("Affix::Platform", "SIZEOF_UCHAR", "sizeof", sizeof(unsigned char));
    export_constant("Affix::Platform", "SIZEOF_WCHAR", "sizeof", sizeof(wchar_t));
    export_constant("Affix::Platform", "SIZEOF_SHORT", "sizeof", sizeof(short));
    export_constant("Affix::Platform", "SIZEOF_USHORT", "sizeof", sizeof(unsigned short));
    export_constant("Affix::Platform", "SIZEOF_INT", "sizeof", sizeof(int));
    export_constant("Affix::Platform", "SIZEOF_UINT", "sizeof", sizeof(unsigned int));
    export_constant("Affix::Platform", "SIZEOF_LONG", "sizeof", sizeof(long));
    export_constant("Affix::Platform", "SIZEOF_ULONG", "sizeof", sizeof(unsigned long));
    export_constant("Affix::Platform", "SIZEOF_LONGLONG", "sizeof", sizeof(long long));
    export_constant("Affix::Platform", "SIZEOF_ULONGLONG", "sizeof", sizeof(unsigned long long));
    export_constant("Affix::Platform", "SIZEOF_FLOAT", "sizeof", sizeof(float));
    export_constant("Affix::Platform", "SIZEOF_DOUBLE", "sizeof", sizeof(double));
    export_constant("Affix::Platform", "SIZEOF_LONG_DOUBLE", "sizeof", sizeof(long double));
    export_constant("Affix::Platform", "SIZEOF_SIZE_T", "sizeof", sizeof(size_t));
    export_constant("Affix::Platform", "SIZEOF_SSIZE_T", "sizeof", sizeof(ssize_t));
    export_constant("Affix::Platform", "SIZEOF_INTPTR_T", "sizeof", sizeof(intptr_t));
    export_constant("Affix::Platform", "SIZEOF_PTR", "sizeof", sizeof(void *));

    // ALIGNOF Constants
    // Also essential for Perl-side struct layout calculations.
    export_constant("Affix::Platform", "ALIGNOF_BOOL", "alignof", ALIGNOF_(bool));
    export_constant("Affix::Platform", "ALIGNOF_CHAR", "alignof", ALIGNOF_(char));
    export_constant("Affix::Platform", "ALIGNOF_SCHAR", "alignof", ALIGNOF_(signed char));
    export_constant("Affix::Platform", "ALIGNOF_UCHAR", "alignof", ALIGNOF_(unsigned char));
    export_constant("Affix::Platform", "ALIGNOF_WCHAR", "alignof", ALIGNOF_(wchar_t));
    export_constant("Affix::Platform", "ALIGNOF_SHORT", "alignof", ALIGNOF_(short));
    export_constant("Affix::Platform", "ALIGNOF_USHORT", "alignof", ALIGNOF_(unsigned short));
    export_constant("Affix::Platform", "ALIGNOF_INT", "alignof", ALIGNOF_(int));
    export_constant("Affix::Platform", "ALIGNOF_UINT", "alignof", ALIGNOF_(unsigned int));
    export_constant("Affix::Platform", "ALIGNOF_LONG", "alignof", ALIGNOF_(long));
    export_constant("Affix::Platform", "ALIGNOF_ULONG", "alignof", ALIGNOF_(unsigned long));
    export_constant("Affix::Platform", "ALIGNOF_LONGLONG", "alignof", ALIGNOF_(long long));
    export_constant("Affix::Platform", "ALIGNOF_ULONGLONG", "alignof", ALIGNOF_(unsigned long long));
    export_constant("Affix::Platform", "ALIGNOF_FLOAT", "alignof", ALIGNOF_(float));
    export_constant("Affix::Platform", "ALIGNOF_DOUBLE", "alignof", ALIGNOF_(double));
    export_constant("Affix::Platform", "ALIGNOF_LONG_DOUBLE", "alignof", ALIGNOF_(long double));
    export_constant("Affix::Platform", "ALIGNOF_SIZE_T", "alignof", ALIGNOF_(size_t));
    export_constant("Affix::Platform", "ALIGNOF_SSIZE_T", "alignof", ALIGNOF_(ssize_t));
    export_constant("Affix::Platform", "ALIGNOF_INTPTR_T", "alignof", ALIGNOF_(intptr_t));
    export_constant("Affix::Platform", "ALIGNOF_PTR", "alignof", ALIGNOF_(void *));
}
