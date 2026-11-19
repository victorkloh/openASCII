# AsciiPaper — Motor de wallpapers ASCII reativos para Windows

> Nome provisório. Este ficheiro é o brief do projeto: lê-o todo antes de escrever código.

## 1. Visão

Uma app nativa para Windows 10/11 que desenha **wallpapers ASCII animados e interativos** atrás dos ícones do ambiente de trabalho. Tem de ser **leve**, **robusta** e **integrada no sistema como se fosse nativa**. Cada wallpaper é **programável** e fácil de criar e partilhar pela comunidade.

Não é um clone do Wallpaper Engine: não usamos browser embutido, runtime pesado nem dependências externas em execução. É um único `.exe` pequeno, mais pastas de wallpapers.

## 2. Requisitos não negociáveis

### Performance (orçamento)
| Métrica | Alvo |
|---|---|
| RAM | < 30 MB por monitor 1080p |
| CPU a 30 FPS (1080p, PC médio) | < 2% |
| CPU/GPU em pausa | ~0% (o thread bloqueia à espera de mensagens, sem polling) |
| Arranque até ao primeiro frame | < 300 ms |
| Binário | < 5 MB (sem contar os wallpapers) |

### Integração "nativa"
- Fica **atrás dos ícones** (truque Progman/WorkerW). Tem de funcionar no **Windows 10, no Windows 11 e no Windows 11 24H2+**, onde a hierarquia de janelas mudou: o WorkerW passou a ser filho do Progman. Detetar os dois layouts e ter uma estratégia de fallback.
- **Sem flash preto no arranque**: o primeiro frame tem de estar pronto antes de a janela ser mostrada.
- **Sem flicker, clipping ou bloqueios** ao mudar a escala (100/125/150/175%), a resolução ou a orientação, ou ao ligar e desligar monitores.
- **Multi-monitor** com DPIs diferentes e monitores em coordenadas negativas.
- **O Explorer reinicia ou crasha** → voltar a ligar-se automaticamente (`RegisterWindowMessage(L"TaskbarCreated")`).
- **Ao sair**, repor o wallpaper original do Windows (`SPI_GETDESKWALLPAPER` no arranque, `SPI_SETDESKWALLPAPER` à saída). Nunca deixar o ecrã preto.
- **Recuperar** de `DXGI_ERROR_DEVICE_REMOVED/RESET` (reinício do driver da GPU) sem crashar.
- **Instância única** (mutex com nome).

### Comportamento inteligente (pausar = 0% de CPU)
- App em fullscreen ou jogo: `SHQueryUserNotificationState` + verificar se a janela em primeiro plano cobre o monitor. A pausa é **por monitor**.
- Janela maximizada a tapar o monitor → pausar esse monitor (configurável).
- Ecrã bloqueado: `WTSRegisterSessionNotification`.
- Suspensão e retoma: `WM_POWERBROADCAST`.
- Poupança de bateria: `RegisterPowerSettingNotification` com `GUID_POWER_SAVING_STATUS`. Opcionalmente, baixar o FPS quando está na bateria.

## 3. Stack

- **C++20**, **Win32 puro** (sem MFC/Qt), **CMake** + **MSVC** (e o build com clang-cl também tem de funcionar).
- **Direct3D 11** para o render, **DXGI flip model** (`DXGI_SWAP_EFFECT_FLIP_DISCARD`) com fallback para o modelo blt se falhar numa janela filha.
- **DirectWrite** para rasterizar glifos e **Direct2D** só se ajudar a gerar o atlas.
- **HLSL** compilado em runtime com `D3DCompile` (o `d3dcompiler_47.dll` já vem com o Windows) para os wallpapers do tipo shader.
- **LuaJIT** (via vcpkg) para os wallpapers do tipo script. Se der problemas no build, usar **Lua 5.4** atrás da mesma interface.
- **toml++** (header-only) para a config.
- **doctest** para testes unitários.
- Dependências geridas com **vcpkg em modo manifest** (`vcpkg.json`).
- O CRT pode ser linkado estaticamente (`/MT`) para um `.exe` sem dependências.

## 4. Arquitetura

```
asciipaper/
├─ CMakeLists.txt, vcpkg.json, README.md
├─ docs/ARCHITECTURE.md        # decisões e explicação dos sistemas (manter atualizado)
├─ src/
│  ├─ main.cpp                 # args, instância única, loop principal
│  ├─ app/        App, Tray, Settings, Autostart, HotReload
│  ├─ desktop/    WorkerW (attach/detach/reattach), MonitorManager, PauseManager
│  ├─ input/      MouseInput (Raw Input + GetCursorPos), DesktopHitTest
│  ├─ render/     Device, MonitorSurface (swapchain por monitor), GlyphAtlas, GridRenderer
│  ├─ wallpaper/  Manifest, WallpaperHost, ShaderBackend, ScriptBackend, Params
│  ├─ natives/    noise, fluid, particles, life (simulações em C++ expostas aos backends)
│  └─ util/       log, timing, fs, win32 helpers
├─ shaders/       grid.hlsl (render final), prelude.hlsl (API dos wallpapers shader)
├─ wallpapers/    presets base (ver secção 7)
├─ tests/         testes unitários
└─ res/           manifest (DPI), ícone, .rc
```

