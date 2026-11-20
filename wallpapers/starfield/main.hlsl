// Starfield — estrelas em várias camadas de profundidade que se aproximam do ecrã.
// O ponto de fuga (de onde as estrelas parecem sair) segue o rato: é a direção do voo.

CellOut cell(CellIn i)
{
    int2 c = int2(i.id);
    float4 st = prev(c);

    // Ponto de fuga suavizado (guardado em state.zw; todas as células calculam o mesmo).
    float2 target = mouse.inside ? mouse.world : worldSize * 0.5;
    float2 center = (frameIndex == 0u) ? target : st.zw;
    center = lerp(center, target, 1.0 - exp(-dt * 1.8));

    // Posição relativa ao ponto de fuga, em unidades de mundo (redonda no ecrã e igual
    // com qualquer resolução da grelha).
    float2 p = i.world - center;

    float brightness = 0.0;
    const int LAYERS = 5;
    [unroll] for (int k = 0; k < LAYERS; k++)
    {
        // z vai de 1 (longe) a 0 (perto) e volta a 1: cada camada é um "anel" de estrelas.
        float z = frac(float(k) / LAYERS - time * 0.06 * param("speed"));
        // Espaçamento das estrelas no ecrã: pequeno ao longe, grande ao perto (afastam-se).
        float spacing = 1.9 / max(z, 0.04);
        float2 uv = p / spacing + float2(k * 17.0, k * 9.0);
        float2 cellId = floor(uv);
        if (hash21(cellId + k) > param("density")) continue;
        float2 star = (cellId + hash22(cellId * 1.3 + k) - float2(k * 17.0, k * 9.0)) * spacing;
        float d = length(p - star);
        float fade = smoothstep(1.0, 0.75, z) * smoothstep(0.0, 0.08, z); // aparece ao longe, some ao perto
        float size = lerp(2.3, 0.75, z);
        brightness = max(brightness, saturate(1.15 - d / size) * fade * lerp(1.0, 0.45, z));
    }

    // Rastos: o brilho do frame anterior apaga-se devagar. Como as estrelas se afastam do
    // centro, o rasto fica para trás e parecem riscos de velocidade.
    float trail = st.x * param("trails");
    float v = max(brightness, trail);

    CellOut o;
    o.value = v;
    o.fg = palette(saturate(v * 1.05));
    o.bg = float4(0, 0, 0, 0);
    o.state = float4(v, 0, center);
    o.glyph = 0;
    return o;
}
