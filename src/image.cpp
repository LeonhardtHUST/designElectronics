// designElectronics — pictures: format sniffing, size limits, content keys, the
// decode/rasterize path and the texture cache. See image.h for the policy.
//
// The vendored headers are only *declared* here; their implementations live in
// image_impl.cpp (one translation unit, as those libraries require).
#include "image.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_MAX_DIMENSIONS 16384
#include "stb_image.h"
#include "stb_image_write.h"
#include "stb_image_resize2.h"

#include <webp/decode.h>

#include "nanosvg.h"
#include "nanosvgrast.h"

namespace {

bool endsWithNoCase(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++) {
        char a = (char)std::tolower((unsigned char)s[s.size() - n + i]);
        char b = (char)std::tolower((unsigned char)suffix[i]);
        if (a != b) return false;
    }
    return true;
}

// "<svg" as the first element, after an optional BOM, XML declaration, comments and
// DOCTYPE. Looking for the tag instead of a magic number is the only way to tell SVG
// from any other XML/HTML; we only scan the head of the file.
bool looksLikeSvg(const std::string& b) {
    size_t n = std::min(b.size(), (size_t)16384);
    std::string low = b.substr(0, n);
    for (auto& c : low) c = (char)std::tolower((unsigned char)c);
    size_t p = 0;
    if (low.size() >= 3 && (unsigned char)low[0] == 0xEF && (unsigned char)low[1] == 0xBB && (unsigned char)low[2] == 0xBF) p = 3;
    auto skipSpace = [&]() { while (p < low.size() && std::isspace((unsigned char)low[p])) p++; };
    skipSpace();
    if (low.compare(p, 5, "<?xml") == 0) {
        size_t e = low.find("?>", p + 5);
        if (e == std::string::npos) return false;
        p = e + 2;
    }
    for (;;) {
        skipSpace();
        if (low.compare(p, 4, "<!--") == 0) {
            size_t e = low.find("-->", p + 4);
            if (e == std::string::npos) return false;
            p = e + 3;
            continue;
        }
        if (low.compare(p, 9, "<!doctype") == 0) {
            size_t e = low.find('>', p);
            if (e == std::string::npos) return false;
            p = e + 1;
            continue;
        }
        if (low.compare(p, 2, "<?") == 0) {  // processing instruction of any kind
            size_t e = low.find("?>", p + 2);
            if (e == std::string::npos) return false;
            p = e + 2;
            continue;
        }
        break;
    }
    if (low.compare(p, 4, "<svg") != 0) return false;
    if (p + 4 >= low.size()) return true;
    char c = low[p + 4];
    return c == '>' || c == '/' || std::isspace((unsigned char)c);
}

// nanosvg's XML parser writes NUL bytes into its input, so it needs a private,
// mutable copy. The returned image owns its own copies of the shape data.
NSVGimage* parseSvg(const std::string& text) {
    std::vector<char> buf(text.begin(), text.end());
    buf.push_back('\0');
    return nsvgParse(buf.data(), "px", 96.0f);
}

void writeToMem(void* ctx, void* data, int size) {
    if (size > 0) ((std::string*)ctx)->append((const char*)data, (size_t)size);
}

bool bitmapInfo(const std::string& mime, const std::string& bytes, int& w, int& h) {
    w = h = 0;
    if (bytes.size() < 4) return false;
    if (mime == "image/webp") {
        WebPBitstreamFeatures f;
        std::memset(&f, 0, sizeof f);
        // VP8_STATUS_OK is 0: the status enum is *not* a boolean.
        if (WebPGetFeatures((const uint8_t*)bytes.data(), bytes.size(), &f) != VP8_STATUS_OK) return false;
        w = f.width;
        h = f.height;
        return w > 0 && h > 0;
    }
    int ch = 0;
    if (!stbi_info_from_memory((const stbi_uc*)bytes.data(), (int)bytes.size(), &w, &h, &ch)) return false;
    return w > 0 && h > 0;
}