### Loop principal
- Um thread de UI com `MsgWaitForMultipleObjectsEx` mais um **waitable timer de alta resolução** (`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`) para o ritmo de frames.
- Em pausa, sem timer: o thread só acorda com mensagens.
- FPS configurável por wallpaper (por defeito 30) e limitado globalmente.

### Janelas e monitores
- **Uma janela filha do WorkerW por monitor**, cada uma com a sua swapchain e o seu DPI.
- As coordenadas do WorkerW começam na origem do ecrã virtual: posicionar com `monitorRect - (SM_XVIRTUALSCREEN, SM_YVIRTUALSCREEN)`.
- **DPI**: manifesto `PerMonitorV2`. Como as janelas são filhas, **não contar com `WM_DPICHANGED`**. Reagir a `WM_DISPLAYCHANGE`, `WM_SETTINGCHANGE` e `WM_DEVICECHANGE`, e reler `GetDpiForMonitor` para cada monitor.
- Em qualquer mudança de layout: recalcular a grid, reconstruir o atlas para o novo DPI, `ResizeBuffers`. Sem frames esticados nem cortados pelo meio.
- `WM_ERASEBKGND` devolve 1, e não se usa GDI para pintar.

### WorkerW
1. `FindWindow(L"Progman")` → `SendMessageTimeout(progman, 0x052C, 0xD, 0x1, SMTO_NORMAL, 1000, …)`.
2. **Layout clássico**: `EnumWindows` → encontrar a janela que tem um filho `SHELLDLL_DefView` → o `WorkerW` seguinte é o alvo.
3. **Layout 24H2+**: o `WorkerW` e o `SHELLDLL_DefView` são filhos do `Progman`. Garantir a ordem Z correta: o wallpaper fica abaixo dos ícones e acima do fundo.
4. Estudar a implementação do **Lively Wallpaper** (open source, github.com/rocksdanister/lively) como referência para os casos-limite. Não copiar código, perceber a abordagem.
5. Encapsular tudo em `desktop/WorkerW` com `attach()`, `detach()` e `reattach()`, mais logs claros de qual estratégia funcionou.

### Render (uma draw call por monitor)
- **GlyphAtlas**: rasterizar cada caractere do charset com DirectWrite, **no tamanho exato em píxeis para o DPI atual** (nunca escalar o atlas). Anti-aliasing em escala de cinza, textura R8.
- O tamanho da célula vem da config em **px lógicos** × escala do DPI. A largura vem do avanço da fonte; as métricas são arredondadas a píxeis inteiros para não haver blur nem costuras.
- **Blocos e box-drawing** (`░▒▓█▀▄`, `─│┌`…) desenhados **proceduralmente** (como fazem os terminais modernos), para encaixarem sem falhas entre células.
- Suportar **Braille** (`⠀`–`⣿`) para modos de "alta resolução".
- **Cell buffer** (StructuredBuffer): `{ glyph:uint16, fg:rgba8, bg:rgba8, flags }` por célula.
- `grid.hlsl`: um triângulo de ecrã inteiro → o pixel shader descobre a célula, lê o atlas e mistura fg/bg. Opcional: glow/bloom barato e configurável (desligado por defeito).
- Se a grid não dividir o ecrã por igual, centrar e preencher a margem com a cor de fundo.

### Rato
- **Raw Input** com `RIDEV_INPUTSINK` numa janela message-only (recebe input sem foco) + `GetCursorPos` para a posição absoluta. Evitar `WH_MOUSE_LL`, que adiciona latência ao sistema todo e pode ser removido pelo Windows.
- Dados para os wallpapers: posição (em píxeis e em células), velocidade suavizada, botões, arrasto, scroll, `inside` (o cursor está neste monitor).
- **DesktopHitTest**: `WindowFromPoint` → é o desktop (`SHELLDLL_DefView`/`SysListView32`/`WorkerW`/`Progman`)? Modo de interação configurável: `desktop_only` (por defeito) ou `always`.

## 5. Formato de um wallpaper (pensado para a comunidade)

Cada wallpaper é **uma pasta**, partilhável como zip ou num repositório git:

```
wallpapers/fluid/
├─ wallpaper.toml     # manifest
├─ main.hlsl          # OU main.lua
└─ preview.png        # opcional
```

