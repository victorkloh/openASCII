// Synthwave — o pôr do sol dos anos 80: céu em degradê, sol às riscas, montanhas com contorno
// néon e uma grelha em perspetiva que avança para o horizonte.
//
// Tudo é relativo ao monitor PRINCIPAL (o sol fica centrado nele); no modo estendido o céu
// e a grelha continuam pelos outros ecrãs.

static const float4 NEON_GRID = float4(1.0, 0.25, 0.75, 1.0);
static const float4 NEON_EDGE = float4(0.35, 0.95, 1.0, 1.0);

CellOut cell(CellIn i)
{
    float2 center = primaryCenterWorld();
    float2 size = primarySizeWorld();

    // Câmara: segue o rato devagar (parallax), guardada no state de todas as células.
    float2 target = (mouse.world - center) / size * mouse.presence;
    float2 cam = prev(int2(i.id)).xy;
    cam = lerp(cam, target, 1.0 - exp(-dt * 2.0));
    float2 par = cam * param("parallax");

    float horizonY = center.y - size.y * 0.5 + size.y * param("horizon") + par.y * size.y * 0.04;
    float2 p = i.world - float2(center.x - par.x * size.x * 0.05, horizonY); // y < 0 = céu
    float scale = size.y;

    CellOut o;
    o.bg = float4(0, 0, 0, 0);
    o.state = float4(cam, 0, 0);
    o.glyph = 0;

    if (p.y < 0.0)
    {
        // ---------------- Céu ----------------
        float up = saturate(-p.y / (scale * 0.6)); // 0 no horizonte, 1 lá em cima
        float4 sky = palette(lerp(0.62, 0.0, pow(up, 0.6)));
        float v = lerp(0.25, 0.02, up);

        // Estrelas (só na parte de cima, a cintilar).
        float2 sc = floor(i.world / float2(3.0, 3.0));
        float star = hash21(sc);
        float2 starPos = (sc + 0.25 + hash22(sc) * 0.5) * 3.0;
        bool starCell = all(int2(floor(starPos / worldPerCell)) == int2(i.id));
        if (up > 0.3 && starCell && star > 1.0 - 0.08 * param("stars"))
        {
            float tw = 0.5 + 0.5 * sin(time * (1.0 + star * 3.0) + star * 40.0);
            o.glyph = glyph(tw > 0.8 ? 5u : 4u);
            o.fg = float4(1, 0.9, 1, 1) * (0.5 + 0.5 * tw);
            o.value = 1;
            return o;
        }

        // Sol: disco grande no horizonte, com riscas horizontais na metade de baixo.
        float r = scale * 0.28;
        float2 sp = p - float2(par.x * size.x * 0.02, -r * 0.55);
        float d = length(sp);
        if (d < r)
        {
            float t = saturate((sp.y + r) / (2.0 * r)); // 0 em cima, 1 em baixo
            float stripe = frac((sp.y / r) * 7.0 - time * 0.15 * param("speed"));
            float gap = smoothstep(0.0, 1.0, t) * 0.55; // as riscas engrossam para baixo
            if (t > 0.45 && stripe < gap)
            {
                o.fg = sky;
                o.value = v;
            }
            else
            {
                o.fg = lerp(float4(1.0, 0.92, 0.45, 1), float4(1.0, 0.25, 0.55, 1), t);
                o.value = lerp(1.0, 0.8, t);
            }
            return o;
        }
        // Brilho à volta do sol.
        float glow = exp(-(d - r) / (scale * 0.12));
        sky = lerp(sky, float4(1.0, 0.45, 0.6, 1), glow * 0.5);
        v = max(v, glow * 0.35);

        // Montanhas: uma linha de cumes à frente do sol, com contorno néon.
        float ridge = fbm(float2((i.world.x + par.x * size.x * 0.08) * 0.018, 3.0));
        float mountainTop = -scale * (0.03 + ridge * 0.24);
        if (p.y > mountainTop)
        {
            bool edge = p.y - mountainTop < worldPerCell.y * 1.2;
            o.fg = edge ? NEON_EDGE : float4(0.22, 0.06, 0.36, 1);
            o.value = edge ? 0.7 : 0.3;
            o.glyph = edge ? glyph(0) : 0;
            return o;
        }
        o.fg = sky;
        o.value = v;
        return o;
    }

    // ---------------- Grelha em perspetiva ----------------
    // Projeção: uma linha do ecrã a `p.y` abaixo do horizonte vê o chão à distância z.
    float camHeight = scale * 0.12;
    float z = camHeight * scale * 0.9 / max(p.y, 0.001);
    float gx = p.x * z / (scale * 0.9);
    float travel = time * 22.0 * param("speed");
    float lineW = z / scale * 0.9; // largura de uma célula no chão, a esta distância
    float cellX = worldPerCell.x * lineW * 1.4;
    float cellZ = worldPerCell.y * z * z / (camHeight * scale * 0.9) * 1.2;
    float spacing = 7.0;
    float fx = abs(frac(gx / spacing + 0.5) - 0.5) * spacing;
    float fz = abs(frac((z + travel) / spacing + 0.5) - 0.5) * spacing;
    bool vertical = fx < max(cellX * 0.6, 0.15);
    bool horizontal = fz < max(cellZ * 0.6, 0.15);
    float fade = saturate(1.0 - z / (scale * 6.0));

    float4 ground = float4(0.07, 0.01, 0.12, 1);
    o.fg = ground;
    o.value = 0.05;
    if (horizontal || vertical)
    {
        o.fg = lerp(float4(0.35, 0.05, 0.4, 1), NEON_GRID, fade);
        o.value = 0.6 * fade + 0.2;
        if (horizontal) o.glyph = glyph(0);
        else
        {
            // A inclinação da linha vertical no ecrã escolhe / | \.
            float slope = gx / max(z, 0.001);
            o.glyph = glyph(abs(slope) < 0.08 ? 1u : (slope > 0 ? 3u : 2u));
        }
    }
    // Reflexo do sol no chão, perto do horizonte.
    float reflect = exp(-p.y / (scale * 0.05)) * exp(-abs(p.x) / (scale * 0.25));
    o.fg = lerp(o.fg, float4(1, 0.4, 0.6, 1), reflect * 0.6);
    o.bg = float4(lerp(ground.rgb, float3(0.3, 0.05, 0.25), reflect * 0.5), 1);
    return o;
}
