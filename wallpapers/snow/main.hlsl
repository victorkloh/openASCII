// Snow — neve que cai e ASSENTA em cima das tuas janelas, dos ícones e da barra de tarefas.
//
// Tech demo de [inputs] windows = true e icons = true: a app entrega ao shader os retângulos
// das janelas abertas (de cima para baixo) e dos ícones, em células. obstacleAt(c) diz se a
// célula está por baixo de algum. Mexe uma janela e a neve que estava em cima cai.
//
// A neve assente é um autómato celular guardado no estado (prev/state), como areia:
//   - um floco sem apoio por baixo cai uma célula por frame;
//   - um floco apoiado pode escorregar na diagonal (o ângulo das pilhas). Para dois flocos
//     nunca irem para a mesma célula, em frames pares só se escorrega para a esquerda e nos
//     ímpares só para a direita, e a célula de destino confirma a mesma regra (gather);
//   - em cima de qualquer superfície vão "aterrando" flocos ao acaso, até uma altura máxima.
// Os flocos que caem pelo ar não têm estado: são calculados a partir do tempo (3 camadas).
//
// state.x = há neve assente (0/1), state.y = altura da pilha nesta célula (células até ao chão)

static const float PI = 3.14159265;

// "Chão": fora dos monitores (por baixo do último ecrã), ou por baixo de uma janela/ícone.
bool solidSlow(int2 p)
{
    if (p.y >= int(grid.y) || p.x < 0 || p.x >= int(grid.x)) return true;
    if (p.y < 0) return false;
    return !visible(p) || obstacleAt(p) >= 0;
}

// O autómato só olha para a vizinhança 3x3 de cada célula, mas pergunta por ela ~20 vezes, e
// cada pergunta percorria TODOS os obstáculos (até 256 janelas e ícones): com o ambiente de
// trabalho cheio eram milhares de testes por célula, a cada frame. Agora a vizinhança é
// calculada uma vez, numa só passagem pelos obstáculos, e guardada em 9 bits.
// Truque útil para qualquer wallpaper que consulte muito os obstáculos.
static int2 C0;     // a célula desta thread
static uint SOLID9; // bit (dy + 1) * 3 + (dx + 1) = solidSlow(C0 + (dx, dy))

void buildSolid(int2 c)
{
    C0 = c;
    uint bits = 0;
    [unroll] for (int dy = -1; dy <= 1; dy++)
    [unroll] for (int dx = -1; dx <= 1; dx++)
    {
        int2 p = c + int2(dx, dy);
        bool s = p.y >= int(grid.y) || p.x < 0 || p.x >= int(grid.x) || (p.y >= 0 && !visible(p));
        if (s) bits |= 1u << uint((dy + 1) * 3 + (dx + 1));
    }
    [loop] for (uint k = 0; k < obstacleCount(); k++)
    {
        int4 r = obstacleRect(k);
        if (r.z < c.x || r.x > c.x + 1 || r.w < c.y || r.y > c.y + 1) continue; // longe desta vizinhança
        [unroll] for (int dy2 = -1; dy2 <= 1; dy2++)
        [unroll] for (int dx2 = -1; dx2 <= 1; dx2++)
        {
            int2 p = c + int2(dx2, dy2);
            if (p.y >= 0 && p.x >= r.x && p.y >= r.y && p.x < r.z && p.y < r.w)
                bits |= 1u << uint((dy2 + 1) * 3 + (dx2 + 1));
        }
    }
    SOLID9 = bits;
}

bool solid(int2 p)
{
    int2 d = p - C0;
    if (all(abs(d) <= 1)) return ((SOLID9 >> uint((d.y + 1) * 3 + (d.x + 1))) & 1u) != 0u;
    return solidSlow(p);
}

bool snowAt(int2 p) { return p.y >= 0 && !solid(p) && prev(p).x > 0.5; }

bool supported(int2 p) { return solid(p + int2(0, 1)) || snowAt(p + int2(0, 1)); }

// O floco em s escorrega para s + (dir, 1)? A mesma resposta para a origem e para o destino.
bool slides(int2 s, int dir)
{
    if (!snowAt(s) || !supported(s)) return false;
    int2 t = s + int2(dir, 1);
    int2 side = s + int2(dir, 0);
    if (solid(t) || snowAt(t) || solid(side) || snowAt(side)) return false;
    return hash21(float2(s) + float(frameIndex % 1024u) * 0.731) < 0.55;
}

// Vento: uma brisa lenta. O que conta para a posição dos flocos é o caminho já percorrido
// (o integral do vento), não "vento x tempo": com time grande, qualquer mudança no vento
// faria todos os flocos saltarem ao mesmo tempo.
//   vento(t) = 0.6·sin(0.13t) + 0.4·sin(0.041t + 1.3)   ->   deriva(t) = integral de 0 a t
float windDrift()
{
    return (-0.6 / 0.13 * cos(time * 0.13) - 0.4 / 0.041 * cos(time * 0.041 + 1.3)) * param("wind");
}

// O rato afasta os flocos à sua volta (um desvio de posição, sem mexer no tempo).
float2 mousePush(float2 world)
{
    if (mouse.presence < 0.001) return 0;
    float2 d = world - mouse.world;
    float r2 = dot(d, d);
    return d / max(sqrt(r2), 1e-3) * 5.0 * exp(-r2 / 60.0) * mouse.presence;
}

