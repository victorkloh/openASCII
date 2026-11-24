// HUD — um painel de sistema ao vivo: hora, CPU, RAM e o tempo lá fora.
//
// Tech demo de [inputs] system = true e weather = true, e de "interface" num shader:
//   systemStats.x/.y   uso de CPU e de RAM (0..1), atualizados a cada segundo;
//   weather            chuva (mm/h), nuvens (0..1), vento (m/s), temperatura (°C);
//   weather2           é de dia, elevação do sol (graus), dados válidos, código WMO.
//   O tempo vem do Open-Meteo para a cidade que escreves nas Definições (sem cidade, a
//   app nunca vai à internet e o painel diz isso mesmo).
//
// Técnicas que podes copiar:
//   - texto com asciiGlyph(): rótulos guardados como códigos Latin-1 (LABEL_CHARS);
//   - números grandes com bigFontPixel() (fonte 5x7) e escala adaptada ao painel;
//   - molduras com caracteres de caixa (┌─┐│└┘), que o motor desenha à mão (píxeis certos);
//   - gráficos com histórico: cada coluna do gráfico guarda um valor no estado e, uma vez por
//     segundo, copia o da vizinha da direita; a última coluna recebe o valor atual.

// Rótulos (gerados a partir de texto; os números são códigos Latin-1).
//  0 CPU  1 RAM  2 TEMPO  3 SISTEMA  4 CÉU LIMPO  5 POUCO NUBLADO  6 NUBLADO  7 NEVOEIRO
//  8 CHUVISCO  9 CHUVA  10 NEVE  11 AGUACEIROS  12 TROVOADA  13 SEM CIDADE: VER DEFINIÇÕES
//  14 VENTO  15 NUVENS  16 M/S  17..23 DOM..SÁB  24..35 JAN..DEZ  36 USO  37 MEMÓRIA
//  38 HORA LOCAL  39 SOL
static const uint LABEL_CHARS[210] = {67, 80, 85, 82, 65, 77, 84, 69, 77, 80, 79, 83, 73, 83, 84, 69, 77, 65, 67, 201, 85, 32, 76, 73, 77, 80, 79, 80, 79, 85, 67, 79, 32, 78, 85, 66, 76, 65, 68, 79, 78, 85, 66, 76, 65, 68, 79, 78, 69, 86, 79, 69, 73, 82, 79, 67, 72, 85, 86, 73, 83, 67, 79, 67, 72, 85, 86, 65, 78, 69, 86, 69, 65, 71, 85, 65, 67, 69, 73, 82, 79, 83, 84, 82, 79, 86, 79, 65, 68, 65, 83, 69, 77, 32, 67, 73, 68, 65, 68, 69, 58, 32, 86, 69, 82, 32, 68, 69, 70, 73, 78, 73, 199, 213, 69, 83, 86, 69, 78, 84, 79, 78, 85, 86, 69, 78, 83, 77, 47, 83, 68, 79, 77, 83, 69, 71, 84, 69, 82, 81, 85, 65, 81, 85, 73, 83, 69, 88, 83, 193, 66, 74, 65, 78, 70, 69, 86, 77, 65, 82, 65, 66, 82, 77, 65, 73, 74, 85, 78, 74, 85, 76, 65, 71, 79, 83, 69, 84, 79, 85, 84, 78, 79, 86, 68, 69, 90, 85, 83, 79, 77, 69, 77, 211, 82, 73, 65, 72, 79, 82, 65, 32, 76, 79, 67, 65, 76, 83, 79, 76};
static const uint2 LABELS[40] = {uint2(0, 3), uint2(3, 3), uint2(6, 5), uint2(11, 7), uint2(18, 9), uint2(27, 13), uint2(40, 7), uint2(47, 8), uint2(55, 8), uint2(63, 5), uint2(68, 4), uint2(72, 10), uint2(82, 8), uint2(90, 26), uint2(116, 5), uint2(121, 6), uint2(127, 3), uint2(130, 3), uint2(133, 3), uint2(136, 3), uint2(139, 3), uint2(142, 3), uint2(145, 3), uint2(148, 3), uint2(151, 3), uint2(154, 3), uint2(157, 3), uint2(160, 3), uint2(163, 3), uint2(166, 3), uint2(169, 3), uint2(172, 3), uint2(175, 3), uint2(178, 3), uint2(181, 3), uint2(184, 3), uint2(187, 3), uint2(190, 7), uint2(197, 10), uint2(207, 3)};

// glyphs = "┌┐└┘─│█▁▂▃▄▅▆▇·"
static const uint G_TL = 0, G_TR = 1, G_BL = 2, G_BR = 3, G_H = 4, G_V = 5, G_FULL = 6, G_BLOCK1 = 7, G_DOT = 14;

