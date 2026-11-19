# AsciiPaper

**Wallpapers ASCII animados e interativos para Windows 10/11**, desenhados atrás dos ícones do ambiente de trabalho. Um único `.exe` nativo e pequeno (C++20, Win32, Direct3D 11), sem browser embutido nem runtime pesado.

> ⚠️ **Estado: alpha (v0.1.0).** Esta versão cobre as **Fases 0 e 1**: a integração com o desktop está feita (atrás dos ícones, vários monitores, DPI, pausas, recuperação do Explorer e da GPU), mas o wallpaper ainda é **uma cor animada de teste**. O render ASCII chega na Fase 2. Ver o [roadmap](#roadmap).

## O que já funciona

- Desenha **atrás dos ícones** no Windows 10, Windows 11 e Windows 11 24H2+ (os dois layouts do `WorkerW`).
- **Vários monitores**, com DPIs diferentes e em coordenadas negativas (monitor à esquerda ou acima do principal).
- **Sem flash preto** no arranque: o primeiro frame fica pronto antes de a janela aparecer.
- Adapta-se a mudanças de **escala, resolução, orientação** e a monitores ligados/desligados.
- **Pausa automática (0% de CPU)** por monitor: jogo ou vídeo em ecrã inteiro, janela maximizada, ecrã bloqueado, suspensão, poupança de bateria.
- **O Explorer reinicia ou crasha** → volta a ligar-se sozinho.
- **Reinício do driver da GPU** → recupera sem crashar.
- **Ao sair, repõe o wallpaper original** do Windows.
- **Instância única** e **ícone na tray** (Pausar/Retomar, Abrir pasta de logs, Sair).

## Instalar e usar

1. Transfere o `AsciiPaper-<versão>-win64.zip` da página de [Releases](../../releases) e extrai-o para uma pasta.
2. Corre `asciipaper.exe`. O ícone aparece na área de notificação (junto ao relógio).
3. Para sair, clica no ícone → **Sair**. O teu wallpaper original volta.

Não precisa de instalação nem de permissões de administrador. O Windows SmartScreen pode avisar, porque o executável ainda não está assinado digitalmente.

### Linha de comandos

| Opção | Descrição |
|---|---|
| `--windowed` | Renderiza numa janela normal redimensionável (para desenvolver sem mexer no desktop) |
| `--verbose`, `-v` | Logs detalhados, também na consola |
| `--wallpaper <pasta>` | Carrega um wallpaper específico *(Fase 3)* |
| `--bench <segundos>` | Mede frame time, CPU e RAM *(Fase 2)* |
| `--help`, `-h` | Ajuda |

### Logs

`%APPDATA%\AsciiPaper\log.txt` (roda para `log.1.txt` acima de 1 MB). Também acessível pela tray → **Abrir pasta de logs**.

## Compilar a partir do código

Requisitos: **Windows 10/11 x64**, **Visual Studio 2022** com o workload *Desenvolvimento para desktop com C++* (MSVC v143 + Windows SDK) e **Git**.

```powershell
# 1. Ferramentas (CMake, Ninja, vcpkg) numa pasta do utilizador, sem admin
pwsh scripts/setup.ps1

# 2. Configurar, compilar e testar (Release por defeito)
pwsh scripts/build.ps1
pwsh scripts/build.ps1 -Config Debug

# 3. Correr numa janela, com logs na consola
build\msvc\Release\asciipaper.exe --windowed --verbose

# 4. Criar o zip de release em dist/
pwsh scripts/package.ps1
```

Se já tens CMake ≥ 3.25 e vcpkg instalados, basta definir `VCPKG_ROOT` e usar os presets:

```powershell
cmake --preset msvc
cmake --build --preset msvc-release
ctest --preset msvc-release
```

As dependências (`wil`, `doctest`) são instaladas automaticamente pelo vcpkg em modo manifest (`vcpkg.json`), compiladas estaticamente.

## Documentação

- [`PROJECT.md`](PROJECT.md) — a especificação completa do projeto (requisitos, formato dos wallpapers, fases, checklist de testes).
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — como funciona por dentro e porquê: o truque do WorkerW, o flip model, DPI, pausas, o loop sem polling.
- [`CHANGELOG.md`](CHANGELOG.md) — histórico de versões.

## Roadmap

| Fase | Conteúdo | Estado |
|---|---|---|
| 0 — Esqueleto | CMake + vcpkg, manifesto PerMonitorV2, ícone, log, instância única, doctest | ✅ |
| 1 — Integração com o desktop | WorkerW (dois layouts), janela por monitor, D3D11 flip, DPI/monitores, reattach, pausas, repor wallpaper | ✅ código · ⏳ checklist manual |
| 2 — Render ASCII | GlyphAtlas (DirectWrite), cell buffer, `grid.hlsl`, blocos procedurais, `--bench` | ⏳ |
| 3 — Input + backend shader | Raw Input, HLSL com `prelude.hlsl`, manifest TOML, hot reload; presets `ripple`, `matrix`, `terrain`, `starfield` | ⏳ |
| 4 — Backend script | LuaJIT com sandbox, natives (fluid, particles, life); presets `fluid`, `particles`, `life`, `clock` | ⏳ |
| 5 — App e polimento | Tray completa, config global, temas, autostart, wallpaper por monitor, guia "Criar o teu primeiro wallpaper" | ⏳ |

## Licença

[MIT](LICENSE).
