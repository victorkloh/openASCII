// Fire — o algoritmo de fogo clássico dos demos dos anos 90, em ASCII.
//
// Cada célula guarda o seu "calor" (state.x). A linha de baixo de cada monitor é a fonte
// (sempre quente, a tremeluzir). As outras células copiam o calor de uma vizinha de baixo,
// escolhida um pouco ao acaso, e arrefecem um bocadinho. É só isto, e parecem chamas.

CellOut cell(CellIn i)
{
    int2 c = int2(i.id);
    int4 mon = monitorRectAt(c);
    float monH = float(mon.w - mon.y);
    float fromBottom = float(mon.w - 1 - c.y); // 0 = linha de baixo deste monitor

    // Arrefecimento por célula: as chamas chegam a `height` da altura do ecrã.
    float cooling = 1.25 / max(param("height") * monH, 4.0);

    float heat;
    if (!fixedStep(30.0))
    {
        heat = prev(c).x; // passos a 30 por segundo: as chamas sobem igual a qualquer FPS
    }
    else if (fromBottom < 1.0)
    {
        // A fonte: tremeluz devagar ao longo do fundo.
        float flicker = noise(float2(i.world.x * 0.35, time * 2.0));
        heat = 0.75 + 0.25 * flicker;
    }
    else
    {
        // Copiar de baixo, de uma coluna ao lado (aleatória + vento) e arrefecer.
        float r = hash21(float2(c) + frac(time * 37.0) * 91.0);
        int dx = int(floor(r * 3.0)) - 1 + int(round(param("wind") * hash11(time + c.y)));
        float below = prev(c + int2(dx, 1)).x;
        float below2 = prev(c + int2(0, 2)).x;
        heat = max(0.0, (below * 0.75 + below2 * 0.25) - cooling * (0.4 + r * 1.2));
    }

    // O rato acende o fogo à sua volta; um clique faz uma explosão.
    if (mouse.presence > 0.001)
    {
        float d = length(i.world - mouse.world);
        float moving = saturate(length(mouse.velWorld) * 0.01);
        heat = max(heat, exp(-d * d * 0.08) * (0.55 + moving * 0.45) * param("mouse") * mouse.presence);
    }
    float sinceClick = time - mouse.clickTime;
    if (mouse.clickTime >= 0 && sinceClick < 0.25)
    {
        float d = length(i.world - mouse.clickWorld);
        float ring = exp(-pow(d - sinceClick * 90.0, 2.0) * 0.02);
        heat = max(heat, (exp(-d * d * 0.004) + ring) * (1.0 - sinceClick * 3.0) * 1.2);
    }
    heat = saturate(heat);

    CellOut o;
    o.value = heat;
    o.fg = palette(heat);
    o.bg = float4(palette(heat * 0.5).rgb * 0.35, heat > 0.05 ? 1.0 : 0.0);
    o.state = float4(heat, 0, 0, 0);
    o.glyph = 0;

    // Faíscas: pontos que sobem e se apagam, por cima das chamas.
    float rate = param("embers");
    if (rate > 0.0 && heat < 0.35)
    {
        float column = floor(i.world.x / 3.0);
        float speed = 6.0 + hash11(column * 3.7) * 10.0;
        float life = frac(time * speed / max(monH * worldPerCell.y, 1.0) + hash11(column));
        float y = (1.0 - life) * monH * worldPerCell.y * 0.9;      // altura da faísca (mundo)
        float sway = sin(time * 2.0 + column) * 1.5;
        float2 ember = float2(column * 3.0 + 1.5 + sway, float(mon.w) * worldPerCell.y - y);
        if (hash11(column * 1.9 + floor(time * speed / max(monH * worldPerCell.y, 1.0) + hash11(column))) <
                0.18 * rate &&
            all(int2(floor(ember / worldPerCell)) == c))
        {
            o.glyph = glyph(life < 0.5 ? 2u : 0u);
            o.fg = lerp(float4(1, 0.9, 0.5, 1), float4(0.9, 0.25, 0.05, 1), life);
        }
    }
    return o;
}
