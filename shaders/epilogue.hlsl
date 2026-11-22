// epilogue.hlsl — o ponto de entrada do compute shader. Vem DEPOIS do main.hlsl do
// wallpaper, para poder chamar a função cell() que ele define.

uint _packColor(float4 c)
{
    uint4 u = (uint4)round(saturate(c) * 255.0);
    return u.r | (u.g << 8) | (u.b << 16) | (u.a << 24);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= grid.x || id.y >= grid.y)
        return;
    uint index = id.y * grid.x + id.x;

    // Células fora de todos os monitores (no modo estendido, os "buracos" do ecrã virtual)
    // ficam vazias e com estado 0: para uma simulação, funcionam como uma parede.
    if (!visible(int2(id.xy)))
    {
        _cells[index] = uint3(0, 0, 0);
        _stateNext[index] = float4(0, 0, 0, 0);
        return;
    }

    worldSize = float2(grid) * worldPerCell;
    mouse.pos = _mousePos;
    mouse.vel = _mouseVel;
    mouse.world = _mousePos * worldPerCell;
    mouse.velWorld = _mouseVel * worldPerCell;
    mouse.buttons = _buttons;
    mouse.inside = _inside != 0;
    mouse.presence = _presence;
    mouse.clickPos = _clickPos;
    mouse.clickWorld = _clickPos * worldPerCell;
    mouse.clickTime = _clickTime;
    mouse.clickButton = _clickButton;
    mouse.scroll = _scroll;

    CellIn i;
    i.id = id.xy;
    i.pos = float2(id.xy) + 0.5;
    i.uv = i.pos / float2(grid);
    i.world = i.pos * worldPerCell;

    CellOut o = cell(i);

    // o.glyph > 0: caractere do charset/sprites; < 0: slot bruto do atlas (asciiGlyph, texto);
    // 0: a rampa do charset escolhida por o.value.
    uint n = max(charsetSize, 1u);
    uint maxSlot = n + glyphCount - 1u;
    uint g;
    if (o.glyph > 0)
        g = min((uint)(o.glyph - 1), maxSlot);
    else if (o.glyph < 0)
        g = min((uint)(-(o.glyph + 1)), max(_atlasSlots, 1u) - 1u);
    else
        g = min((uint)(saturate(o.value) * n), n - 1);
    _cells[index] = uint3(g, _packColor(o.fg), _packColor(o.bg));
    _stateNext[index] = o.state;
}
