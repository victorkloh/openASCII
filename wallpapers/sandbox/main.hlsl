// Sandbox — areia, água e pedra a cair, como nos "falling sand games".
//
// Tech demo de simulação por autómato celular com vários passos por frame
// (render.substeps): cada passo vê o resultado do anterior via prev().
//
// O problema difícil numa GPU: todas as células decidem AO MESMO TEMPO, e cada uma só pode
// escrever na sua própria célula. Por isso cada movimento é uma "troca" combinada: a célula
// de origem e a de destino avaliam exatamente a mesma condição (uma função pura do estado
// anterior) e cada uma escreve a sua metade da troca. Para dois grãos nunca escolherem o
// mesmo destino:
//   - cair a direito tem prioridade sobre tudo;
//   - escorregar na diagonal e escorrer para o lado só acontecem num sentido por passo
//     (esquerda nos pares, direita nos ímpares);
//   - a água só escorre para uma célula que ninguém mais vai ocupar nesse passo.
// A água tem "momento" (continua no sentido em que vinha e só vira quando bate em algo);
// sem isso ia e voltava no mesmo sítio e fazia pilhas como a areia.
//
// Truque de desempenho: cada célula lê UMA vez a vizinhança de que precisa (3x4 células)
// para arrays locais, espelhada consoante o sentido do passo, e as regras trabalham sempre
// "para a direita" com índices constantes. Escrever as regras a chamar prev() umas dentro
// das outras fazia o compilador gerar uma árvore enorme (9 minutos a compilar!).
//
// Rato: botão esquerdo = areia, direito = água, do meio = pedra, esquerdo+direito = apagar.
// Sem ninguém a mexer, há bicas no topo que vão alternando entre areia e água, e o fundo
// vai "absorvendo" devagar.
//
// state.x = material (0 vazio, 1 areia, 2 água, 3 pedra), state.y = variação de cor,
// state.z = sentido da água (-1/+1, no mundo)

static const float EMPTY = 0, SAND = 1, WATER = 2, STONE = 3;

uint stepIndex() { return frameIndex * passCount + passIndex; }
float rnd(int2 p, float salt) { return hash21(float2(p) * 0.913 + float(stepIndex() % 4096u) * 0.317 + salt); }

// Prateleiras de pedra fixas (dependem só do tamanho de cada monitor): dão forma às cascatas.
bool shelfAt(int2 c)
{
    if (param("shelves") < 0.5) return false;
    int4 m = monitorRectAt(c);
    float2 r = (float2(c - m.xy) + 0.5) / float2(m.zw - m.xy); // 0..1 dentro do monitor
    float2 px = 1.0 / float2(m.zw - m.xy);                      // uma célula, nessa escala
    bool slope1 = abs(r.y - (0.38 + (r.x - 0.18) * 0.22)) < px.y * 0.75 && r.x > 0.1 && r.x < 0.42;
    bool slope2 = abs(r.y - (0.55 - (r.x - 0.6) * 0.25)) < px.y * 0.75 && r.x > 0.55 && r.x < 0.9;
    bool bowl = abs(r.y - 0.8) < px.y * 0.75 && r.x > 0.3 && r.x < 0.62;
    bool bowlL = abs(r.x - 0.3) < px.x * 0.75 && r.y > 0.7 && r.y < 0.81;
    bool bowlR = abs(r.x - 0.62) < px.x * 0.75 && r.y > 0.7 && r.y < 0.81;
    return slope1 || slope2 || bowl || bowlL || bowlR;
}

float matAt(int2 p)
{
    if (p.x < 0 || p.y < 0 || p.x >= int(grid.x) || p.y >= int(grid.y) || !visible(p) || shelfAt(p)) return STONE;
    return round(prev(p).x);
}

// Vizinhança local: NM = material, NW = momento da água (+1 = "para a direita" local).
// Índices: [dx + 2][dy + 2], dx ∈ -2..2 (espelhado pelo sentido do passo), dy ∈ -2..1.
static float NM[5][4];
static float NW[5][4];
static int2 C;
static int D;

