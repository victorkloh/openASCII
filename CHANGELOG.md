# Changelog

Formato baseado em [Keep a Changelog](https://keepachangelog.com/pt-PT/1.1.0/); versões em [SemVer](https://semver.org/lang/pt-PT/).

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

[0.2.0]: ../../releases/tag/v0.2.0
[0.1.0]: ../../releases/tag/v0.1.0
