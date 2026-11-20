// Rain — a janela de uma noite de chuva, ao estilo "lofi".
//
// Lá fora: luzes de cidade desfocadas (bokeh). No vidro: gotículas paradas, gotas que
// escorrem e deixam rasto, e embaciamento. O embaciamento é ESTADO (state.x): volta
// devagar, e o rato com o botão premido limpa-o — dá para desenhar e escrever no vidro.

float bokeh(float2 w, out float3 color)
{
    float total = 0.0;
    color = float3(0, 0, 0);
    // Uma grelha de luzes; cada célula da grelha tem (ou não) uma luz redonda e suave.
    [unroll] for (int layer = 0; layer < 2; layer++)
    {
        float s = layer == 0 ? 22.0 : 13.0;
        float2 gp = w / s + layer * 17.3;
        float2 id = floor(gp);
        [unroll] for (int dy = -1; dy <= 1; dy++)
        [unroll] for (int dx = -1; dx <= 1; dx++)
        {
            float2 cid = id + float2(dx, dy);
            float h = hash21(cid + layer * 5.0);
            if (h > 0.55) continue;
            float2 pos = cid + hash22(cid * 1.7 + layer);
            float radius = lerp(0.35, 0.75, hash11(h * 91.0));
            float d = length(gp - pos) / radius;
            float flicker = 0.85 + 0.15 * sin(time * (0.5 + h * 2.0) + h * 30.0);
            float k = saturate(1.0 - d) * flicker;
            k = k * k * (3.0 - 2.0 * k);
            float3 c = palette(lerp(0.4, 0.95, hash11(h * 13.0))).rgb;
            color += c * k;
            total += k * (layer == 0 ? 1.0 : 0.6);
        }
    }
    return total * param("lights");
}

CellOut cell(CellIn i)
{
    int2 c = int2(i.id);
    float4 st = prev(c);
    float2 w = i.world;

    // ---- Embaciamento (estado): volta devagar, o rato limpa ----
    float fog = (frameIndex == 0u) ? param("fog") : st.x;
    fog = min(param("fog"), fog + dt * 0.02 * param("regrow"));
    if (mouse.presence > 0.01)
    {
        // Passar o rato limpa um pouco; com o botão premido limpa tudo (um "dedo" no vidro).
        float d = length(w - mouse.world);
        bool pressing = (mouse.buttons & MOUSE_LEFT) != 0;
        float strength = (pressing ? 1.0 : saturate(length(mouse.velWorld) * 0.004) * 0.5) * mouse.presence;
        fog *= 1.0 - strength * (1.0 - smoothstep(1.2, 3.5, d));
    }

    // ---- Gotas que escorrem (deixam um rasto limpo no embaciado) ----
    float lane = floor(w.x / 4.0);
    float speedLane = lerp(4.0, 12.0, hash11(lane * 3.3));
    float rate = param("rain");
    float trail = 0.0;
    bool head = false;
    if (rate > 0.0 && hash11(lane * 7.1) < 0.45 * rate)
    {
        float monTop = float(monitorRectAt(c).y) * worldPerCell.y;
        float monH = float(monitorRectAt(c).w - monitorRectAt(c).y) * worldPerCell.y;
        float cycle = monH + 30.0;
        float yHead = frac(time * speedLane / cycle + hash11(lane)) * cycle + monTop - 15.0;
        float wiggle = sin(yHead * 0.3 + lane) * 0.8;
        float xLane = lane * 4.0 + 2.0 + wiggle;
        float dy = yHead - w.y;
        if (abs(w.x - xLane) < worldPerCell.x * 0.75)
        {
            if (dy >= 0.0 && dy < worldPerCell.y) head = true;
            else if (dy > 0.0 && dy < 18.0) trail = 1.0 - dy / 18.0;
        }
        if (abs(w.x - xLane) < 1.2 && dy > 0.0 && dy < 25.0) fog *= 0.6 + 0.4 * (dy / 25.0);
    }

    // ---- Lá fora: cidade desfocada; mais desfocada e baça onde está embaciado ----
    float3 lightColor;
    float b = bokeh(w + float2(sin(time * 0.05) * 3.0, 0), lightColor);
    float3 night = lerp(palette(0.05).rgb, palette(0.18).rgb, saturate(1.0 - w.y / max(worldSize.y, 1.0)));
    float3 outside = night + lightColor * 0.9;
    float clarity = 1.0 - fog * 0.75;
    float v = saturate(b * 0.55) * lerp(0.45, 1.0, clarity);
    float3 color = lerp(outside * 0.55 + 0.06, outside, clarity);

    CellOut o;
    o.glyph = 0;
    o.value = v;
    o.fg = float4(color, 1);
    o.bg = float4(lerp(night * 0.6, color * 0.25, 0.5) + fog * 0.035, 1);

    // ---- Gotículas paradas no vidro (renovam-se de vez em quando) ----
    float2 dc = floor(w / float2(2.5, 2.5));
    float epoch = floor(time / 9.0 + hash21(dc) * 9.0);
    float hd = hash21(dc + epoch * 0.37);
    if (hd < 0.07 * (0.3 + rate) && all(int2(floor((dc * 2.5 + 1.25) / worldPerCell)) == c))
    {
        o.glyph = glyph(hd < 0.02 ? 0u : (hd < 0.045 ? 1u : 3u));
        o.fg = float4(lerp(color, float3(0.9, 0.95, 1.0), 0.55), 1); // a gota "acende" a luz de trás
    }
    if (trail > 0.0)
    {
        o.glyph = glyph(trail > 0.6 ? 4u : 2u);
        o.fg = float4(lerp(color, float3(0.85, 0.9, 1.0), 0.4 * trail), 1);
    }
    if (head)
    {
        o.glyph = glyph(0);
        o.fg = float4(0.95, 0.97, 1.0, 1);
    }

    o.state = float4(fog, 0, 0, 0);
    return o;
}
