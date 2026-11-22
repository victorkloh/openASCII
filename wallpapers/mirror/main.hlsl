// Mirror — tu, em ASCII, ao vivo (webcam). Mexe-te e saltam faíscas.
//
// Tech demo de [inputs] source = "webcam" e de DETEÇÃO DE MOVIMENTO por estado: cada célula
// guarda a luminância que viu no frame anterior; a diferença para a de agora é o movimento.
// Onde há movimento, acende-se um rasto que desvanece devagar (state.y).
//
// A webcam só liga depois de aceitares o pedido (o Windows mostra que a câmara está em uso);
// a imagem nunca sai do PC. Sem câmara (ou sem autorização), aparece uma mensagem.
// Estilos (param "style"): 0 = a cores, 1 = fósforo verde, 2 = só contornos (neon).
//
// state = (luminância do frame anterior, rasto do movimento, -, -)

static const float PI = 3.14159265;
static const uint MSG[30] = {65, 67, 69, 73, 84, 65, 32, 79, 32, 80, 69, 68, 73, 68, 79, 32, 80, 65, 82, 65, 32, 65, 32, 87, 69, 66, 67, 65, 77, 32};
// "ACEITA O PEDIDO PARA A WEBCAM "

CellOut cell(CellIn i)
{
    CellOut o;
    o.glyph = 0;
    o.bg = 0;
    o.value = 0;
    o.fg = 0;
    o.state = 0;
    int2 c = int2(i.id);
    int4 mon = monitorRectAt(c);

    if (!hasSource())
    {
        float2 center = (float2(mon.xy) + float2(mon.zw)) * 0.5;
        int col = c.x - int(center.x) + 15;
        o.fg = palette(0.8);
        if (c.y == int(center.y) && col >= 0 && col < 30) o.glyph = asciiGlyph(MSG[col]);
        return o;
    }

    // Espelho: x invertido (como olhar para um espelho). A imagem cobre cada monitor.
    float2 rectMin = float2(mon.xy) * worldPerCell;
    float2 rectSize = float2(mon.zw - mon.xy) * worldPerCell;
    float2 uv = fitSource(i.world, rectMin, rectSize, 1.0);
    uv.x = 1.0 - uv.x;
    float2 cellUv = worldPerCell / rectSize;

    float3 rgb = sampleSource(uv).rgb;
    float lum = luminance(rgb);
    float2 edge = sourceEdge(uv, cellUv);

    // Movimento: diferença para o que esta célula viu no frame anterior.
    float4 s = prev(c);
    float motion = saturate(abs(lum - s.x) * 6.0 - 0.15);
    float trail = max(s.y * (1.0 - dt * 1.8), motion);
    o.state = float4(lum, trail, 0, 0);

    uint style = uint(param("style") + 0.5);
    float contrast = param("contrast");
    float l = saturate((lum - 0.5) * contrast + 0.5);
    if (style == 0u)
    {
        o.value = l;
        o.fg = float4(rgb / max(max(rgb.r, max(rgb.g, rgb.b)), 0.1) * (0.35 + 0.65 * l), 1);
        o.bg = float4(rgb * 0.18, 1);
    }
    else if (style == 1u)
    {
        o.value = l;
        o.fg = float4(float3(0.25, 1.0, 0.45) * (0.25 + 0.85 * l), 1);
        o.bg = float4(0.0, 0.05 * l, 0.01, 1);
    }
    else
    {
        float e = saturate(edge.x * 1.4);
        o.value = e;
        o.fg = palette(0.5 + 0.5 * e) * (0.3 + e);
    }

    // Faíscas onde há movimento: traços na direção da aresta, com a cor de destaque.
    if (trail > 0.12)
    {
        float a = edge.y + PI * 0.5;
        uint k = uint(frac((a + PI / 8.0) / PI) * 4.0) % 4u;
        static const uint SPRITE[4] = {0u, 3u, 2u, 1u}; // glyphs = "-/|\"
        if (edge.x > 0.25) o.glyph = glyph(SPRITE[k]);
        else o.glyph = glyph(4u); // '*'
        o.fg = lerp(o.fg, palette(1.0), saturate(trail * 1.5));
    }
    return o;
}
