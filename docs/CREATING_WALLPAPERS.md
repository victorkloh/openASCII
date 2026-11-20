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

Onde pôr a pasta: `%APPDATA%\AsciiPaper\wallpapers\` (a tray → **Abrir pasta de wallpapers** leva-te lá perto). Aparece logo no menu **Wallpaper** da tray.

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

### Globais
| Nome | |
|---|---|
| `time`, `dt` | segundos desde o arranque / desde o frame anterior |
| `grid` | `uint2` colunas, linhas |
| `cellAspect` | largura/altura da célula (~0.5) |
| `cellPx`, `resolution` | tamanho da célula e do ecrã em px |
| `frameIndex`, `charsetSize`, `bgColor` | |

### Rato
| Nome | |
|---|---|
| `mouse.pos` | posição em células (pode estar fora da grelha) |
| `mouse.vel` | velocidade suavizada em células/s |
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
| `render.cell` | 4–128 px lógicos | 10 |
| `render.font` | nome ou `.ttf/.otf` | `"Cascadia Mono"` (alternativas: Consolas, Courier New) |
| `render.font_weight` | 100–950 | 400 |
| `render.charset` | texto UTF-8 | `" .:-=+*#%@"` |
| `colors.background` | `"#rgb"`, `"#rrggbb"`, `"#rrggbbaa"` | `#05070d` |
| `colors.palette` | 1–8 cores | `[background, #e0f2fe]` |
| `params.<nome>` | `{ default, min, max }` (máx. 16) | |