// Decodes a bitmap to RGBA8, scaling it down to `cap` on the longest side when it is
// bigger. No scaling up: a small picture is drawn small.
bool decodeBitmap(const std::string& mime, const std::string& bytes, int cap, int64_t maxPixels, std::vector<uint8_t>& rgba, int& w,
                  int& h) {
    rgba.clear();
    w = h = 0;
    int iw = 0, ih = 0;
    unsigned char* data = nullptr;
    const bool webp = mime == "image/webp";
    if (webp) {
        WebPBitstreamFeatures f;
        std::memset(&f, 0, sizeof f);
        if (WebPGetFeatures((const uint8_t*)bytes.data(), bytes.size(), &f) != VP8_STATUS_OK) return false;
        iw = f.width;
        ih = f.height;
        if (iw <= 0 || ih <= 0 || (int64_t)iw * ih > maxPixels) return false;
        data = WebPDecodeRGBA((const uint8_t*)bytes.data(), bytes.size(), &iw, &ih);
    } else {
        int ch = 0;
        if (!stbi_info_from_memory((const stbi_uc*)bytes.data(), (int)bytes.size(), &iw, &ih, &ch)) return false;
        if (iw <= 0 || ih <= 0 || (int64_t)iw * ih > maxPixels) return false;
        data = stbi_load_from_memory((const stbi_uc*)bytes.data(), (int)bytes.size(), &iw, &ih, &ch, 4);
    }
    if (!data) return false;
    int ow = iw, oh = ih;
    if (std::max(iw, ih) > cap) {
        float scale = (float)cap / (float)std::max(iw, ih);
        ow = std::max(1, (int)std::lround((float)iw * scale));
        oh = std::max(1, (int)std::lround((float)ih * scale));
    }
    bool ok = false;
    if (ow == iw && oh == ih) {
        rgba.assign(data, data + (size_t)iw * (size_t)ih * 4);
        ok = true;
    } else {
        std::vector<uint8_t> out((size_t)ow * (size_t)oh * 4);
        // sRGB-aware filtering: photos and anti-aliased graphics both resize well.
        if (stbir_resize_uint8_srgb(data, iw, ih, 0, out.data(), ow, oh, 0, STBIR_RGBA)) {
            rgba.swap(out);
            ok = true;
        }
    }
    if (webp) WebPFree(data); else stbi_image_free(data);
    if (ok) { w = ow; h = oh; }
    return ok;
}

// ---- SHA-1 (content keys) --------------------------------------------------
inline uint32_t rotl32(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

void sha1Block(const unsigned char* b, uint32_t* h) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)b[i * 4] << 24) | ((uint32_t)b[i * 4 + 1] << 16) | ((uint32_t)b[i * 4 + 2] << 8) |
               (uint32_t)b[i * 4 + 3];
    for (int i = 16; i < 80; i++) {
        uint32_t v = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
        w[i] = rotl32(v, 1);
    }
    uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (bb & c) | (~bb & d); k = 0x5A827999u; }
        else if (i < 40) { f = bb ^ c ^ d; k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (bb & c) | (bb & d) | (c & d); k = 0x8F1BBCDCu; }
        else { f = bb ^ c ^ d; k = 0xCA62C1D6u; }
        uint32_t t = rotl32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rotl32(bb, 30);
        bb = a;
        a = t;
    }
    h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e;
}

std::string sha1Hex(const std::string& data) {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    const unsigned char* p = (const unsigned char*)data.data();
    size_t size = data.size();
    size_t i = 0;
    for (; i + 64 <= size; i += 64) sha1Block(p + i, h);
    unsigned char tail[128];
    std::memset(tail, 0, sizeof tail);
    size_t rem = size - i;
    if (rem) std::memcpy(tail, p + i, rem);
    tail[rem] = 0x80;
    size_t tailLen = rem + 1 + 8 <= 64 ? 64 : 128;
    uint64_t bits = (uint64_t)size * 8;
    for (int k = 0; k < 8; k++) tail[tailLen - 1 - (size_t)k] = (unsigned char)(bits >> (8 * k));
    sha1Block(tail, h);
    if (tailLen == 128) sha1Block(tail + 64, h);
    char out[41];
    for (int k = 0; k < 5; k++) std::snprintf(out + k * 8, 9, "%08x", h[k]);
    return std::string(out, 40);
}

}  // namespace

