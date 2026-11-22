// Coral — reação-difusão de Gray-Scott: duas "substâncias" que se espalham e reagem.
//
// Tech demo de equações diferenciais na GPU, com 24 passos por frame (render.substeps).
// Cada célula guarda a concentração de A e de B. A cada passo:
//     A' = A + (Da·∇²A − A·B² + f·(1 − A))·dt
//     B' = B + (Db·∇²B + A·B² − (k + f)·B)·dt
// ∇² (o laplaciano) mede quanto a célula difere das vizinhas. Conforme f (alimentação) e k
// (morte), nascem corais, manchas, labirintos ou "mitoses". Aqui f e k variam devagar pelo
// ecrã e pelo tempo, por isso vês vários regimes ao mesmo tempo, sempre a mudar.
//
// O laplaciano é o clássico de 9 pontos (Karl Sims), estável com dt = 1.
// O rato semeia B onde passa; clicar semeia uma mancha grande.
//
// state = (A, B, idade da semente, -)

// Concentrações de uma vizinha; fora dos monitores conta como a própria célula (sem fluxo
// pelas bordas; se contasse como "só A", as bordas virariam uma fonte de alimento).
float2 rd(int2 p, float2 self)
{
    if (p.x < 0 || p.y < 0 || p.x >= int(grid.x) || p.y >= int(grid.y) || !visible(p)) return self;
    return prev(p).xy;
}

CellOut cell(CellIn i)
{
    CellOut o;
    o.glyph = 0;
    o.bg = 0;
    int2 c = int2(i.id);
    float4 s = prev(c);
    float2 ab = s.xy;

    // Primeiro frame (ou depois de mudar o tamanho): tudo A, com sementes de B espalhadas.
    if (s.w > 0.5 && !fixedStep(30.0))
    {
        // Entre passos (FPS acima de 30): manter o estado, para evoluir igual a qualquer FPS.
    }
    else if (s.w < 0.5)
    {
        float seed = step(0.96, hash21(floor(i.world / 2.5) + 17.0));
        ab = float2(1.0, seed);
    }
    else
    {
        // Laplaciano de 9 pontos (Karl Sims): vizinhos de lado 0.2, diagonais 0.05, centro -1.
        float2 lap = -ab;
        [unroll] for (int y = -1; y <= 1; y++)
        [unroll] for (int x = -1; x <= 1; x++)
            if (x != 0 || y != 0) lap += rd(c + int2(x, y), ab) * ((x == 0 || y == 0) ? 0.2 : 0.05);

        // As células são mais altas que largas e o laplaciano é em células: os padrões ficam
        // esticados na vertical, como algas e corais a crescer para cima.
        // f e k a variar pelo espaço e pelo tempo: de coral (centro) a manchas e labirintos.
        float2 q = i.world / max(worldSize, 1.0);
        float drift = time * 0.01 * param("drift");
        // (A zona interessante é estreita: um pouco ao lado e fica tudo B, ou morre tudo.)
        float f = 0.0545 + 0.0035 * sin(q.x * 3.1 + drift) * cos(q.y * 2.3 - drift * 0.7);
        float k = 0.0625 + 0.0012 * sin(q.y * 2.7 + drift * 1.3) + 0.0006 * cos(q.x * 4.1);
        float dtStep = 1.0;
        float abb = ab.x * ab.y * ab.y;
        ab.x += (1.0 * lap.x - abb + f * (1.0 - ab.x)) * dtStep;
        ab.y += (0.5 * lap.y + abb - (k + f) * ab.y) * dtStep;

        // Rato e clique semeiam B (só no primeiro passo de cada frame).
        if (passIndex == 0u)
        {
            if (mouse.presence > 0.001 && length(i.world - mouse.world) < 2.5) ab.y = max(ab.y, 0.6 * mouse.presence);
            if (justClicked() && length(i.world - mouse.clickWorld) < 8.0) ab.y = 0.8;
            // Uma semente nova de vez em quando, para nunca morrer de vez.
            float2 cellId = floor(i.world / 40.0);
            float pulse = floor(time / 9.0);
            if (hash21(cellId + pulse * 3.7) > 0.985 && length(frac(i.world / 40.0) - 0.5) < 0.06) ab.y = 0.7;
        }
        ab = saturate(ab);
    }
    o.state = float4(ab, 0, 1);

    // Desenho: B escolhe o caractere e a cor; as bordas (onde B muda depressa) brilham mais.
    float b = ab.y;
    float edge = saturate(abs(b - rd(c + int2(1, 0), ab).y) * 6.0 + abs(b - rd(c + int2(0, 1), ab).y) * 6.0);
    float v = saturate(b * 2.6);
    o.value = v;
    o.fg = palette(saturate(v * 0.85 + edge * 0.3));
    o.bg = float4(palette(0.12 + 0.25 * v).rgb * (0.25 + 0.35 * v), 1);
    return o;
}
