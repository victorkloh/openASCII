# Criar wallpapers para o OpenASCII

Um wallpaper é **uma pasta** com um `wallpaper.toml` e um ficheiro de código. Partilha-a como zip ou num repositório git.

```
meu-wallpaper/
├─ wallpaper.toml   # manifest: nome, fonte, cores, parâmetros
├─ main.hlsl        # o efeito (corre na GPU, uma vez por célula)
├─ preview.png      # opcional
└─ themes/          # opcional: variações sem código
   └─ noite.toml
```

O caminho mais rápido: launcher → **Novo wallpaper**. Escolhe um modelo, dá um nome, e a app cria a pasta em `%APPDATA%\OpenASCII\wallpapers\`, ativa-o e abre o `main.hlsl` no teu editor. Também podes criar a pasta à mão nesse sítio; aparece logo no launcher e no menu da bandeja.

> Os wallpapers do tipo *script* (Lua) chegam na Fase 4. Por agora só há o tipo *shader*.

## 1. O primeiro wallpaper em 2 minutos

**`wallpaper.toml`**
```toml
[meta]
name = "Pulso"
author = "eu"
version = "1.0.0"
description = "Círculos que pulsam a partir do rato"

[render]
mode = "shader"
entry = "main.hlsl"
fps = 30
cell = 12                  # altura da célula em px lógicos (a 100% de escala)
font = "Cascadia Mono"     # fonte do sistema, ou um .ttf dentro da pasta
charset = " .:-=+*#%@"     # do mais "vazio" ao mais "cheio"

[colors]
background = "#05070d"
palette = ["#05070d", "#1e3a8a", "#38bdf8", "#e0f2fe"]