CellOut cell(CellIn i)
{
    CellOut o;
    o.value = 0;
    o.glyph = 0;
    o.fg = 0;
    o.state = 0;
    int2 c = int2(i.id);
    buildSolid(c);

    // Céu: um degradê muito discreto (mais escuro em cima).
    int4 mon = monitorRectAt(c);
    float v = float(c.y - mon.y) / max(float(mon.w - mon.y), 1.0);
    o.bg = float4(lerp(palette(0.0).rgb, palette(0.15).rgb, v * v), 1);

    if (solid(c)) return o; // por baixo de uma janela: invisível e sem neve

    // --- Neve assente (autómato), a 30 passos por segundo a qualquer FPS ---
    int dir = (uint(floor(time * 30.0)) & 1u) == 0u ? -1 : 1;
    bool self = snowAt(c);
    bool snow;
    bool stepNow = fixedStep(30.0);
    if (!stepNow)
        snow = self;
    else if (self)
        snow = supported(c) && !slides(c, dir);                  // fica, a não ser que caia ou escorregue
    else
        // Esta célula está vazia, por isso um floco logo acima não tem apoio e cai para aqui;
        // ou chega um floco a escorregar na diagonal.
        snow = snowAt(c - int2(0, 1)) || slides(c - int2(dir, 1), dir);

    // Altura da pilha: 1 em cima do chão, +1 por cada floco por baixo.
    float below = solid(c + int2(0, 1)) ? 0.0 : prev(c + int2(0, 1)).y;
    float depth = below + 1.0;

    // Aterrar: flocos novos em cima de superfícies, até à altura máxima.
    float rnd = hash21(float2(c) * 1.37 + frac(time * 0.917) * 517.0);
    if (stepNow && !snow && supported(c) && depth <= param("depth") && rnd < param("rate") * (1.0 / 30.0) * 0.08)
        snow = true;
    // Derreter devagarinho no topo das pilhas (para nunca ficar tudo branco).
    if (stepNow && snow && !snowAt(c - int2(0, 1)) && rnd > 1.0 - param("melt") * (1.0 / 30.0) * 0.004)
        snow = false;
    // Pilha mais alta do que o limite (baixaste "depth"): o topo vai-se embora logo.
    if (stepNow && snow && !snowAt(c - int2(0, 1)) && depth > param("depth") + 0.5 && rnd < 0.25)
        snow = false;

    // O rato varre a neve quando passa depressa; um clique é uma bola de neve.
    if (mouse.presence > 0.001)
    {
        float d = length(i.world - mouse.world);
        float speed = length(mouse.velWorld);
        if (snow && d < 6.0 && speed > 60.0 && rnd < 0.5 * mouse.presence) snow = false;
    }
    float since = time - mouse.clickTime;
    if (mouse.clickTime >= 0 && since < 0.1 && length(i.world - mouse.clickWorld) < 10.0) snow = false;

    o.state = float4(snow ? 1.0 : 0.0, snow ? depth : 0.0, 0, 0);

    if (snow)
    {
        bool top = !snowAt(c - int2(0, 1));
        float shade = 0.82 + 0.18 * hash21(float2(c) * 0.5);
        o.fg = palette(1.0) * shade;
        o.glyph = glyph(top ? 5u : 6u);       // ▄ no topo (meia célula: superfície mais suave), █ por dentro
        if (!top) o.bg = float4(palette(0.7).rgb * 0.5, 1);
        return o;
    }

    // --- Flocos a cair (sem estado), em três camadas de profundidade ---
    [unroll] for (int layer = 0; layer < 3; layer++)
    {
        float near = float(layer) / 2.0;                     // 0 = longe, 1 = perto
        // Mais "rate" = nevão mais forte também no ar (vê-se logo, a acumulação demora).
        float cellSize = lerp(14.0, 7.0, near) / (param("density") * sqrt(max(param("rate"), 0.05)));
        float fall = lerp(9.0, 22.0, near) * param("speed");
        float drift = windDrift() * lerp(4.0, 12.0, near);
        float2 p = i.world - mousePush(i.world) * near;      // os flocos de perto desviam-se mais
        p.y -= time * fall;
        p.x -= drift;
        float2 id = floor(p / cellSize);
        float2 h = hash22(id + float(layer) * 31.0);
        if (h.x > lerp(0.45, 0.65, near)) continue;          // nem todas as células têm floco
        // Posição do floco dentro da sua célula, a balançar.
        float2 flake = (id + 0.2 + 0.6 * h) * cellSize;
        flake.x += sin(time * (0.8 + h.y) + h.x * 20.0) * cellSize * 0.25;
        // Voltar ao espaço do ecrã e ver se cai nesta célula.
        float2 screen = flake + float2(drift, time * fall) + mousePush(i.world) * near;
        if (all(int2(floor(screen / worldPerCell)) == c))
        {
            o.glyph = glyph(layer == 0 ? 0u : (layer == 1 ? 1u : (h.y > 0.6 ? 3u : 2u)));
            o.fg = palette(lerp(0.55, 1.0, near)) * lerp(0.55, 1.0, near);
        }
    }
    // Brilho do clique.
    if (mouse.clickTime >= 0 && since < 0.6)
    {
        float d = length(i.world - mouse.clickWorld);
        float ring = exp(-pow(d - since * 40.0, 2.0) * 0.2) * (1.0 - since / 0.6);
        if (ring > 0.2) { o.glyph = glyph(2u); o.fg = palette(1.0) * ring; }
    }
    return o;
}
