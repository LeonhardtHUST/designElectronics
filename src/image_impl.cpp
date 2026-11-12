// designElectronics — the vendored single-header image libraries, expanded once.
//
// Each of these headers is a "compile it in exactly one translation unit" library,
// so every implementation macro lives here and nowhere else: other files only
// include the declarations (see image.cpp). Building them as C++ keeps MSVC,
// clang and Emscripten on one code path.
//
//   stb_image         v2.31  - public domain (MIT alternative)
//   stb_image_write   v1.17  - public domain (MIT alternative)
//   stb_image_resize2 v2.18  - public domain (MIT alternative)
//   nanosvg / nanosvgrast    - zlib licence, see third_party/nanosvg/LICENSE.txt
//
// The format sniffing, the size limits and the re-encoding policy live in
// image.cpp; this file only provides the raw decoders/encoders.

// stb_image: memory-only (no stdio), no HDR/PIC/PNM. STBI_MAX_DIMENSIONS keeps a
// crafted 100000x100000 PNG from asking for a 40 GB buffer; anything above it is
// refused instead of decoded.
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_MAX_DIMENSIONS 16384

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"

#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"
