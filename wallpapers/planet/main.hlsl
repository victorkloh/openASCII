// Planet — um planeta procedural a rodar, com dia e noite da estação do ano real.
//
// Tech demo de geometria e texturas sem imagens: cada célula calcula o ponto da esfera que
// vê (projeção ortográfica), roda-o (rotação do planeta + inclinação do eixo) e daí tira a
// latitude/longitude. Continentes, montanhas, desertos e gelo saem de ruído fbm 3D na
// superfície da esfera (sem costuras, ao contrário de uma textura 2D embrulhada).
//
// A luz vem do Sol com a declinação de HOJE (localDate): em novembro o hemisfério sul está
// mais iluminado, em junho o norte. No lado escuro acendem-se as cidades. Há nuvens a
// correr mais depressa que o planeta, um halo de atmosfera nas bordas e uma lua em órbita.
// O rato roda o planeta (com mouse.presence para entrar e sair suavemente).

static const float PI = 3.14159265;

float3 rotY(float3 p, float a) { float s = sin(a), c = cos(a); return float3(c * p.x + s * p.z, p.y, -s * p.x + c * p.z); }
float3 rotX(float3 p, float a) { float s = sin(a), c = cos(a); return float3(p.x, c * p.y - s * p.z, s * p.y + c * p.z); }
float3 rotZ(float3 p, float a) { float s = sin(a), c = cos(a); return float3(c * p.x - s * p.y, s * p.x + c * p.y, p.z); }

// Declinação do Sol (graus) para o dia do ano.
float solarDeclination()
{
    static const uint DAYS[12] = {0u, 31u, 59u, 90u, 120u, 151u, 181u, 212u, 243u, 273u, 304u, 334u};
    float day = float(DAYS[clamp(localDate.y, 1u, 12u) - 1u] + localDate.z);
    return -23.44 * cos(2.0 * PI / 365.0 * (day + 10.0));
}

