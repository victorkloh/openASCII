// Photo — qualquer imagem, GIF ou vídeo em ASCII a cores.
//
// Tech demo de [inputs] source: a imagem chega ao shader como uma textura e lê-se com
// sampleSource(uv). Por defeito é o TEU wallpaper do Windows (source = "wallpaper"); no
// launcher podes escolher outra foto, um GIF animado ou um vídeo (.mp4).
//
// O que mostra:
//   - luminância -> caractere (a rampa do charset, do mais vazio ao mais cheio);
//   - contornos com o operador de Sobel (sourceEdge): onde há uma aresta forte, em vez da
//     rampa desenha-se um traço com a direção da aresta: - / | \ ;
//   - cor da imagem nos caracteres e, mais escura, no fundo de cada célula;
//   - um "Ken Burns" lento (zoom e deslocamento suaves) para a foto nunca estar parada;
//   - uma lupa que segue o rato e uma onda quando clicas.

static const float PI = 3.14159265;

// uv da imagem para um ponto do mundo, com a imagem a cobrir o retângulo (cover) e o
// movimento de câmara aplicado.
float2 photoUv(float2 world, float2 rectMin, float2 rectSize)
{
    float2 uv = fitSource(world, rectMin, rectSize, 1.0);
    // Ken Burns: zoom de 1.04x a 1.04x+zoom e um passeio suave pelo centro.
    float t = time * 0.03 * param("speed");
    float z = 1.04 + param("zoom") * (0.5 + 0.5 * sin(t * 1.3));
    float2 pan = float2(sin(t * 0.9), cos(t * 0.7)) * 0.5 * (1.0 - 1.0 / z);
    return (uv - 0.5) / z + 0.5 + pan;
}

// Mensagem quando não há imagem: escrita com asciiGlyph (qualquer caractere Latin-1).
static const uint MSG[22] = {83, 69, 77, 32, 73, 77, 65, 71, 69, 77, 32, 40, 118, 101, 114, 32, 108, 111, 103, 41, 32, 32};

CellOut cell(CellIn i)
{
    CellOut o;
    o.state = 0;
    o.glyph = 0;
    o.bg = 0;
    int2 c = int2(i.id);

    // Cada monitor mostra a imagem inteira (span = 0) ou a imagem estende-se por todos (span = 1).
    int4 mon = param("span") > 0.5 ? int4(0, 0, int(grid.x), int(grid.y)) : monitorRectAt(c);
    float2 rectMin = float2(mon.xy) * worldPerCell;
    float2 rectSize = float2(mon.zw - mon.xy) * worldPerCell;

    if (!hasSource())
    {
        float2 center = (float2(mon.xy) + float2(mon.zw)) * 0.5;
        int col = c.x - int(center.x) + 11;
        o.value = 0;
        o.fg = palette(0.8);
        if (c.y == int(center.y) && col >= 0 && col < 22) o.glyph = asciiGlyph(MSG[col]);
        return o;
    }

    float2 world = i.world;

    // Onda de um clique: desloca a imagem radialmente, como uma gota num lago.
    float since = time - mouse.clickTime;
    if (mouse.clickTime >= 0 && since < 2.5)
    {
        float2 d = world - mouse.clickWorld;
        float r = length(d);
        float wave = sin(r * 0.25 - since * 12.0) * exp(-abs(r - since * 60.0) * 0.06) * (1.0 - since / 2.5);
        world += d / max(r, 1e-3) * wave * 6.0;
    }

    // Lupa: perto do rato a imagem aparece ampliada 2x (e mais nítida, com mais contraste).
    float lens = 0;
    if (mouse.presence > 0.001 && param("lens") > 0)
    {
        float r = length(world - mouse.world);
        float radius = 28.0 * param("lens");
        lens = saturate(1.0 - r / radius) * mouse.presence;
        lens = lens * lens * (3.0 - 2.0 * lens);
        world = lerp(world, mouse.world + (world - mouse.world) * 0.5, lens);
    }

    float2 uv = photoUv(world, rectMin, rectSize);
    float2 uvX = photoUv(world + float2(worldPerCell.x, 0), rectMin, rectSize);
    float2 uvY = photoUv(world + float2(0, worldPerCell.y), rectMin, rectSize);
    float2 cellUv = float2(abs(uvX.x - uv.x), abs(uvY.y - uv.y));

    float3 rgb = sampleSource(uv).rgb;
    float lum = luminance(rgb);
    // Contraste (centrado em 0.5) e um pouco de "dither" para os gradientes não fazerem degraus.
    float contrast = param("contrast") + lens * 0.4;
    lum = saturate((lum - 0.5) * contrast + 0.5 + (hash21(float2(c)) - 0.5) * 0.04);

    // Cor: 0 = paleta do wallpaper (monocromático), 1 = as cores da imagem.
    float3 tinted = palette(lum).rgb;
    float3 natural = rgb / max(max(rgb.r, max(rgb.g, rgb.b)), 0.08) * (0.35 + 0.65 * lum); // cor viva mesmo no escuro
    float3 fg = lerp(tinted, natural, param("color"));

    o.value = lum;
    o.fg = float4(fg * (1.0 + lens * 0.25), 1);
    o.bg = float4(lerp(tinted, rgb, param("color")) * param("fill") * 0.35, 1);

    // Arestas: Sobel à escala de uma célula. O traço segue a aresta (perpendicular ao gradiente).
    float2 edge = sourceEdge(uv, cellUv);
    float strength = edge.x * (1.0 + lens);
    if (param("edges") > 0 && strength > lerp(1.6, 0.35, param("edges")))
    {
        float a = edge.y + PI * 0.5;                        // direção da aresta
        float k = frac((a + PI / 8.0) / PI) * 4.0;          // 0..4 em quartos de volta (mod 180°)
        uint dir = uint(k) % 4u;                            // 0: -  1: \  2: |  3: /   (y cresce para baixo)
        static const uint SPRITE[4] = {0u, 3u, 2u, 1u};    // glyphs = "-/|\"
        o.glyph = glyph(SPRITE[dir]);
        o.fg = float4(lerp(fg, float3(1, 1, 1), 0.25 * param("edges")), 1);
    }
    return o;
}
