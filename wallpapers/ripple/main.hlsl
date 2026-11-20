// Ripple — uma simulação de ondas a sério (equação de onda discreta).
//
// Cada célula guarda no `state` a altura da água agora (x) e no frame anterior (y).
// A altura nova depende da média dos vizinhos (via prev()) e da altura de há dois frames:
// é isto que faz as ondas propagarem-se, refletirem nas bordas e interferirem entre si.

CellOut cell(CellIn i)
{
    int2 c = int2(i.id);
    float4 here = prev(c);

    // As células são mais altas que largas: os vizinhos da esquerda/direita estão mais
    // perto (em píxeis) do que os de cima/baixo. Pesamos cada eixo para as ondas ficarem
    // redondas no ecrã em vez de ovais.
    float kx = 1.0 / (cellAspect * cellAspect);
    float ky = 1.0;
    float wx = kx / (kx + ky), wy = ky / (kx + ky);
    float lapX = prev(c + int2(-1, 0)).x + prev(c + int2(1, 0)).x - 2.0 * here.x;
    float lapY = prev(c + int2(0, -1)).x + prev(c + int2(0, 1)).x - 2.0 * here.x;

    float h = 2.0 * here.x - here.y + 0.95 * (wx * lapX + wy * lapY);
    h *= param("damping");

    // O rato a mexer-se perturba a água à sua volta, proporcionalmente à velocidade.
    float d = cellDistance(i.pos, mouse.pos);
    if (mouse.inside)
    {
        float speed = min(length(mouse.vel * float2(cellAspect, 1.0)) * 0.004, 1.0);
        h += exp(-d * d * 0.35) * speed * 0.6 * param("strength");
    }

    // Clique: uma pedrada forte (só no frame do clique).
    if (justClicked())
    {
        float dc = cellDistance(i.pos, mouse.clickPos);
        h -= 3.0 * exp(-dc * dc * 0.15);
    }

    // Chuva: de vez em quando uma gota num sítio aleatório, para nunca ficar parado.
    float rate = param("rain");
    if (rate > 0.0)
    {
        float slot = floor(time * rate);
        if (floor((time - dt) * rate) != slot) // a gota cai neste frame
        {
            float2 drop = hash22(float2(slot, 7.13)) * float2(grid);
            float dd = cellDistance(i.pos, drop);
            h -= 1.0 * exp(-dd * dd * 0.5);
        }
    }

    // Mostrar a inclinação da água (como reflexos de luz) em vez da altura pura.
    float slope = abs(prev(c + int2(1, 0)).x - prev(c + int2(-1, 0)).x) +
                  abs(prev(c + int2(0, 1)).x - prev(c + int2(0, -1)).x);
    float v = saturate((abs(h) * 0.5 + slope * 1.3) * param("contrast"));

    CellOut o;
    o.value = v;
    o.fg = palette(saturate(v * 1.1));
    o.bg = float4(0, 0, 0, 0); // transparente = cor de fundo
    o.state = float4(h, here.x, 0, 0);
    o.glyph = 0;
    return o;
}
