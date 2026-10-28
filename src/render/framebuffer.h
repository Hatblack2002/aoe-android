#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct SDL_Renderer;
struct SDL_Texture;

namespace render {

// Composición por software (spec §3): búfer propio ARGB8888 que se sube a una
// textura SDL cada cuadro. Predecible, sin GPU, y genera imágenes de prueba
// sin ventana (rendertest/simtest lo reutilizarán en M1+).
class Framebuffer {
public:
    ~Framebuffer();
    bool init(SDL_Renderer* renderer, int width, int height);

    void clear(uint8_t r, uint8_t g, uint8_t b);
    void fillRect(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b);
    void drawText(int x, int y, const std::string& texto,
                  uint8_t r, uint8_t g, uint8_t b, int escala = 1);

    // Vuelta un bufer RGBA8888 (orden por filas) con escala entera; los
    // pixeles con alfa 0 se dejan transparentes. Recorta lo que salga.
    void blitRgba(int x, int y, int ancho, int alto, const uint8_t* rgba, int escala = 1);

    bool present();  // sube la textura, RenderCopy y Present; false si algo falló
    bool saveBmp(const std::string& ruta);

    int width() const { return w_; }
    int height() const { return h_; }

private:
    inline void setPixel(int x, int y, uint32_t color);
    std::vector<uint32_t> pixels_;  // 0xAARRGGBB (little-endian, igual a ARGB8888)
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    int w_ = 0;
    int h_ = 0;
};

} // namespace render