[params]
speed = { default = 1.0, min = 0.0, max = 4.0 }
```

**`main.hlsl`**
```hlsl
CellOut cell(CellIn i)
{
    float d = cellDistance(i.pos, mouse.inside ? mouse.pos : float2(grid) * 0.5);
    float v = sin(d * 0.4 - time * 4.0 * param("speed")) * exp(-d * 0.05);

    CellOut o;
    o.value = v * 0.5 + 0.5;   // 0..1 escolhe o caractere do charset
    o.fg = palette(o.value);   // cor do caractere
    o.bg = float4(0, 0, 0, 0); // transparente = background do toml
    o.state = float4(0, 0, 0, 0);
    o.glyph = 0;               // 0 = usar value
    return o;
}
```

Para desenvolver, corre numa janela, com hot reload (grava o ficheiro e vês logo o resultado):

```powershell
openascii.exe --windowed --verbose --wallpaper C:\caminho\para\meu-wallpaper
```

Se houver um erro, aparece num canto do ecrã com a linha (`main.hlsl(12,5): error ...`) e o último estado bom continua a correr.

## 2. A API do shader

A tua função `cell()` corre **uma vez por célula, por frame**, em paralelo na GPU.

### Entrada e saída
| `CellIn i` | |
|---|---|
| `i.id` | `uint2` coluna, linha |
| `i.pos` | `float2` centro da célula em células (`id + 0.5`): compara diretamente com `mouse.pos` |
| `i.uv` | `float2` em [0,1] |

| `CellOut o` | |
|---|---|
| `o.value` | 0..1 → caractere do charset (0 = primeiro, 1 = último) |
| `o.glyph` | 0 = usar `value`; `n` > 0 = usar o caractere `n-1` do charset |
| `o.fg` | cor do caractere (RGBA, 0..1) |
| `o.bg` | cor de fundo da célula; alpha 0 = `background` do toml |
| `o.state` | `float4` guardado para o próximo frame (ver `prev`) |

### Células ou mundo?
Há dois sistemas de coordenadas:

- **Células** (`i.pos`, `i.id`, `mouse.pos`): bom para efeitos que SÃO feitos de caracteres (as colunas do `matrix`) e para simulações vizinho-a-vizinho com `prev()` (o `ripple`).
- **Mundo** (`i.world`, `mouse.world`, `worldSize`): uma unidade = `scale` px lógicos (no `wallpaper.toml`). **Não depende do tamanho da célula**: se o utilizador mudar a resolução da grelha no launcher, o efeito fica do mesmo tamanho, só mais fino ou mais "pixelado". Além disso, `x` e `y` têm a mesma escala (não precisas de `cellAspect`). Usa mundo para paisagens, formas e distâncias.

### Vários monitores
No modo estendido (por defeito), `grid` e `worldSize` cobrem **todos** os monitores: o teu efeito é um só, e atravessa os ecrãs. Algumas células do canvas caem fora de qualquer monitor (o espaço entre ecrãs de tamanhos diferentes); `visible(c)` diz se uma célula se vê. Para as simulações, essas células ficam com estado 0, como uma parede.

### Globais
| Nome | |
|---|---|
| `time`, `dt` | segundos desde o arranque / desde o frame anterior |
| `grid` | `uint2` colunas, linhas do canvas |
| `worldSize`, `worldPerCell` | tamanho do canvas em mundo; mundo por célula |
| `cellAspect` | largura/altura da célula (~0.5) |
| `cellPx`, `resolution` | tamanho da célula e do canvas em px |
| `frameIndex`, `charsetSize`, `glyphCount`, `bgColor` | |

### Rato
| Nome | |
|---|---|
| `mouse.pos`, `mouse.world` | posição em células / em mundo (pode estar fora da grelha) |
| `mouse.vel`, `mouse.velWorld` | velocidade suavizada (células/s, mundo/s) |
| `mouse.clickWorld` | onde foi o último clique, em mundo |
| `mouse.presence` | 0..1 suave: **multiplica os efeitos do rato por isto**. Sobe quando o rato entra no fundo e desce devagar quando sai; enquanto desce, `mouse.pos` fica na última posição. Para parallax: `lerp(neutro, alvo, mouse.presence)` |
| `mouse.inside` | o rato está sobre o ambiente de trabalho deste monitor |
| `mouse.buttons` | `MOUSE_LEFT`, `MOUSE_RIGHT`, `MOUSE_MIDDLE` (só cliques que começaram no desktop) |
| `mouse.clickPos`, `mouse.clickTime`, `mouse.clickButton` | último clique |
| `justClicked()` | o clique aconteceu neste frame |
| `mouse.scroll` | reservado (sempre 0: o rato é lido sem mensagens desde a v0.4) |

Por defeito o wallpaper só reage quando o rato está **sobre o desktop** (não sobre outras janelas). Muda com `interaction = "always"` no `config.toml`.

### Funções
| Função | |
|---|---|
| `param("nome")` | valor de um parâmetro de `[params]` (um nome que não existe é um erro com a linha) |
| `palette(t)` | cor da paleta em t ∈ [0,1], interpolada |
| `prev(int2 c)` | o `state` que a célula `c` escreveu no frame anterior (fora da grelha = borda) |
| `glyph(k)` | para `o.glyph`: o caractere `k` da lista `glyphs` do toml (sprites: animais, pássaros...) |
| `visible(int2 c)` | a célula `c` está dentro de algum monitor? |
| `primaryCenterWorld()`, `primarySizeWorld()` | centro e tamanho do monitor principal em mundo (para centrar um relógio, um sol, um modelo 3D) |
| `primaryCenter()`, `primaryRect()` | o mesmo em células |
| `monitorRectAt(int2 c)` | retângulo (células) do monitor onde está `c`: o "chão" de cada ecrã para fogo, areia, aquários |
| `primaryWorkRect()`, `workRectAt(int2 c)` | o mesmo, mas só a **área de trabalho** (sem a barra de tarefas, só células inteiras). Usa-os para o que tem de se ver no fundo ou no topo do ecrã (a raquete do `arcade`, o vaso do `bonsai`, a areia do `aquarium`). O fundo do monitor fica por trás da barra. Para testar sem desktop: `--windowed --work-inset 48` simula uma barra de 48 px. |
| `localTime`, `localDate` | hora (h, m, s com fração) e data (ano, mês, dia, dia da semana) locais |
| `cellDistance(a, b)` | distância em alturas de célula, corrigida para ficar redonda no ecrã |
| `noise(p)`, `fbm(p)` | ruído suave em [0,1], 2D ou 3D; `fbm` soma 5 oitavas |
| `hash11`, `hash21`, `hash22`, `hash33` | pseudo-aleatório em [0,1) |
| `asciiGlyph(c)` | para `o.glyph`: **qualquer** caractere Latin-1 (acentos incluídos), mesmo fora do charset: rótulos, números, texto |
| `bigFontPixel(ch, x, y)` | fonte grande 5x7 "em píxeis" (letras, dígitos, pontuação) para relógios e títulos gigantes |
| `sdSphere`, `sdBox`, `sdTorus`, `sdCylinder`, `sdPlane`, `opUnion`, `opSmoothUnion`, `opRepeat`, `rotateX/Y/Z`, `cameraRay` | 3D por raymarching (ver `temple`) |
| `justClicked()` | verdadeiro uma única vez por clique (no frame do clique) |
| `fixedStep(hz)` | simulações que avançam um passo por frame: só avançam `hz` vezes por segundo, para correrem igual a 30, 60 ou 144 fps (nos outros frames, copia o estado) |

### Efeitos com memória
`o.state` + `prev()` permitem simulações: ondas que se propagam, rastos, autómatos celulares. O `ripple` incluído é uma equação de onda completa em ~40 linhas; vale a pena lê-lo.

## 3. Entradas: o mundo lá fora (`[inputs]`)

Um wallpaper pode pedir dados do PC. **Só se lê/captura o que é pedido**: um wallpaper sem `[inputs]` não custa nada.

```toml
[inputs]
windows = true        # janelas abertas como obstáculos (em células, de cima para baixo)
icons = true          # ícones do ambiente de trabalho como obstáculos
audio = true          # o som que está a tocar (saída do PC, nunca o microfone)
system = true         # CPU e RAM
weather = true        # tempo real da cidade escrita nas Definições (sem cidade: nada sai do PC)
source = "foto.jpg"   # imagem, GIF animado, vídeo (.mp4 .mov .wmv...), "wallpaper" ou "webcam"
text = "lines:frases.txt"
text_interval = 20    # "lines:": segundos por linha
```

| No shader | O que é |
|---|---|
| `obstacleCount()`, `windowCount()`, `obstacleRect(k)` | retângulos (células: x0, y0, x1, y1); os primeiros `windowCount()` são janelas, o resto ícones |
| `obstacleAt(c)`, `insideWindow(c)`, `surfaceBelow(c)` | a célula está tapada? qual é o topo da janela mais próxima por baixo? |
| `audioBand(i)` (0..63), `audioSpectrum(t)`, `audioLevels` | espectro de 64 bandas (graves no 0) e graves/médios/agudos/batida |
| `systemStats.x`, `.y` | CPU e RAM, 0..1 |
| `weather` / `weather2` | chuva (mm/h), nuvens (0..1), vento (m/s), temperatura (°C) / é de dia, elevação do sol, válido, código WMO |
| `hasSource()`, `sampleSource(uv)`, `fitSource(p, min, size, cover)`, `sourceEdge(uv, cellUv)` | a imagem/vídeo: amostrar, encaixar num retângulo, contornos (Sobel) |
| `textLength`, `textChar(i)`, `textGlyph(i)`, `textAge()`, `textLineIndex()` | o texto (Latin-1) e, no modo `lines:`, há quanto tempo a linha atual apareceu |

- `source`: caminho relativo à pasta do wallpaper ou absoluto, com `%VARIAVEIS%`. `"wallpaper"` = o wallpaper do Windows que o OpenASCII substituiu. `"webcam"` pede autorização na primeira vez. No launcher, "Escolher imagem ou vídeo" troca a imagem sem mexer no código.
- `text`: texto literal, `file:<ficheiro>`, `folder:<pasta>` (o código de uma pasta, até 64 K caracteres) ou `lines:<ficheiro>` (uma linha de cada vez; `#` = comentário).
- Um **tema** também pode ligar entradas (secção `[inputs]` no tema): `synthwave/beat` liga o áudio, `valley/weather` o tempo real, `matrix/code` o texto.

