// Provides stbi_zlib_compress needed by tinyexr when TINYEXR_USE_STB_ZLIB=1.
// Compiled separately to avoid linkage conflicts with tinyexr's extern "C" declarations.

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
