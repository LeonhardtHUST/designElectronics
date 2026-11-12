// designElectronics — pictures in a design.
//
// A design can carry pictures (logos, pinouts, board photos, screenshots) and put
// them on the canvas as image annotations. The picture itself is stored once,
// content addressed by the SHA-1 of its bytes, and every annotation points at that
// key: importing the same file twice costs one copy, and an undo snapshot only has
// to remember the key (see Document::images).
//
// Import policy (see imageImport):
//   * the format is sniffed from the bytes, never trusted from the file name:
//     PNG, JPEG, GIF (first frame), BMP, WebP, PSD, SVG; TGA by extension only
//     (it has no magic number);
//   * at most 20 MB of encoded bytes and 4096 px on the longest side. A larger
//     bitmap is scaled down and re-encoded (PNG while it has transparency, JPEG
//     otherwise); a picture that is still over 20 MB afterwards is refused;
//   * SVG is kept as text (at most 5 MB) and rasterized on demand, so it stays
//     sharp at every zoom level.
// Not supported: AVIF, HEIC/HEIF, JPEG XL, animated GIF beyond the first frame,
// compressed SVG (.svgz) — see docs/FILE_FORMAT.md.
#pragma once
#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

// One picture: the encoded bytes exactly as they came in (or as they were
// re-encoded after scaling) plus what is needed to lay it out.
struct ImageBlob {
    std::string mime;   // "image/png", "image/jpeg", … "image/svg+xml"
    std::string bytes;  // encoded bytes; SVG text when vector
    int w = 0;          // intrinsic width in pixels (SVG: its own width/viewBox)
    int h = 0;          // intrinsic height in pixels
    bool vector = false;  // SVG: rasterized at the size the canvas needs
};

// Limits, all in one place so the import, the loader and the UI can agree.
constexpr size_t kMaxImageBytes = 20u * 1024u * 1024u;  // encoded bytes of one picture
constexpr size_t kMaxSvgBytes = 5u * 1024u * 1024u;    // SVG markup
constexpr int kMaxImageSide = 4096;                     // longest side, bitmaps and rasters
constexpr int kMaxImagePixels = 32 * 1024 * 1024;        // decoded pixels before scaling (128 MB of RGBA)
// Import alone may decode a bigger photo once to scale it down: a 48-50 MP phone camera
// picture (8160x6120) must import, a decompression bomb must not.
constexpr int kMaxImportPixels = 64 * 1024 * 1024;

// "image/png" for the known magic numbers, "" when the bytes are not a picture we
// can read. Pure sniffing: the name is not consulted (TGA is the exception — the
// format has no signature, so a .tga/.targa hint is believed).
const char* imageMimeFromBytes(const std::string& bytes, const std::string& hintName = std::string());

// Short, locale-free name of a format ("PNG", "JPEG", "SVG", …; "?" for an unknown
// mime). The interface uses it in the inspector and the Layers panel.
std::string imageFormatLabel(const std::string& mime);

// Reads a picture. Fills *out on success; on failure returns false and sets *err
// to something the interface can show. *note may receive a remark about what was
// done to the picture (for example that it was scaled down).
bool imageImport(const std::string& bytes, const std::string& hintName, ImageBlob* out, std::string* err,
                 std::string* note = nullptr);

// Content key: SHA-1 of the bytes as lowercase hex. Equal pictures share a key, so
// Document::images stores each picture once and undo snapshots stay small.
std::string imageKey(const ImageBlob& blob);

// Decodes to straight (non-premultiplied) RGBA8 for a texture. maxSide caps the
// longest side (0 = up to kMaxImageSide); SVG is rasterized at that size, a bitmap
// is scaled down when it is bigger, never scaled up.
bool imageDecodeRGBA(const ImageBlob& blob, int maxSide, std::vector<uint8_t>& rgba, int& w, int& h);

// Encodes raw pixels as a PNG. Used by the self-test to build pictures in memory
// and by anything that needs to store a generated picture. channels is 3 (RGB) or
// 4 (RGBA). Returns false on bad input or out of memory.
bool imageEncodePng(const uint8_t* pixels, int w, int h, int channels, std::string* out);

// ---------------------------------------------------------------------------
// Texture cache: (content key, size bucket) -> SDL_Texture, with an LRU cap so a
// design full of photos cannot eat the GPU's memory. SVG is rasterized per bucket,
// so a symbol stays crisp while zooming instead of being stretched.
// Every call is safe without a renderer (headless self-test): it returns nullptr.
// ---------------------------------------------------------------------------
class ImageCache {
public:
    void setRenderer(SDL_Renderer* r);  // call after the renderer is created / replaced
    SDL_Renderer* renderer() const { return r_; }

    // The texture for `blob` at roughly `wantedSide` pixels (0 = intrinsic size).
    // nullptr when there is no renderer or the picture cannot be decoded.
    SDL_Texture* get(const ImageBlob& blob, int wantedSide);
    // Same, with the content key the caller already has (Anno::image). Drawing calls this
    // every frame, so it must not hash megabytes of picture bytes each time.
    SDL_Texture* get(const std::string& key, const ImageBlob& blob, int wantedSide);

    void clear();                     // destroy every cached texture
    void setBudgetBytes(size_t b);    // default 96 MB of RGBA
    size_t usedBytes() const { return used_; }
    size_t count() const { return entries_.size(); }

    // The pixel size this cache will rasterize/cache for a wanted side: a power of
    // two, never above kMaxImageSide and never above a bitmap's own size.
    static int bucketFor(const ImageBlob& blob, int wantedSide);

private:
    struct Entry {
        SDL_Texture* tex = nullptr;
        size_t bytes = 0;
        std::list<std::string>::iterator lru;
    };
    SDL_Renderer* r_ = nullptr;
    size_t budget_ = 96u * 1024u * 1024u;
    size_t used_ = 0;
    std::unordered_map<std::string, Entry> entries_;
    std::list<std::string> lru_;
    // (key, bucket) pairs that failed to decode: not retried every frame.
    std::unordered_map<std::string, bool> failed_;
    void evictOne();
};