## 4. Pós-processamento (`[post]`)

Efeitos aplicados ao desenhar as células em píxeis, 0..1 cada, em **qualquer** wallpaper (e em temas):

```toml
[post]
glow = 0.6        # halo à volta dos caracteres claros (cada glifo brilha conforme a sua "tinta")
scanlines = 0.5   # linhas de monitor CRT
curvature = 0.4   # vidro abaulado
vignette = 0.5    # cantos mais escuros
aberration = 0.3  # vermelho e azul desalinhados
grain = 0.2       # grão de filme
```

Sem `[post]`, o render usa o caminho rápido de sempre (zero custo). O utilizador pode ligar, desligar ou afinar estes efeitos em qualquer wallpaper no launcher ("efeitos de ecrã": Nenhum, CRT, Néon, Cinema ou à medida).

**Charset ou caracteres próprios?** Se o teu wallpaper desenha só com `o.glyph` (sprites, `asciiGlyph`), põe `charset = " "`: o launcher explica que o wallpaper usa os seus próprios caracteres. Quando alguém escolhe um conjunto no launcher, liga-se `charset_only`: os teus sprites e o teu texto passam a ser desenhados com o caractere do conjunto mais parecido (na forma e na densidade). Não precisas de fazer nada para isso funcionar. As mensagens de erro da app ficam sempre legíveis.