// ---------------------------------------------------------------------------
// Sniffing
// ---------------------------------------------------------------------------

const char* imageMimeFromBytes(const std::string& b, const std::string& hintName) {
    auto at = [&](size_t i) { return i < b.size() ? (unsigned char)b[i] : (unsigned char)0; };
    if (b.size() >= 8 && at(0) == 0x89 && at(1) == 'P' && at(2) == 'N' && at(3) == 'G' && at(4) == 0x0D &&
        at(5) == 0x0A && at(6) == 0x1A && at(7) == 0x0A)
        return "image/png";
    if (b.size() >= 3 && at(0) == 0xFF && at(1) == 0xD8 && at(2) == 0xFF) return "image/jpeg";
    if (b.size() >= 6 && (b.compare(0, 6, "GIF87a") == 0 || b.compare(0, 6, "GIF89a") == 0)) return "image/gif";
    if (b.size() >= 2 && at(0) == 'B' && at(1) == 'M') return "image/bmp";
    if (b.size() >= 12 && b.compare(0, 4, "RIFF") == 0 && b.compare(8, 4, "WEBP") == 0) return "image/webp";
    if (b.size() >= 4 && b.compare(0, 4, "8BPS") == 0) return "image/vnd.adobe.photoshop";
    if (looksLikeSvg(b)) return "image/svg+xml";
    // TGA has no signature at all: only the name can tell, and only as a last resort.
    if (endsWithNoCase(hintName, ".tga") || endsWithNoCase(hintName, ".targa")) return "image/x-tga";
    return "";
}

std::string imageFormatLabel(const std::string& mime) {
    if (mime == "image/png") return "PNG";
    if (mime == "image/jpeg") return "JPEG";
    if (mime == "image/gif") return "GIF";
    if (mime == "image/bmp") return "BMP";
    if (mime == "image/webp") return "WebP";
    if (mime == "image/vnd.adobe.photoshop") return "PSD";
    if (mime == "image/x-tga") return "TGA";
    if (mime == "image/svg+xml") return "SVG";
    return "?";
}

// ---------------------------------------------------------------------------
// Import
// ---------------------------------------------------------------------------

std::string imageKey(const ImageBlob& blob) { return sha1Hex(blob.bytes); }

