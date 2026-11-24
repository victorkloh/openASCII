// Aquarium — peixes de vários tamanhos a nadar, bolhas a subir, algas a ondular e areia.
//
// Os peixes são "sprites" de vários caracteres (ex.: ><(((°>). Cada célula pergunta a cada
// peixe "estás a passar por aqui?" e, se sim, qual dos caracteres do peixe lhe calha.
// Tudo é calculado a partir do tempo: sem estado, por isso funciona com qualquer resolução
// e atravessa os monitores no modo estendido.

// Índices em `glyphs`:  < > ( ) ° o O . ~ ' / \ ▄ · ¸
static const uint G_LT = 0, G_GT = 1, G_LP = 2, G_RP = 3, G_DEG = 4, G_o = 5, G_O = 6, G_DOT = 7, G_TILDE = 8,
                  G_TICK = 9, G_SLASH = 10, G_BSLASH = 11, G_LOW = 12, G_MID = 13, G_CED = 14;

// Sprites virados para a direita (a cauda à esquerda). Virados para a esquerda: ao contrário,
// trocando < por > e ( por ).
static const uint FISH_S[3] = {G_GT, G_LT, G_GT};                               // ><>
static const uint FISH_M[5] = {G_GT, G_LT, G_LP, G_DEG, G_GT};                  // ><(°>
static const uint FISH_L[7] = {G_GT, G_LT, G_LP, G_LP, G_LP, G_DEG, G_GT};      // ><(((°>

uint mirrorGlyph(uint g)
{
    if (g == G_LT) return G_GT;
    if (g == G_GT) return G_LT;
    if (g == G_LP) return G_RP;
    if (g == G_RP) return G_LP;
    return g;
}

uint spriteGlyph(uint kind, uint k)
{
    if (kind == 0) return FISH_S[min(k, 2u)];
    if (kind == 1) return FISH_M[min(k, 4u)];
    return FISH_L[min(k, 6u)];
}