## 5. Técnicas avançadas

- **Vários passos por frame**: `render.substeps = N` corre o shader N vezes por frame; `passIndex` (0..N-1) diz qual é o passo e `prev()` vê o passo anterior. Serve para simulações rápidas (`sandbox`: 4 passos) e para encadear fases de um algoritmo (`fluid`: advecção, 22 iterações de pressão, projeção).
- **Estado global numa célula reservada**: escolhe uma célula sempre visível (ex.: `primaryRect().xy`) para guardar valores do "jogo inteiro" no `state` dela; todas as outras leem-na com `prev()`. O `arcade` guarda assim a bola e os pontos; o `spectrum` o instante da última batida; o `bonsai` usa duas linhas inteiras para os 63 ramos da árvore.
- **Movimentos sem conflitos** (areia, neve): origem e destino avaliam a mesma condição sobre o estado anterior e cada um escreve a sua metade da troca; o sentido diagonal alterna entre passos.
- **Ler a vizinhança uma vez**: em regras que se chamam umas às outras, copia a vizinhança para arrays locais (ver `sandbox`). Chamar `prev()` em funções encadeadas pode fazer o compilador demorar minutos.
- **Glifos brutos**: `o.glyph = asciiGlyph(c)` (valores negativos) desenha qualquer caractere do atlas; `o.glyph > 0` usa `glyph(k)`; `0` usa a rampa do charset.

## 6. As demos: o que cada uma ensina

| Wallpaper | Mostra |
|---|---|
| `photo` | `[inputs] source` (imagem/GIF/vídeo), rampa de luminância, contornos de Sobel com traços direcionais, lupa no rato |
| `spectrum` | `[inputs] audio`, blocos de 1/8 (precisão sub-célula), picos com estado por coluna, estado global da batida |
| `snow` | `[inputs] windows` e `icons`, autómato de neve que assenta e cai quando a janela se mexe |
| `temple` | 3D por raymarching: SDF, repetição polar, sombras suaves, oclusão ambiente, nevoeiro, câmara com o rato |
| `sandbox` | autómato celular com `substeps`, trocas sem conflitos, água com momento, rato que pinta materiais |
| `fluid` | multi-pass: fluido incompressível (stable fluids), MacCormack, confinamento de vorticidade |
| `coral` | equações de reação-difusão (Gray-Scott) com 24 passos por frame |
| `life` | Jogo da Vida com idade, rastos e "meteoros"; planadores ao clicar |
| `fractal` | Mandelbrot com zoom contínuo e coloração suave (matemática pesada por célula) |
| `hud` | `[inputs] system` e `weather`, texto com `asciiGlyph`, números com `bigFontPixel`, gráficos com histórico, `[post]` |
| `planet` | esfera e texturas procedurais 3D, Sol com a declinação de hoje, luzes noturnas |
| `bonsai` | estado global em células reservadas (63 ramos), crescimento lento, estações do ano reais |
| `mirror` | webcam com autorização, deteção de movimento por diferença entre frames |
| `terminal` | `[inputs] text` em `lines:`, quebra de linha por palavras no shader, CRT completo com `[post]` |
| `arcade` | um jogo inteiro (Breakout) com estado global; joga sozinho ou com o rato |

