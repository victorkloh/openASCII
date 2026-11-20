// Valley — um mapa vivo visto de cima.
//
// Camadas, de baixo para cima: relevo (fbm) com água, areia, erva, rocha e neve; florestas
// e aldeias; animais em manada (fogem do rato) e peixes; nuvens com sombra; pássaros em V;
// chuva e relâmpagos. Um ciclo dia/noite tinge tudo, e à noite as casas acendem as janelas.
//
// Tudo está em unidades de MUNDO (i.world): mudar a resolução da grelha não muda o tamanho
// da paisagem. No modo estendido o mundo atravessa os monitores, e os animais também.

static const float WATER = 0.45;

float heightAt(float2 q) { return saturate((fbm(q) - 0.5) * 1.9 + 0.5); }
float2 wrap(float2 p, float2 size) { return p - size * floor(p / size); }
float2 toQ(float2 world, float2 par, float2 drift) { return world * (param("scale") / 120.0) + drift + par * 0.12; }

// A célula onde cai um ponto do mundo (para desenhar sprites com exatamente 1 célula).
bool sameCell(float2 world, int2 id) { return all(int2(floor(world / worldPerCell)) == id); }

CellOut cell(CellIn i)
{
    // ---- Parallax suavizado (todas as células calculam e guardam o mesmo valor) ----
    float2 target = (mouse.world / worldSize - 0.5) * mouse.presence; // volta à origem quando o rato sai
    float2 smoothed = prev(int2(i.id)).xy;
    smoothed = lerp(smoothed, target, 1.0 - exp(-dt * 2.5));
    float2 par = smoothed * param("parallax");

    float wind = param("speed");
    float2 drift = float2(0.012, 0.005) * time * wind;
    float2 q = toQ(i.world, par, drift);
    float h = heightAt(q);
    float light = saturate(0.5 + (h - heightAt(q + 0.02)) * 18.0); // sol do canto superior esquerdo

    // ---- Dia e noite ----
    float phase = frac(time / (param("daylength") * 60.0) + 0.35);       // começa de manhã
    float sun = sin(phase * 6.2831853);                                    // > 0 de dia
    float day = smoothstep(-0.25, 0.25, sun);
    float dusk = saturate(1.0 - abs(sun) * 4.0);                           // nascer/pôr do sol

    // ---- Tempo: chuvadas que vêm e vão ao longo de minutos ----
    float storm = smoothstep(0.55, 0.8, noise(float2(time * 0.012, 3.7))) * param("rain");

    // ---- Relevo ----
    float v, t;
    float4 bgTint;
    if (h < WATER)
    {
        float wave = noise(q * 22.0 + time * (0.8 + wind * 0.3));
        t = (h / WATER) * 0.3;
        v = wave > 0.72 - storm * 0.2 ? 0.12 + t * 0.4 : 0.0;
    }
    else
    {
        float land = (h - WATER) / (1.0 - WATER);
        t = 0.38 + pow(land, 1.7) * 0.62;
        v = saturate(0.12 + land * 0.65) * (0.5 + 0.7 * light);
    }
    float4 color = palette(t) * lerp(0.75, 1.25, light);
    int sprite = 0;

    // ---- Florestas e aldeias (fixas na paisagem) ----
    float land01 = (h - WATER) / (1.0 - WATER);
    bool forest = land01 > 0.12 && land01 < 0.42 && noise(q * 7.0 + 11.0) > 0.6;
    if (forest && hash21(float2(i.id)) > 0.35)
    {
        sprite = glyph(9);
        color = float4(0.16, 0.42, 0.18, 1) * lerp(0.7, 1.2, light);
    }
    float2 villageCell = floor(q * 2.5);
    bool village = land01 > 0.05 && land01 < 0.3 && hash21(villageCell + 5.1) < 0.07 &&
                   length(frac(q * 2.5) - 0.5) < 0.18;
    if (village && hash21(float2(i.id) * 1.7) > 0.45)
    {
        sprite = glyph(10);
        // À noite as janelas acendem (cor quente que não escurece com a noite).
        color = lerp(float4(1.0, 0.78, 0.42, 1) * 1.6, float4(0.78, 0.66, 0.55, 1), day);
    }

    // ---- Peixes na água ----
    float2 world = worldSize;
    [loop] for (int f = 0; f < 6; f++)
    {
        float2 dir = normalize(hash22(float2(f, 4.2)) - 0.5);
        float2 fp = wrap(hash22(float2(f, 1.3)) * world + dir * time * (1.5 + hash11(f) * 2.0), world);
        // (O HLSL avalia os dois lados de um &&: testar a célula primeiro poupa o fbm.)
        if (sameCell(fp, int2(i.id)))
        {
            if (heightAt(toQ(fp, par, drift)) < WATER - 0.04)
            {
                sprite = glyph(dir.x < 0 ? 4 : 5);
                color = float4(0.75, 0.88, 1.0, 1);
            }
        }
    }

    // ---- Animais em manada (fogem do rato) ----
    int herds = int(round(5.0 * param("animals")));
    [loop] for (int k = 0; k < herds; k++)
    {
        float2 dir = normalize(hash22(float2(k, 9.0)) - 0.5);
        float wander = sin(time * 0.05 + k * 2.0) * 0.6;
        dir = float2(dir.x * cos(wander) - dir.y * sin(wander), dir.x * sin(wander) + dir.y * cos(wander));
        float2 center = wrap(hash22(float2(k, 2.0)) * world + dir * time * 2.2, world);
        [loop] for (int j = 0; j < 4; j++)
        {
            float2 a = center + (hash22(float2(k * 7 + j, 3.3)) - 0.5) * 7.0;
            {
                float2 away = a - mouse.world;
                float d = length(away);
                if (d < 14.0) a += normalize(away + 0.001) * (14.0 - d) * 0.9 * mouse.presence;
            }
            if (sameCell(a, int2(i.id)))
            {
                float ah = heightAt(toQ(a, par, drift));
                if (ah > WATER + 0.02 && ah < 0.75)
                {
                    sprite = glyph((uint(floor(time * 3.0 + j)) & 1u) ? 2 : 3);
                    color = lerp(float4(0.5, 0.45, 0.4, 1), float4(0.93, 0.86, 0.74, 1), day);
                }
            }
        }
    }

    // ---- Nuvens (e a sombra delas no chão) ----
    float2 cq = i.world * (param("scale") / 160.0) + drift * 2.5 + par * 0.35 + 40.0;
    float coverage = param("clouds") * (0.75 + storm * 0.6);
    float cloud = smoothstep(0.58 - coverage * 0.18, 0.78, fbm(cq)) * coverage;
    float shadow = smoothstep(0.58 - coverage * 0.18, 0.78, fbm(cq + float2(0.05, 0.08))) * coverage;
    color.rgb *= 1.0 - shadow * 0.45;
    v *= 1.0 - shadow * 0.3;
    float4 cloudColor = lerp(float4(0.92, 0.94, 0.98, 1), float4(0.45, 0.48, 0.55, 1), storm);
    if (cloud > 0.35)
    {
        // Nuvem espessa tapa o chão (e os animais).
        sprite = 0;
        v = lerp(v, 0.55 + cloud * 0.35, cloud);
    }
    color = lerp(color, cloudColor, cloud * 0.8);

    // ---- Pássaros em V (voam mais alto: mais parallax) ----
    int flocks = int(round(3.0 * param("birds")));
    if (day > 0.3)
    {
        [loop] for (int b = 0; b < flocks; b++)
        {
            float2 dir = normalize(hash22(float2(b, 21.0)) - 0.5);
            float2 lead = wrap(hash22(float2(b, 22.0)) * world + dir * time * 6.0 + par * 25.0, world);
            float2 side = float2(-dir.y, dir.x);
            [loop] for (int j = 0; j < 7; j++)
            {
                float row = floor((j + 1) / 2);
                float s = (j % 2 == 0) ? 1.0 : -1.0;
                float2 bp = lead - dir * row * 2.2 + side * s * row * 1.6;
                if (sameCell(bp, int2(i.id)))
                {
                    sprite = glyph((uint(floor(time * 5.0 + j)) & 1u) ? 0 : 1);
                    color = float4(0.08, 0.09, 0.11, 1);
                }
            }
        }
    }

    // ---- Chuva ----
    if (storm > 0.02)
    {
        // Riscos inclinados que caem; a inclinação segue o vento.
        float slant = floor(i.id.y * 0.5 * (0.6 + wind * 0.2));
        float column = float(i.id.x) + slant;
        float fall = time * 14.0 + hash11(column) * 50.0;
        float streak = frac(fall / 9.0 - float(i.id.y) / 9.0);
        if (streak < 0.12 && hash11(column * 1.3 + floor(fall / 9.0)) < storm * 0.45)
        {
            sprite = glyph(h < WATER ? 8 : 6);
            color = float4(0.62, 0.72, 0.86, 1);
        }
        color.rgb *= 1.0 - storm * 0.25;
    }

    // ---- Relâmpagos: aleatórios na tempestade, ou onde clicares ----
    float flash = 0.0;
    float slot = floor(time * 2.0);
    if (storm > 0.55 && hash11(slot * 3.1) > 0.93) flash = saturate(1.0 - frac(time * 2.0) * 6.0);
    float sinceClick = time - mouse.clickTime;
    if (mouse.clickTime >= 0 && sinceClick < 0.6)
    {
        flash = max(flash, saturate(1.0 - sinceClick * 3.0));
        // O raio: uma linha em ziguezague do topo até ao clique.
        float2 c = mouse.clickWorld;
        float dy = c.y - i.world.y;
        if (dy > 0)
        {
            float zig = (noise(float2(i.world.y * 0.4, floor(mouse.clickTime * 10.0))) - 0.5) * 6.0 * (dy / max(c.y, 1.0));
            if (abs(i.world.x - (c.x + zig)) < worldPerCell.x * 0.75 && sinceClick < 0.35)
            {
                sprite = glyph(11);
                color = float4(1, 1, 0.9, 1);
            }
        }
    }

    // ---- Luz do dia sobre tudo ----
    float3 nightTint = float3(0.25, 0.32, 0.55);
    float3 duskTint = float3(1.15, 0.85, 0.7);
    bool emissive = village && sprite == glyph(10);
    if (!emissive) color.rgb *= lerp(nightTint, lerp(float3(1, 1, 1), duskTint, dusk), day);
    color.rgb = lerp(color.rgb, float3(1, 1, 1), flash * 0.6);
    v = lerp(v, 1.0, flash * 0.5);

    CellOut o;
    o.value = saturate(v);
    o.glyph = sprite;
    o.fg = float4(color.rgb, 1.0);
    float3 bg = palette(t).rgb * 0.2 * lerp(nightTint, float3(1, 1, 1), day) * (1.0 - shadow * 0.4);
    o.bg = float4(lerp(bg, float3(0.5, 0.5, 0.55), flash * 0.3), 1.0);
    o.state = float4(smoothed, 0, 0);
    return o;
}
