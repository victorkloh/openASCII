// Clock — horas e minutos em dígitos de blocos (fonte de 3x5 "píxeis"), com sombra,
// a barra dos segundos e a data por baixo. Centrado no monitor principal.

// Cada dígito: 5 linhas de 3 bits (bit mais alto = canto superior esquerdo).
static const uint DIGITS[10] = {0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7292, 0x7BEF, 0x7BCF};

// Índices em `glyphs`.
static const uint G_SPACE = 10, G_DOT = 31, G_FULL = 32, G_SHADE = 33, G_HEAVY = 34, G_LIGHT = 35;
// Letras: A11 B12 D13 E14 F15 G16 I17 J18 L19 M20 N21 O22 Q23 R24 S25 T26 U27 V28 X29 Z30
static const uint3 WEEKDAYS[7] = {uint3(13, 22, 20), uint3(25, 14, 16), uint3(26, 14, 24), uint3(23, 27, 11),
                                  uint3(23, 27, 17), uint3(25, 14, 29), uint3(25, 11, 12)}; // DOM..SAB
static const uint3 MONTHS[12] = {uint3(18, 11, 21), uint3(15, 14, 28), uint3(20, 11, 24), uint3(11, 12, 24),
                                 uint3(20, 11, 17), uint3(18, 27, 21), uint3(18, 27, 19), uint3(11, 16, 22),
                                 uint3(25, 14, 26), uint3(22, 27, 26), uint3(21, 22, 28), uint3(13, 14, 30)};

// O "píxel" (fx, fy) da fonte está aceso no relógio HH:MM?
// Layout em píxeis da fonte: D D D _ D D D _ : _ D D D _ D D D  (17 de largura, 5 de altura)
bool clockPixel(int fx, int fy, uint4 digits, bool colonOn)
{
    if (fy < 0 || fy > 4 || fx < 0 || fx > 16) return false;
    int starts[4] = {0, 4, 10, 14};
    [unroll] for (int k = 0; k < 4; k++)
    {
        int dx = fx - starts[k];
        if (dx >= 0 && dx < 3)
        {
            uint bit = 14u - uint(fy * 3 + dx);
            return ((DIGITS[digits[k]] >> bit) & 1u) != 0;
        }
    }
    return colonOn && fx == 8 && (fy == 1 || fy == 3);
}

uint dateChar(int k, uint4 date)
{
    // "SEX 20 NOV 2026": 15 caracteres.
    uint3 wd = WEEKDAYS[date.w % 7u];
    uint3 mo = MONTHS[(date.y + 11u) % 12u];
    if (k < 3) return wd[k];
    if (k == 3 || k == 6 || k == 10) return G_SPACE;
    if (k == 4) return (date.z / 10u) % 10u;
    if (k == 5) return date.z % 10u;
    if (k < 10) return mo[k - 7];
    uint y = date.x;
    uint pow10[4] = {1000u, 100u, 10u, 1u};
    return (y / pow10[k - 11]) % 10u;
}

CellOut cell(CellIn i)
{
    int2 c = int2(i.id);
    float2 w = i.world;
    float2 center = primaryCenterWorld();
    float2 size = primarySizeWorld();

    // Hora atual.
    float hour = localTime.x;
    if (param("h24") < 0.5)
    {
        hour = fmod(hour, 12.0);
        if (hour < 0.5) hour = 12.0;
    }
    uint h = uint(hour), m = uint(localTime.y);
    float sec = localTime.z;
    uint4 digits = uint4(h / 10u, h % 10u, m / 10u, m % 10u);
    bool colonOn = frac(sec) < 0.55;

    // Geometria: píxel da fonte quadrado em unidades de mundo.
    float px = size.y * param("size") / 5.0;
    float2 origin = center - float2(17.0 * px, 5.0 * px) * 0.5 - float2(0, size.y * 0.06);
    float2 f = (w - origin) / px;
    int fx = int(floor(f.x)), fy = int(floor(f.y));

    // ---- Fundo calmo: degradê lento + pó a flutuar ----
    float drift = noise(w * 0.01 + float2(time * 0.01, 0));
    float4 bg = lerp(palette(0.02), palette(0.22), drift * (1.0 - saturate(abs(w.y - center.y) / size.y)));
    CellOut o;
    o.glyph = 0;
    o.state = float4(0, 0, 0, 0);
    o.value = drift * 0.15;
    o.fg = palette(0.3);
    o.bg = float4(bg.rgb, 1);

    float dust = param("dust");
    if (dust > 0.0)
    {
        float2 dc = floor(w / 6.0);
        float hsh = hash21(dc);
        float2 pos = dc * 6.0 + hash22(dc) * 6.0 + float2(sin(time * 0.3 + hsh * 6.0), -time * (0.3 + hsh)) ;
        pos.y = frac(pos.y / 6.0) * 6.0 + dc.y * 6.0;
        if (hsh < 0.12 * dust && all(int2(floor(pos / worldPerCell)) == c))
        {
            o.glyph = glyph(G_DOT);
            o.fg = palette(0.6) * (0.4 + 0.6 * sin(time + hsh * 30.0) * 0.5 + 0.3);
        }
    }

    // ---- Dígitos com sombra ----
    float glow = exp(-length(w - mouse.world) * 0.02) * mouse.presence;
    bool on = clockPixel(fx, fy, digits, colonOn);
    float2 fs = (w - origin - px * 0.22) / px; // sombra: deslocada para baixo e para a direita
    bool shadow = !on && clockPixel(int(floor(fs.x)), int(floor(fs.y)), digits, colonOn);
    if (on)
    {
        float t = saturate(frac(f.y / 5.0) + (fy / 5.0)); // degradê de cima para baixo
        float shimmer = 0.06 * sin(time * 1.5 + w.x * 0.08);
        o.glyph = glyph(G_FULL);
        o.fg = lerp(palette(1.0), palette(0.72), t) * (1.0 + shimmer + glow * 0.4);
        o.value = 1;
    }
    else if (shadow)
    {
        o.glyph = glyph(G_SHADE);
        o.fg = palette(0.35);
    }

    // ---- Barra dos segundos ----
    if (param("seconds") > 0.5)
    {
        float barY = origin.y + 5.0 * px + px * 0.55;
        if (int(floor(barY / worldPerCell.y)) == c.y && w.x >= origin.x && w.x < origin.x + 17.0 * px)
        {
            float progress = (w.x - origin.x) / (17.0 * px);
            bool filled = progress < sec / 60.0;
            o.glyph = glyph(filled ? G_HEAVY : G_LIGHT);
            o.fg = filled ? palette(0.85) : palette(0.32);
        }
    }

    // ---- Data, espaçada: "S E X   2 0   N O V   2 0 2 6" ----
    float dateY = origin.y + 5.0 * px + px * 1.4;
    int dateRow = int(floor(dateY / worldPerCell.y));
    int centerCol = int(floor(center.x / worldPerCell.x));
    int startCol = centerCol - 15;
    if (c.y == dateRow)
    {
        int k = c.x - startCol;
        if (k >= 0 && k < 30 && (k & 1) == 0)
        {
            uint g = dateChar(k / 2, localDate);
            if (g != G_SPACE)
            {
                o.glyph = glyph(g);
                o.fg = palette(0.6);
            }
        }
    }
    return o;
}