## 7. Charsets e glifos especiais

- Qualquer caractere Unicode que a fonte tenha. Os que a fonte não tem aparecem como `?` (o log avisa).
- **Blocos** `░▒▓█▀▄▌▐▖▗▘▝…` e **linhas de caixa** `─│┌┐└┘├┤┬┴┼━┃…` são desenhados pela app, píxel a píxel, para encaixarem **sem costuras** entre células.
- **Braille** `⠀`–`⣿` também é desenhado pela app: ótimo para modos de "alta resolução".

## 8. Temas (sem código)

Um tema herda de um wallpaper e só muda valores. Põe-no em `themes/` dentro da pasta do wallpaper e aparece na tray como `wallpaper / tema`.

```toml
base = "pulso"
font = "JetBrains Mono"
cell = 7
charset = " ·•●"
background = "#0d0d0d"
palette = ["#0d0d0d", "#ff2e63", "#fafafa"]

[params]
speed = 2.0
```

Valores fora de `[min, max]` são limitados (com um aviso no log).

## 9. Referência do `wallpaper.toml`

| Chave | Tipo | Por defeito |
|---|---|---|
| `meta.name/author/version/description` | texto | |
| `meta.category` | grupo no launcher: `Paisagens`, `Ambiente`, `Retro`, `3D e espaço`, `Simulações`, `Dados e média`, `Jogos` (outro = "Outros") | vazio |
| `render.mode` | `"shader"` \| `"script"` | `"shader"` |
| `render.entry` | ficheiro na pasta | `main.hlsl` |
| `render.fps` | 1–240 (limitado pelo `max_fps` global) | 30 |
| `render.cell` | 4–128 px lógicos: **resolução** da grelha | 10 |
| `render.scale` | 1–400 px lógicos por unidade de mundo: **escala** do efeito | 10 |
| `render.glyphs` | texto UTF-8 (até 256): caracteres extra para `glyph(k)` | vazio |
| `render.font` | nome ou `.ttf/.otf` | `"Cascadia Mono"` (alternativas: Consolas, Courier New) |
| `render.font_weight` | 100–950 | 400 |
| `render.charset` | texto UTF-8 | `" .:-=+*#%@"` |
| `render.charset_only` | `true`/`false`: só os caracteres do charset aparecem; os de `glyph(k)` e `asciiGlyph` são trocados pelo do charset mais parecido (forma e densidade). Também funciona em temas. | `false` |
| `colors.background` | `"#rgb"`, `"#rrggbb"`, `"#rrggbbaa"` | `#05070d` |
| `colors.palette` | 1–8 cores | `[background, #e0f2fe]` |
| `params.<nome>` | `{ default, min, max }` (máx. 16) | |
| `render.substeps` | 1–64 passos por frame | 1 |
| `inputs.windows/icons/audio/system/weather` | `true`/`false` | `false` |
| `inputs.source` | caminho, `"wallpaper"` ou `"webcam"` | vazio |
| `inputs.text` / `inputs.text_interval` | ver secção 3 / 1–3600 s | vazio / 15 |
| `post.glow/scanlines/curvature/vignette/aberration/grain` | 0–1 | 0 |
