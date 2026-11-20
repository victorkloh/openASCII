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
//   mouse.inside        o rato está sobre o ambiente de trabalho
//   mouse.clickPos/.clickWorld, mouse.clickTime, mouse.clickButton; justClicked() = clique neste frame
//   mouse.scroll        rodas do scroll neste frame (+ para cima)
//   param("nome")       valor de um parâmetro declarado em [params] do wallpaper.toml
//   palette(t)          cor da paleta em t ∈ [0,1] (interpola entre as cores)
//   bgColor             cor de fundo do wallpaper.toml
//   charsetSize         número de caracteres do charset (a rampa de `value`)
//   glyph(k)            para o.glyph: o k-ésimo caractere de `glyphs` no wallpaper.toml (sprites)
//   noise(p), fbm(p)    ruído suave em [0,1] (2D e 3D) e soma de oitavas
//   hash11/21/22/33     números pseudo-aleatórios em [0,1)
//   prev(c)             o `state` que esta célula (ou a vizinha c) escreveu no frame anterior
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
    float  _pad1;

    int4   _monitors[8]; // zonas visíveis do canvas (células): x0, y0, x1, y1
};

StructuredBuffer<float4>   _statePrev : register(t0);
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

bool justClicked() { return mouse.clickTime >= 0 && time - mouse.clickTime < dt; }

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
