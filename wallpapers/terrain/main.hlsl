// Terrain — mapa visto de cima, feito de ruído fbm, com nuvens numa camada mais alta.
// O rato desloca as camadas a velocidades diferentes (parallax): o chão mexe pouco,
// as nuvens mexem mais, o que dá sensação de profundidade.

// O fbm concentra-se perto de 0.5; esticamos para haver mar fundo e montanhas altas.
float heightAt(float2 p) { return saturate((fbm(p) - 0.5) * 1.9 + 0.5); }

CellOut cell(CellIn i)
{
    // Coordenadas de mundo: o tamanho da paisagem não depende da resolução da grelha.
    float2 p = i.world / 120.0;

    // O parallax segue o rato suavemente: cada célula guarda a mesma posição suavizada
    // no state (todas calculam o mesmo valor), assim não há saltos quando o rato entra/sai.
    float2 target = mouse.inside ? (mouse.world / worldSize - 0.5) : float2(0, 0);
    float2 smoothed = prev(int2(i.id)).xy;
    smoothed = lerp(smoothed, target, 1.0 - exp(-dt * 2.5));
    float2 par = smoothed * param("parallax");

    float2 drift = float2(0.012, 0.005) * time * param("speed");
    float2 q = p * param("scale") + drift + par * 0.12;

    float h = heightAt(q);
    // Luz vinda do canto superior esquerdo: comparar com a altura um pouco ao lado.
    float e = 0.02;
    float light = saturate(0.5 + (h - heightAt(q + float2(e, e))) * 18.0);

    float water = 0.45;
    float v, t;
    if (h < water)
    {
        // Água: quase vazia (só ondulação ligeira), mais escura com a profundidade.
        float wave = noise(q * 22.0 + time * 0.8);
        t = (h / water) * 0.3;
        v = wave > 0.72 ? 0.12 + t * 0.4 : 0.0;
    }
    else
    {
        // pow(): a maior parte da terra fica em areia/erva; rocha e neve só nos picos.
        float land = (h - water) / (1.0 - water);
        t = 0.38 + pow(land, 1.7) * 0.62;
        v = saturate(0.12 + land * 0.65) * (0.5 + 0.7 * light);
    }
    float4 color = palette(t) * lerp(0.75, 1.25, light);

    // Nuvens: camada mais alta, mexe-se mais com o rato e com o vento.
    float c = fbm(p * 1.3 + drift * 2.5 + par * 0.35 + 40.0);
    float cloud = smoothstep(0.55, 0.75, c) * param("clouds");
    v = lerp(v, 0.95, cloud * 0.8);
    color = lerp(color, float4(0.92, 0.94, 0.98, 1.0), cloud * 0.75);

    CellOut o;
    o.value = saturate(v);
    o.fg = float4(color.rgb, 1.0);
    o.bg = float4(color.rgb * 0.22, 1.0); // fundo tingido, para o mapa não ter "buracos"
    o.state = float4(smoothed, 0, 0);
    o.glyph = 0;
    return o;
}