static int4 P; // área de trabalho do monitor principal (células, sem a barra de tarefas)

// A paleta tem 8 cores com papéis fixos: 0 fundo, 1 painel, 2-3 apagado, 4 destaque, 5 claro,
// 6 quente (alertas, sol), 7 texto.
float4 pal(float k) { return palette(k / 7.0); }

int4 panel(float4 f)
{
    float2 s = float2(P.zw - P.xy);
    return int4(P.x + int(f.x * s.x), P.y + int(f.y * s.y), P.x + int(f.z * s.x), P.y + int(f.w * s.y));
}
bool inside(int2 c, int4 r) { return c.x >= r.x && c.y >= r.y && c.x < r.z && c.y < r.w; }

int frameGlyph(int2 c, int4 r)
{
    bool l = c.x == r.x, rt = c.x == r.z - 1, t = c.y == r.y, b = c.y == r.w - 1;
    if (t && l) return glyph(G_TL);
    if (t && rt) return glyph(G_TR);
    if (b && l) return glyph(G_BL);
    if (b && rt) return glyph(G_BR);
    if (t || b) return glyph(G_H);
    if (l || rt) return glyph(G_V);
    return 0;
}

// Um rótulo começando em pos (uma linha).
int labelGlyph(int2 c, int2 pos, uint id)
{
    uint2 L = LABELS[id];
    int k = c.x - pos.x;
    if (c.y != pos.y || k < 0 || k >= int(L.y)) return 0;
    return asciiGlyph(LABEL_CHARS[L.x + uint(k)]);
}

// Um número inteiro (texto normal), alinhado à direita em pos; devolve 0 fora dele.
int numberGlyph(int2 c, int2 rightPos, int value, uint suffix)
{
    if (c.y != rightPos.y) return 0;
    int k = rightPos.x - c.x; // 0 = sufixo (se houver), depois as unidades...
    if (suffix != 0u) { if (k == 0) return asciiGlyph(suffix); k -= 1; }
    if (k < 0) return 0;
    int v = abs(value);
    int digits = v >= 1000 ? 4 : (v >= 100 ? 3 : (v >= 10 ? 2 : 1));
    if (k < digits) { int d = (v / int(pow(10.0, float(k)) + 0.5)) % 10; return asciiGlyph(48u + uint(d)); }
    if (k == digits && value < 0) return asciiGlyph(45u);
    return 0;
}

// Um caractere da fonte grande 5x7, cada píxel = sx x sy células.
bool bigChar(int2 c, int2 pos, uint ch, int sx, int sy)
{
    int2 q = c - pos;
    if (q.x < 0 || q.y < 0) return false;
    return bigFontPixel(ch, q.x / sx, q.y / sy);
}

// Gráfico com histórico (estado): devolve o valor desta coluna e escreve-o no estado.
float history(int2 c, int4 g, float current, inout float4 state)
{
    bool tick = fixedStep(1.0); // passou um segundo (fixedStep aguenta dias ligado; `time` não)
    float v = prev(c).x;
    if (tick) v = c.x == g.z - 1 ? current : prev(c + int2(1, 0)).x;
    state.x = v;
    return v;
}

// Barra/gráfico de blocos: a coluna enche de baixo para cima até v (0..1).
int barGlyph(int2 c, int4 g, float v)
{
    float h = v * float(g.w - g.y);
    float fromBottom = float(g.w - 1 - c.y);
    if (fromBottom < floor(h)) return glyph(G_FULL);
    if (fromBottom == floor(h) && frac(h) > 0.12) return glyph(G_BLOCK1 + min(uint(frac(h) * 7.0), 6u));
    return 0;
}

uint weatherLabel(float code)
{
    int k = int(code);
    if (k == 0) return 4u;
    if (k <= 2) return 5u;
    if (k == 3) return 6u;
    if (k == 45 || k == 48) return 7u;
    if (k >= 51 && k <= 57) return 8u;
    if ((k >= 61 && k <= 67)) return 9u;
    if (k >= 71 && k <= 77) return 10u;
    if (k >= 80 && k <= 86) return 11u;
    if (k >= 95) return 12u;
    return 6u;
}

