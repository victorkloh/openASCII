# Criar wallpapers para o AsciiPaper

Um wallpaper é **uma pasta** com um `wallpaper.toml` e um ficheiro de código. Partilha-a como zip ou num repositório git.

```
meu-wallpaper/
├─ wallpaper.toml   # manifest: nome, fonte, cores, parâmetros
├─ main.hlsl        # o efeito (corre na GPU, uma vez por célula)
├─ preview.png      # opcional
└─ themes/          # opcional: variações sem código
   └─ noite.toml
```

O caminho mais rápido: launcher → **Novo wallpaper**. Escolhe um modelo, dá um nome, e a app cria a pasta em `%APPDATA%\AsciiPaper\wallpapers\`, ativa-o e abre o `main.hlsl` no teu editor. Também podes criar a pasta à mão nesse sítio; aparece logo no launcher e no menu da bandeja.

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
asciipaper.exe --windowed --verbose --wallpaper C:\caminho\para\meu-wallpaper
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
| `mouse.inside` | o rato está sobre o ambiente de trabalho deste monitor |
| `mouse.buttons` | `MOUSE_LEFT`, `MOUSE_RIGHT`, `MOUSE_MIDDLE` (só cliques que começaram no desktop) |
| `mouse.clickPos`, `mouse.clickTime`, `mouse.clickButton` | último clique |
| `justClicked()` | o clique aconteceu neste frame |
| `mouse.scroll` | entalhes de scroll neste frame |

Por defeito o wallpaper só reage quando o rato está **sobre o desktop** (não sobre outras janelas). Muda com `interaction = "always"` no `config.toml`.

### Funções
| Função | |
|---|---|
| `param("nome")` | valor de um parâmetro de `[params]` (um nome que não existe é um erro com a linha) |
| `palette(t)` | cor da paleta em t ∈ [0,1], interpolada |
| `prev(int2 c)` | o `state` que a célula `c` escreveu no frame anterior (fora da grelha = borda) |
| `glyph(k)` | para `o.glyph`: o caractere `k` da lista `glyphs` do toml (sprites: animais, pássaros...) |
| `visible(int2 c)` | a célula `c` está dentro de algum monitor? |
| `cellDistance(a, b)` | distância em alturas de célula, corrigida para ficar redonda no ecrã |
| `noise(p)`, `fbm(p)` | ruído suave em [0,1], 2D ou 3D; `fbm` soma 5 oitavas |
| `hash11`, `hash21`, `hash22`, `hash33` | pseudo-aleatório em [0,1) |

### Efeitos com memória
`o.state` + `prev()` permitem simulações: ondas que se propagam, rastos, autómatos celulares. O `ripple` incluído é uma equação de onda completa em ~40 linhas; vale a pena lê-lo.

## 3. Charsets e glifos especiais

- Qualquer caractere Unicode que a fonte tenha. Os que a fonte não tem aparecem como `?` (o log avisa).
- **Blocos** `░▒▓█▀▄▌▐▖▗▘▝…` e **linhas de caixa** `─│┌┐└┘├┤┬┴┼━┃…` são desenhados pela app, píxel a píxel, para encaixarem **sem costuras** entre células.
- **Braille** `⠀`–`⣿` também é desenhado pela app: ótimo para modos de "alta resolução".

## 4. Temas (sem código)

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

## 5. Referência do `wallpaper.toml`

| Chave | Tipo | Por defeito |
|---|---|---|
| `meta.name/author/version/description` | texto | |
| `render.mode` | `"shader"` \| `"script"` | `"shader"` |
| `render.entry` | ficheiro na pasta | `main.hlsl` |
| `render.fps` | 1–240 (limitado pelo `max_fps` global) | 30 |
| `render.cell` | 4–128 px lógicos: **resolução** da grelha | 10 |
| `render.scale` | 1–400 px lógicos por unidade de mundo: **escala** do efeito | 10 |
| `render.glyphs` | texto UTF-8 (até 256): caracteres extra para `glyph(k)` | vazio |
| `render.font` | nome ou `.ttf/.otf` | `"Cascadia Mono"` (alternativas: Consolas, Courier New) |
| `render.font_weight` | 100–950 | 400 |
| `render.charset` | texto UTF-8 | `" .:-=+*#%@"` |
| `colors.background` | `"#rgb"`, `"#rrggbb"`, `"#rrggbbaa"` | `#05070d` |
| `colors.palette` | 1–8 cores | `[background, #e0f2fe]` |
| `params.<nome>` | `{ default, min, max }` (máx. 16) | |
