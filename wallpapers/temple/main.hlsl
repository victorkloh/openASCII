// Temple — uma cena 3D em tempo real, desenhada em ASCII.
//
// Tech demo do 3D por raymarching (funções SDF do prelude): cada célula lança um raio da
// câmara e avança pela "distância à superfície mais próxima" até bater em algo. Daí sai a
// normal (gradiente da SDF), a luz do sol com sombras suaves, oclusão ambiente, nevoeiro e
// um brilho à volta da esfera. A luminância final escolhe o caractere; a cor vai no fg.
//
// O que podes copiar daqui:
//   - map(): a cena inteira como uma só função de distância (formas + repetição polar);
//   - march(), normalAt(), softShadow(), ambientOcclusion(): o kit básico de raymarching;
//   - a câmara: cameraRay() com uv em unidades de mundo, para a cena se estender pelos
//     monitores como um só ecrã (o principal fica no centro).
//   - o rato roda a câmara (com mouse.presence a suavizar a entrada e a saída).

static const float PI = 3.14159265;

// Materiais
static const float M_GROUND = 0, M_MARBLE = 1, M_GOLD = 2, M_ORB = 3;

float2 orbCenter() { return float2(0, 4.25 + sin(time * 0.9) * 0.18); } // (x=0, y) — está no eixo

// A cena. Devolve a distância e, em mat, o material da superfície mais próxima.
float map(float3 p, out float mat)
{
    // Chão
    float d = p.y;
    mat = M_GROUND;

    // Plataforma em três degraus
    float base = sdBox(p - float3(0, 0.12, 0), float3(3.6, 0.12, 3.6));
    base = min(base, sdBox(p - float3(0, 0.36, 0), float3(3.25, 0.12, 3.25)));
    base = min(base, sdBox(p - float3(0, 0.6, 0), float3(2.9, 0.12, 2.9)));

    // Colunas: repetição polar (12 à volta). Rodamos p para o "setor" mais próximo e
    // desenhamos UMA coluna; a SDF fica igual para as 12.
    float sector = 2.0 * PI / 12.0;
    float a = atan2(p.z, p.x);
    float k = round(a / sector);
    float3 q = mul(rotateY(k * sector), p);         // q.x = distância ao eixo, no setor
    q.x -= 2.3;
    float column = sdCylinder(q - float3(0, 1.75, 0), 1.05, 0.17 + 0.015 * sin(q.y * 18.0)); // estrias
    column = min(column, sdBox(q - float3(0, 0.82, 0), float3(0.26, 0.08, 0.26)));            // base
    column = min(column, sdBox(q - float3(0, 2.86, 0), float3(0.27, 0.07, 0.27)));            // capitel

    float marble = min(base, column);
    // Anel do topo (o entablamento) e uma cúpula aberta
    float ring = sdTorus(p - float3(0, 3.02, 0), float2(2.3, 0.2));
    float dome = max(sdSphere(p - float3(0, 2.9, 0), 1.95), -sdSphere(p - float3(0, 2.9, 0), 1.85));
    dome = max(dome, -(p.y - 3.4));   // só a parte de cima
    dome = max(dome, p.y - 4.0);      // aberta no topo (vê-se a esfera a flutuar)
    float gold = min(ring, dome);

    if (marble < d) { d = marble; mat = M_MARBLE; }
    if (gold < d) { d = gold; mat = M_GOLD; }

    // A esfera que flutua, com dois anéis a rodar à volta
    float2 oc = orbCenter();
    float3 po = p - float3(0, oc.y, 0);
    float orb = sdSphere(po, 0.5);
    float3 r1 = mul(rotateX(time * 0.7), mul(rotateZ(0.5), po));
    float3 r2 = mul(rotateZ(time * 0.5), mul(rotateX(1.2), po));
    float rings = min(sdTorus(r1, float2(0.85, 0.035)), sdTorus(r2, float2(1.05, 0.03)));
    if (orb < d) { d = orb; mat = M_ORB; }
    if (rings < d) { d = rings; mat = M_GOLD; }
    return d;
}

float mapD(float3 p) { float m; return map(p, m); }

// Avança pelo raio até bater (ou desistir). Devolve a distância percorrida (-1 = céu).
float march(float3 ro, float3 rd, out float mat)
{
    float t = 0.0;
    mat = -1;
    [loop] for (int s = 0; s < 96; s++)
    {
        float m;
        float d = map(ro + rd * t, m);
        if (d < 0.0015 * t) { mat = m; return t; }
        t += d * 0.9;
        if (t > 60.0) break;
    }
    return -1.0;
}

float3 normalAt(float3 p)
{
    const float2 e = float2(0.0015, 0);
    return normalize(float3(mapD(p + e.xyy) - mapD(p - e.xyy), mapD(p + e.yxy) - mapD(p - e.yxy),
                            mapD(p + e.yyx) - mapD(p - e.yyx)));
}

// Sombra suave: quão perto o raio para a luz passa de outras superfícies.
float softShadow(float3 ro, float3 rd)
{
    float res = 1.0, t = 0.03;
    [loop] for (int s = 0; s < 40; s++)
    {
        float h = mapD(ro + rd * t);
        res = min(res, 10.0 * h / t);
        t += clamp(h, 0.03, 0.5);
        if (res < 0.01 || t > 20.0) break;
    }
    return saturate(res);
}

// Oclusão ambiente: cantos e frestas recebem menos luz do céu.
float ambientOcclusion(float3 p, float3 n)
{
    float occ = 0.0, w = 1.0;
    [unroll] for (int s = 1; s <= 5; s++)
    {
        float h = 0.06 * s;
        occ += (h - mapD(p + n * h)) * w;
        w *= 0.6;
    }
    return saturate(1.0 - 2.5 * occ);
}

