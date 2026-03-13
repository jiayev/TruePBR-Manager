// tinyexr implementation file.
// Uses STB's zlib functions (stbi_zlib_decode_buffer from stb_image,
// stbi_zlib_compress from stb_image_write) instead of miniz.
// The STB functions are compiled in separate TUs to avoid linkage conflicts.

#define TINYEXR_USE_MINIZ 0
#define TINYEXR_USE_STB_ZLIB 1
#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"
