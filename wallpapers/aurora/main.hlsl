// Aurora — cortinas de luz que ondulam sobre montanhas, refletidas num lago.
//
// Uma cortina é uma faixa que serpenteia na horizontal (fbm ao longo de x e do tempo).
// O brilho é forte na borda de baixo e desvanece para cima, cortado em "raios" verticais
// por ruído fino: é isso que dá o aspeto de cortina. A cor vai do verde (em baixo) ao
// violeta e rosa (em cima), como as auroras reais.

float auroraAt(float2 w, float2 center, float2 size, float t)
{
    float speed = param("speed");
    float total = 0.0;
    [unroll] for (int k = 0; k < 3; k++)
    {
        float fk = float(k);
        float x = w.x * (0.006 + fk * 0.002);
        // Linha da borda de baixo da cortina (serpenteia).
        float base = center.y - size.y * (0.02 + fk * 0.1) -
                     (fbm(float2(x + t * 0.03 * speed + fk * 7.0, fk)) - 0.5) * size.y * 0.35;
        // Agitação perto do rato.
        {
            float d = abs(w.x - mouse.world.x);
            base += sin(t * 3.0 + w.x * 0.15) * exp(-d * d * 0.0015) * 4.0 * mouse.presence;
        }
        float above = base - w.y; // > 0 = acima da borda
        if (above < -2.0) continue;
        float tall = size.y * param("height") * 0.5 * (0.5 + 0.5 * noise(float2(x * 3.0, t * 0.05 + fk)));
        float body = exp(-max(above, 0.0) / max(tall, 1.0)) * smoothstep(-2.0, 1.0, above);
        // Raios verticais finos que se mexem; elevados a uma potência para haver céu escuro entre eles.
        float rays = noise(float2(w.x * 0.3 + fk * 13.0, t * 0.4 * speed));
        rays = pow(saturate(rays * 1.45 - 0.1), 1.5);
        rays *= 0.6 + 0.4 * noise(float2(w.x * 1.1, t * 1.1 * speed + fk));
        // A cortina mais forte manda (não somar: senão o céu enche-se de luz).
        total = max(total, body * rays * (1.0 - fk * 0.3));
    }
    return total * 1.7 * param("intensity");
}

CellOut cell(CellIn i)
{
    float2 center = primaryCenterWorld();
    float2 size = primarySizeWorld();
    int4 mon = monitorRectAt(int2(i.id));
    float monBottom = float(mon.w) * worldPerCell.y;
    float monTop = float(mon.y) * worldPerCell.y;
    float monH = monBottom - monTop;

    // Horizonte (base das montanhas) e lago, por monitor.
    float horizon = monTop + monH * (param("lake") > 0.0 ? 0.72 : 0.9);
    float2 w = i.world;
    bool inLake = w.y > horizon && param("lake") > 0.0;
    float rippleX = 0.0;
    if (inLake)
    {
        // Espelho: ver o céu refletido, com ondulação.
        float depth = w.y - horizon;
        rippleX = sin(w.y * 1.3 + time * 1.5) * (0.6 + depth * 0.05);
        w = float2(w.x + rippleX, horizon - depth * 1.15);
    }

    CellOut o;
    o.bg = float4(0, 0, 0, 0);
    o.state = float4(0, 0, 0, 0);
    o.glyph = 0;

    // Montanhas (silhueta escura, com neve nos picos).
    // Cristas recortadas: ruído largo + ruído fino, com "dentes" (abs) para parecer rocha.
    float ridge = fbm(float2(w.x * 0.018, 1.7)) * 0.6 + abs(noise(float2(w.x * 0.09, 4.0)) - 0.5) * 0.8;
    float mountainTop = horizon - monH * (0.04 + ridge * 0.3);
    if (w.y > mountainTop && w.y <= horizon)
    {
        float snow = smoothstep(0.0, 1.0, 1.0 - (w.y - mountainTop) / (monH * 0.05));
        float4 rock = float4(0.07, 0.09, 0.15, 1);
        float4 c = lerp(rock, float4(0.55, 0.65, 0.75, 1), snow * 0.6);
        float glow = auroraAt(float2(w.x, mountainTop - 2.0), center, size, time) * 0.25;
        c.rgb += palette(0.4).rgb * glow * 0.4;
        // Silhueta: o fundo da célula é a rocha; só a neve dos picos leva caracteres.
        o.bg = float4((inLake ? c.rgb * 0.5 : c.rgb) * 0.8, 1);
        o.fg = float4(0.75, 0.82, 0.9, 1) * (inLake ? 0.5 : 1.0);
        o.value = snow > 0.35 ? 0.35 * snow : 0.0;
        o.glyph = 0;
        return o;
    }

    // Céu.
    float up = saturate((horizon - w.y) / monH);
    float4 sky = lerp(float4(0.02, 0.05, 0.1, 1), float4(0.0, 0.01, 0.03, 1), up);
    float a = auroraAt(w, center, size, time);
    // Cor: verde na borda de baixo, violeta/rosa no topo das cortinas.
    float hue = saturate(0.35 + (1.0 - exp(-a * 0.6)) * 0.1 + up * 0.55);
    float4 col = palette(lerp(0.4, 1.0, hue));
    float v = saturate(a * 1.1);

    // Estrelas (atrás da aurora).
    float2 sc = floor(w / 3.0);
    float s = hash21(sc);
    float2 starPos = (sc + 0.25 + hash22(sc) * 0.5) * 3.0; // uma estrela por quadrado, numa só célula
    bool starHere = s > 1.0 - 0.06 * param("stars") && all(int2(floor(starPos / worldPerCell)) == int2(floor(w / worldPerCell)));
    float twinkle = 0.5 + 0.5 * sin(time * (2.0 + s * 4.0) + s * 60.0);

    // Estrela cadente: periódica e no clique.
    float shoot = 0.0;
    float slot = floor(time / 7.0);
    float tt = frac(time / 7.0) * 7.0;
    float2 start = float2(hash11(slot) * worldSize.x, monTop + hash11(slot + 3.0) * monH * 0.3);
    if (mouse.clickTime >= 0 && time - mouse.clickTime < 1.2)
    {
        start = mouse.clickWorld;
        tt = time - mouse.clickTime;
    }
    if (tt < 1.2)
    {
        float2 dir = normalize(float2(1.0, 0.45));
        float2 headPos = start + dir * tt * 55.0;
        float2 rel = w - headPos;
        float along = dot(rel, -dir);
        float across = abs(dot(rel, float2(-dir.y, dir.x)));
        if (along > 0 && along < 14.0 && across < worldPerCell.y * 0.6) shoot = (1.0 - along / 14.0) * (1.0 - tt / 1.2);
    }

    o.fg = lerp(sky, col, saturate(v * 2.0));
    o.value = v;
    if (starHere && v < 0.25)
    {
        o.glyph = glyph(twinkle > 0.85 ? 2u : (twinkle > 0.5 ? 1u : 0u));
        o.fg = float4(0.85, 0.9, 1.0, 1) * (0.4 + 0.6 * twinkle);
    }
    if (shoot > 0.05)
    {
        o.glyph = glyph(4);
        o.fg = float4(1, 1, 1, 1) * saturate(shoot * 1.5);
    }
    if (inLake)
    {
        // O reflexo é mais escuro e azulado.
        o.fg.rgb *= float3(0.55, 0.65, 0.8);
        o.value *= 0.7;
        if (o.glyph == glyph(4)) o.glyph = 0;
    }
    o.bg = float4(o.fg.rgb * 0.12 * saturate(v * 2.0), 1);
    return o;
}