CellOut cell(CellIn i)
{
    CellOut o;
    o.value = 0;
    o.glyph = 0;
    o.state = 0;
    o.fg = pal(3);
    o.bg = 0;
    int2 c = int2(i.id);
    P = primaryWorkRect(); // os painéis dentro da área visível (sem a barra de tarefas)

    float4 accent = pal(4), text = pal(7), dim = pal(2.5);

    // Fundo: grelha de pontos e uma linha de varrimento lenta.
    int4 mon = monitorRectAt(c);
    float scan = frac(time * 0.06);
    float scanRow = float(mon.y) + scan * float(mon.w - mon.y);
    float near = exp(-abs(float(c.y) - scanRow) * 0.6);
    if ((c.x % 6) == 0 && (c.y % 3) == 0) { o.glyph = glyph(G_DOT); o.fg = dim * (0.5 + near); }
    o.bg = float4(accent.rgb * near * 0.06, 1);

    int4 clockP = panel(float4(0.04, 0.07, 0.47, 0.42));
    int4 weatherP = panel(float4(0.53, 0.07, 0.96, 0.42));
    int4 cpuP = panel(float4(0.04, 0.5, 0.47, 0.92));
    int4 ramP = panel(float4(0.53, 0.5, 0.96, 0.92));

    // ---------------- Relógio ----------------
    if (inside(c, clockP))
    {
        o.bg = float4(pal(1).rgb, 1);
        int f = frameGlyph(c, clockP);
        if (f != 0) { o.glyph = f; o.fg = accent; return o; }
        int g = labelGlyph(c, clockP.xy + int2(2, 1), 38u);
        if (g != 0) { o.glyph = g; o.fg = dim; return o; }
        int pw = clockP.z - clockP.x;
        // Um píxel da fonte = 2x1 células (as células são ~2x mais altas que largas: fica quadrado).
        int s = max(1, pw / 70), sx = 2 * s, sy = s;
        int charW = 6 * sx;
        int2 start = int2(clockP.x + (pw - 5 * charW) / 2, clockP.y + 3);
        uint hh = uint(localTime.x), mm = uint(localTime.y);
        uint chars[5] = {48u + hh / 10u, 48u + hh % 10u, 58u, 48u + mm / 10u, 48u + mm % 10u};
        bool blink = frac(localTime.z) < 0.5;
        [unroll] for (int k = 0; k < 5; k++)
        {
            if (k == 2 && !blink) continue;
            if (bigChar(c, start + int2(k * charW, 0), chars[k], sx, sy)) { o.glyph = glyph(G_FULL); o.fg = text; return o; }
        }
        // Data: "SÁB 22 NOV 2026" por baixo.
        int dateRow = start.y + 7 * sy + 1;
        int2 d0 = int2(clockP.x + (pw - 15) / 2, dateRow);
        g = labelGlyph(c, d0, 17u + localDate.w);
        if (g == 0) g = numberGlyph(c, d0 + int2(5, 0), int(localDate.z), 0u);
        if (g == 0) g = labelGlyph(c, d0 + int2(7, 0), 23u + localDate.y);
        if (g == 0) g = numberGlyph(c, d0 + int2(14, 0), int(localDate.x), 0u);
        if (g != 0) { o.glyph = g; o.fg = accent; return o; }
        // Segundos: uma barra que enche ao longo do minuto.
        if (c.y == clockP.w - 2 && c.x > clockP.x + 1 && c.x < clockP.z - 2)
        {
            float t = float(c.x - clockP.x - 2) / float(clockP.z - clockP.x - 4);
            o.glyph = glyph(t < localTime.z / 60.0 ? G_FULL : G_DOT);
            o.fg = t < localTime.z / 60.0 ? accent * 0.8 : dim;
        }
        return o;
    }

    // ---------------- Tempo ----------------
    if (inside(c, weatherP))
    {
        o.bg = float4(pal(1).rgb, 1);
        int f = frameGlyph(c, weatherP);
        if (f != 0) { o.glyph = f; o.fg = accent; return o; }
        int g = labelGlyph(c, weatherP.xy + int2(2, 1), 2u);
        if (g != 0) { o.glyph = g; o.fg = dim; return o; }
        bool valid = weather2.z > 0.5;
        int pw = weatherP.z - weatherP.x;
        // Arco do sol: elevação real (com cidade) ou estimada pela hora local.
        float hour = localTime.x + localTime.y / 60.0;
        float elev = valid ? weather2.y : sin((hour - 6.0) / 12.0 * 3.14159) * 55.0;
        float prog = saturate((hour - 6.0) / 12.0);
        float2 arcC = float2(weatherP.x + pw * 0.5, weatherP.w - 2);
        float2 rad = float2(pw * 0.4, (weatherP.w - weatherP.y) * 0.45);
        float2 q = (float2(c) + 0.5 - arcC) / rad;
        if (abs(length(q) - 1.0) < 0.05 && q.y < 0.0) { o.glyph = glyph(G_DOT); o.fg = dim; }
        float ang = 3.14159 * (1.0 - prog);
        float2 sun = arcC + float2(cos(ang), -sin(ang) * saturate(elev / 60.0 + 0.15)) * rad;
        if (all(int2(floor(sun)) == c))
        {
            o.glyph = asciiGlyph(elev > 0 ? 79u : 111u); // O de dia, o à noite
            o.fg = elev > 0 ? pal(6) : dim;
        }
        if (!valid)
        {
            g = labelGlyph(c, int2(weatherP.x + (pw - 26) / 2, weatherP.y + (weatherP.w - weatherP.y) / 2 - 1), 13u);
            if (g != 0) { o.glyph = g; o.fg = text; }
            return o;
        }
        // Temperatura em grande, condição, vento e nuvens.
        int s = max(1, pw / 110), sx = 2 * s, sy = s;
        int temp = int(round(weather.w));
        int2 t0 = int2(weatherP.x + 3, weatherP.y + 3);
        uint tch[4] = {temp < 0 ? 45u : 32u, 48u + uint(abs(temp) / 10 % 10), 48u + uint(abs(temp) % 10), 67u};
        [unroll] for (int k = 0; k < 4; k++)
            if (bigChar(c, t0 + int2(k * 6 * sx, 0), tch[k], sx, sy)) { o.glyph = glyph(G_FULL); o.fg = text; return o; }
        int2 info = int2(t0.x + 4 * 6 * sx + 2, t0.y + 1);
        g = labelGlyph(c, info, weatherLabel(weather2.w));
        if (g != 0) { o.glyph = g; o.fg = accent; return o; }
        g = labelGlyph(c, info + int2(0, 2), 14u);
        if (g == 0) g = numberGlyph(c, info + int2(8, 2), int(round(weather.z)), 0u);
        if (g == 0) g = labelGlyph(c, info + int2(10, 2), 16u);
        if (g == 0) g = labelGlyph(c, info + int2(0, 3), 15u);
        if (g == 0) g = numberGlyph(c, info + int2(10, 3), int(round(weather.y * 100.0)), 37u);
        if (g != 0) { o.glyph = g; o.fg = text; return o; }
        // Chuva a cair dentro do painel.
        if (weather.x > 0.05 && hash21(float2(c.x, floor(c.y - time * 14.0))) < saturate(weather.x * 0.08))
        {
            o.glyph = asciiGlyph(47u);
            o.fg = pal(4.5) * 0.8;
        }
        return o;
    }

    // ---------------- CPU e RAM ----------------
    [unroll] for (int k = 0; k < 2; k++)
    {
        int4 r = k == 0 ? cpuP : ramP;
        if (!inside(c, r)) continue;
        float v = k == 0 ? systemStats.x : systemStats.y;
        o.bg = float4(pal(1).rgb, 1);
        int f = frameGlyph(c, r);
        if (f != 0) { o.glyph = f; o.fg = accent; return o; }
        int g = labelGlyph(c, r.xy + int2(2, 1), k == 0 ? 0u : 1u);
        if (g == 0) g = labelGlyph(c, r.xy + int2(6, 1), k == 0 ? 36u : 37u);
        if (g != 0) { o.glyph = g; o.fg = dim; return o; }
        // Percentagem em grande, à direita.
        int pw = r.z - r.x;
        int s = max(1, pw / 110), sx = 2 * s, sy = s;
        int pct = int(round(v * 100.0));
        uint pch[4] = {pct >= 100 ? 49u : 32u, pct >= 10 ? 48u + uint(pct / 10 % 10) : 32u, 48u + uint(pct % 10), 37u};
        int2 p0 = int2(r.z - 3 - 4 * 6 * sx, r.y + 1);
        float4 hot = lerp(accent, pal(6), saturate((v - 0.6) * 3.0));
        [unroll] for (int j = 0; j < 4; j++)
            if (bigChar(c, p0 + int2(j * 6 * sx, 0), pch[j], sx, sy)) { o.glyph = glyph(G_FULL); o.fg = hot; return o; }
        // Gráfico dos últimos segundos.
        int4 gr = int4(r.x + 2, r.y + 3 + 7 * sy, r.z - 2, r.w - 1);
        if (inside(c, gr))
        {
            float hv = history(c, gr, v, o.state);
            int bg = barGlyph(c, gr, hv);
            float rows = float(gr.w - gr.y);
            float t = float(gr.w - 1 - c.y) / max(rows, 1.0);
            if (bg != 0) { o.glyph = bg; o.fg = lerp(accent * 0.55, hot, t); }
            else if (((gr.w - 1 - c.y) % max(int(rows / 4.0), 1)) == 0) { o.glyph = glyph(G_DOT); o.fg = dim * 0.7; }
        }
        return o;
    }
    return o;
}
