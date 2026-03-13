// tinyexr implementation file — compiled once to provide the library symbols.
// Use STB's zlib instead of miniz (we already have stb in the project).

#define TINYEXR_USE_MINIZ 0
#define TINYEXR_USE_STB_ZLIB 1

// stb_image provides stbi_zlib_decode_buffer
// stb_image_write provides stbi_zlib_compress
// We need to declare them before tinyexr implementation
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

// stb_image is already implemented elsewhere, just declare the function
extern "C" int stbi_zlib_decode_buffer(char* obuffer, int olen, const char* ibuffer, int ilen);

#define TINYEXR_IMPLEMENTATION
#include "tinyexr.h"