float M(int dx, int dy) { return NM[dx + 2][dy + 2]; }
float W(int dx, int dy) { return NW[dx + 2][dy + 2]; }
int2 world(int dx, int dy) { return C + int2(dx * D, dy); }
float R(int dx, int dy, float salt) { return rnd(world(dx, dy), salt); }

bool sandFalls(int dx, int dy) { float b = M(dx, dy + 1); return M(dx, dy) == SAND && (b == EMPTY || b == WATER); }
bool displaced(int dx, int dy) { return M(dx, dy) == WATER && sandFalls(dx, dy - 1); } // a areia de cima troca com esta água
bool waterFalls(int dx, int dy) { return M(dx, dy) == WATER && !displaced(dx, dy) && M(dx, dy + 1) == EMPTY; }
bool fallsInto(int dx, int dy) { return sandFalls(dx, dy - 1) || waterFalls(dx, dy - 1); }
bool sandSlides(int dx, int dy)
{
    return M(dx, dy) == SAND && !sandFalls(dx, dy) && M(dx + 1, dy + 1) == EMPTY && M(dx + 1, dy) == EMPTY &&
           R(dx, dy, 1.0) < 0.85;
}
bool waterSlides(int dx, int dy)
{
    return M(dx, dy) == WATER && !displaced(dx, dy) && !waterFalls(dx, dy) && W(dx, dy) > 0 &&
           M(dx + 1, dy + 1) == EMPTY && M(dx + 1, dy) == EMPTY;
}
// Escorrer para o lado: a célula de destino tem de estar vazia e mais ninguém a pode ocupar
// neste passo (nem quem cai de cima, nem quem escorrega na diagonal para lá).
bool freeTarget(int tx, int dy)
{
    return M(tx, dy) == EMPTY && !fallsInto(tx, dy) && !sandSlides(tx - 1, dy - 1) && !waterSlides(tx - 1, dy - 1);
}
bool canFlow(int dx, int dy)
{
    return M(dx, dy) == WATER && !displaced(dx, dy) && !waterFalls(dx, dy) && !waterSlides(dx, dy) && W(dx, dy) > 0 &&
           R(dx, dy, 2.0) < 0.95;
}
// Duas células de uma vez quando as duas estão livres (a água espalha-se o dobro mais depressa)...
bool waterFlows2(int dx, int dy) { return canFlow(dx, dy) && M(dx + 1, dy) == EMPTY && freeTarget(dx + 2, dy); }
// ...senão, uma.
bool waterFlows(int dx, int dy) { return canFlow(dx, dy) && !waterFlows2(dx, dy) && freeTarget(dx + 1, dy); }

