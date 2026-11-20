// Donut — um toro 3D desenhado por raymarching: para cada célula lança-se um raio, que
// avança pela "distância até à superfície" (SDF) até lhe tocar. A luz na superfície
// (normal · direção da luz) escolhe o caractere da rampa .,-~:;=!*#$@.
//
// A rotação vem do tempo; o rato inclina o donut; um clique dá-lhe um empurrão, que se
// guarda no estado (state.x = ângulo extra, state.y = velocidade do empurrão).

float sdTorus(float3 p, float R, float r)
{
    float2 q = float2(length(p.xz) - R, p.y);
    return length(q) - r;
}

float3x3 rotX(float a) { float s = sin(a), c = cos(a); return float3x3(1, 0, 0, 0, c, -s, 0, s, c); }
float3x3 rotY(float a) { float s = sin(a), c = cos(a); return float3x3(c, 0, s, 0, 1, 0, -s, 0, c); }
float3x3 rotZ(float a) { float s = sin(a), c = cos(a); return float3x3(c, -s, 0, s, c, 0, 0, 0, 1); }

CellOut cell(CellIn i)
{
    float4 st = prev(int2(i.id));
    float2 center = primaryCenterWorld();
    float2 size = primarySizeWorld();

    // Empurrão do clique: velocidade que decai, integrada num ângulo extra (estado partilhado).
    float spinVel = st.y * exp(-dt * 1.2);
    if (justClicked()) spinVel += 6.0;
    float extra = st.x + spinVel * dt;

    // Inclinação pelo rato (suavizada em state.zw).
    float2 target = clamp((mouse.world - center) / (size * 0.5), -1.0, 1.0) * mouse.presence;
    float2 tilt = lerp(st.zw, target, 1.0 - exp(-dt * 3.0));

    float t = time * param("speed");
    float3x3 rot = mul(rotZ(t * 0.5 + extra * 0.3 + 0.4), mul(rotX(t * 1.0 + tilt.y * 1.2 + extra + 1.1), rotY(tilt.x * 1.2)));

    // Raio a partir da câmara (perspetiva suave), em unidades do donut.
    float radius = size.y * param("size") * 0.5;
    float2 uv = (i.world - center) / radius;
    float3 ro = float3(0, 0, -4.0);
    float3 rd = normalize(float3(uv * 0.75, 2.2));
    float R = 1.0, r = param("thickness") * 0.6;

    float dist = 0.0;
    bool hit = false;
    float3 p = ro;
    // Otimização: longe do centro o raio nunca toca no donut (poupa o raymarching).
    if (length(uv) > 1.7) dist = 99.0;
    [loop] for (int k = 0; k < 64 && dist < 8.0; k++)
    {
        p = ro + rd * dist;
        float d = sdTorus(mul(rot, p), R, r);
        if (d < 0.002) { hit = true; break; }
        dist += d;
        if (dist > 8.0) break;
    }

    CellOut o;
    o.glyph = 0;
    o.bg = float4(0, 0, 0, 0);
    o.state = float4(extra, spinVel, tilt);
    if (!hit)
    {
        // Fundo: um brilho suave atrás do donut.
        float g = exp(-length(uv) * 0.9) * 0.18;
        o.value = g * 0.6;
        o.fg = palette(0.3) * g * 3.0;
        return o;
    }

    // Normal por diferenças finitas e luz difusa + especular.
    float3 q = mul(rot, p);
    float e = 0.001;
    float3 n = normalize(float3(sdTorus(q + float3(e, 0, 0), R, r) - sdTorus(q - float3(e, 0, 0), R, r),
                                sdTorus(q + float3(0, e, 0), R, r) - sdTorus(q - float3(0, e, 0), R, r),
                                sdTorus(q + float3(0, 0, e), R, r) - sdTorus(q - float3(0, 0, e), R, r)));
    float3 nWorld = mul(transpose(rot), n);
    float3 light = normalize(float3(0.4, 0.7, -0.6));
    float diff = saturate(dot(nWorld, light));
    float spec = pow(saturate(dot(reflect(rd, nWorld), light)), 16.0);
    float lum = saturate(0.08 + diff * 0.85 + spec * 0.4);

    o.value = lum;
    o.fg = palette(0.25 + lum * 0.75);

    // Granulado: pontinhos coloridos na metade de cima do donut (onde estaria a cobertura).
    if (param("sprinkles") > 0.0 && q.y > r * 0.2)
    {
        float2 ang = float2(atan2(q.z, q.x), atan2(q.y, length(q.xz) - R));
        float2 sc = floor(ang * float2(9.0, 5.0));
        if (hash21(sc) < 0.35 * param("sprinkles"))
        {
            float hue = hash21(sc + 3.0);
            o.glyph = glyph(uint(hash21(sc + 7.0) * 4.0));
            o.fg = hue < 0.25 ? float4(1, 0.35, 0.6, 1)
                 : hue < 0.5  ? float4(0.4, 0.8, 1, 1)
                 : hue < 0.75 ? float4(1, 0.9, 0.3, 1)
                              : float4(0.5, 1, 0.5, 1);
        }
    }
    return o;
}
