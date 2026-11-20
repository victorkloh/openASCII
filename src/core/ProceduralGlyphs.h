#pragma once
// Glifos desenhados à mão, píxel a píxel, em vez de vir da fonte: blocos (░▒▓█▀▄...),
// linhas de caixa (─│┌┐...) e Braille (⠀–⣿). Como cada um ocupa exatamente a célula
// inteira em píxeis inteiros, encaixam sem costuras entre células — o mesmo truque dos
// terminais modernos. Lógica pura, testada.

#include <cstdint>
#include <span>

namespace ap {

bool isProceduralGlyph(char32_t cp);

// Escreve a cobertura (0 = vazio, 255 = cheio) de `cp` numa imagem w×h com `stride` bytes
// por linha. A imagem deve vir a zeros. Devolve false se `cp` não é procedural.
bool rasterizeProceduralGlyph(char32_t cp, int w, int h, std::span<uint8_t> pixels, int stride);

} // namespace ap