CellOut cell(CellIn i)
{
    int2 c = int2(i.id);
    int4 mon = monitorRectAt(c);
    float2 w = i.world;
    float monTop = float(mon.y) * worldPerCell.y;
    // O fundo do aquário é o fundo da área de TRABALHO: a areia e as algas ficam por cima da
    // barra de tarefas (por trás dela continua areia).
    float monBottom = float(workRectAt(c).w) * worldPerCell.y;
    float monH = monBottom - monTop;
    float depth = saturate((w.y - monTop) / monH); // 0 superfície, 1 fundo

    // ---- Água: degradê com raios de luz a ondular ----
    float rays = pow(saturate(noise(float2(w.x * 0.04 + w.y * 0.015, time * 0.15))), 3.0) * (1.0 - depth);
    float4 water = palette(lerp(0.32, 0.05, depth));
    CellOut o;
    o.glyph = 0;
    o.state = float4(0, 0, 0, 0);
    o.value = rays * 0.35;
    o.fg = lerp(water, float4(0.6, 0.9, 1.0, 1), rays * 0.6);
    o.bg = float4(water.rgb * 0.45, 1);

    // ---- Areia e pedras no fundo de cada monitor ----
    float sandTop = monBottom - 3.0 - noise(float2(w.x * 0.05, 1.0)) * 4.0;
    if (w.y > sandTop)
    {
        float grain = hash21(float2(c));
        o.fg = palette(0.6) * (0.55 + grain * 0.35);
        o.bg = float4(palette(0.6).rgb * 0.18, 1);
        o.value = 0.25 + grain * 0.3;
        if (noise(float2(w.x * 0.2, 7.0)) > 0.72) // pedras
        {
            o.glyph = glyph(G_LOW);
            o.fg = float4(0.35, 0.37, 0.42, 1);
        }
        return o;
    }

    // ---- Algas: colunas que ondulam ----
    float weedSpacing = 9.0 / max(param("weed"), 0.05);
    float wcol = floor(w.x / weedSpacing);
    float wx = (wcol + 0.5 + (hash11(wcol) - 0.5) * 0.6) * weedSpacing;
    float wh = monH * lerp(0.12, 0.38, hash11(wcol * 3.1));
    float above = sandTop - w.y;
    if (param("weed") > 0.0 && above > 0.0 && above < wh)
    {
        float sway = sin(time * 1.2 + w.y * 0.25 + wcol) * (above / wh) * 2.0;
        if (abs(w.x - (wx + sway)) < worldPerCell.x * 0.6)
        {
            bool left = sin(time * 1.2 + w.y * 0.25 + wcol + 0.8) > 0.0;
            o.glyph = glyph((uint(floor(w.y / worldPerCell.y)) & 1u) == (left ? 1u : 0u) ? G_LP : G_RP);
            o.fg = lerp(float4(0.1, 0.45, 0.2, 1), float4(0.3, 0.8, 0.35, 1), above / wh);
            o.value = 0.6;
        }
    }

    // ---- Comida (clique): flocos que caem devagar ----
    float sinceClick = time - mouse.clickTime;
    bool food = mouse.clickTime >= 0 && sinceClick < 12.0;
    float2 foodPos = mouse.clickWorld + float2(sin(sinceClick * 1.5) * 1.5, sinceClick * 3.0);
    foodPos.y = min(foodPos.y, sandTop - 1.0);
    if (food)
    {
        [unroll] for (int f = 0; f < 3; f++)
        {
            float2 fp = foodPos + float2((f - 1) * 1.6, f * 0.8);
            if (all(int2(floor(fp / worldPerCell)) == c))
            {
                o.glyph = glyph(G_MID);
                o.fg = float4(0.95, 0.75, 0.4, 1);
            }
        }
    }

    // ---- Bolhas ----
    float bspace = 14.0 / max(param("bubbles"), 0.05);
    float bcol = floor(w.x / bspace);
    if (param("bubbles") > 0.0 && hash11(bcol * 5.3) < 0.55)
    {
        float bspeed = 4.0 + hash11(bcol) * 4.0;
        float rise = frac(time * bspeed / monH + hash11(bcol * 2.0)) * monH;
        float2 bp = float2((bcol + 0.5) * bspace + sin(time * 2.0 + bcol) * 1.0, sandTop - rise);
        if (all(int2(floor(bp / worldPerCell)) == c))
        {
            float sz = rise / monH; // cresce ao subir
            o.glyph = glyph(sz > 0.7 ? G_O : (sz > 0.3 ? G_o : G_DOT));
            o.fg = float4(0.7, 0.9, 1.0, 1);
        }
    }

    // ---- Peixes ----
    int count = int(round(14.0 * param("fish")));
    [loop] for (int k = 0; k < count; k++)
    {
        float fk = float(k);
        uint kind = uint(hash11(fk * 1.7) * 3.0);
        uint len = kind == 0 ? 3u : (kind == 1 ? 5u : 7u);
        bool right = hash11(fk * 3.9) > 0.5;
        float speed = lerp(3.0, 9.0, hash11(fk * 2.3)) * param("speed") * (kind == 2 ? 0.7 : 1.0);
        float span = worldSize.x + 40.0;
        float x = frac(hash11(fk) + time * speed / span) * span - 20.0;
        if (!right) x = worldSize.x - x;
        float laneTop = monTop + monH * 0.12, laneH = monH * 0.6;
        float y = laneTop + hash11(fk * 6.1) * laneH + sin(time * 0.7 + fk * 2.0) * 2.0;

        // Fugir do rato; procurar a comida.
        {
            float2 away = float2(x, y) - mouse.world;
            float d = length(away);
            if (d < 16.0) y += sign(away.y + 0.01) * (16.0 - d) * 0.8 * mouse.presence;
        }
        if (food && abs(foodPos.x - x) < 40.0) y = lerp(y, foodPos.y, saturate(1.0 - abs(foodPos.x - x) / 40.0) * 0.8);

        int row = int(floor(y / worldPerCell.y));
        if (row != c.y) continue;
        // O peixe ocupa `len` células a partir da cauda.
        int headCol = int(floor(x / worldPerCell.x));
        int offset = right ? headCol - c.x : c.x - headCol; // 0 = cabeça
        if (offset < 0 || offset >= int(len)) continue;
        uint idx = len - 1u - uint(offset);                  // índice no sprite (cauda = 0)
        uint g = spriteGlyph(kind, idx);
        o.glyph = glyph(right ? g : mirrorGlyph(g));
        float hue = hash11(fk * 9.7);
        o.fg = hue < 0.33 ? palette(0.85) : (hue < 0.66 ? palette(1.0) : float4(0.55, 0.85, 1.0, 1));
        o.value = 0.8;
    }
    return o;
}
