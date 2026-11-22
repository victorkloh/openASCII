// Fluid — um fluido incompressível a sério (o método "stable fluids" de Jos Stam), em ASCII.
//
// Tech demo de MULTI-PASS: render.substeps = 24 faz o shader correr 24 vezes por frame, e
// passIndex diz qual é o passo. Cada passo vê o resultado do anterior via prev(), por isso
// dá para encadear as fases de um algoritmo:
//   passo 0          advecção: cada célula vai buscar a velocidade e a tinta ao sítio de onde
//                    o fluido veio (semi-Lagrangiano, com interpolação bilinear) + forças;
//   passos 1..N-2    iterações de Jacobi para a pressão (resolve a equação de Poisson);
//   passo N-1        projeção: subtrai o gradiente da pressão, o que deixa a velocidade sem
//                    divergência (o fluido não comprime nem estica) e desenha.
// As células são mais altas que largas, por isso as contas usam unidades de mundo
// (worldPerCell) e o fluido fica redondo no ecrã.
//
// Desenho: a tinta escolhe o caractere; onde o fluido corre depressa, o caractere passa a
// ser um traço na direção do escoamento (- / | \); a cor vem da direção da velocidade.
// Rato: arrasta o fluido; clicar faz uma explosão. Sem rato, há jatos a rodar.
//
// state = (velocidade x, velocidade y [mundo/s], tinta, pressão)

static const float PI = 3.14159265;

bool wall(int2 p) { return p.x < 0 || p.y < 0 || p.x >= int(grid.x) || p.y >= int(grid.y) || !visible(p); }

// Estado anterior com paredes: velocidade nula fora dos monitores.
float4 S(int2 p) { return wall(p) ? float4(0, 0, 0, prev(p).w) : prev(p); }

// Interpolação bilinear do estado num ponto qualquer (em células, centros em +0.5), com o
// mínimo e o máximo dos 4 vizinhos (para limitar a correção do MacCormack).
float4 sampleState(float2 pos, out float4 lo, out float4 hi)
{
    float2 q = pos - 0.5;
    int2 b = int2(floor(q));
    float2 f = q - float2(b);
    float4 a = S(b), bx = S(b + int2(1, 0)), by = S(b + int2(0, 1)), bxy = S(b + int2(1, 1));
    lo = min(min(a, bx), min(by, bxy));
    hi = max(max(a, bx), max(by, bxy));
    return lerp(lerp(a, bx, f.x), lerp(by, bxy, f.x), f.y);
}
float4 sampleState(float2 pos) { float4 lo, hi; return sampleState(pos, lo, hi); }

