// Bonsai — uma árvore que cresce devagar ao longo do dia, e muda com as estações.
//
// Tech demo de ESTADO GLOBAL calculado por células reservadas. A árvore tem 63 ramos (uma
// árvore binária de 6 níveis, guardada como um "heap": o ramo k tem filhos 2k e 2k+1).
// As duas primeiras linhas do monitor principal servem de memória: a célula k calcula o
// ramo k (onde começa, onde acaba, quando nasce, a espessura) e guarda-o no seu estado.
// No frame seguinte, todas as células leem os 63 ramos com prev() e desenham-se a si
// próprias. (O que estas células guardam não tem nada a ver com o que mostram.)
//
//   - semente nova todos os dias (localDate): cada dia, um bonsai diferente;
//   - cresce durante `minutes` minutos desde que o wallpaper arranca;
//   - estação do ano pelo mês: flores na primavera, verde no verão, folhas a cair no
//     outono, ramos nus com neve no inverno;
//   - o vento abana os ramos mais finos; o rato sopra.
// Os ramos são desenhados como numa arte ASCII: o caractere segue a direção ( / \ | _ ).

static const float PI = 3.14159265;
static const uint NODES = 63u;     // 1..63
static const uint DEPTH = 6u;

uint season() // 0 primavera, 1 verão, 2 outono, 3 inverno (hemisfério norte)
{
    uint m = localDate.y;
    if (m >= 3u && m <= 5u) return 0u;
    if (m >= 6u && m <= 8u) return 1u;
    if (m >= 9u && m <= 11u) return 2u;
    return 3u;
}

float growth() { return saturate(time / max(param("minutes") * 60.0, 10.0)); }

// Onde fica a raiz e o tamanho do tronco (unidades de mundo), no monitor principal.
float2 rootPos() { int4 r = primaryRect(); return float2((r.x + r.z) * 0.5, r.w - 7) * worldPerCell; }
float trunkLen() { return primarySizeWorld().y * 0.17 * param("size"); }

// Calcula o ramo k (célula reservada): segue o caminho desde a raiz pelos bits de k.
float4 computeNode(uint k, out float4 extra)
{
    float seed = float(localDate.x * 400u + localDate.y * 32u + localDate.z) * 0.123 + param("seed");
    uint depth = firstbithigh(k);       // 0 = tronco
    float2 pos = rootPos();
    float ang = -PI * 0.5 + (hash11(seed) - 0.5) * 0.3;
    float len = trunkLen();
    float2 start = pos;
    uint node = 1u;
    [loop] for (int d = 0; d <= int(depth); d++)
    {
        if (d > 0)
        {
            uint bit = (k >> (depth - uint(d))) & 1u;
            node = node * 2u + bit;
            float h = hash11(float(node) * 7.31 + seed);
            float spread = 0.35 + 0.35 * hash11(float(node) * 3.7 + seed);
            ang += (bit == 0u ? -spread : spread) + (h - 0.5) * 0.4;
            ang = lerp(ang, -PI * 0.5, 0.18);                 // os ramos tendem a subir
            len *= 0.68 + 0.18 * h;
        }
        // Vento: abana mais os ramos finos (fundos).
        float sway = sin(time * 0.9 + float(node) * 1.7) * 0.025 * float(d);
        if (mouse.presence > 0.001) sway += clamp(mouse.velWorld.x * 0.0004, -0.15, 0.15) * float(d) * mouse.presence;
        float a = ang + sway;
        start = pos;
        pos += float2(cos(a), sin(a)) * len;
    }
    float birth = float(depth) / float(DEPTH) * 0.8 + hash11(float(k) + seed) * 0.08;
    float thick = max(0.35, 3.2 * pow(0.62, float(depth)) * param("size"));
    extra = float4(birth, thick, float(depth), 0);
    return float4(start, pos);
}

float segDistance(float2 p, float2 a, float2 b, out float t)
{
    float2 ab = b - a;
    t = saturate(dot(p - a, ab) / max(dot(ab, ab), 1e-5));
    return length(p - (a + ab * t));
}

// Caractere de um traço com a direção v (y para baixo): - / | \ e _ para quase horizontal.
int strokeGlyph(float2 v)
{
    float a = atan2(v.y, v.x);
    uint k = uint(frac((a + PI / 8.0) / PI) * 4.0) % 4u;
    static const uint SPRITE[4] = {4u, 3u, 2u, 1u}; // glyphs: 1 '\', 2 '|', 3 '/', 4 '_'
    return glyph(SPRITE[k]);
}