float3 sky(float3 rd, float3 sunDir)
{
    // Crepúsculo: horizonte quente, zénite escuro.
    float h = saturate(rd.y);
    float3 col = lerp(palette(0.42).rgb * 0.8, palette(0.08).rgb, pow(h, 0.45));
    col += palette(0.7).rgb * pow(saturate(dot(rd, sunDir)), 12.0) * 0.5;
    return col;
}

CellOut cell(CellIn i)
{
    CellOut o;
    o.glyph = 0;
    o.state = 0;
    o.bg = 0;

    // Câmara: órbita lenta; o rato acrescenta rotação (e sai suavemente).
    float2 m = (mouse.world - primaryCenterWorld()) / primarySizeWorld();
    float az = time * 0.06 * param("orbit") + m.x * 2.4 * mouse.presence;
    float el = 0.32 + (-m.y) * 0.5 * mouse.presence;
    float dist = 9.0 / param("zoom");
    float3 target = float3(0, 1.9, 0);
    float3 eye = target + float3(cos(az) * cos(el), sin(el), sin(az) * cos(el)) * dist;

    // uv em unidades de mundo a partir do centro do monitor principal (x e y na mesma escala).
    float2 uv = (i.world - primaryCenterWorld()) / (primarySizeWorld().y * 0.5);
    float3 rd = cameraRay(eye, target, uv, 1.7);

    float sunA = time * 0.04 * param("sunspeed") + 0.8;
    float3 sunDir = normalize(float3(cos(sunA), 0.75, sin(sunA)));

    float mat;
    float t = march(eye, rd, mat);
    float3 col;
    float lum;
    float3 orbPos = float3(0, orbCenter().y, 0);
    if (t < 0)
    {
        // O céu vive sobretudo da cor de fundo: caracteres quase vazios, para o templo sobressair.
        col = sky(rd, sunDir);
        lum = luminance(col) * 0.22;
        // Estrelas: uma por célula, ao acaso, a cintilar.
        float h = hash21(float2(i.id));
        if (h > 0.985 && rd.y > 0.12)
        {
            float tw = 0.5 + 0.5 * sin(time * (2.0 + h * 5.0) + h * 80.0);
            lum = 0.08 + 0.14 * tw;  // os caracteres mais leves da rampa: . , :
            col = palette(0.95).rgb * (0.4 + 0.6 * tw);
        }
    }
    else
    {
        float3 p = eye + rd * t;
        float3 n = normalAt(p);
        float3 albedo;
        if (mat == M_GROUND)
        {
            // Lajes de pedra: linhas a cada 1.2 unidades.
            float2 g = abs(frac(p.xz / 1.2) - 0.5);
            float tile = smoothstep(0.47, 0.5, max(g.x, g.y));
            albedo = palette(0.4).rgb * (0.7 + 0.3 * hash21(floor(p.xz / 1.2))) * (1.0 - tile * 0.6);
        }
        else if (mat == M_MARBLE) albedo = palette(0.85).rgb;
        else if (mat == M_GOLD) albedo = palette(0.65).rgb;
        else albedo = palette(1.0).rgb;

        if (mat == M_ORB)
        {
            float fres = pow(1.0 - saturate(dot(n, -rd)), 2.0);
            col = albedo * (1.2 + 0.4 * sin(time * 2.0)) + fres * 0.5;
            lum = 1.0;
        }
        else
        {
            float diff = saturate(dot(n, sunDir));
            float shadow = diff > 0.0 ? softShadow(p + n * 0.01, sunDir) : 0.0;
            float ao = ambientOcclusion(p, n);
            float skyLight = 0.5 + 0.5 * n.y;
            float3 h = normalize(sunDir - rd);
            float spec = pow(saturate(dot(n, h)), mat == M_GOLD ? 48.0 : 16.0) * (mat == M_GOLD ? 0.9 : 0.25);
            // A esfera também ilumina o que está perto (luz pontual sem sombra).
            float3 toOrb = orbPos - p;
            float orbLight = saturate(dot(n, normalize(toOrb))) / (1.0 + dot(toOrb, toOrb) * 0.35);
            col = albedo * (diff * shadow * 1.1 * palette(0.9).rgb + skyLight * ao * 0.28 * palette(0.4).rgb) +
                  spec * shadow + palette(1.0).rgb * orbLight * 0.9 * ao;
            lum = luminance(col);
        }
        // Nevoeiro: ao longe tudo se dissolve na cor do céu.
        float fog = 1.0 - exp(-t * t * 0.0009 * param("fog"));
        col = lerp(col, sky(rd, sunDir), fog);
        lum = lerp(lum, luminance(col) * 0.22, fog);
    }

    // Halo à volta da esfera (mesmo no céu): soma ao longo do raio.
    float3 toOrbRay = orbPos - eye;
    float along = max(dot(toOrbRay, rd), 0.0);
    float closest = length(toOrbRay - rd * along);
    if (t < 0 || t > along)
    {
        float glow = 0.18 / (1.0 + closest * closest * 3.0);
        col += palette(1.0).rgb * glow;
        lum += glow;
    }

    o.value = saturate(pow(saturate(lum * param("exposure")), 0.75));
    o.fg = float4(saturate(col * 1.4 * param("exposure")), 1);
    o.bg = float4(col * (t < 0 ? 0.55 : 0.18), 1);
    return o;
}