bool imageImport(const std::string& bytes, const std::string& hintName, ImageBlob* out, std::string* err,
                 std::string* note) {
    if (note) note->clear();
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    if (err) err->clear();
    if (!out) return fail("No destination for the picture.");
    *out = ImageBlob();
    if (bytes.empty()) return fail("The picture is empty.");
    // A gzip stream is either a compressed SVG (which we don't read) or not a picture.
    if (bytes.size() >= 2 && (unsigned char)bytes[0] == 0x1F && (unsigned char)bytes[1] == 0x8B)
        return fail("Compressed SVG (.svgz) is not supported - unzip it first.");
    const char* mime = imageMimeFromBytes(bytes, hintName);
    if (!mime || !*mime)
        return fail("This file is not a picture designElectronics can read (PNG, JPEG, GIF, BMP, WebP, PSD or SVG).");
    const bool vector = std::strcmp(mime, "image/svg+xml") == 0;

    if (vector) {
        if (bytes.size() > kMaxSvgBytes) return fail("The SVG is larger than 5 MB.");
        NSVGimage* svg = parseSvg(bytes);
        if (!svg) return fail("The SVG could not be read.");
        float sw = svg->width, sh = svg->height;
        nsvgDelete(svg);
        if (!(sw > 0) || !(sh > 0)) return fail("The SVG has no size - add width/height or a viewBox.");
        int iw = std::max(1, (int)std::lround(sw)), ih = std::max(1, (int)std::lround(sh));
        out->vector = true;
        out->mime = "image/svg+xml";
        out->bytes = bytes;
        if (std::max(iw, ih) > kMaxImageSide) {  // keep the intrinsic size sane
            double scale = (double)kMaxImageSide / (double)std::max(iw, ih);
            int cw = std::max(1, (int)std::lround(iw * scale)), chh = std::max(1, (int)std::lround(ih * scale));
            if (note) *note = "Scaled from " + std::to_string(iw) + "x" + std::to_string(ih) + " to " +
                              std::to_string(cw) + "x" + std::to_string(chh) + " px.";
            iw = cw;
            ih = chh;
        }
        out->w = iw;
        out->h = ih;
        return true;
    }

    if (bytes.size() > kMaxImageBytes) return fail("The picture is larger than 20 MB.");
    int w = 0, h = 0;
    if (!bitmapInfo(mime, bytes, w, h)) return fail("The picture is damaged or truncated.");
    if ((int64_t)w * h > kMaxImportPixels) return fail("The picture has too many pixels to import (over 64 megapixels).");
    out->mime = mime;
    if (std::max(w, h) <= kMaxImageSide) {
        // Small enough: keep the bytes exactly as they came in, format and all.
        out->bytes = bytes;
        out->w = w;
        out->h = h;
        return true;
    }
    // Too big for the canvas: scale the longest side to 4096 and re-encode.
    std::vector<uint8_t> rgba;
    int sw = 0, sh = 0;
    if (!decodeBitmap(mime, bytes, kMaxImageSide, kMaxImportPixels, rgba, sw, sh)) return fail("The picture could not be decoded.");
    bool alpha = false;
    for (size_t i = 3; i < rgba.size(); i += 4)
        if (rgba[i] != 255) { alpha = true; break; }
    std::string encoded;
    const char* newMime = "";
    if (alpha) {
        if (imageEncodePng(rgba.data(), sw, sh, 4, &encoded)) newMime = "image/png";
    } else {
        // No transparency: JPEG keeps a photograph's file size reasonable.
        std::vector<uint8_t> rgb((size_t)sw * (size_t)sh * 3);
        for (size_t i = 0, n = (size_t)sw * (size_t)sh; i < n; i++) {
            rgb[i * 3] = rgba[i * 4];
            rgb[i * 3 + 1] = rgba[i * 4 + 1];
            rgb[i * 3 + 2] = rgba[i * 4 + 2];
        }
        encoded.clear();
        if (stbi_write_jpg_to_func(writeToMem, &encoded, sw, sh, 3, rgb.data(), 90) != 0) newMime = "image/jpeg";
    }
    if (!newMime || encoded.empty()) return fail("The picture could not be re-encoded after scaling.");
    if (encoded.size() > kMaxImageBytes)
        return fail("The picture is still larger than 20 MB after scaling it down to 4096 px.");
    out->mime = newMime;
    out->bytes = std::move(encoded);
    out->w = sw;
    out->h = sh;
    if (note) *note = "Scaled from " + std::to_string(w) + "x" + std::to_string(h) + " to " + std::to_string(sw) + "x" +
                      std::to_string(sh) + " px.";
    return true;
}

// ---------------------------------------------------------------------------
// Decoding / rasterizing
// ---------------------------------------------------------------------------

bool imageDecodeRGBA(const ImageBlob& blob, int maxSide, std::vector<uint8_t>& rgba, int& w, int& h) {
    rgba.clear();
    w = h = 0;
    if (blob.bytes.empty()) return false;
    int cap = maxSide > 0 ? std::clamp(maxSide, 1, kMaxImageSide) : kMaxImageSide;
    if (blob.vector) {
        NSVGimage* svg = parseSvg(blob.bytes);
        if (!svg) return false;
        float sw = svg->width > 0 ? svg->width : 1.f;
        float sh = svg->height > 0 ? svg->height : 1.f;
        float scale = (float)cap / std::max(sw, sh);
        int tw = std::max(1, (int)std::lround(sw * scale));
        int th = std::max(1, (int)std::lround(sh * scale));
        if ((int64_t)tw * th > kMaxImagePixels) {
            nsvgDelete(svg);
            return false;
        }
        NSVGrasterizer* rast = nsvgCreateRasterizer();
        if (!rast) {
            nsvgDelete(svg);
            return false;
        }
        std::vector<uint8_t> px((size_t)tw * (size_t)th * 4, 0);
        nsvgRasterize(rast, svg, 0, 0, scale, px.data(), tw, th, tw * 4);
        nsvgDeleteRasterizer(rast);
        nsvgDelete(svg);
        rgba.swap(px);
        w = tw;
        h = th;
        return true;
    }
    return decodeBitmap(blob.mime, blob.bytes, cap, kMaxImagePixels, rgba, w, h);
}

