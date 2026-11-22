// Fractal — um zoom sem fim no conjunto de Mandelbrot.
//
// Tech demo de matemática pesada por célula (centenas de iterações):
//   - o zoom vai visitando pontos famosos (vale dos cavalos-marinhos, dos elefantes, espirais),
//     cada um durante `dwell` segundos, sempre a aproximar (até ~20 000x, o limite da precisão
//     de float na GPU);
//   - cor por "contagem suave" de iterações (sem degraus entre bandas), com a paleta a rodar.
// Sem interação com o rato: um fractal é para se ver, e trocar de imagem ao passar o rato
// tornava-o confuso.

static const float2 TARGETS[5] = {
    float2(-0.743643887, 0.131825904), // vale dos cavalos-marinhos
    float2(0.254987037, -0.000567979), // vale dos elefantes
    float2(-0.088000000, 0.654000000), // espiral tripla
    float2(-1.768778833, -0.001738996),// mini-Mandelbrot na antena
    float2(-0.775683770, 0.136467370), // ponto de Misiurewicz
};

// Contagem suave: n + 1 - log2(log|z|). Devolve -1 se não escapou (dentro do conjunto).
float iterate(float2 z, float2 c, int maxIter)
{
    [loop] for (int n = 0; n < maxIter; n++)
    {
        z = float2(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
        float m2 = dot(z, z);
        if (m2 > 256.0) return float(n) + 1.0 - log2(max(log2(m2) * 0.5, 1e-6));
    }
    return -1.0;
}

CellOut cell(CellIn i)
{
    CellOut o;
    o.glyph = 0;
    o.state = 0;
    o.bg = 0;

    float dwell = max(param("dwell"), 5.0);
    float cycle = time / dwell;
    uint k = uint(floor(cycle)) % 5u;
    float t = frac(cycle);
    // Zoom exponencial: 1x -> ~20000x; a última fração desvanece para o próximo ponto.
    float zoom = exp(t * log(20000.0 * param("depth")));
    float2 center = lerp(float2(-0.6, 0.0), TARGETS[k], saturate(t * 6.0)); // primeiro centra, depois mergulha

    // Coordenadas do plano complexo: o monitor principal mostra ~3 unidades de altura no 1x.
    float2 p = (i.world - primaryCenterWorld()) / primarySizeWorld().y;
    p.y = -p.y;
    float2 cM = center + p * 3.0 / zoom;

    int maxIter = int(clamp(80.0 + log2(zoom) * 22.0, 80.0, 420.0) * param("quality"));
    float mu = iterate(float2(0, 0), cM, maxIter);

    // Fim de cada mergulho: desvanecer para preto antes de saltar para o próximo ponto.
    float fade = saturate((1.0 - t) * 12.0) * saturate(t * 40.0);

    if (mu < 0)
    {
        o.value = 0;
        o.fg = 0;
        o.bg = float4(palette(0.0).rgb, 1);
        return o;
    }
    float band = mu * 0.035 * param("bands") + time * 0.04 * param("cycle");
    float edge = saturate(mu / float(maxIter) * 6.0); // perto do conjunto = mais denso
    // Caractere: denso junto à fronteira do conjunto, leve longe dela (dá profundidade).
    o.value = saturate((0.12 + edge * 0.95 + 0.15 * frac(band * 3.0)) * fade);
    float3 col = palette(frac(band)).rgb;
    o.fg = float4(col * (0.55 + 0.6 * edge) * fade, 1);
    o.bg = float4(col * 0.18 * fade, 1);
    return o;
}
