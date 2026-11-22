// prelude.hlsl — a API disponível para os wallpapers do tipo shader.
//
// O teu main.hlsl só precisa de definir:
//
//     CellOut cell(CellIn i) { ... }
//
// que corre uma vez por célula, por frame, na GPU (compute shader, uma thread por célula).
//
// ---------------------------------------------------------------------------
// Disponível:
//   time, dt            segundos desde o arranque / desde o último frame
//   grid                uint2: colunas, linhas do canvas (no modo estendido cobre TODOS os monitores)
//   cellPx, resolution  tamanho de uma célula e do canvas em píxeis
//   cellAspect          largura/altura da célula (~0.5): multiplica dx por isto para distâncias redondas
//   worldPerCell        float2: unidades de mundo por célula. O "mundo" não depende do tamanho
//                       da célula (só de `scale`): mudar a resolução da grelha não muda o efeito
//   worldSize           tamanho do canvas em unidades de mundo
//   visible(c)          a célula c está dentro de algum monitor? (as outras não aparecem)
//   mouse.pos/.world    posição do rato em células / em unidades de mundo
//   mouse.vel/.velWorld velocidade suavizada (células/s, mundo/s)
//   mouse.buttons       bits MOUSE_LEFT | MOUSE_RIGHT | MOUSE_MIDDLE (só quando o clique começou no desktop)
//   mouse.inside        o rato está sobre o ambiente de trabalho (agora)
//   mouse.presence      0..1 suave: multiplica os efeitos do rato por isto para entrarem e
//                       saírem com uma transição (quando sai, mouse.pos fica na última posição)
//   mouse.clickPos/.clickWorld, mouse.clickTime, mouse.clickButton; justClicked() = clique neste frame
//   mouse.scroll        rodas do scroll neste frame (+ para cima)
//   param("nome")       valor de um parâmetro declarado em [params] do wallpaper.toml
//   palette(t)          cor da paleta em t ∈ [0,1] (interpola entre as cores)
//   bgColor             cor de fundo do wallpaper.toml
//   charsetSize         número de caracteres do charset (a rampa de `value`)
//   glyph(k)            para o.glyph: o k-ésimo caractere de `glyphs` no wallpaper.toml (sprites)
//   asciiGlyph(c)       para o.glyph: qualquer caractere Latin-1 (texto), mesmo fora do charset
//   noise(p), fbm(p)    ruído suave em [0,1] (2D e 3D) e soma de oitavas
//   hash11/21/22/33     números pseudo-aleatórios em [0,1)
//   prev(c)             o `state` que esta célula (ou a vizinha c) escreveu no frame anterior
//   fixedStep(hz)       simulações ao mesmo ritmo a qualquer FPS (ver mais abaixo)
//   localTime, localDate hora e data locais (para relógios)
//   primaryCenter(), primaryCenterWorld(), primarySizeWorld(), primaryRect(), monitorRectAt(c)
//                       para centrar coisas no monitor principal ou achar o chão de cada ecrã
// ---------------------------------------------------------------------------

cbuffer Frame : register(b0)
{
    float  time;
    float  dt;
    uint2  grid;

    float2 _mousePos;
    float2 _mouseVel;

    float2 _clickPos;
    float  _clickTime;
    uint   _buttons;

    uint   _inside;
    uint   _clickButton;
    float  _scroll;
    uint   charsetSize;

    float4 bgColor;
    float4 _palette[8];

    uint   paletteCount;
    uint   frameIndex;
    float  cellAspect;
    uint   glyphCount;

    float4 _params[4];

    float2 cellPx;
    float2 resolution;

    float2 worldPerCell;
    uint   _monitorCount;
    uint   _primary;

    int4   _monitors[8]; // zonas visíveis do canvas (células): x0, y0, x1, y1

    float3 localTime;    // hora local: horas, minutos, segundos (com fração)
    float  _presence;
    uint4  localDate;    // ano, mês (1-12), dia, dia da semana (0 = domingo)

    uint   _clicked;       // 1 só no frame (e no 1º passo) em que houve um clique novo
    uint   passIndex;      // passo atual, 0..passCount-1 (render.substeps no wallpaper.toml)
    uint   passCount;
    uint   _obstacleCount; // janelas + ícones em _obstacles

    float4 _spectrum[16];  // 64 bandas de áudio, 0..1 ([inputs] audio = true)
    float4 audioLevels;    // grave, médios, agudos, batida (0..1)
    float4 systemStats;    // CPU 0..1, RAM 0..1, -, - ([inputs] system = true)
    float4 weather;        // chuva mm/h, nuvens 0..1, vento m/s, temperatura °C ([inputs] weather = true)
    float4 weather2;       // é de dia (0/1), elevação do sol (graus), dados válidos (0/1), código WMO
    float2 sourceSize;     // imagem/vídeo de entrada em px (0 = nenhuma)
    uint   textLength;     // caracteres em _text (inputs.text)
    uint   _windowCount;   // os primeiros _windowCount obstáculos são janelas; o resto ícones
    float4 textInfo;       // "lines:": segundos desde que a linha mudou, índice da linha, nº de linhas, intervalo
    uint   _atlasSlots;    // glifos no atlas (os glifos brutos de asciiGlyph() estão abaixo disto)
    uint3  _pad2;
};

