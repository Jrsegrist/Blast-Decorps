/* Force-included before every ultralib object of the MSVC build
 * (port/CMakeLists.txt; the game files get the same from port_ultratypes.h):
 * the SDK's own sinf/cosf, never MSVC's intrinsic forms. */
float sinf(float);
float cosf(float);
#pragma function(sinf, cosf)
