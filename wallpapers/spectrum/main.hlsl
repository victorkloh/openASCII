// Spectrum — o som que está a tocar no PC, em ASCII.
//
// Tech demo de [inputs] audio = true. A app escuta a SAÍDA de som (loopback: o que vai para
// as colunas, nunca o microfone), faz uma FFT e entrega ao shader:
//   audioBand(i)      64 bandas de frequência, 0..1 (graves no 0)
//   audioSpectrum(t)  o mesmo, contínuo em t ∈ [0,1]
//   audioLevels       graves, médios, agudos e batida (1 numa batida, decai para 0)
//
// Também mostra duas técnicas úteis:
//   - estado por coluna: a célula de baixo de cada barra guarda a altura do "pico" que cai
//     devagar (prev() lê-o no frame seguinte);
//   - estado global numa célula reservada: o canto do monitor principal guarda o instante
//     da última batida, e todas as células o leem para desenhar a onda de choque.

static const float PI = 3.14159265;

// Espectro com uma animação suave por baixo quando está tudo em silêncio.
float spectrumAt(float t)
{
    float energy = audioLevels.x + audioLevels.y + audioLevels.z;
    float idleMix = 1.0 - saturate(energy * 6.0);
    float idle = 0.18 + 0.12 * sin(t * 11.0 + time * 1.3) * sin(t * 3.0 - time * 0.7) + 0.08 * (1.0 - t);
    return max(audioSpectrum(t) * param("gain"), idle * idleMix * param("idle"));
}

// Blocos de 1/8 a 8/8 de altura: glyph(0..7) = ▁▂▃▄▅▆▇█
int blockGlyph(float fill) { return glyph(uint(clamp(fill * 8.0, 1.0, 8.0)) - 1u); }

// Traço com a direção de um vetor (y para baixo): glyph(11..14) = - \ | /
int lineGlyph(float2 v)
{
    float a = atan2(v.y, v.x);
    uint k = uint(frac((a + PI / 8.0) / PI) * 4.0) % 4u;
    static const uint SPRITE[4] = {11u, 14u, 13u, 12u};
    return glyph(SPRITE[k]);
}

