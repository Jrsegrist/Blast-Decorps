#ifndef VERSION_H
#define VERSION_H

/* The ROM version being built (docs/versions.md). The Makefile passes
 * -DVERSION_US_V10=1 or -DVERSION_US_V11=1; a build that passes neither
 * (the port) is US 1.1. Code that differs between versions is written
 *     #if VERSION_US_V10 ... #else ... #endif
 * with US 1.1 (the reference version) in the #else. */
#if !defined(VERSION_US_V10) && !defined(VERSION_US_V11)
#define VERSION_US_V11 1
#endif
#ifndef VERSION_US_V10
#define VERSION_US_V10 0
#endif
#ifndef VERSION_US_V11
#define VERSION_US_V11 0
#endif

/* Raw addresses the matched code spells as numbers are US 1.1's. hd_code's
 * .data sits 0xB0 lower in US 1.0 (its .text is 0xB0 shorter), and the
 * Japanese text tables from US 1.1 0x8030360C to 0x80304A8C 0xB4 lower
 * (the words just in front of them were edited between the versions). */
#if VERSION_US_V10
#define HD_JTEXT_ADDR(a) ((a) - 0xB4)
#else
#define HD_JTEXT_ADDR(a) (a)
#endif

#endif /* VERSION_H */
