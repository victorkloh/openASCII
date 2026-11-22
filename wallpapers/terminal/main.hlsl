// Terminal — um ecrã de fósforo antigo que escreve citações, letra a letra.
//
// Tech demo de TEXTO e de PÓS-PROCESSAMENTO:
//   - [inputs] text = "lines:quotes.txt" entrega uma linha de cada vez (a rodar a cada
//     text_interval segundos); textChar(i) lê o caractere i, textAge() diz há quanto tempo a
//     linha apareceu (para a animação de escrever) e asciiGlyph(c) desenha qualquer
//     caractere Latin-1 (acentos incluídos), mesmo fora do charset;
//   - quebra de linha por PALAVRAS feita no próprio shader: as células da caixa de texto
//     percorrem a frase e calculam em que linha/coluna cai cada letra;
//   - a secção [post] do wallpaper.toml liga o look de monitor CRT: brilho (glow), linhas,
//     vidro curvo, vinheta, aberração cromática e grão. Funciona com qualquer wallpaper.
// Formato das linhas: "texto|autor" ('|' muda de linha e o autor fica alinhado à direita).

static const uint HEX[16] = {48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 65, 66, 67, 68, 69, 70};
static const uint TITLE[22] = {65, 83, 67, 73, 73, 80, 65, 80, 69, 82, 32, 47, 47, 32, 84, 69, 82, 77, 73, 78, 65, 76};
// "ASCIIPAPER // TERMINAL"

// Onde cai o caractere i (devolve o índice do caractere que está em (row, col), ou -1).
// Quebra gulosa por palavras numa largura W; '|' força uma linha nova (o autor).
int charAt(int row, int col, int W, out bool isAuthor)
{
    isAuthor = false;
    uint n = textLength;
    int r = 0, cc = 0;
    bool author = false;
    int authorLen = 0;
    [loop] for (uint i = 0; i < n; i++)
    {
        uint ch = textChar(i);
        if (ch == 124u) // '|': linha em branco, depois o autor alinhado à direita
        {
            author = true;
            r += 2;
            authorLen = int(n - i - 1u) + 2;
            cc = max(W - authorLen, 0);
            if (r == row && col == cc) { isAuthor = true; return -2; } // o travessão
            cc += 2;
            continue;
        }
        if (ch == 32u && !author)
        {
            int wl = 0;
            [loop] for (uint j = i + 1u; j < min(n, i + 26u); j++)
            {
                uint cj = textChar(j);
                if (cj == 32u || cj == 124u) break;
                wl++;
            }
            if (cc + 1 + wl > W) { r++; cc = 0; continue; } // a próxima palavra não cabe
        }
        if (r == row && cc == col) { isAuthor = author; return int(i); }
        cc++;
        if (cc >= W && !author) { r++; cc = 0; }
        if (r > row) return -1;
    }
    return -1;
}

CellOut cell(CellIn i)
{
    CellOut o;
    o.glyph = 0;
    o.value = 0;
    o.bg = 0;
    o.state = 0;
    o.fg = palette(0.6);
    int2 c = int2(i.id);
    int4 mon = monitorRectAt(c);
    int4 P = primaryRect();
    float4 phosphor = palette(0.8), dim = palette(0.3);

    // Barra de título com o relógio, no topo de cada monitor.
    if (c.y == mon.y + 1)
    {
        int k = c.x - mon.x - 2;
        if (k >= 0 && k < 22) { o.glyph = asciiGlyph(TITLE[k]); o.fg = phosphor; return o; }
        int r = mon.z - 3 - c.x; // relógio HH:MM:SS alinhado à direita
        uint hh = uint(localTime.x), mm = uint(localTime.y), ss = uint(localTime.z);
        uint clock[8] = {48u + ss % 10u, 48u + ss / 10u, 58u, 48u + mm % 10u, 48u + mm / 10u, 58u, 48u + hh % 10u, 48u + hh / 10u};
        if (r >= 0 && r < 8) { o.glyph = asciiGlyph(clock[r]); o.fg = phosphor; return o; }
        if (c.x > mon.x + 1 && c.x < mon.z - 2) { o.glyph = asciiGlyph(45u); o.fg = dim; }
        return o;
    }

    // Caixa da citação, no monitor principal.
    int W = clamp(int(float(P.z - P.x) * 0.55), 24, 90);
    int rowsGuess = int(ceil(float(textLength) / float(W))) + 3;
    int2 box = int2(P.x + (P.z - P.x - W) / 2, P.y + (P.w - P.y - rowsGuess) / 2);
    int2 q = c - box;
    float age = textAge();
    int typed = int(age * 28.0 * param("speed"));
    if (q.x >= 0 && q.x < W && q.y >= 0 && q.y < rowsGuess + 1)
    {
        bool isAuthor;
        int idx = charAt(q.y, q.x, W, isAuthor);
        if (idx == -2 && typed >= int(textLength)) { o.glyph = asciiGlyph(45u); o.fg = dim * 1.6; return o; }
        if (idx >= 0 && idx < typed)
        {
            o.glyph = asciiGlyph(textChar(uint(idx)));
            float fresh = saturate(1.0 - float(typed - idx) / 12.0); // as últimas letras brilham mais
            o.fg = isAuthor ? dim * 1.8 : lerp(phosphor, palette(1.0), fresh);
            return o;
        }
        // Cursor: um bloco a piscar logo a seguir à última letra escrita.
        if (idx >= 0 && idx == min(typed, int(textLength)) && frac(time * 1.6) < 0.55)
        {
            o.glyph = glyph(0u);
            o.fg = phosphor;
            return o;
        }
    }

    // Fundo: um "registo do sistema" em código hexadecimal a subir devagar, muito apagado.
    float scroll = time * 1.2;
    int row = c.y + int(scroll);
    float rh = hash11(float(row) * 1.37);
    int lineLen = int(rh * float(mon.z - mon.x) * 0.5);
    int col = c.x - mon.x - 2;
    if (rh > 0.35 && col >= 0 && col < lineLen && c.y > mon.y + 2)
    {
        float h = hash21(float2(c.x, row));
        bool gap = (col % 9) == 8;
        if (!gap)
        {
            o.glyph = asciiGlyph(HEX[uint(h * 16.0) % 16u]);
            o.fg = dim * (0.25 + 0.25 * rh);
        }
    }
    return o;
}