StructuredBuffer<float4>   _statePrev : register(t0);
StructuredBuffer<int4>     _obstacles : register(t1); // retângulos em células do canvas: x0, y0, x1, y1
StructuredBuffer<uint>     _text      : register(t2); // [0..255] = slot de cada caractere Latin-1; [256..] = texto (codepoints)
Texture2D<float4>          _source    : register(t3); // imagem/GIF/vídeo/webcam ([inputs] source)
SamplerState               _linear    : register(s0);
RWStructuredBuffer<uint3>  _cells     : register(u0);
RWStructuredBuffer<float4> _stateNext : register(u1);

#define MOUSE_LEFT   1u
#define MOUSE_RIGHT  2u
#define MOUSE_MIDDLE 4u

struct MouseState
{
    float2 pos;
    float2 vel;
    float2 world;
    float2 velWorld;
    uint   buttons;
    bool   inside;
    float  presence; // 0..1 suave: sobe quando o rato entra no fundo, desce devagar quando sai
    float2 clickPos;
    float2 clickWorld;
    float  clickTime;
    uint   clickButton;
    float  scroll;
};
static MouseState mouse;
static float2 worldSize;

struct CellIn
{
    uint2  id;    // coluna, linha (inteiros)
    float2 pos;   // centro da célula em células (id + 0.5): compara diretamente com mouse.pos
    float2 uv;    // pos / grid, em [0,1]
    float2 world; // centro da célula em unidades de mundo (não depende da resolução da grelha)
};

bool visible(int2 c)
{
    [loop] for (uint k = 0; k < _monitorCount; k++)
    {
        int4 r = _monitors[k];
        if (c.x >= r.x && c.y >= r.y && c.x < r.z && c.y < r.w) return true;
    }
    return false;
}

int glyph(uint k) { return int(charsetSize + min(k, max(glyphCount, 1u) - 1u)) + 1; }

// ---------------------------------------------------------------------------
// Monitores (no modo estendido o canvas cobre vários). Retângulos em células: x0, y0, x1, y1.
// ---------------------------------------------------------------------------
int4 primaryRect() { return _monitors[min(_primary, max(_monitorCount, 1u) - 1u)]; }
float2 primaryCenter() { int4 r = primaryRect(); return (float2(r.xy) + float2(r.zw)) * 0.5; }
float2 primaryCenterWorld() { return primaryCenter() * worldPerCell; }
float2 primarySizeWorld() { int4 r = primaryRect(); return float2(r.zw - r.xy) * worldPerCell; }

// O retângulo do monitor que contém a célula c (para "o chão" ou "o topo" de cada ecrã).
int4 monitorRectAt(int2 c)
{
    [loop] for (uint k = 0; k < _monitorCount; k++)
    {
        int4 r = _monitors[k];
        if (c.x >= r.x && c.y >= r.y && c.x < r.z && c.y < r.w) return r;
    }
    return int4(0, 0, int(grid.x), int(grid.y));
}

struct CellOut
{
    float  value; // 0..1 -> escolhe o caractere do charset
    float4 fg;    // cor do caractere (RGBA)
    float4 bg;    // cor de fundo da célula (alpha 0 = bgColor)
    float4 state; // guardado para prev() no próximo frame
    int    glyph; // 0 = usar value; n > 0 = usar o caractere n-1 do charset
};

float _param(uint i) { return _params[i >> 2][i & 3]; }

