# Changelog

Formato baseado em [Keep a Changelog](https://keepachangelog.com/pt-PT/1.1.0/); versões em [SemVer](https://semver.org/lang/pt-PT/).

## [0.1.0] — 2026-11-19

Primeira versão alpha: **Fases 0 e 1** (esqueleto + integração com o desktop). O wallpaper é uma cor animada de teste; o render ASCII chega na Fase 2.

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
- Documentação: `README.md`, `docs/ARCHITECTURE.md`.

[0.1.0]: ../../releases/tag/v0.1.0