CellOut cell(CellIn i)
{
    CellOut o;
    o.value = 0;
    o.glyph = 0;
    o.fg = 0;
    o.bg = 0;
    o.state = 0;
    int2 c = int2(i.id);
    int4 mon = monitorRectAt(c);
    int2 reserved = primaryRect().xy; // célula que guarda o estado global

    // --- Estado global: instante da última batida (ou clique) ---
    float4 g = prev(reserved);
    float lastBeat = g.y > 0 ? g.y : -100.0;
    bool beatNow = (audioLevels.w > 0.9 && g.x < 0.5) || justClicked();
    if (beatNow) lastBeat = time;
    if (all(c == reserved)) o.state = float4(audioLevels.w, lastBeat, 0, 0);
    float sinceBeat = time - lastBeat;

    float bass = audioLevels.x, beat = audioLevels.w;

    // --- Fundo: poeira de estrelas que foge do centro, mais depressa com mais som ---
    float2 center = primaryCenterWorld();
    float2 d = i.world - center;
    float r = length(d);
    float ang = atan2(d.x, -d.y); // 0 em cima, positivo para a direita
    {
        // Uma estrela (no máximo) por bloco de 9x9 unidades, num sítio aleatório do bloco.
        float2 block = floor(i.world / 9.0);
        float h = hash21(block);
        float2 star = (block + 0.15 + 0.7 * hash22(block + 7.0)) * 9.0;
        if (h > 0.55 && all(int2(floor(star / worldPerCell)) == c))
        {
            float tw = 0.5 + 0.5 * sin(time * (1.0 + h * 3.0) + h * 40.0);
            o.value = 0.15 + 0.35 * tw * (0.6 + bass);
            o.fg = palette(0.25 + 0.5 * h) * (0.35 + 0.4 * tw + beat * 0.8);
            o.glyph = glyph(h > 0.93 ? 10u : 9u);
        }
    }

    // --- Onda de choque a cada batida ---
    float R = min(primarySizeWorld().x, primarySizeWorld().y) * 0.17 * param("size") * (1.0 + bass * 0.12 + beat * 0.06);
    if (sinceBeat < 1.6)
    {
        float front = R + sinceBeat * 90.0;
        float wave = exp(-pow((r - front) * 0.35, 2.0)) * (1.0 - sinceBeat / 1.6);
        if (wave > 0.08)
        {
            o.value = max(o.value, wave * 0.8);
            o.fg = palette(0.9) * wave + o.fg * (1.0 - wave);
            o.glyph = 0;
        }
    }

    // --- Anel: barras radiais à volta de um disco que pulsa com os graves ---
    float segs = clamp(floor(2.0 * PI * R / (worldPerCell.x * 2.2)), 24.0, 128.0); // ~1 barra a cada 2 células
    float a01 = (ang + PI) / (2.0 * PI);                    // 0..1 à volta
    float seg = floor(a01 * segs);
    float segFrac = frac(a01 * segs);
    float mirror = abs((seg + 0.5) / segs * 2.0 - 1.0);     // 1 em cima, 0 em baixo (simétrico)
    float band = 1.0 - mirror;                              // graves em baixo, agudos em cima
    float level = spectrumAt(band);
    float barLen = level * R * 1.35;
    if (r >= R + worldPerCell.y * 0.5 && r <= R + barLen && segFrac > 0.25)
    {
        // Barra radial: um traço na direção do raio, mais claro junto ao anel.
        float k = (r - R) / max(barLen, 1e-3);
        o.glyph = lineGlyph(d);
        o.fg = palette(0.35 + 0.65 * band) * (0.8 + 0.6 * (1.0 - k));
        o.bg = float4(palette(0.35 + 0.65 * band).rgb * 0.10 * (1.0 - k), 1);
    }
    float rim = abs(r - R);
    if (rim < worldPerCell.y * 0.5)
    {
        o.glyph = lineGlyph(float2(-d.y, d.x)); // tangente: um círculo desenhado a traços
        o.fg = palette(1.0) * (0.65 + beat * 0.6);
    }
    else if (r < R)
    {
        // Disco: pulsa com os graves; a batida acende-o por inteiro.
        float fill = (bass * 0.55 + beat * 0.45) * (1.0 - r / R) + beat * 0.25;
        float swirl = 0.5 + 0.5 * sin(ang * 3.0 + time * 0.8 + r * 0.15);
        o.value = saturate(fill * (0.6 + 0.4 * swirl));
        o.fg = palette(0.5 + 0.5 * o.value);
        o.bg = float4(palette(0.4).rgb * fill * 0.25, 1);
        o.glyph = 0;
    }

    // --- Barras no fundo de cada monitor, com picos que caem devagar ---
    float monW = float(mon.z - mon.x), monH = float(mon.w - mon.y);
    float barW = max(2.0, floor(monW / clamp(param("bars"), 8.0, 160.0)));
    float barCount = floor(monW / barW);
    float bx = float(c.x - mon.x);
    float bar = floor(bx / barW);
    if (bar < barCount)
    {
        float t = (bar + 0.5) / barCount;
        float h = spectrumAt(pow(t, 0.8)) * monH * param("height");   // altura em células
        int bottom = workRectAt(c).w - 1; // por cima da barra de tarefas, não por trás dela
        float fromBottom = float(bottom - c.y);
        // Pico: guardado na célula de baixo da barra; desce 12 células/s.
        float peakPrev = prev(int2(c.x, bottom)).z;
        float peak = max(h, peakPrev - dt * 12.0 * max(monH / 80.0, 0.5));
        if (c.y == bottom) o.state.z = peak;
        bool gap = fmod(bx, barW) >= barW - 1.0;
        if (!gap)
        {
            float3 col = palette(0.3 + 0.7 * t).rgb;
            if (fromBottom < floor(h))
            {
                o.glyph = blockGlyph(1.0);
                o.fg = float4(col * (0.55 + 0.45 * fromBottom / max(h, 1.0)), 1);
                o.bg = 0;
            }
            else if (fromBottom == floor(h) && frac(h) > 0.06)
            {
                o.glyph = blockGlyph(frac(h));
                o.fg = float4(col, 1);
            }
            else if (fromBottom == floor(peak) && peak > 1.0)
            {
                o.glyph = glyph(8u); // ▀
                o.fg = palette(1.0);
            }
        }
    }
    return o;
}