float4 palette(float t)
{
    uint n = max(paletteCount, 1u);
    if (n == 1u) return _palette[0];
    float x = saturate(t) * (n - 1);
    uint i = min((uint)x, n - 2u);
    return lerp(_palette[i], _palette[i + 1u], x - i);
}

float4 prev(int2 c)
{
    c = clamp(c, int2(0, 0), int2(grid) - 1);
    return _statePrev[c.y * grid.x + c.x];
}

// Verdadeiro exatamente uma vez por clique (calculado na CPU: não depende de tempos).
bool justClicked() { return _clicked != 0; }

// Uma simulação que avança um passo por frame fica com a velocidade do FPS (a 60 fps corre
// ao dobro de 30). Para correr igual a qualquer FPS, avança só quando é hora de um passo de
// `hz` por segundo e, nos outros frames, copia o estado: if (!fixedStep(30)) o.state = prev(c);
bool fixedStep(float hz) { return floor(time * hz) != floor((time - dt) * hz); }

// ---------------------------------------------------------------------------
// Obstáculos: as janelas abertas ([inputs] windows = true) e os ícones do ambiente de
// trabalho ([inputs] icons = true), em células do canvas. A chuva pode bater nas janelas,
// a neve acumular em cima delas, os peixes contorná-las.
// ---------------------------------------------------------------------------
uint obstacleCount() { return _obstacleCount; }
uint windowCount() { return _windowCount; }
int4 obstacleRect(uint k) { return _obstacles[k]; }
bool isWindowObstacle(uint k) { return k < _windowCount; }

// A célula c está por baixo de uma janela (ou ícone)? Devolve o índice, ou -1.
int obstacleAt(int2 c)
{
    [loop] for (uint k = 0; k < _obstacleCount; k++)
    {
        int4 r = _obstacles[k];
        if (c.x >= r.x && c.y >= r.y && c.x < r.z && c.y < r.w) return int(k);
    }
    return -1;
}
bool insideWindow(int2 c) { int k = obstacleAt(c); return k >= 0 && uint(k) < _windowCount; }

// A linha (célula) do topo da janela mais próxima por baixo de c, na mesma coluna; -1 se não há.
int surfaceBelow(int2 c)
{
    int best = -1;
    [loop] for (uint k = 0; k < _windowCount; k++)
    {
        int4 r = _obstacles[k];
        if (c.x >= r.x && c.x < r.z && r.y >= c.y && (best < 0 || r.y < best)) best = r.y;
    }
    return best;
}

// ---------------------------------------------------------------------------
// Áudio do sistema ([inputs] audio = true): o que está a tocar, em bandas de frequência.
// ---------------------------------------------------------------------------
float audioBand(uint i) { i = min(i, 63u); return _spectrum[i >> 2][i & 3]; }
// Espectro contínuo em t ∈ [0,1] (graves à esquerda).
float audioSpectrum(float t)
{
    float x = saturate(t) * 63.0;
    uint i = (uint)x;
    return lerp(audioBand(i), audioBand(min(i + 1u, 63u)), x - i);
}

// ---------------------------------------------------------------------------
// Texto dinâmico ([inputs] text): uma frase, um ficheiro, o código de uma pasta, ou uma
// linha de cada vez ("lines:"). textChar(i) dá o caractere i (Latin-1; o resto vira "?"),
// asciiGlyph(c) o valor de o.glyph que desenha o caractere c (qualquer Latin-1, mesmo que
// não esteja no charset) e textGlyph(i) as duas coisas juntas.
// ---------------------------------------------------------------------------
int asciiGlyph(uint codepoint) { return -int(_text[min(codepoint, 255u)]) - 1; } // para o.glyph
uint textChar(uint i) { return textLength == 0 ? 32u : _text[256u + (i % textLength)]; }
int textGlyph(uint i) { return asciiGlyph(textChar(i)); }
float textAge() { return textInfo.x; }            // segundos desde que a linha atual apareceu
uint textLineIndex() { return (uint)textInfo.y; } // qual das linhas ("lines:")