float3 hue(float h)
{
    float3 k = abs(frac(h + float3(0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0;
    return saturate(k);
}

// Rotação local da velocidade (vorticidade), em mundo/s por unidade.
float curlAt(int2 p, float2 h)
{
    return (S(p + int2(1, 0)).y - S(p - int2(1, 0)).y) / (2.0 * h.x) -
           (S(p + int2(0, 1)).x - S(p - int2(0, 1)).x) / (2.0 * h.y);
}

int lineGlyph(float2 v)
{
    uint k = uint(frac((atan2(v.y, v.x) + PI / 8.0) / PI) * 4.0) % 4u;
    static const uint SPRITE[4] = {0u, 3u, 2u, 1u}; // glyphs = "-/|\"
    return glyph(SPRITE[k]);
}

CellOut cell(CellIn i)
{
    CellOut o;
    o.glyph = 0;
    o.bg = 0;
    int2 c = int2(i.id);
    float2 h = worldPerCell; // tamanho de uma célula em unidades de mundo
    float4 s = S(c);
    uint last = max(passCount, 3u) - 1u;

    if (passIndex == 0u)
    {
        // --- Advecção: de onde veio o fluido que agora está aqui? ---
        // MacCormack: advecção para trás, depois "para a frente" a partir do ponto de partida
        // para medir o erro, e metade desse erro corrigido (limitado aos vizinhos, para não
        // criar oscilações). Muito mais nítido do que a advecção simples, que esbate tudo.
        float stepDt = min(dt, 1.0 / 20.0);
        float2 from = i.pos - s.xy * stepDt / h;
        float4 lo, hi;
        float4 a = sampleState(from, lo, hi);
        float4 back = sampleState(from + a.xy * stepDt / h);
        a.xyz = clamp(a.xyz + 0.5 * (s.xyz - back.xyz), lo.xyz, hi.xyz);
        float2 vel = a.xy * (1.0 - 0.15 * stepDt);           // atrito leve
        float dye = a.z * (1.0 - param("fade") * 0.25 * stepDt);

        // Confinamento de vorticidade: empurra o fluido à volta dos remoinhos que já existem,
        // devolvendo os detalhes que a advecção (que suaviza) vai apagando. É o que dá os caracóis.
        float wc = curlAt(c, h);
        float2 eta = float2(abs(curlAt(c + int2(1, 0), h)) - abs(curlAt(c - int2(1, 0), h)),
                            abs(curlAt(c + int2(0, 1), h)) - abs(curlAt(c - int2(0, 1), h)));
        float2 n = eta / (length(eta) + 1e-5);
        vel += float2(n.y, -n.x) * wc * param("curl") * 6.0 * stepDt;

        // --- Forças: jatos que rodam (para nunca parar) ---
        int4 mon = monitorRectAt(c);
        float2 mc = (float2(mon.xy) + float2(mon.zw)) * 0.5 * h;
        float2 ms = float2(mon.zw - mon.xy) * h;
        [unroll] for (int k = 0; k < 3; k++)
        {
            float ang = time * 0.25 * param("swirl") + k * 2.0944;
            float2 pos = mc + float2(cos(ang * 0.7 + k), sin(ang * 0.9)) * ms * float2(0.32, 0.3);
            float2 dir = float2(cos(ang * 1.6 + k * 1.3), sin(ang * 1.6 + k * 1.3));
            float d = length(i.world - pos);
            float wgt = exp(-d * d / (2.0 * 3.0));                  // jato estreito: mais corte, mais remoinhos
            vel += dir * 520.0 * param("jets") * wgt * stepDt;
            dye += wgt * 4.0 * param("jets") * stepDt;
        }
        // --- Rato: arrasta o fluido ---
        if (mouse.presence > 0.001)
        {
            float d = length(i.world - mouse.world);
            float wgt = exp(-d * d / (2.0 * 25.0)) * mouse.presence;
            vel += clamp(mouse.velWorld, -600.0, 600.0) * wgt * 0.9 * stepDt * 8.0;
            dye += wgt * saturate(length(mouse.velWorld) * 0.004) * 3.0 * stepDt;
        }
        if (justClicked())
        {
            float2 d = i.world - mouse.clickWorld;
            float r = length(d);
            float wgt = exp(-r * r / (2.0 * 40.0));
            vel += d / max(r, 1e-3) * 180.0 * wgt;
            dye += wgt * 2.0;
        }
        o.state = float4(vel, saturate(dye), s.w); // a pressão anterior é um bom ponto de partida
    }
    else
    {
        // Divergência da velocidade (diferenças centradas, com espaçamentos diferentes em x e y).
        float4 l = S(c - int2(1, 0)), r = S(c + int2(1, 0)), u = S(c - int2(0, 1)), dn = S(c + int2(0, 1));
        float div = (r.x - l.x) / (2.0 * h.x) + (dn.y - u.y) / (2.0 * h.y);
        // Paredes: pressão "espelhada" (sem fluxo através delas).
        float pl = wall(c - int2(1, 0)) ? s.w : l.w, pr = wall(c + int2(1, 0)) ? s.w : r.w;
        float pu = wall(c - int2(0, 1)) ? s.w : u.w, pd = wall(c + int2(0, 1)) ? s.w : dn.w;
        if (passIndex < last)
        {
            // --- Jacobi: aproxima a pressão que anula a divergência ---
            float ix = 1.0 / (h.x * h.x), iy = 1.0 / (h.y * h.y);
            float p = ((pl + pr) * ix + (pu + pd) * iy - div) / (2.0 * ix + 2.0 * iy);
            o.state = float4(s.xyz, p);
        }
        else
        {
            // --- Projeção: tira o gradiente da pressão à velocidade ---
            float2 grad = float2((pr - pl) / (2.0 * h.x), (pd - pu) / (2.0 * h.y));
            o.state = float4(s.xy - grad, s.z, s.w);
        }
    }

    // Desenho (só o do último passo chega ao ecrã, mas é barato).
    float2 v = o.state.xy;
    float dye = o.state.z;
    float speed = length(v);
    // Cor pela vorticidade: remoinhos num sentido quentes, no outro frios (como nas
    // visualizações científicas de escoamentos); o resto vem da paleta pela quantidade de tinta.
    float w = curlAt(c, h) / 25.0;
    float3 warm = lerp(float3(1.0, 0.45, 0.15), float3(1.0, 0.85, 0.5), saturate(dye));
    float3 cool = lerp(float3(0.15, 0.45, 1.0), float3(0.55, 0.95, 1.0), saturate(dye));
    float3 swirl = lerp(cool, warm, saturate(0.5 + w));
    float3 col = lerp(palette(0.3 + 0.7 * dye).rgb, swirl, param("color") * saturate(abs(w) * 2.0 + 0.25));
    float bright = saturate(dye * 1.6);
    bright = bright * (2.0 - bright); // sobe depressa nos tons baixos, satura devagar
    o.value = saturate(bright * 1.1);
    o.fg = float4(col * (0.35 + 0.9 * bright), 1);
    o.bg = float4(col * bright * 0.16, 1);
    if (speed > 90.0 && dye > 0.25) o.glyph = lineGlyph(v);
    return o;
}
