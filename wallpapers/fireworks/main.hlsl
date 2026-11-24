// Fireworks — foguetes que sobem, explodem em partículas com gravidade e se apagam.
//
// Cada "lançador" k repete-se com um período próprio; a fase dentro do período diz se o
// foguete está a subir ou já explodiu, e onde está cada partícula (física de projétil:
// p = p0 + v·t + ½·g·t²). O rasto vem do estado: cada célula guarda a cor do frame anterior
// e apaga-a devagar, por isso as partículas deixam riscos de luz.

static const int LAUNCHERS = 7;
static const int PARTICLES = 28;
static const float RISE = 1.1; // segundos a subir

// Uma explosão em `burst`, há `age` segundos, com a cor `col`: acende esta célula?
float explosion(float2 w, float2 burst, float age, float seed, float radius, out bool sparkle)
{
    sparkle = false;
    if (age < 0.0 || age > 2.6) return 0.0;
    float best = 0.0;
    [loop] for (int p = 0; p < PARTICLES; p++)
    {
        float a = (float(p) + hash11(seed + p) * 0.6) / PARTICLES * 6.2831853;
        float sp = radius * (0.75 + 0.25 * hash11(seed * 3.0 + p));
        float2 vel = float2(cos(a), sin(a)) * sp;
        float drag = 1.0 - exp(-age * 1.6);                 // abranda com o ar
        float2 pos = burst + vel * drag / 1.6 + float2(0, 9.0 * age * age); // e cai
        if (all(int2(floor(pos / worldPerCell)) == int2(floor(w / worldPerCell))))
        {
            float life = 1.0 - age / 2.6;
            if (life > best)
            {
                best = life;
                sparkle = hash11(seed + p + floor(time * 12.0)) > 0.5;
            }
        }
    }
    return best;
}

CellOut cell(CellIn i)
{
    int2 c = int2(i.id);
    float4 st = prev(c);
    float2 w = i.world;
    int4 mon = monitorRectAt(c);
    float monTop = float(mon.y) * worldPerCell.y;
    // A cidade e os lançamentos no fundo da área de trabalho (não por trás da barra de tarefas).
    float monBottom = float(workRectAt(c).w) * worldPerCell.y;
    float monH = monBottom - monTop;

    // Rasto: a cor do frame anterior a apagar-se.
    float3 color = st.rgb * param("trails");
    float bright = st.a * param("trails");
    int glyphOut = 0;

    // ---- Céu: brilho geral quando há explosões ----
    float skyGlow = 0.0;

    // ---- Foguetes automáticos ----
    [loop] for (int k = 0; k < LAUNCHERS; k++)
    {
        float fk = float(k);
        float period = (3.0 + hash11(fk * 1.3) * 3.0) / param("rate");
        float t = time + hash11(fk * 7.7) * period;
        float cycle = floor(t / period);
        float age = t - cycle * period;
        float seed = fk * 13.0 + cycle * 1.618;
        float x = hash11(seed) * worldSize.x;
        float launchY = monBottom;
        float burstY = monTop + monH * (0.15 + hash11(seed + 2.0) * 0.3);
        float3 col = palette(0.2 + hash11(seed + 5.0) * 0.8).rgb;
        if (age < RISE)
        {
            // A subir: cabeça + pequeno rasto.
            float y = lerp(launchY, burstY, 1.0 - pow(1.0 - age / RISE, 2.0));
            float2 rocket = float2(x + sin(age * 8.0 + seed) * 0.6, y);
            if (all(int2(floor(rocket / worldPerCell)) == c))
            {
                glyphOut = glyph(3);
                color = float3(1, 0.9, 0.7);
                bright = 1;
            }
        }
        else
        {
            bool sparkle;
            float e = explosion(w, float2(x, burstY), age - RISE, seed, 22.0 * param("size"), sparkle);
            if (e > 0.0 && e > bright * 0.9)
            {
                color = lerp(col, float3(1, 1, 1), e > 0.85 ? 0.6 : 0.0);
                bright = e;
                glyphOut = glyph(sparkle ? 1u : (e > 0.5 ? 0u : 2u));
            }
            float d = length(w - float2(x, burstY));
            skyGlow += exp(-d * 0.02) * saturate(1.0 - (age - RISE) * 1.2) * 0.25;
        }
    }

    // ---- Foguete do clique: rebenta logo onde clicaste ----
    float sinceClick = time - mouse.clickTime;
    if (mouse.clickTime >= 0)
    {
        bool sparkle;
        float seed = mouse.clickTime * 7.0;
        float e = explosion(w, mouse.clickWorld, sinceClick, seed, 28.0 * param("size"), sparkle);
        if (e > 0.0 && e > bright * 0.9)
        {
            color = lerp(palette(0.2 + hash11(seed) * 0.8).rgb, float3(1, 1, 1), e > 0.85 ? 0.6 : 0.0);
            bright = e;
            glyphOut = glyph(sparkle ? 1u : (e > 0.5 ? 0u : 2u));
        }
        skyGlow += exp(-length(w - mouse.clickWorld) * 0.02) * saturate(1.0 - sinceClick * 1.2) * 0.35;
    }

    // ---- Cidade em silhueta, com janelas acesas ----
    CellOut o;
    float skyline = 0.0;
    if (param("city") > 0.0)
    {
        float bw = 9.0;
        float b = floor(w.x / bw);
        float bh = monH * lerp(0.06, 0.24, hash11(b * 2.1)) * (hash11(b * 4.4) > 0.15 ? 1.0 : 0.3);
        skyline = monBottom - bh;
        if (w.y > skyline)
        {
            float2 win = floor(w / float2(2.0, 2.5));
            bool lit = hash21(win) > 0.72 && frac(w.x / bw) > 0.12 && frac(w.x / bw) < 0.88;
            o.glyph = lit ? glyph(6) : 0;
            o.fg = lit ? float4(1.0, 0.82, 0.45, 1) : float4(0.05, 0.06, 0.1, 1);
            o.bg = float4(0.02, 0.025, 0.05, 1) + skyGlow * 0.05;
            o.value = 0;
            o.state = float4(0, 0, 0, 0);
            return o;
        }
    }

    float up = saturate((monBottom - w.y) / monH);
    float3 sky = lerp(float3(0.03, 0.04, 0.1), float3(0.0, 0.0, 0.02), up) + skyGlow * float3(0.25, 0.18, 0.3);
    o.glyph = bright > 0.05 ? glyphOut : 0;
    o.value = 0;
    o.fg = float4(color, saturate(bright * 1.4));
    o.bg = float4(sky + color * bright * 0.12, 1);
    // Um rasto que já não tem glifo próprio mostra-se como poeira.
    if (o.glyph == 0 && bright > 0.08) o.glyph = glyph(2);
    o.state = float4(color, bright);
    return o;
}
