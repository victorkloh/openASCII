// Ripple — uma simulação de ondas a sério (equação de onda discreta).
//
// Cada célula guarda no `state` a altura da água agora (x) e no frame anterior (y).
// A altura nova depende da média dos vizinhos (via prev()) e da altura de há dois frames:
// é isto que faz as ondas propagarem-se, refletirem nas bordas e interferirem entre si.
//
// No modo estendido o canvas cobre todos os monitores: uma onda que sai de um ecrã entra
// no outro. As zonas fora dos monitores têm altura 0 e funcionam como paredes.
// Os impulsos (rato, cliques, gotas) são medidos em unidades de mundo, por isso têm o
// mesmo tamanho com qualquer resolução da grelha.

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

    // param("speed") é o número de Courant: até 1 a simulação é estável.
    float h = 2.0 * here.x - here.y + param("speed") * (wx * lapX + wy * lapY);
    h *= param("damping");

    // O rato a mexer-se perturba a água à sua volta, proporcionalmente à velocidade.
    if (mouse.presence > 0.001)
    {
        float d = length(i.world - mouse.world);
        float speed = min(length(mouse.velWorld) * 0.0035, 1.0);
        h += exp(-d * d * 0.24) * speed * 0.6 * param("strength") * mouse.presence;
    }

    // Clique: uma pedrada forte (só no frame do clique).
    if (justClicked())
    {
        float dc = length(i.world - mouse.clickWorld);
        h -= 3.0 * exp(-dc * dc * 0.1);
    }

    // Chuva: de vez em quando uma gota num sítio aleatório, para nunca ficar parado.
    float rate = param("rain");
    if (rate > 0.0)
    {
        float slot = floor(time * rate);
        if (floor((time - dt) * rate) != slot) // a gota cai neste frame
        {
            float2 drop = hash22(float2(slot, 7.13)) * worldSize;
            float dd = length(i.world - drop);
            h -= 1.0 * exp(-dd * dd * 0.35);
        }
    }

    // Mostrar a inclinação da água (como reflexos de luz) em vez da altura pura.
    float slope = abs(prev(c + int2(1, 0)).x - prev(c + int2(-1, 0)).x) +
                  abs(prev(c + int2(0, 1)).x - prev(c + int2(0, -1)).x);
    // A 60 passos por segundo (a qualquer FPS): nos frames a mais só se redesenha o mesmo.
    bool stepNow = fixedStep(60.0);
    if (!stepNow) h = here.x;
    float v = saturate((abs(stepNow ? h : here.x) * 0.5 + slope * 1.3) * param("contrast"));

    CellOut o;
    o.value = v;
    o.fg = palette(saturate(v * 1.1));
    o.bg = float4(0, 0, 0, 0); // transparente = cor de fundo
    o.state = stepNow ? float4(h, here.x, 0, 0) : here;
    o.glyph = 0;
    return o;
}