// ---------------------------------------------------------------------------
// Imagem/vídeo de entrada (render.source): uv em [0,1].
// ---------------------------------------------------------------------------
bool hasSource() { return sourceSize.x > 0; }
float4 sampleSource(float2 uv) { return _source.SampleLevel(_linear, uv, 0); }
float luminance(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
// uv que encaixa a imagem num retângulo (cover = 1 corta para preencher; 0 = cabe toda).
float2 fitSource(float2 p, float2 rectMin, float2 rectSize, float cover)
{
    float imgAspect = sourceSize.x / max(sourceSize.y, 1.0);
    float boxAspect = rectSize.x / max(rectSize.y, 1e-3);
    float2 uv = (p - rectMin) / rectSize;
    float2 scale = (imgAspect > boxAspect) == (cover > 0.5) ? float2(boxAspect / imgAspect, 1) : float2(1, imgAspect / boxAspect);
    return (uv - 0.5) * scale + 0.5;
}
// Contornos (Sobel sobre a luminância, à escala de uma célula): x = força, y = ângulo (rad).
float2 sourceEdge(float2 uv, float2 cellUv)
{
    float l[9];
    [unroll] for (int j = 0; j < 3; j++)
    [unroll] for (int i = 0; i < 3; i++)
        l[j * 3 + i] = luminance(sampleSource(uv + float2(i - 1, j - 1) * cellUv).rgb);
    float gx = (l[2] + 2 * l[5] + l[8]) - (l[0] + 2 * l[3] + l[6]);
    float gy = (l[6] + 2 * l[7] + l[8]) - (l[0] + 2 * l[1] + l[2]);
    return float2(length(float2(gx, gy)), atan2(gy, gx));
}

// ---------------------------------------------------------------------------
// Fonte grande de 5x7 "píxeis" (letras, dígitos, pontuação), para textos gigantes.
// bigFontPixel('A', x, y) com x 0..4, y 0..6.
// ---------------------------------------------------------------------------
static const uint2 BIG_FONT[59] = {
    uint2(0x00000, 0x0000), uint2(0x21084, 0x1004), uint2(0x52800, 0x0000), uint2(0x57D4A, 0x7D40), uint2(0x23E8E, 0x17C4), uint2(0xC6444, 0x2263),
    uint2(0x64A88, 0x564D), uint2(0x21000, 0x0000), uint2(0x11108, 0x2082), uint2(0x41042, 0x0888), uint2(0x012AE, 0x5480), uint2(0x0109F, 0x1080),
    uint2(0x00000, 0x3088), uint2(0x0001F, 0x0000), uint2(0x00000, 0x018C), uint2(0x00444, 0x2200), uint2(0x74675, 0x662E), uint2(0x23084, 0x108E),
    uint2(0x74422, 0x111F), uint2(0xF8882, 0x062E), uint2(0x11952, 0x7C42), uint2(0xFC3C1, 0x062E), uint2(0x3221E, 0x462E), uint2(0xF8444, 0x2108),
    uint2(0x7462E, 0x462E), uint2(0x7462F, 0x044C), uint2(0x03180, 0x3180), uint2(0x03180, 0x3088), uint2(0x11110, 0x2082), uint2(0x003E0, 0x7C00),
    uint2(0x41041, 0x0888), uint2(0x74422, 0x1004), uint2(0x7442D, 0x56AE), uint2(0x7463F, 0x4631), uint2(0xF463E, 0x463E), uint2(0x74610, 0x422E),
    uint2(0xE4A31, 0x465C), uint2(0xFC21E, 0x421F), uint2(0xFC21E, 0x4210), uint2(0x74617, 0x462F), uint2(0x8C63F, 0x4631), uint2(0x71084, 0x108E),
    uint2(0x38842, 0x0A4C), uint2(0x8CA98, 0x5251), uint2(0x84210, 0x421F), uint2(0x8EEB5, 0x4631), uint2(0x8C735, 0x4E31), uint2(0x74631, 0x462E),
    uint2(0xF463E, 0x4210), uint2(0x74631, 0x564D), uint2(0xF463E, 0x5251), uint2(0x7C20E, 0x043E), uint2(0xF9084, 0x1084), uint2(0x8C631, 0x462E),
    uint2(0x8C631, 0x4544), uint2(0x8C635, 0x56AA), uint2(0x8C544, 0x2A31), uint2(0x8C544, 0x1084), uint2(0xF8444, 0x221F),
};
bool bigFontPixel(uint ch, int x, int y)
{
    if (x < 0 || x > 4 || y < 0 || y > 6) return false;
    if (ch >= 97 && ch <= 122) ch -= 32; // minúsculas como maiúsculas
    if (ch < 32 || ch > 90) return false;
    uint2 g = BIG_FONT[ch - 32];
    uint row = y < 4 ? (g.x >> (5 * (3 - y))) & 31u : (g.y >> (5 * (6 - y))) & 31u;
    return ((row >> (4 - x)) & 1u) != 0;
}

// ---------------------------------------------------------------------------
// 3D por raymarching: formas (SDF), combinações e uma câmara simples.
// ---------------------------------------------------------------------------
float sdSphere(float3 p, float r) { return length(p) - r; }
float sdBox(float3 p, float3 b) { float3 q = abs(p) - b; return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0); }
float sdTorus(float3 p, float2 t) { float2 q = float2(length(p.xz) - t.x, p.y); return length(q) - t.y; }
float sdCylinder(float3 p, float h, float r) { float2 d = abs(float2(length(p.xz), p.y)) - float2(r, h); return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)); }
float sdPlane(float3 p, float height) { return p.y - height; }
float opUnion(float a, float b) { return min(a, b); }
float opSmoothUnion(float a, float b, float k) { float h = saturate(0.5 + 0.5 * (b - a) / k); return lerp(b, a, h) - k * h * (1.0 - h); }
float3 opRepeat(float3 p, float3 period) { return p - period * round(p / period); }
float3x3 rotateX(float a) { float s = sin(a), c = cos(a); return float3x3(1, 0, 0, 0, c, -s, 0, s, c); }
float3x3 rotateY(float a) { float s = sin(a), c = cos(a); return float3x3(c, 0, s, 0, 1, 0, -s, 0, c); }
float3x3 rotateZ(float a) { float s = sin(a), c = cos(a); return float3x3(c, -s, 0, s, c, 0, 0, 0, 1); }
// Raio da câmara para um ponto do ecrã. uv: -1..1 (x já com a proporção corrigida).
float3 cameraRay(float3 eye, float3 target, float2 uv, float zoom)
{
    float3 f = normalize(target - eye);
    float3 r = normalize(cross(float3(0, 1, 0), f));
    float3 u = cross(f, r);
    return normalize(f * zoom + r * uv.x - u * uv.y);
}

