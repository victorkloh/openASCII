// Arcade — um Breakout que joga sozinho; com o rato no ambiente de trabalho, jogas tu.
//
// Tech demo de um JOGO inteiro num shader, com estado global em células reservadas:
//   B (canto do monitor principal)      a bola: posição e velocidade (unidades de mundo)
//   S (a célula ao lado de B)           raquete, pontos, vidas e nível
//   H (a seguinte)                      o tijolo em que a bola bateu neste frame
// Todas as células leem B/S/H do frame anterior com prev(). A bola só "vê" os tijolos
// através do estado das próprias células dos tijolos (vivo/morto); o tijolo apaga-se no
// frame a seguir a ser atingido (lê H). Tudo o que cada célula decide depende só do frame
// anterior, por isso nunca há conflitos.
//
// Sem rato, a raquete segue a bola (com algum atraso, para às vezes falhar). Com o rato no
// fundo, a raquete segue o rato. Partida perdida = recomeça; parede limpa = próximo nível.

static const float BRICK_W = 6.0; // células
static const int ROWS = 6;

static int4 P;   // monitor principal (células)
int2 cellB() { return P.xy; }
int2 cellS() { return P.xy + int2(1, 0); }
int2 cellH() { return P.xy + int2(2, 0); }

// Zona dos tijolos e da raquete (células).
int4 brickArea() { int w = P.z - P.x; int cols = int(float(w - 4) / BRICK_W); int x0 = P.x + (w - cols * int(BRICK_W)) / 2; return int4(x0, P.y + 5, x0 + cols * int(BRICK_W), P.y + 5 + ROWS); }
int paddleRow() { return P.w - 3; }
float paddleHalf() { return max(5.0, float(P.z - P.x) * 0.06); }

bool brickAlive(int2 p)
{
    int4 a = brickArea();
    if (p.x < a.x || p.x >= a.z || p.y < a.y || p.y >= a.w) return false;
    return prev(p).x > 0.5;
}
int2 brickOrigin(int2 p) { int4 a = brickArea(); return int2(a.x + int(floor(float(p.x - a.x) / BRICK_W) * BRICK_W), p.y); }

static const uint SCORE_LABEL[6] = {80, 79, 78, 84, 79, 83}; // "PONTOS"
static const uint LEVEL_LABEL[5] = {78, 73, 86, 69, 76};     // "NIVEL"

int digitAt(int2 c, int2 rightPos, int value)
{
    if (c.y != rightPos.y) return 0;
    int k = rightPos.x - c.x;
    if (k < 0 || k > 5) return 0;
    int v = value;
    [unroll] for (int j = 0; j < 6; j++) { if (j == k) break; v /= 10; }
    if (k > 0 && value < int(pow(10.0, float(k)) + 0.5)) return 0;
    return asciiGlyph(48u + uint(v % 10));
}