CellOut cell(CellIn i)
{
    CellOut o;
    o.glyph = 0;
    o.value = 0;
    o.bg = 0;
    C = int2(i.id);
    D = (stepIndex() & 1u) == 0u ? -1 : 1;
    [unroll] for (int y = 0; y < 4; y++)
    [unroll] for (int x = 0; x < 5; x++)
    {
        int2 p = world(x - 2, y - 2);
        NM[x][y] = matAt(p);
        NW[x][y] = (prev(p).z < 0 ? -1.0 : 1.0) * float(D);
    }

    float m = M(0, 0);
    float tint = prev(C).y;
    float mom = W(0, 0); // local
    bool shelf = shelfAt(C);

    // 60 frames de simulação por segundo a qualquer FPS (acima disso, os frames a mais só desenham).
    bool stepNow = fixedStep(60.0);
    if (shelf) { m = STONE; tint = 0.5; }
    else if (!stepNow) { }
    else if (m == SAND)
    {
        if (sandFalls(0, 0)) { m = M(0, 1); tint = prev(world(0, 1)).y; mom = W(0, 1); } // troca com o de baixo
        else if (sandSlides(0, 0)) { m = EMPTY; }
    }
    else if (m == WATER)
    {
        if (displaced(0, 0)) { m = SAND; tint = prev(world(0, -1)).y; }                    // a areia caiu para aqui
        else if (waterFalls(0, 0) || waterSlides(0, 0) || waterFlows2(0, 0) || waterFlows(0, 0)) { m = EMPTY; }
        else if (mom > 0 && M(1, 0) != EMPTY) { mom = -mom; }                              // bateu: vira
    }
    else if (m == EMPTY)
    {
        if (sandFalls(0, -1)) { m = SAND; tint = prev(world(0, -1)).y; }
        else if (waterFalls(0, -1)) { m = WATER; tint = prev(world(0, -1)).y; mom = W(0, -1); }
        else if (sandSlides(-1, -1)) { m = SAND; tint = prev(world(-1, -1)).y; }
        else if (waterSlides(-1, -1)) { m = WATER; tint = prev(world(-1, -1)).y; mom = 1; }
        else if (waterFlows(-1, 0)) { m = WATER; tint = prev(world(-1, 0)).y; mom = 1; }
        else if (waterFlows2(-2, 0)) { m = WATER; tint = prev(world(-2, 0)).y; mom = 1; }
    }
    float momWorld = mom * float(D);

    // --- Só no primeiro passo de cada frame: bicas, rato e o fundo a absorver ---
    if (passIndex == 0u && !shelf && stepNow)
    {
        int4 mon = monitorRectAt(C);
        float monW = float(mon.z - mon.x);
        // Bicas: 3 por monitor, a alternar areia/água a cada ~12 s, cada uma com o seu ritmo.
        if (C.y <= mon.y + 1 && m == EMPTY)
        {
            [unroll] for (int k = 0; k < 3; k++)
            {
                float x = float(mon.x) + monW * (0.2 + 0.3 * k) + sin(time * 0.2 + k) * monW * 0.05;
                float phase = floor(time / 12.0 + k * 0.37);
                bool on = hash11(phase * 7.1 + k) < 0.75;
                float kind = hash11(phase * 3.3 + k * 1.7) < 0.55 ? SAND : WATER;
                if (on && abs(float(C.x) - x) < 1.2 * param("flow") && rnd(C, 3.0) < (kind == WATER ? 0.18 : 0.3))
                {
                    m = kind;
                    tint = rnd(C, 4.0);
                    momWorld = rnd(C, 8.0) < 0.5 ? -1.0 : 1.0;
                }
            }
        }
        // Rato (o clique tem de começar no ambiente de trabalho).
        if (mouse.buttons != 0u && length(i.world - mouse.world) < param("brush") && m != STONE)
        {
            bool erase = (mouse.buttons & (MOUSE_LEFT | MOUSE_RIGHT)) == (MOUSE_LEFT | MOUSE_RIGHT);
            if (erase) m = EMPTY;
            else if (mouse.buttons & MOUSE_MIDDLE) m = STONE;
            else if (rnd(C, 5.0) < 0.6) m = (mouse.buttons & MOUSE_LEFT) ? SAND : WATER;
            tint = rnd(C, 6.0);
            momWorld = rnd(C, 8.0) < 0.5 ? -1.0 : 1.0;
        }
        // O fundo de cada monitor absorve devagar (senão o ecrã acabava cheio).
        if (C.y == mon.w - 1 && m != STONE && rnd(C, 7.0) < param("drain") * (m == WATER ? 0.08 : 0.01)) m = EMPTY;
    }

    o.state = float4(m, tint, momWorld, 0);

    // --- Desenho ---
    float n = hash21(float2(C) * 0.37);
    if (m == SAND)
    {
        o.fg = palette(0.55 + 0.35 * tint);
        o.glyph = glyph(tint > 0.66 ? 0u : (tint > 0.33 ? 1u : 2u));
        o.bg = float4(palette(0.45).rgb * 0.25, 1);
    }
    else if (m == WATER)
    {
        float shimmer = 0.5 + 0.5 * sin(time * 3.0 + float(C.x) * 0.7 + float(C.y) * 0.3);
        bool surface = M(0, -1) == EMPTY;
        o.fg = lerp(float4(0.15, 0.45, 0.95, 1), float4(0.6, 0.85, 1.0, 1), surface ? 0.8 : shimmer * 0.35);
        o.glyph = glyph(surface ? 3u : 4u);
        o.bg = float4(0.02, 0.10, 0.28, 1);
    }
    else if (m == STONE)
    {
        o.fg = palette(0.2 + 0.1 * n);
        o.glyph = glyph(5u);
        o.bg = float4(palette(0.12).rgb, 1);
    }
    return o;
}
