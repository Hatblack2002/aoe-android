#include "render/framebuffer.h"

#include "render/font8x8.h"

#include <SDL.h>
#include <algorithm>
#include <cstring>

namespace render {

Framebuffer::~Framebuffer() {
    if (texture_) SDL_DestroyTexture(texture_);
}

bool Framebuffer::init(SDL_Renderer* renderer, int width, int height) {
    renderer_ = renderer;
    w_ = width;
    h_ = height;
    pixels_.assign(static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_), 0xFF000000u);
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_STREAMING, w_, h_);
    return texture_ != nullptr;
}

inline void Framebuffer::setPixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
    pixels_[static_cast<std::size_t>(y) * w_ + x] = color;
}

void Framebuffer::clear(uint8_t r, uint8_t g, uint8_t b) {
    const uint32_t px =
        0xFF000000u | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
    std::fill(pixels_.begin(), pixels_.end(), px);
}

void Framebuffer::fillRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    const uint32_t px =
        0xFF000000u | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
    const int x0 = std::max(x, 0);
    const int y0 = std::max(y, 0);
    const int x1 = std::min(x + w, w_);
    const int y1 = std::min(y + h, h_);
    for (int yy = y0; yy < y1; ++yy) {
        uint32_t* fila = pixels_.data() + static_cast<std::size_t>(yy) * w_;
        for (int xx = x0; xx < x1; ++xx) fila[xx] = px;
    }
}

// Fuente embebida font8x8 (dominio público, Daniel Hepper). Solo ASCII 32..127;
// cualquier otro carácter se dibuja como '?'. Cada glifo: 8 filas de 8 bits,
// bit gx = columna izquierda-derecha.
void Framebuffer::drawText(int x, int y, const std::string& texto,
                           uint8_t r, uint8_t g, uint8_t b, int escala) {
    if (escala < 1) escala = 1;
    const uint32_t col =
        0xFF000000u | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
    int cx = x;
    for (char ch : texto) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (c < 32 || c > 127) c = '?';
        const unsigned char* glifo = font8x8_basic[c];
        for (int gy = 0; gy < 8; ++gy) {
            const unsigned char bits = static_cast<unsigned char>(glifo[gy]);
            if (!bits) continue;
            for (int gx = 0; gx < 8; ++gx) {
                if (bits & (1u << gx)) {
                    for (int sy = 0; sy < escala; ++sy)
                        for (int sx = 0; sx < escala; ++sx)
                            setPixel(cx + gx * escala + sx, y + gy * escala + sy, col);
                }
            }
        }
        cx += 8 * escala;
    }
}

bool Framebuffer::present() {
    if (!texture_) return false;
    if (SDL_UpdateTexture(texture_, nullptr, pixels_.data(), w_ * 4) != 0) return false;
    if (SDL_RenderCopy(renderer_, texture_, nullptr, nullptr) != 0) return false;
    SDL_RenderPresent(renderer_);
    return true;
}

bool Framebuffer::saveBmp(const std::string& ruta) {
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(pixels_.data(), w_, h_, 32, w_ * 4,
                                                        SDL_PIXELFORMAT_ARGB8888);
    if (!s) return false;
    const bool ok = SDL_SaveBMP(s, ruta.c_str()) == 0;
    SDL_FreeSurface(s);
    return ok;
}

} // namespace render
