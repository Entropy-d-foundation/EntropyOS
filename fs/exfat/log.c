/*
	log.c (02.09.09)
	exFAT file system implementation library.

	Free exFAT implementation.
	Copyright (C) 2010-2023  Andrew Nayenko

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "exfat.h"
#include <stdarg.h>

#ifndef __UEFI__
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>
#endif

#ifdef __UEFI__
#include "../../kernel/console.h"      /* text() */
#endif
#ifdef __ANDROID__
#include <android/log.h>
#else
#ifndef __UEFI__
#include <syslog.h>
#include <unistd.h>
#endif
#endif

int exfat_errors;

/*
 * This message means an internal bug in exFAT implementation.
 */
void exfat_bug(const char* format, ...)
{
    va_list ap;

    va_start(ap, format);
#ifndef __UEFI__
    fflush(stdout);
    fputs("BUG: ", stderr);
    vfprintf(stderr, format, ap);
    fputs(".\n", stderr);
#else
    /* simple UEFI path: just print format string literally */
    text("BUG: ");
    if (format) text(format);
    text(".\n");
#endif

#ifdef __ANDROID__
    __android_log_vprint(ANDROID_LOG_FATAL, PACKAGE, format, ap);
#else
#ifndef __UEFI__
    if (!isatty(STDERR_FILENO))
        vsyslog(LOG_CRIT, format, ap);
#endif
#endif
    va_end(ap);

#ifndef __UEFI__
    abort();
#else
    /* on UEFI we cannot abort; just hang */
    while (1) __asm__ volatile ("hlt");
#endif
}

/*
 * This message means an error in exFAT file system.
 */
void exfat_error(const char* format, ...)
{
    va_list ap;

    exfat_errors++;
    va_start(ap, format);
#ifndef __UEFI__
    fflush(stdout);
    fputs("ERROR: ", stderr);
    vfprintf(stderr, format, ap);
    fputs(".\n", stderr);
#else
    text("ERROR: ");
    if (format) text(format);
    text(".\n");
#endif

#ifdef __ANDROID__
    __android_log_vprint(ANDROID_LOG_ERROR, PACKAGE, format, ap);
#endif
    va_end(ap);
}

/*
 * This message means that there is something unexpected in exFAT file system
 * that can be a potential problem.
 */
void exfat_warn(const char* format, ...)
{
    va_list ap;

    va_start(ap, format);
#ifndef __UEFI__
    fflush(stdout);
    fputs("WARN: ", stderr);
    vfprintf(stderr, format, ap);
    fputs(".\n", stderr);
#else
    text("WARN: ");
    if (format) text(format);
    text(".\n");
#endif

#ifdef __ANDROID__
    __android_log_vprint(ANDROID_LOG_WARN, PACKAGE, format, ap);
#endif
    va_end(ap);
}

/*
 * Just debug message. Disabled by default.
 */
void exfat_debug(const char* format, ...)
{
    va_list ap;

    va_start(ap, format);
#ifndef __UEFI__
    fflush(stdout);
    fputs("DEBUG: ", stderr);
    vfprintf(stderr, format, ap);
    fputs(".\n", stderr);
#else
    text("DEBUG: ");
    if (format) text(format);
    text(".\n");
#endif

#ifdef __ANDROID__
    __android_log_vprint(ANDROID_LOG_DEBUG, PACKAGE, format, ap);
#endif
    va_end(ap);
}