### `wallpaper.toml`
```toml
[meta]
name = "Fluid"
author = "VictorKloh"
version = "1.0.0"
description = "Fumo ASCII que segue o rato"

[render]
mode = "shader"            # "shader" | "script"
entry = "main.hlsl"
fps = 30
cell = 10                  # altura da célula em px lógicos
font = "Cascadia Mono"     # nome do sistema ou caminho para .ttf na pasta
font_weight = 400
charset = " .:-=+*#%@"

[colors]
background = "#05070d"
palette = ["#05070d", "#1e3a8a", "#38bdf8", "#e0f2fe"]

[params]                   # expostos ao utilizador e aos temas, sem tocar no código
speed     = { default = 1.0, min = 0.0, max = 4.0 }
viscosity = { default = 0.2, min = 0.0, max = 1.0 }
```

### Temas (nível sem código)
Um `.toml` que herda de um wallpaper e só altera valores:
```toml
base = "fluid"
font = "JetBrains Mono"
cell = 7
charset = " ·•●"
palette = ["#0d0d0d", "#ff2e63", "#fafafa"]
[params]
speed = 2.0
```

### Backend shader (nível 2: o mais leve e bonito)
O utilizador escreve só uma função. O `prelude.hlsl` dá a API:
```hlsl
// Disponível: time, dt, grid (cols, rows), mouse (pos em células, vel, buttons, inside),
// param("speed"), palette(t), noise(p), fbm(p), prev(cell) = estado do frame anterior.
CellOut cell(CellIn i) {
    float d = distance(i.pos, mouse.pos);
    float v = sin(d * 0.4 - time * 4.0 * param("speed")) * exp(-d * 0.05);
    CellOut o;
    o.value = v * 0.5 + 0.5;       // 0..1 → mapeado para o charset
    o.fg = palette(o.value);
    o.bg = bgColor;
    o.state = float4(v, 0, 0, 0);  // guardado para prev() no próximo frame (efeitos com feedback)
    return o;
}
```
- Corre como **compute shader**, uma thread por célula, a escrever diretamente no cell buffer.
- `prev()` com double buffering para efeitos com memória (rastos, ondas que se propagam).
- Opcional: `o.glyph` para escolher o caractere diretamente em vez de usar `value`.

### Backend script (nível 3: lógica com estado)
```lua
local fluid = natives.fluid.new{ viscosity = param("viscosity") }

function frame(g, t, dt)
  fluid:step(dt)
  g:map(fluid:density(), palette)      -- operação em massa feita em C++
end

function on_drag(x, y, dx, dy) fluid:impulse(x, y, dx, dy) end
function on_click(x, y, button) fluid:splash(x, y, 4.0) end
```
- API: `frame`, `on_mouse`, `on_drag`, `on_click`, `on_scroll`, `on_resize`; `g:set`, `g:get`, `g:fill`, `g:text`, `g:map`; `natives.*` (noise, fluid, particles, life).
- **Sandbox**: sem `io`, `os.execute`, `ffi` nem `require` fora da pasta do wallpaper. Limite de tempo por frame: se o script exceder o orçamento várias vezes, avisar e baixar o FPS.
- As operações em massa (`map`, `fill`, sims) ficam sempre em C++. O Lua orquestra e não itera células quando não é preciso.

### Hot reload e erros
- `ReadDirectoryChangesW` na pasta do wallpaper ativo → recarregar ao gravar.
- Erro de compilação ou de runtime: **manter o último estado bom**, mostrar o erro em ASCII num canto do wallpaper e escrever no log.

## 6. App