CellOut cell(CellIn i)
{
    CellOut o;
    o.glyph = 0;
    o.state = 0;
    o.bg = 0;
    o.value = 0;
    o.fg = 0;

    float2 center = primaryCenterWorld();
    float R = primarySizeWorld().y * 0.36 * param("size");
    float2 q = (i.world - center) / R; // em raios do planeta
    float r = length(q);

    // Sol: vem da esquerda-frente, inclinado pela declinação de hoje (em relação ao equador).
    float declin = solarDeclination() * PI / 180.0;
    float3 sunDir = normalize(float3(-0.6, sin(declin) * 1.1, 0.8));

    // --- Estrelas e lua (atrás ou à frente do planeta) ---
    float moonA = time * 0.05 * param("speed") + 1.0;
    float2 moonPos = float2(cos(moonA) * 1.75, sin(moonA) * 0.28);
    bool moonFront = sin(moonA + PI * 0.5) > 0.0;
    float moonR = 0.17;
    float2 mq = (q - moonPos) / moonR;
    bool moonHere = dot(mq, mq) < 1.0;

    if (r >= 1.0 || (moonHere && moonFront))
    {
        if (moonHere && (moonFront || r >= 1.0))
        {
            float3 mn = float3(mq.x, -mq.y, sqrt(saturate(1.0 - dot(mq, mq))));
            float crater = fbm(mn * 4.0 + 7.0);
            float light = saturate(dot(mn, sunDir)) * (0.6 + 0.5 * crater);
            o.value = 0.1 + 0.8 * light;
            o.fg = float4(palette(0.85).rgb * (0.3 + 0.9 * light), 1);
            return o;
        }
        // Halo da atmosfera por fora do planeta.
        float halo = exp(-(r - 1.0) * 18.0) * saturate(dot(normalize(float3(q.x, -q.y, 0.2)), sunDir) + 0.6);
        float h = hash21(float2(i.id));
        if (h > 0.988)
        {
            float tw = 0.5 + 0.5 * sin(time * (1.5 + h * 4.0) + h * 70.0);
            o.value = 0.05 + 0.12 * tw;
            o.fg = palette(0.9) * (0.4 + 0.6 * tw);
        }
        if (halo > 0.05)
        {
            o.value = max(o.value, halo * 0.35);
            o.fg = float4(float3(0.35, 0.65, 1.0) * halo, 1);
            o.bg = float4(float3(0.1, 0.25, 0.6) * halo * 0.4, 1);
        }
        return o;
    }

    // --- Superfície: ponto da esfera visto daqui ---
    float3 n = float3(q.x, -q.y, sqrt(1.0 - r * r)); // normal em espaço de vista (y para cima)
    float2 m = (mouse.world - center) / max(primarySizeWorld(), 1.0);
    float spin = time * 0.04 * param("speed") + m.x * 3.0 * mouse.presence;
    float tilt = 23.44 * PI / 180.0;
    float3 p = rotY(rotX(rotZ(n, -tilt * 0.6), -m.y * 1.5 * mouse.presence), spin); // ponto no planeta
    float lat = asin(clamp(p.y, -1.0, 1.0));

    // Continentes e relevo (ruído 3D na esfera: sem costuras).
    float land = fbm(p * 1.8 + float3(3.1, 1.7, 5.2));
    float detail = fbm(p * 7.0 + 11.0);
    float sea = param("sea");
    bool isLand = land > sea;
    float height = saturate((land - sea) * 6.0);
    float absLat = abs(lat) / (PI * 0.5);
    bool ice = absLat > 0.78 - detail * 0.12;

    float3 albedo;
    if (ice) albedo = float3(0.92, 0.96, 1.0);
    else if (isLand)
    {
        float desert = saturate(1.0 - abs(absLat - 0.27) * 6.0) * saturate(detail * 1.6 - 0.3);
        float3 green = lerp(float3(0.16, 0.42, 0.18), float3(0.36, 0.5, 0.22), detail);
        float3 sand = float3(0.78, 0.66, 0.42);
        float3 rock = float3(0.5, 0.45, 0.42);
        albedo = lerp(lerp(green, sand, desert), rock, smoothstep(0.55, 0.9, height));
    }
    else
    {
        float depth = saturate((sea - land) * 5.0);
        albedo = lerp(float3(0.1, 0.35, 0.6), float3(0.02, 0.1, 0.32), depth);
    }

    // Nuvens (mais rápidas que o planeta).
    float3 pc = rotY(p, time * 0.012 * param("speed"));
    float cloud = smoothstep(0.52, 0.7, fbm(pc * 3.0 + float3(0, time * 0.02, 0))) * param("clouds");

    // Luz: dia/noite com um crepúsculo suave; brilho especular no oceano.
    float3 nv = n; // normal em espaço de vista
    float day = smoothstep(-0.08, 0.2, dot(nv, sunDir));
    float diff = saturate(dot(nv, sunDir));
    float spec = (!isLand && !ice) ? pow(saturate(dot(reflect(-sunDir, nv), float3(0, 0, 1))), 28.0) * 0.8 : 0.0;
    float3 col = albedo * (0.06 + 1.05 * diff) + spec * day;
    col = lerp(col, float3(1, 1, 1) * (0.1 + 0.95 * diff), cloud * 0.85);

    // Cidades no lado noturno: pontos laranja nas zonas de terra (não no gelo).
    float night = 1.0 - day;
    float city = (isLand && !ice) ? step(0.78, hash21(floor(float2(atan2(p.z, p.x), lat) * 90.0))) * step(0.45, detail) : 0.0;
    col += float3(1.0, 0.7, 0.3) * city * night * (1.0 - cloud) * 0.9;

    // Atmosfera: azul nas bordas (mais forte no lado de dia).
    float rim = pow(1.0 - nv.z, 3.0);
    col += float3(0.3, 0.55, 1.0) * rim * (0.25 + 0.75 * day) * 0.8;

    float lum = luminance(col);
    o.value = saturate(pow(lum, 0.75) * 1.15);
    o.fg = float4(saturate(col * 1.3), 1);
    o.bg = float4(col * 0.22, 1);
    if (city * night > 0.5 && cloud < 0.3) o.value = max(o.value, 0.45);
    return o;
}