// Distância "redonda" em alturas de célula (corrige as células serem mais altas que largas).
float cellDistance(float2 a, float2 b)
{
    float2 d = a - b;
    d.x *= cellAspect;
    return length(d);
}

// ---------------------------------------------------------------------------
// Aleatório e ruído
// ---------------------------------------------------------------------------
float hash11(float p)
{
    p = frac(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return frac(p);
}

float hash21(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float2 hash22(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.xx + p3.yz) * p3.zy);
}

float3 hash33(float3 p3)
{
    p3 = frac(p3 * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yxz + 33.33);
    return frac((p3.xxy + p3.yxx) * p3.zyx);
}

float noise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + float2(1, 0));
    float c = hash21(i + float2(0, 1));
    float d = hash21(i + float2(1, 1));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float noise(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    float3 u = f * f * (3.0 - 2.0 * f);
    float n000 = hash33(i).x,                   n100 = hash33(i + float3(1, 0, 0)).x;
    float n010 = hash33(i + float3(0, 1, 0)).x, n110 = hash33(i + float3(1, 1, 0)).x;
    float n001 = hash33(i + float3(0, 0, 1)).x, n101 = hash33(i + float3(1, 0, 1)).x;
    float n011 = hash33(i + float3(0, 1, 1)).x, n111 = hash33(i + float3(1, 1, 1)).x;
    return lerp(lerp(lerp(n000, n100, u.x), lerp(n010, n110, u.x), u.y),
                lerp(lerp(n001, n101, u.x), lerp(n011, n111, u.x), u.y), u.z);
}

float fbm(float2 p)
{
    float sum = 0, amp = 0.5, norm = 0;
    [unroll] for (int k = 0; k < 5; k++)
    {
        sum += amp * noise(p);
        norm += amp;
        p = mul(float2x2(1.6, 1.2, -1.2, 1.6), p) + 17.0;
        amp *= 0.5;
    }
    return sum / norm;
}

float fbm(float3 p)
{
    float sum = 0, amp = 0.5, norm = 0;
    [unroll] for (int k = 0; k < 5; k++)
    {
        sum += amp * noise(p);
        norm += amp;
        p = p * 2.03 + 11.0;
        amp *= 0.5;
    }
    return sum / norm;
}