bool imageEncodePng(const uint8_t* pixels, int w, int h, int channels, std::string* out) {
    if (!pixels || w <= 0 || h <= 0 || (channels != 3 && channels != 4) || !out) return false;
    out->clear();
    return stbi_write_png_to_func(writeToMem, out, w, h, channels, pixels, w * channels) != 0;
}

// ---------------------------------------------------------------------------
// Texture cache
// ---------------------------------------------------------------------------

void ImageCache::setRenderer(SDL_Renderer* r) {
    if (r == r_) return;
    clear();
    r_ = r;
}

int ImageCache::bucketFor(const ImageBlob& blob, int wantedSide) {
    int nat = std::max(blob.w, blob.h);
    int side = wantedSide > 0 ? wantedSide : nat;
    side = std::clamp(side, 1, kMaxImageSide);
    int bucket = 64;
    while (bucket < side && bucket < kMaxImageSide) bucket <<= 1;
    if (bucket > kMaxImageSide) bucket = kMaxImageSide;
    if (!blob.vector) {
        // A bitmap never needs more pixels than it has: scaling beyond that is the
        // renderer's job (and it is linear-filtered anyway).
        int cap = 1;
        while (cap < nat && cap < kMaxImageSide) cap <<= 1;
        cap = std::min(std::max(cap, 1), kMaxImageSide);
        bucket = std::min(bucket, cap);
    }
    return std::max(bucket, 1);
}

SDL_Texture* ImageCache::get(const ImageBlob& blob, int wantedSide) {
    if (!r_ || blob.bytes.empty()) return nullptr;
    return get(imageKey(blob), blob, wantedSide);
}

SDL_Texture* ImageCache::get(const std::string& contentKey, const ImageBlob& blob, int wantedSide) {
    if (!r_ || blob.bytes.empty()) return nullptr;
    const int bucket = bucketFor(blob, wantedSide);
    std::string key = contentKey;
    key += '@';
    key += std::to_string(bucket);
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        lru_.splice(lru_.begin(), lru_, it->second.lru);
        return it->second.tex;
    }
    if (failed_.count(key)) return nullptr;  // a broken picture is a placeholder, not re-decoded each frame
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    SDL_Texture* tex = nullptr;
    if (imageDecodeRGBA(blob, bucket, rgba, w, h) && w > 0 && h > 0)
        tex = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (tex && !SDL_UpdateTexture(tex, nullptr, rgba.data(), w * 4)) {
        SDL_DestroyTexture(tex);
        tex = nullptr;
    }
    if (!tex) {
        failed_[key] = true;
        return nullptr;
    }
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    Entry e;
    e.tex = tex;
    e.bytes = (size_t)w * (size_t)h * 4;
    lru_.push_front(key);
    e.lru = lru_.begin();
    used_ += e.bytes;
    entries_[key] = e;
    while (used_ > budget_ && entries_.size() > 1) evictOne();
    return tex;
}

void ImageCache::evictOne() {
    if (entries_.size() <= 1 || lru_.empty()) return;
    const std::string& key = lru_.back();
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        SDL_DestroyTexture(it->second.tex);
        used_ -= std::min(used_, it->second.bytes);
        entries_.erase(it);
    }
    lru_.pop_back();
}

void ImageCache::clear() {
    for (auto& kv : entries_) SDL_DestroyTexture(kv.second.tex);
    entries_.clear();
    lru_.clear();
    failed_.clear();
    used_ = 0;
}

void ImageCache::setBudgetBytes(size_t b) {
    budget_ = b;
    while (used_ > budget_ && entries_.size() > 1) evictOne();
}
