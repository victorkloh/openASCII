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
    uint   flags;
    float2 monitorToCanvas; // origem do monitor - origem do canvas, em px
    float2 canvasCell;      // tamanho de uma célula do canvas, em px
    int2   canvasSize;      // colunas, linhas do canvas
    int2   _pad;
    float4 background;      // cor da margem e das células com bg.a = 0
};

StructuredBuffer<uint3> cells : register(t0); // x = glifo (16 bits) | flags, y = fg RGBA8, z = bg RGBA8
Texture2D<float>        atlas : register(t1); // R8: cobertura 0..1 de cada glifo

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

float4 PSMain(float4 pos : SV_Position) : SV_Target
{
    int2 p = int2(pos.xy) - origin;
    if (any(p < 0) || any(p >= gridSize * cellSize))
        return float4(background.rgb, 1);

    int2 c = p / cellSize;
    int2 local = p - c * cellSize;

    // Centro desta célula em px do canvas -> célula do canvas. Com o mesmo DPI é 1:1;
    // com DPIs diferentes, um monitor com células maiores salta células do canvas,
    // mantendo a mesma escala física entre ecrãs.
    float2 center = monitorToCanvas + float2(origin) + (float2(c) + 0.5) * float2(cellSize);
    int2 g = int2(floor(center / canvasCell));
    if (any(g < 0) || any(g >= canvasSize))
        return float4(background.rgb, 1);
    uint3 cell = cells[g.y * canvasSize.x + g.x];

    uint glyph = cell.x & 0xFFFF;
    int2 a = int2(glyph % atlasCols, glyph / atlasCols) * cellSize + local;
    // Load (e não Sample): lemos o píxel exato do atlas, sem filtragem nem blur.
    float coverage = atlas.Load(int3(a, 0));

    float4 fg = unpackColor(cell.y);
    float4 bg = unpackColor(cell.z);
    float3 base = lerp(background.rgb, bg.rgb, bg.a);
    return float4(lerp(base, fg.rgb, coverage * fg.a), 1);
}