- **Tray icon**: lista de wallpapers e temas, pausar/retomar, recarregar, abrir pasta de wallpapers, abrir config, "Iniciar com o Windows", sair.
- **Config global** em `%APPDATA%\AsciiPaper\config.toml`: wallpaper por monitor ou o mesmo em todos, FPS máximo, regras de pausa, modo de interação do rato, overrides de params.
- **Pastas de wallpapers**: as embutidas (ao lado do exe) + `%APPDATA%\AsciiPaper\wallpapers\` (as do utilizador).
- **Autostart**: `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`.
- **Log** em `%APPDATA%\AsciiPaper\log.txt` com rotação.
- **Argumentos CLI**:
  - `--windowed` → renderiza numa janela normal redimensionável (para desenvolver e testar sem mexer no desktop)
  - `--wallpaper <pasta>` → carrega um wallpaper específico
  - `--bench <segundos>` → corre e imprime frame time médio/p99, CPU% e RAM
  - `--verbose`

## 7. Presets base (têm de ficar bonitos de verdade)

| Nome | Backend | Interação |
|---|---|---|
| **fluid** | script + native fluid | arrastar empurra fumo; clicar cria um splash |
| **ripple** | shader | ondas a partir do cursor; clicar cria uma onda forte |
| **particles** | script + native particles | o rato repele; o botão direito atrai |
| **matrix** | shader | as colunas desviam do cursor |
| **life** | script + native life | clicar/arrastar semeia células |
| **terrain** | shader | paisagem de ruído fbm a mover-se devagar; o rato faz parallax |
| **starfield** | shader | a posição do rato define a direção do voo |
| **clock** | script | relógio ASCII grande com dígitos em blocos; partículas no segundo |

Cada preset inclui 2–3 temas de exemplo (paletas e fontes diferentes, uma grid fina e uma grossa).

## 8. Fases (não avançar sem cumprir os critérios da fase)

### Fase 0 — Esqueleto
- CMake + vcpkg, build Debug/Release, `.exe` com manifesto PerMonitorV2, ícone, log, instância única, doctest a correr.
- ✅ `cmake --build` sem warnings em `/W4`, e o teste vazio passa.

### Fase 1 — Integração com o desktop (o mais importante)
- WorkerW (os dois layouts), uma janela por monitor, D3D11 + swapchain flip, limpar com uma cor animada.
- Mudanças de DPI e de monitores, reattach com o Explorer, repor o wallpaper à saída, pausas.
- ✅ Checklist manual da secção 9 a passar inteira com um ecrã de cor sólida animada.

### Fase 2 — Render ASCII
- GlyphAtlas (DirectWrite, por DPI), cell buffer, `grid.hlsl`, glifos procedurais de blocos e box-drawing.
- Modo `--windowed` e `--bench`.
- ✅ Texto nítido com células de 6 px a 200% de escala; sem costuras entre blocos; o bench cumpre o orçamento da secção 2.

### Fase 3 — Input + backend shader
- Raw Input, DesktopHitTest, `prelude.hlsl`, compute pass, `prev()`, params, manifest TOML, hot reload, erros no ecrã.
- ✅ Presets `ripple`, `matrix`, `terrain` e `starfield` a funcionar com o rato.

### Fase 4 — Backend script + natives
- LuaJIT com sandbox, API `g:*`, natives fluid/particles/life/noise.
- ✅ Presets `fluid`, `particles`, `life` e `clock`; script com erro não crasha a app.

### Fase 5 — App e polimento
- Tray, config global, temas, autostart, wallpaper por monitor.
- README com guia "Criar o teu primeiro wallpaper", passo a passo.
- ✅ Utilizador novo instala, escolhe um preset na tray, cria um tema, e tudo sobrevive a um reboot.

## 9. Testes

### Unitários (doctest)
Lógica pura sem Win32: parsing do manifest e dos temas (incluindo erros), herança de temas, cálculo da grid (colunas/linhas/margens por DPI), conversão de coordenadas do ecrã virtual, mapeamento valor → charset, parsing de cores, limites de params.

### Checklist manual de integração (correr no fim das fases 1, 2 e 5)
- [ ] Arranque sem flash preto; os ícones continuam clicáveis e por cima.
- [ ] Mudar a escala 100 → 125 → 150 → 175% → 100% em cada monitor: sem blur, corte nem bloqueio.
- [ ] Mudar a resolução e a orientação.
- [ ] Ligar e desligar um segundo monitor (também à esquerda do principal).
- [ ] `taskkill /f /im explorer.exe` e depois `start explorer` → o wallpaper volta sozinho.
- [ ] Jogo ou vídeo em fullscreen → CPU ~0% nesse monitor (confirmar no Gestor de Tarefas).
- [ ] Bloquear o ecrã (Win+L), suspender e retomar.
- [ ] Sair pela tray → o wallpaper original do Windows volta.
- [ ] Win+D e "Mostrar ambiente de trabalho" continuam a funcionar.
- [ ] Bench dentro do orçamento em 1080p e 1440p.

## 10. Como trabalhar neste projeto (instruções para o Claude Code)

- **Começa pela Fase 0 e depois pela Fase 1.** Não saltes para os efeitos bonitos antes de a integração estar sólida.
- No fim de cada fase: corre o build, os testes e o bench, diz-me o que testar manualmente da checklist e espera pela minha confirmação.
- **Commits pequenos** com mensagens claras, um por passo lógico.
- Mantém `docs/ARCHITECTURE.md` atualizado. **Estou a aprender C++/Win32**: explica as decisões não óbvias em comentários curtos e no doc (porquê o flip model, porquê Raw Input, como funciona o WorkerW). Prefere código claro a código "esperto".
- Se uma decisão desta spec se revelar má na prática (por exemplo, o flip model não funciona numa janela filha num caso específico), **para, explica e propõe uma alternativa** antes de mudar.
- Qualidade: `/W4 /permissive-`, RAII para handles Win32 e COM (`wil` ou `Microsoft::WRL::ComPtr`), sem `new`/`delete` soltos, erros HRESULT sempre verificados e registados.
- Código e identificadores em inglês; doc e conversa em português.
