// grid.hlsl — render final: transforma o canvas de células em píxeis de UM monitor.
//
// Um único triângulo que cobre o ecrã inteiro (mais barato que dois triângulos: não há
// diagonal a meio onde a GPU processa píxeis duas vezes). Para cada píxel, o pixel shader
// descobre em que célula DO MONITOR está, que célula DO CANVAS ela mostra (no modo estendido
// o canvas cobre todos os monitores), vai buscar a cobertura do glifo ao atlas e mistura fg/bg.

cbuffer GridCB : register(b0)
{
    int2   gridSize;        // colunas, linhas deste monitor
    int2   cellSize;        // px físicos (do atlas deste monitor)
    int2   origin;          // margem (grelha centrada)
    int    atlasCols;       // glifos por linha no atlas
    uint   flags;           // bit 0: há pós-processamento
    float2 monitorToCanvas; // origem do monitor - origem do canvas, em px
    float2 canvasCell;      // tamanho de uma célula do canvas, em px
    int2   canvasSize;      // colunas, linhas do canvas
    int    slotCount;       // glifos no atlas
    int    _pad;
    float4 background;      // cor da margem e das células com bg.a = 0
    float4 post0;           // glow, scanlines, curvature, vignette (0..1)
    float4 post1;           // aberration, grain, tempo (s), escala dos efeitos (~altura da célula / 16)
};

StructuredBuffer<uint3> cells : register(t0); // x = glifo (16 bits) | flags, y = fg RGBA8, z = bg RGBA8
Texture2D<float>        atlas : register(t1); // R8: cobertura 0..1 de cada glifo
StructuredBuffer<float> ink   : register(t2); // cobertura média de cada glifo (para o glow)

float4 VSMain(uint id : SV_VertexID) : SV_Position
{
    // id 0,1,2 -> (0,0), (2,0), (0,2) em UV -> triângulo que cobre [-1,1]².
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

float4 unpackColor(uint c)
{
    return float4(c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF, c >> 24) / 255.0;
}

// A célula do canvas que a célula c DESTE monitor mostra. Com o mesmo DPI é 1:1; com DPIs
// diferentes, um monitor com células maiores salta células do canvas, mantendo a mesma
// escala física entre ecrãs.
bool canvasCellOf(int2 c, out int2 g)
{
    float2 center = monitorToCanvas + float2(origin) + (float2(c) + 0.5) * float2(cellSize);
    g = int2(floor(center / canvasCell));
    return all(c >= 0) && all(c < gridSize) && all(g >= 0) && all(g < canvasSize);
}

// Cor final de um píxel do monitor (sem efeitos).
float3 shade(int2 pixel)
{
    int2 p = pixel - origin;
    if (any(p < 0) || any(p >= gridSize * cellSize))
        return background.rgb;
    int2 c = p / cellSize;
    int2 local = p - c * cellSize;
    int2 g;
    if (!canvasCellOf(c, g))
        return background.rgb;
    uint3 cell = cells[g.y * canvasSize.x + g.x];
    uint glyph = cell.x & 0xFFFF;
    int2 a = int2(glyph % atlasCols, glyph / atlasCols) * cellSize + local;
    // Load (e não Sample): lemos o píxel exato do atlas, sem filtragem nem blur.
    float coverage = atlas.Load(int3(a, 0));
    float4 fg = unpackColor(cell.y);
    float4 bg = unpackColor(cell.z);
    float3 base = lerp(background.rgb, bg.rgb, bg.a);
    return lerp(base, fg.rgb, coverage * fg.a);
}

// Halo: a luz das células vizinhas, pesada pela "tinta" de cada glifo (um '@' brilha mais
// que um '.') e pela distância ao centro de cada célula.
float3 glowAt(float2 pixel)
{
    float2 p = pixel - float2(origin);
    int2 c = int2(floor(p / float2(cellSize)));
    float3 sum = 0;
    [unroll] for (int dy = -1; dy <= 1; dy++)
    [unroll] for (int dx = -2; dx <= 2; dx++)
    {
        int2 cc = c + int2(dx, dy);
        int2 g;
        if (!canvasCellOf(cc, g)) continue;
        uint3 cell = cells[g.y * canvasSize.x + g.x];
        float k = ink[min(cell.x & 0xFFFF, uint(max(slotCount - 1, 0)))];
        float4 fg = unpackColor(cell.y);
        float2 d = (p - (float2(cc) + 0.5) * float2(cellSize)) / float(cellSize.y);
        sum += fg.rgb * (fg.a * k * exp(-dot(d, d) * 1.8));
    }
    return sum;
}

float hashPixel(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float4 PSMain(float4 pos : SV_Position) : SV_Target
{
    if ((flags & 1u) == 0u)
        return float4(shade(int2(pos.xy)), 1); // sem efeitos: o caminho rápido

    float glow = post0.x, scan = post0.y, curve = post0.z, vignette = post0.w;
    float aberration = post1.x, grain = post1.y, time = post1.z, fx = max(post1.w, 0.5);
    float2 size = float2(gridSize * cellSize + 2 * origin);
    float2 uvc = pos.xy / size * 2.0 - 1.0; // -1..1 a partir do centro do monitor

    // Ecrã abaulado: puxa os píxeis para o centro; fora do vidro fica preto.
    float2 warped = uvc;
    if (curve > 0)
    {
        float2 k = curve * float2(0.10, 0.14);
        warped = uvc * (1.0 + k * uvc.yx * uvc.yx);
        if (any(abs(warped) > 1.0)) return float4(0, 0, 0, 1);
    }
    float2 pixel = (warped * 0.5 + 0.5) * size;

    float3 color;
    if (aberration > 0)
    {
        float off = aberration * fx * 3.0 * (0.35 + 0.65 * abs(uvc.x));
        color = float3(shade(int2(pixel + float2(off, 0))).r, shade(int2(pixel)).g, shade(int2(pixel - float2(off, 0))).b);
    }
    else
    {
        color = shade(int2(pixel));
    }
    if (glow > 0) color += glowAt(pixel) * glow * 0.9;
    if (scan > 0)
    {
        float period = max(2.0, round(fx * 3.0));
        float s = 0.5 + 0.5 * cos(6.2831853 * pixel.y / period);
        color *= 1.0 - scan * 0.5 * (1.0 - s);
        color *= 1.0 + scan * 0.18; // compensa o escurecimento médio
    }
    if (curve > 0)
    {
        // Bordas do vidro: um escurecimento suave em vez de um corte seco.
        float2 e = smoothstep(1.0, 0.96, abs(warped));
        color *= e.x * e.y;
    }
    if (vignette > 0)
        color *= lerp(1.0, smoothstep(1.55, 0.35, length(uvc * float2(1.0, 0.85))), vignette);
    if (grain > 0)
        color += (hashPixel(pos.xy + frac(time * 7.13) * 1000.0) - 0.5) * grain * 0.12;
    return float4(saturate(color), 1);
}