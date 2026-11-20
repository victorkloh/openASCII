# Changelog

Formato baseado em [Keep a Changelog](https://keepachangelog.com/pt-PT/1.1.0/); versões em [SemVer](https://semver.org/lang/pt-PT/).

## [0.4.0] — 2026-11-20

### Adicionado
- **8 efeitos novos**, escolhidos a partir de pesquisa sobre o que é mais popular em wallpapers animados e arte ASCII: `synthwave`, `aurora`, `rain` (janela lofi), `aquarium`, `clock`, `fire`, `fireworks`, `donut` — cada um com uma variante.
- **FPS do fundo**: automático (cada wallpaper decide) ou fixo (15–144), no menu da bandeja e nas Definições (`fps` na config).
- **Compatibilidade com Wallpaper Engine e Lively**: deteta-os ao arrancar e pergunta se os pausa (Wallpaper Engine: `-control stop`/`play`; Lively: `app --play false/true`, os comandos oficiais deles, sem mexer nas definições); ao sair pergunta se os retoma. "Lembrar a escolha" e opções nas Definições.
- **Presença do rato** (`mouse.presence`): os efeitos do rato entram e saem com uma transição suave e voltam ao repouso quando o rato sai do fundo.
- Prelude: `localTime`/`localDate` (relógios), `primaryCenter()`, `primaryCenterWorld()`, `primarySizeWorld()`, `primaryRect()`, `monitorRectAt(c)`.
- Diagnóstico de desempenho: o `--bench` mede o tempo de GPU (queries de timestamp), o `Present`, os engasgos e as vezes que o loop acorda; o `--verbose` regista o mesmo a cada 5 s e avisa de mensagens lentas.
- `ideias.md`: brainstorm de efeitos avançados, ordenado.

### Corrigido
- **O fundo perdia FPS ao mexer o rato** (sobretudo dentro de janelas): o Raw Input com `RIDEV_INPUTSINK` recebia uma mensagem por cada movimento do rato no sistema inteiro, numa fila de input ligada à do Explorer. Agora o rato é lido uma vez por frame (`GetCursorPos` + `GetAsyncKeyState`): medido 500 → 31 despertares/s e 370 → 1 mensagem/s com o rato a mexer, 0 engasgos.
- `taskkill` sem `/f` voltou a fechar a app com limpeza (o `WM_CLOSE` chegava à janela escondida do launcher).
- Otimizações de GPU: o valley calcula o relevo só onde há um animal (o HLSL avalia os dois lados de `&&`); o donut salta o raymarching longe do centro.

## [0.3.0] — 2026-11-20

### Adicionado
- **Efeito estendido entre monitores**: um só canvas cobre todos os ecrãs (como o modo Estender do Windows). Uma onda que sai de um monitor entra no outro; com DPIs diferentes a escala física mantém-se. As zonas fora dos monitores são "paredes". Opção nas Definições (`span_monitors`).
- **Valley**: um terrain vivo com chuvadas, nuvens com sombra, manadas que fogem do rato, pássaros em V, peixes, florestas, aldeias que acendem à noite, ciclo dia/noite e relâmpagos ao clicar. Variantes `autumn` e `miniature`.
- **Menu da bandeja próprio** (no lugar do menu de contexto do Windows): wallpapers em miniatura, variantes, velocidade e resolução, pausar, personalizar, sair.
- **Launcher**: personalizar cada wallpaper (variantes, resolução da grelha, escala do efeito, FPS, fonte, cores com seletor HSV, caracteres, todos os parâmetros) ao vivo; guardar como tema; criar wallpapers novos a partir de um modelo e abrir o código; definições gerais (modo estendido, interação do rato, pausas, limite de FPS, iniciar com o Windows).
- **Resolução vs escala**: `render.scale` e coordenadas de mundo (`i.world`, `mouse.world`, `worldSize`) — mudar a resolução da grelha já não muda o tamanho do efeito.
- **Sprites**: `render.glyphs` + `glyph(k)` no shader.
- Ajustes do utilizador por wallpaper em `%APPDATA%\AsciiPaper\overrides\` (formato de tema).
- `visible(c)` no prelude; `--launcher`, `--ui-screenshot`, `--flyout-screenshot`.
- Toolkit de interface em modo imediato com Direct2D (repinta só com input), tema claro/escuro do Windows.

### Alterado
- `ripple`, `terrain` e `starfield` usam coordenadas de mundo; o `ripple` ganhou o parâmetro `speed`.
- O log abre em modo partilhado (uma segunda instância, ex.: `--windowed`, também consegue escrever).

## [0.2.0] — 2026-11-20

**Fases 2 e 3**: render ASCII a sério, wallpapers programáveis em HLSL, rato e os primeiros quatro efeitos.

### Adicionado
- **Render ASCII** (Fase 2): atlas de glifos rasterizado com DirectWrite no tamanho exato para o DPI de cada monitor (textura R8, anti-aliasing em cinza), cell buffer na GPU e `grid.hlsl` (um triângulo de ecrã inteiro, uma draw call por monitor). Grelha centrada, margem com a cor de fundo.
- **Glifos procedurais**: blocos `░▒▓█▀▄…`, linhas de caixa `─│┌┐…` e Braille `⠀–⣿` desenhados píxel a píxel, sem costuras entre células.
- **Backend shader** (Fase 3): o `main.hlsl` do wallpaper corre como compute shader (uma thread por célula), com `prelude.hlsl` (tempo, rato, `param()`, `palette()`, `noise/fbm`, `prev()` com double buffering).
- **Manifest `wallpaper.toml`** e **temas** com herança (`base = "..."`), validação com mensagens claras e número da linha.
- **Hot reload** com `ReadDirectoryChangesW` (sem polling); compilação num thread de trabalho; erros mostrados no ecrã, mantendo o último estado bom.
- **Rato** via Raw Input (`RIDEV_INPUTSINK`, desligado em pausa) + **DesktopHitTest**: só reage sobre o desktop por defeito.
- **Presets**: `ripple` (equação de onda), `matrix`, `terrain`, `starfield`, cada um com 2 temas e `preview.png`.
- **Tray**: submenu para escolher wallpaper/tema, Recarregar, Abrir pasta de wallpapers.
- **`config.toml`** mínimo (wallpaper escolhido, modo de interação, `max_fps`, regras de pausa).
- **`--bench <s>`**: frame time médio/p99, CPU% e RAM (working set e private bytes).
- **`--screenshot <png>`**: renderiza fora do ecrã e grava um PNG (usado para as previews).
- Guia `docs/CREATING_WALLPAPERS.md`.

### Corrigido
- A pasta `wallpapers/` é copiada para junto do `.exe` em todos os builds (antes só quando o `.exe` era religado).

## [0.1.0] — 2026-11-19

Primeira versão alpha: **Fases 0 e 1** (esqueleto + integração com o desktop). O wallpaper era uma cor animada de teste.

### Adicionado
- Build com CMake + vcpkg (manifest, `x64-windows-static`, CRT estático), `/W4 /WX /permissive-`, presets MSVC e Ninja/clang-cl.
- Manifesto PerMonitorV2 + UTF-8, ícone, informação de versão no `.exe`.
- Log em `%APPDATA%\AsciiPaper\log.txt` com rotação; `--verbose` também na consola.
- Instância única (mutex com nome).
- Ligação ao desktop atrás dos ícones: layout clássico (`WorkerW` de topo), layout 24H2+ (`WorkerW` filho do `Progman`) e fallback.
- Uma janela + swapchain D3D11 por monitor, flip model com fallback para blt, primeiro frame antes de mostrar (sem flash preto).
- Reação a mudanças de DPI, resolução, orientação e monitores (com debounce e segunda passagem).
- Reattach automático quando o Explorer reinicia (`TaskbarCreated` + deteção de janelas destruídas).
- Recuperação de `DXGI_ERROR_DEVICE_REMOVED/RESET`.
- Pausas por monitor: ecrã inteiro, janela maximizada; e globais: ecrã bloqueado, suspensão, poupança de bateria, pausa manual.
- Repõe o wallpaper original à saída, no encerramento do Windows e em caso de crash.
- Tray: Pausar/Retomar, Abrir pasta de logs, Sair. `taskkill` sem `/f` sai de forma limpa.
- Modo `--windowed` para desenvolvimento.
- Testes unitários (doctest): geometria/coordenadas do ecrã virtual, DPI, política de pausas, argumentos.

[0.4.0]: ../../releases/tag/v0.4.0
[0.3.0]: ../../releases/tag/v0.3.0
[0.2.0]: ../../releases/tag/v0.2.0
[0.1.0]: ../../releases/tag/v0.1.0
