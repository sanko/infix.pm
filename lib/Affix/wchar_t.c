/**
 * @file wchar_t.c
 * @brief Implements cross-platform wide character string conversions.
 *
 * @details This file provides utility functions to convert between Perl's internal
 * UTF-8 string representation (in an SV*) and the platform's native wide
 * character representation (`wchar_t*`).
 *
 * This is a critical component for interoperability with C libraries that use
 * wide character APIs, which are especially common on Windows. The implementation
 * is platform-specific:
 *
 * - **On Windows:** It uses the Win32 API functions `MultiByteToWideChar` and
 *   `WideCharToMultiByte` for correct handling of UTF-16, which is the native
 *   `wchar_t` encoding on that platform.
 *
 * - **On POSIX-like systems (Linux, macOS, BSD):** It uses a combination of
 *   Perl's internal UTF-8 functions and the standard C library's `mbstowcs` to
 *   convert to the system's locale-dependent `wchar_t` encoding (often UTF-32).
 */

#include "../Affix.h"

/**
 * @brief Converts a native wide character string to a Perl UTF-8 SV.
 *
 * @param src A pointer to the null-terminated `wchar_t` string.
 * @param len The number of wide characters in the string (excluding null).
 * @return A new Perl SV* containing the equivalent UTF-8 string. The SV is mortal.
 */
SV * wchar2utf(pTHX_ wchar_t * src, size_t len) {
#if defined(INFIX_OS_WINDOWS)
    // Windows uses UTF-16 for wchar_t.
    // First, determine the required buffer size for the UTF-8 string.
    int outlen = WideCharToMultiByte(CP_UTF8, 0, src, len, NULL, 0, NULL, NULL);
    if (outlen == 0) {
        return sv_newmortal();  // Return empty SV on error
    }

    // Allocate buffer and perform the conversion.
    char * r = (char *)safecalloc(outlen + 1, sizeof(char));
    WideCharToMultiByte(CP_UTF8, 0, src, len, r, outlen, NULL, NULL);

    // Create a new Perl SV, telling Perl it's UTF-8.
    SV * RETVAL = sv_2mortal(newSVpvn_utf8(r, outlen, 1));
    safefree((void *)r);

#else
    // POSIX-like systems (typically UTF-32 for wchar_t).
    // We can use Perl's robust internal Unicode functions.
    SV * RETVAL = sv_newmortal();
    STRLEN ulen = 0;
    // `sv_set_wide_ww` is a safe and efficient way to do this conversion.
    sv_set_wide_ww(RETVAL, src, len, &ulen);
    (void)ulen;  // ulen is the number of characters, which we don't need here.

#endif
    return RETVAL;
}

/**
 * @brief Converts a Perl UTF-8 SV to a native wide character string.
 *
 * @param src The Perl SV* containing the UTF-8 string.
 * @param len The length of the UTF-8 string in bytes.
 * @return A newly allocated `wchar_t*` string. The caller is responsible for
 *         freeing this memory with `safefree`.
 */
wchar_t * utf2wchar(pTHX_ SV * src, size_t len) {
    // Allocate enough space for the wide characters and a null terminator.
    wchar_t * RETVAL = (wchar_t *)safemalloc((len + 1) * sizeof(wchar_t));
    if (!RETVAL)
        return NULL;

#if defined(INFIX_OS_WINDOWS)
    // On Windows, convert from UTF-8 to UTF-16.
    const char * utf8_str = SvPV_nolen(src);
    int ret = MultiByteToWideChar(CP_UTF8, 0, utf8_str, len, RETVAL, len + 1);
    RETVAL[ret] = L'\0';  // Ensure null termination

#else
    // On POSIX-like systems, use mbstowcs which respects the current locale.
    // This assumes the locale is set to a UTF-8 variant.
    if (SvUTF8(src)) {
        STRLEN utf8_len;
        char * raw = SvPVutf8(src, utf8_len);
        // `mbstowcs` converts the multibyte string to a wide character string.
        mbstowcs(RETVAL, raw, len + 1);
    }
    else {
        // Handle non-UTF8 SVs as plain byte strings.
        STRLEN raw_len;
        char * raw = SvPV(src, raw_len);
        mbstowcs(RETVAL, raw, len + 1);
    }
#endif
    return RETVAL;
}
