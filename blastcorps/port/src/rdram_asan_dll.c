/* bc_rdram.dll: N64 memory for the AddressSanitizer build (CMake BC_ASAN).
 *
 * ASan's run time places its shadow memory in the first large free range of
 * the address space while its DLL initialises, before any code of the exe
 * runs; that range starts at 0x7FFF0000 (just above KUSER_SHARED_DATA) and so
 * covers 0x80000000, where rdram.c would reserve the N64's memory.  This DLL
 * has no code and one section holding the 8 MB array; it is linked at a fixed
 * base of 0x7FFF0000 with 64 KB section alignment, so the section starts at
 * 0x80000000.  (A read-only section: link.exe puts it first, before writable
 * data; the export directory's few bytes come before the array, which is why
 * rdram.c makes the whole range 0x80000000-0x80800000 writable and clears it
 * rather than use the array's address.)  The loader maps the DLL with the
 * exe's other imports, before any DLL initialises, and ASan then puts its
 * shadow elsewhere.  rdram.c uses it instead of VirtualAlloc. */
#pragma section(".rdata$0", read)
__declspec(dllexport) __declspec(allocate(".rdata$0")) unsigned char bc_rdram[0x800000];
