// Matrix — colunas de caracteres a cair. Tudo é calculado a partir de `time` e de números
// pseudo-aleatórios por coluna (sem estado), por isso cada célula sabe sozinha o que mostrar.

// Uma "gota" numa coluna: devolve a intensidade do rasto nesta linha (0 = nada, 1 = cabeça).
float drop(float column, float row, float seed)
{
    float speed = lerp(5.0, 16.0, hash11(column * 1.7 + seed)) * param("speed");
    float trail = lerp(6.0, 22.0, hash11(column * 3.1 + seed + 4.0));
    float span = float(grid.y) + trail;
    float head = frac(time * speed / span + hash11(column + seed * 13.0)) * span;
    float behind = head - row; // quantas linhas acima da cabeça
    if (behind < 0.0 || behind > trail) return 0.0;
    return 1.0 - behind / trail;
}

CellOut cell(CellIn i)
{
    // Desvio: perto do cursor, cada coluna é empurrada para os lados. Para o pixel em x,
    // lemos a coluna que "veio" de mais perto do cursor.
    float2 d = i.pos - mouse.pos;
    float dist = cellDistance(i.pos, mouse.pos);
    float r = param("radius");
    float push = (mouse.inside && r > 0.0) ? r * 0.9 * exp(-(dist * dist) / (r * r)) : 0.0;
    float sourceX = i.pos.x - sign(d.x) * push / cellAspect;
    float column = floor(sourceX);
    float row = i.pos.y;

    float on = step(hash11(column * 0.37 + 2.0), param("density"));
    float a = drop(column, row, 0.0);
    float b = drop(column, row, 31.0) * 0.8; // segunda gota na mesma coluna
    float v = max(a, b) * on;

    // Uma bolha vazia mesmo à volta do cursor.
    if (mouse.inside && r > 0.0) v *= smoothstep(r * 0.25, r * 0.45, dist);

    CellOut o;
    o.bg = float4(0, 0, 0, 0);
    o.state = float4(0, 0, 0, 0);
    o.value = v;
    if (v <= 0.02)
    {
        o.glyph = 1; // espaço
        o.fg = float4(0, 0, 0, 0);
    }
    else
    {
        // Caractere aleatório que muda de vez em quando (mais depressa perto da cabeça).
        float flicker = floor(time * lerp(2.0, 12.0, v) + hash21(float2(column, row)) * 10.0);
        uint pick = 1u + (uint)(hash21(float2(column * 7.0 + flicker, floor(row))) * (charsetSize - 1));
        o.glyph = int(pick) + 1;
        o.fg = v > 0.95 ? float4(0.85, 1.0, 0.9, 1.0) : palette(v);
    }
    return o;
}