CellOut cell(CellIn i)
{
    CellOut o;
    o.glyph = 0;
    o.value = 0;
    o.bg = 0;
    o.fg = 0;
    o.state = 0;
    int2 c = int2(i.id);
    P = primaryRect();
    float2 wpc = worldPerCell;

    float4 B = prev(cellB());   // bola: x, y (mundo), vx, vy (mundo/s)
    float4 S = prev(cellS());   // raquete x (células), pontos, vidas, nível
    float4 H = prev(cellH());   // tijolo atingido: x, y (célula), tem?, -
    bool fresh = S.w < 0.5;     // primeiro frame
    int4 area = brickArea();
    float speed = (38.0 + S.w * 4.0) * param("speed");

    // ---------------- Estado do jogo (células reservadas) ----------------
    if (all(c == cellB()) || all(c == cellS()) || all(c == cellH()))
    {
        float2 pos = B.xy, vel = B.zw;
        float paddle = S.x, score = S.y, lives = S.z, level = max(S.w, 1.0);
        float2 hit = float2(-1, -1);
        if (fresh || length(vel) < 1.0)
        {
            pos = float2((P.x + P.z) * 0.5, paddleRow() - 4) * wpc;
            vel = normalize(float2(hash11(time) - 0.5, -1.0)) * speed;
            paddle = (P.x + P.z) * 0.5;
            if (fresh) { score = 0; lives = 3; level = 1; }
        }
        // Raquete: segue o rato (se estiver no fundo) ou a bola, com atraso.
        float target = mouse.presence > 0.5 ? mouse.pos.x : pos.x / wpc.x + sin(time * 0.7) * paddleHalf() * 0.6;
        paddle += (target - paddle) * saturate(dt * (mouse.presence > 0.5 ? 25.0 : 6.5));
        paddle = clamp(paddle, float(P.x) + paddleHalf(), float(P.z) - paddleHalf());

        float2 np = pos + vel * dt;
        float2 cellF = np / wpc;
        // Paredes do monitor principal.
        if (cellF.x < P.x + 0.5 || cellF.x > P.z - 0.5) { vel.x = -vel.x; np.x = pos.x; }
        if (cellF.y < P.y + 2.5) { vel.y = abs(vel.y); np.y = pos.y; }
        // Raquete: o ângulo de saída depende de onde bate.
        if (vel.y > 0 && cellF.y >= paddleRow() - 0.5 && cellF.y < paddleRow() + 0.6 && abs(cellF.x - paddle) < paddleHalf() + 0.6)
        {
            float k = clamp((cellF.x - paddle) / paddleHalf(), -1.0, 1.0);
            vel = normalize(float2(k * 1.1, -1.0)) * speed;
            np.y = (paddleRow() - 0.6) * wpc.y;
        }
        // Tijolos: se a próxima célula tem um tijolo vivo, ressalta e regista o choque.
        // O tijolo atingido no frame anterior (H) ainda aparece vivo em prev() neste frame (só
        // se apaga agora): conta como morto, senão pontuava duas vezes e o nível acabava cedo.
        int2 nc = int2(floor(cellF));
        bool hitBefore = H.z > 0.5 && all(brickOrigin(int2(H.xy)) == brickOrigin(nc));
        if (brickAlive(nc) && !hitBefore)
        {
            int2 pc = int2(floor(pos / wpc));
            if (pc.y != nc.y) vel.y = -vel.y; else vel.x = -vel.x;
            np = pos;
            hit = float2(nc);
            score += 10.0 * level;
        }
        // Perdeu a bola: menos uma vida (e recomeça tudo quando acabam).
        if (cellF.y > P.w)
        {
            lives -= 1.0;
            if (lives < 0.5) { score = 0; lives = 3; level = 1; }
            np = float2(paddle, paddleRow() - 4) * wpc;
            vel = normalize(float2(hash11(time + 3.0) - 0.5, -1.0)) * speed;
        }
        // Parede limpa (todos os pontos possíveis deste nível feitos): próximo nível.
        float bricks = float((area.z - area.x) / int(BRICK_W) * ROWS);
        if (score >= bricks * 10.0 * level * (level + 1.0) * 0.5 - 0.5) level += 1.0;

        if (all(c == cellB())) o.state = float4(np, vel);
        if (all(c == cellS())) o.state = float4(paddle, score, lives, level);
        if (all(c == cellH())) o.state = float4(hit, hit.x >= 0 ? 1 : 0, 0);
    }

    // ---------------- Tijolos ----------------
    if (c.x >= area.x && c.x < area.z && c.y >= area.y && c.y < area.w && !any(c == cellB()))
    {
        float alive = prev(c).x;
        float myLevel = prev(c).y;
        if (fresh || myLevel != S.w) alive = 1.0;                       // nível novo: parede nova
        if (H.z > 0.5 && all(brickOrigin(int2(H.xy)) == brickOrigin(c))) alive = 0.0; // fui atingido
        o.state = float4(alive, S.w, 0, 0);
        if (alive > 0.5)
        {
            int local = int(fmod(float(c.x - area.x), BRICK_W));
            float t = float(c.y - area.y) / float(ROWS);
            o.glyph = local == 0 ? asciiGlyph(91u) : (local == int(BRICK_W) - 1 ? asciiGlyph(93u) : glyph(0u));
            o.fg = palette(0.95 - t * 0.7) * 0.8;
            o.bg = float4(palette(0.9 - t * 0.7).rgb * 0.2, 1);
            return o;
        }
    }

    // ---------------- Bola, rasto, raquete, marcador ----------------
    float2 ballCell = B.xy / wpc;
    if (all(int2(floor(ballCell)) == c)) { o.glyph = asciiGlyph(79u); o.fg = palette(1.0); return o; }
    float2 back = ballCell - normalize(B.zw + 1e-4) * 1.6;
    float dt2 = length((float2(c) + 0.5 - back) * float2(cellAspect, 1.0));
    if (dt2 < 0.7 && length(B.zw) > 1.0) { o.glyph = glyph(2u); o.fg = palette(0.7) * 0.6; return o; }

    if (c.y == paddleRow() && abs(float(c.x) + 0.5 - S.x) < paddleHalf())
    {
        o.glyph = glyph(1u);
        o.fg = mouse.presence > 0.5 ? palette(1.0) : palette(0.75);
        return o;
    }
    if (c.y == P.y + 1)
    {
        int k = c.x - P.x - 2;
        if (k >= 0 && k < 6) { o.glyph = asciiGlyph(SCORE_LABEL[k]); o.fg = palette(0.5); return o; }
        int g = digitAt(c, int2(P.x + 14, P.y + 1), int(S.y));
        if (g != 0) { o.glyph = g; o.fg = palette(1.0); return o; }
        k = c.x - (P.z - 12);
        if (k >= 0 && k < 5) { o.glyph = asciiGlyph(LEVEL_LABEL[k]); o.fg = palette(0.5); return o; }
        g = digitAt(c, int2(P.z - 4, P.y + 1), int(S.w));
        if (g != 0) { o.glyph = g; o.fg = palette(1.0); return o; }
        // Vidas: corações (o) no meio.
        int mid = (P.x + P.z) / 2;
        if (c.x >= mid - 2 && c.x < mid - 2 + int(S.z) * 2 && ((c.x - mid) & 1) == 0) { o.glyph = asciiGlyph(111u); o.fg = palette(0.85); return o; }
    }
    // Fundo: uma grelha de pontos discreta.
    if ((c.x % 4) == 0 && (c.y % 2) == 0) { o.glyph = glyph(3u); o.fg = palette(0.15); }
    return o;
}