CellOut cell(CellIn i)
{
    CellOut o;
    o.glyph = 0;
    o.value = 0;
    o.fg = 0;
    o.bg = 0;
    o.state = 0;
    int2 c = int2(i.id);
    int4 P = primaryRect();
    uint s = season();
    float G = growth();

    // --- Células reservadas: calculam os ramos (linhas 0 e 1 do monitor principal) ---
    int k = c.x - P.x;
    if ((c.y == P.y || c.y == P.y + 1) && k >= 1 && k <= int(NODES))
    {
        float4 extra;
        float4 seg = computeNode(uint(k), extra);
        o.state = c.y == P.y ? seg : extra;
    }

    float2 w = i.world;

    // --- Céu: degradê suave pela estação e pela hora ---
    float hour = localTime.x + localTime.y / 60.0;
    float daylight = saturate(sin((hour - 6.0) / 12.0 * PI) * 1.4);
    int4 mon = monitorRectAt(c);
    float v = float(c.y - mon.y) / max(float(mon.w - mon.y), 1.0);
    o.bg = float4(lerp(palette(0.02).rgb, palette(0.12).rgb, v) * (0.5 + 0.5 * daylight), 1);

    // --- Vaso e terra ---
    float2 root = rootPos();
    float potW = trunkLen() * 1.1;
    float2 d = w - root;
    if (d.y > 0.0 && d.y < 6.5 * worldPerCell.y && abs(d.x) < potW - d.y * 0.25)
    {
        bool rim = d.y < worldPerCell.y * 1.2;
        bool edge = abs(abs(d.x) - (potW - d.y * 0.25)) < worldPerCell.x * 1.2 || d.y > 5.5 * worldPerCell.y;
        o.glyph = rim ? asciiGlyph(58u) : (edge ? glyph(d.x < 0 ? 1u : 3u) : glyph(5u));
        if (!rim && d.y > 5.5 * worldPerCell.y) o.glyph = glyph(4u);
        o.fg = rim ? palette(0.4) : palette(0.55);
        o.bg = float4(palette(0.3).rgb * 0.35, 1);
        return o;
    }

    // --- Ramos: o mais próximo dos 63 (lidos do estado das células reservadas) ---
    float best = 1e9;
    float2 bestDir = 0;
    float bestDepth = 0;
    float tipNear = 1e9;
    [loop] for (uint n = 1u; n <= NODES; n++)
    {
        int2 rc = int2(P.x + int(n), P.y);
        float4 seg = prev(rc);
        float4 ex = prev(rc + int2(0, 1));
        float g = saturate((G - ex.x) / 0.18);
        if (g <= 0.0) continue;
        float2 a = seg.xy, b = lerp(seg.xy, seg.zw, g);
        float t;
        float dist = segDistance(w, a, b, t) - lerp(ex.y, ex.y * 0.7, t) * 0.5;
        if (dist < best) { best = dist; bestDir = b - a; bestDepth = ex.z; }
        if (ex.z >= float(DEPTH) - 1.0 && g >= 1.0) tipNear = min(tipNear, length(w - b));
    }
    if (best < worldPerCell.x * 0.55)
    {
        o.glyph = bestDepth < 1.5 ? glyph(6u) : strokeGlyph(bestDir);
        o.fg = lerp(palette(0.45), palette(0.6), bestDepth / float(DEPTH));
        // Inverno: neve por cima dos ramos (a célula de cima está vazia).
        if (s == 3u && best > -worldPerCell.x * 0.2 && hash21(float2(c)) < 0.5) { o.glyph = glyph(7u); o.fg = palette(1.0); }
        return o;
    }

    // --- Folhagem: tufos à volta das pontas, quando a árvore já cresceu ---
    float leafR = (2.6 + 1.4 * hash21(floor(w / 3.0))) * param("size");
    float foliage = saturate((G - 0.85) / 0.15);
    if (s != 3u && tipNear < leafR * foliage)
    {
        float n = noise(w * 0.9 + 3.0);
        if (n > 0.32)
        {
            float4 leaf = s == 0u ? lerp(palette(0.7), palette(0.85), n) :            // primavera: rosa
                          s == 1u ? lerp(float4(0.15, 0.55, 0.2, 1), float4(0.4, 0.8, 0.3, 1), n) :
                                    lerp(float4(0.85, 0.35, 0.1, 1), float4(0.95, 0.7, 0.2, 1), n); // outono
            o.glyph = (s == 0u && n > 0.7) ? glyph(8u) : glyph(9u);   // * flor, & folha
            o.fg = leaf;
            return o;
        }
    }

    // --- Partículas: pétalas (primavera), folhas (outono) ou neve (inverno) a cair ---
    if (s != 1u && G > 0.85)
    {
        float cellSize = 9.0;
        float2 p = w;
        p.y -= time * (s == 3u ? 6.0 : 3.5);
        p.x -= time * 1.5 + sin(time * 0.5 + p.y * 0.1) * 2.0;
        float2 id = floor(p / cellSize);
        float2 h = hash22(id + 5.0);
        float2 fall = (id + 0.2 + 0.6 * h) * cellSize;
        float2 screen = fall + float2(time * 1.5, time * (s == 3u ? 6.0 : 3.5));
        screen.x += sin(time * 0.5 + fall.y * 0.1) * 2.0;
        if (h.x < 0.35 && all(int2(floor(screen / worldPerCell)) == c))
        {
            o.glyph = s == 3u ? glyph(7u) : glyph(s == 0u ? 8u : 10u);
            o.fg = s == 3u ? palette(1.0) : (s == 0u ? palette(0.8) : float4(0.9, 0.5, 0.15, 1));
        }
    }
    return o;
}
