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

    mouse.pos = _mousePos;
    mouse.vel = _mouseVel;
    mouse.buttons = _buttons;
    mouse.inside = _inside != 0;
    mouse.clickPos = _clickPos;
    mouse.clickTime = _clickTime;
    mouse.clickButton = _clickButton;
    mouse.scroll = _scroll;

    CellIn i;
    i.id = id.xy;
    i.pos = float2(id.xy) + 0.5;
    i.uv = i.pos / float2(grid);

    CellOut o = cell(i);

    uint n = max(charsetSize, 1u);
    uint glyph = o.glyph > 0 ? min((uint)(o.glyph - 1), n - 1) : min((uint)(saturate(o.value) * n), n - 1);
    uint index = id.y * grid.x + id.x;
    _cells[index] = uint3(glyph, _packColor(o.fg), _packColor(o.bg));
    _stateNext[index] = o.state;
}
