// Life — o Jogo da Vida de Conway, com memória e cor.
//
// Tech demo de autómatos clássicos com estado rico: além de viva/morta, cada célula guarda
// há quanto tempo está viva (a cor muda com a idade) e um rasto que desvanece depois de
// morrer. As regras são as de sempre (B3/S23): nasce com 3 vizinhas, sobrevive com 2 ou 3.
//
// O mundo dá a volta nas bordas (toro). Para nunca acabar parado, caem "meteoros" de vez em
// quando: pequenas manchas aleatórias que reacendem a vida. O rato deixa um rasto de células
// vivas; clicar lança um planador (glider) em cada uma das 4 diagonais.
//
// O ritmo é "rate" gerações por segundo, a qualquer FPS (fixedStep).
//
// state = (viva 0/1, idade em gerações, rasto 0..1, -)

int2 wrap(int2 p) { return (p + int2(grid)) % int2(grid); }
bool alive(int2 p) { p = wrap(p); return visible(p) && prev(p).x > 0.5; }

// Planador de 3x3 (célula (x,y) relativa ao centro), orientado por (sx, sy) = ±1.
bool gliderCell(int2 d, int sx, int sy)
{
    int2 q = int2(d.x * sx, d.y * sy) + 1; // 0..2
    if (any(q < 0) || any(q > 2)) return false;
    // .#.
    // ..#
    // ###
    return (q.y == 0 && q.x == 1) || (q.y == 1 && q.x == 2) || q.y == 2;
}

CellOut cell(CellIn i)
{
    CellOut o;
    o.glyph = 0;
    o.bg = 0;
    int2 c = int2(i.id);
    float4 s = prev(c);

    // "rate" gerações por segundo; nos outros frames só se desenha (o rasto continua a desvanecer).
    bool step = fixedStep(param("rate"));

    bool was = s.x > 0.5;
    bool now = was;
    float age = s.y, trail = s.z;
    if (step)
    {
        int n = 0;
        [unroll] for (int y = -1; y <= 1; y++)
        [unroll] for (int x = -1; x <= 1; x++)
            if (x != 0 || y != 0) n += alive(c + int2(x, y)) ? 1 : 0;
        now = n == 3 || (was && n == 2);
        age = now ? (was ? age + 1.0 : 0.0) : 0.0;
        trail = now ? 1.0 : trail * 0.88;
    }

    // Arranque: uma "sopa" aleatória.
    if (s.w < 0.5) { now = hash21(float2(c) * 0.71 + 3.0) < 0.28; age = 0; trail = now ? 1 : 0; }

    // Meteoros: a cada ~4 s, algumas zonas recebem uma mancha aleatória.
    float2 zone = floor(i.world / 24.0);
    float tick = floor(time / 4.0);
    if (step && hash21(zone + tick * 7.3) > 0.93 && length(frac(i.world / 24.0) - 0.5) < 0.18)
        now = now || hash21(float2(c) + tick) < 0.45;

    // Rato e clique.
    if (mouse.presence > 0.3 && length(i.world - mouse.world) < 2.0 && hash21(float2(c) + time) < 0.5) now = true;
    if (justClicked())
    {
        int2 m = int2(floor(mouse.clickPos));
        [unroll] for (int k = 0; k < 4; k++)
        {
            int sx = (k & 1) ? 1 : -1, sy = (k & 2) ? 1 : -1;
            if (gliderCell(c - (m + int2(sx, sy) * 4), sx, sy)) now = true;
        }
    }
    if (!visible(c)) now = false;

    o.state = float4(now ? 1 : 0, age, now ? max(trail, 1.0) : trail, 1);

    // Desenho: as recém-nascidas brilham, as velhas arrefecem; o rasto fica como poeira.
    if (now)
    {
        float t = saturate(age / 40.0);
        o.value = 1.0 - t * 0.35;
        o.fg = palette(1.0 - t * 0.55);
        o.bg = float4(palette(0.5 - t * 0.2).rgb * 0.22, 1);
    }
    else
    {
        o.value = trail * 0.35;
        o.fg = palette(0.3 + 0.3 * trail) * trail;
    }
    return o;
}
