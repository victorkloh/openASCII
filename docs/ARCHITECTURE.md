# Arquitetura do AsciiPaper

> Documento vivo: explica **porquê** o código é como é. Atualizado no fim de cada fase.
> Estado atual: **Fases 0–3** (esqueleto, integração com o desktop, render ASCII, rato + backend shader).

## 1. Visão geral

```
                ┌──────────────────────── processo asciipaper.exe ─────────────────────────┐
                │                                                                          │
 Windows ──msg──▶  Janela controladora (escondida, de topo)                                 │
 (broadcasts,   │    ├─ TaskbarCreated      → reattach ao Explorer                         │
  energia,      │    ├─ WM_DISPLAYCHANGE…   → syncLayout (monitores/DPI)                   │
  sessão, tray) │    ├─ WTS / POWERBROADCAST→ PauseManager                                 │
                │    └─ tray (menu)                                                        │
                │                                                                          │
                │  Loop principal: MsgWaitForMultipleObjectsEx(timer de frames, mensagens) │
                │    └─ renderFrame() → para cada MonitorSurface ativa: Clear + Present    │
                │                                                                          │
                │  MonitorSurface ×N  (uma janela filha + swapchain por monitor)           │
                └───────────┬──────────────────────────────────────────────────────────────┘
                            │ SetParent (janela filha noutro processo)
                ┌───────────▼───────────── explorer.exe ─────────────┐
                │ Progman ─┬─ SHELLDLL_DefView (ícones)   ← por cima  │
                │          ├─ [as nossas janelas]                     │
                │          └─ WorkerW (wallpaper estático) ← por baixo│
                └─────────────────────────────────────────────────────┘
```

## 2. Estrutura de pastas

| Pasta | Conteúdo |
|---|---|
| `src/core/` | Lógica **pura** sem Win32: geometria, grelha, cores, UTF-8, manifest/temas, `param()`, glifos procedurais, pausas, argumentos. É a parte testada por unit tests. |
| `src/util/` | Log, helpers de Win32. |
| `src/desktop/` | `WorkerW` (ligar ao Explorer), `MonitorManager` (monitores + DPI), `PauseManager` (quem tapa o quê), `Wallpaper` (guardar/repor o do Windows). |
| `src/render/` | `Device` (D3D11 partilhado), `MonitorSurface` (janela + swapchain por monitor), `GlyphAtlas`, `GridRenderer` (cell buffer + `grid.hlsl`), `ShaderCompiler`, `Screenshot`. |
| `src/wallpaper/` | `Wallpaper` (encontrar/carregar pastas e temas), `ShaderBackend` (compute shader do wallpaper), `DirectoryWatcher` (hot reload). |
| `src/input/` | `MouseInput` (Raw Input), `DesktopHitTest`. |
| `src/app/` | `App` (cola tudo e corre o loop), `Tray`, `Config`. |
| `shaders/` | `grid.hlsl` (render final), `prelude.hlsl` + `epilogue.hlsl` (a API dos wallpapers). Embebidos no `.exe` como recursos. |
| `wallpapers/` | Os presets; copiados para junto do `.exe` em cada build. |
| `res/` | Manifesto (PerMonitorV2), ícone, `.rc` com versão e shaders. |
| `tests/` | doctest sobre `src/core`. |
| `scripts/` | `setup.ps1` (ferramentas), `build.ps1`, `package.ps1` (zip de release), `make-icon.ps1`. |

## 3. Loop principal: porquê `MsgWaitForMultipleObjectsEx` + waitable timer

O objetivo é **~0% de CPU em pausa** e um ritmo de frames estável quando ativo.

- `MsgWaitForMultipleObjectsEx` adormece o thread até chegar **uma mensagem** *ou* **um handle** ficar sinalizado. Não há `Sleep` nem polling.
- O handle é um **waitable timer de alta resolução** (`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`, Windows 10 1803+). Acorda no instante pedido sem `timeBeginPeriod(1)`, que aumentaria a frequência do relógio do sistema inteiro e gastaria bateria.
- O timer é armado **frame a frame** com tempo absoluto (QPC): `next += period`. Se ficarmos para trás (PC ocupado), saltamos para `agora + period` em vez de disparar uma rajada de frames.
- **Em pausa**, o timer é cancelado e **não entra** na espera: o thread só acorda com mensagens.

## 4. Janela controladora: porquê não é *message-only*

As janelas `HWND_MESSAGE` são leves, mas **não recebem broadcasts**: `WM_DISPLAYCHANGE`, `WM_SETTINGCHANGE`, `WM_DEVICECHANGE` e sobretudo `TaskbarCreated` (o aviso de que o Explorer reiniciou). Por isso usamos uma janela de topo `WS_POPUP` invisível com `WS_EX_TOOLWINDOW` (não aparece na barra de tarefas nem no Alt+Tab).

Também recebe: `WM_WTSSESSION_CHANGE` (bloqueio), `WM_POWERBROADCAST` (suspensão, poupança de bateria), as mensagens da tray e `WM_CLOSE` (`taskkill` sem `/f` → saída limpa).

## 5. WorkerW: como ficamos atrás dos ícones

O ambiente de trabalho é desenhado pelo Explorer em janelas especiais:

- **`Progman`**: a janela "Program Manager", raiz do desktop.
- **`SHELLDLL_DefView`**: contém a lista de ícones (`SysListView32`).
- **`WorkerW`**: janelas auxiliares; uma delas pinta o wallpaper estático.

Enviar a mensagem **não documentada `0x052C`** ao `Progman` (wParam `0xD`, lParam `0x1`) faz o Explorer criar um `WorkerW` **por trás dos ícones**. É o truque que o Lively, o Wallpaper Engine e afins usam. Depois disso, há dois layouts possíveis:

### Layout clássico (Windows 10, Windows 11 até 23H2)
```
(topo)  WorkerW ── SHELLDLL_DefView ── SysListView32   ← ícones
        WorkerW                                         ← alvo: desenhamos aqui dentro
        Progman
```
Procuramos com `EnumWindows` a janela de topo que contém `SHELLDLL_DefView`; o `WorkerW` **seguinte** na ordem Z é o anfitrião.

### Layout 24H2+ (Windows 11 24H2 e posteriores)
```
Progman ─┬─ SHELLDLL_DefView   ← ícones (por cima)
         ├─ [nós]              ← inseridos logo abaixo do DefView
         └─ WorkerW            ← wallpaper estático (por baixo)
```
O `WorkerW` passou a ser **filho** do `Progman`. Ficamos filhos do `Progman` e usamos `SetWindowPos(child, defView, …)`, que coloca a nossa janela **imediatamente abaixo** dos ícones; depois empurramos o `WorkerW` para `HWND_BOTTOM`.

### Fallback
Se nenhum dos dois for reconhecido, ficamos filhos do `Progman` (abaixo do `DefView` se existir). O log diz sempre qual estratégia foi usada:
```
WorkerW: estratégia = 24H2+ (WorkerW filho do Progman); anfitriã 0x…(Progman), 3000x1920 com origem em (-1080, -615)
```

### Cuidados
- **Parentesco entre processos**: uma janela filha de uma janela de outro processo **liga as filas de input** dos dois threads (como `AttachThreadInput`). Se o nosso thread de UI bloquear, **o desktop bloqueia**. Regra: o thread de UI nunca faz trabalho longo (compilar shaders, ler ficheiros grandes) — isso irá para threads de trabalho nas próximas fases.
- **`WS_POPUP` → `WS_CHILD` antes de `SetParent`**, como pede a documentação.
- Mensagens ao Explorer com **`SendMessageTimeout`** (1 s), nunca `SendMessage`: se o Explorer estiver pendurado, não ficamos pendurados com ele.
- `WM_NCHITTEST → HTTRANSPARENT` e `WS_EX_NOACTIVATE`: nunca roubamos cliques nem o foco.

### Explorer reinicia ou crasha
1. O `WorkerW`/`Progman` morre e leva as nossas janelas filhas com ele → recebemos `WM_DESTROY` sem o termos pedido → `WM_APP_SURFACE_LOST`.
2. A nova barra de tarefas envia `TaskbarCreated` a todas as janelas de topo.
3. Em ambos os casos agendamos um **reattach** (com timer, porque o Explorer demora a ficar pronto): destruir superfícies → `attach()` → recriar. Até 30 tentativas, 1 por segundo. O ícone da tray também é readicionado.

`ChangeWindowMessageFilterEx` deixa passar `TaskbarCreated` mesmo que a app corra elevada.

## 6. Monitores, DPI e coordenadas

- **Uma janela por monitor**, cada uma com a sua swapchain do tamanho exato do monitor. Isto permite DPIs diferentes por monitor e pausar um monitor sem afetar os outros.
- **Coordenadas**: os monitores vêm em coordenadas de **ecrã virtual**, que podem ser negativas (monitor à esquerda/acima do principal). As nossas janelas são filhas, logo posicionam-se nas coordenadas **cliente** do anfitrião. Usamos `ClientToScreen(host, {0,0})` para obter a origem do anfitrião e subtraímos (`toParentSpace`). Na prática é igual a subtrair `SM_XVIRTUALSCREEN/SM_YVIRTUALSCREEN` (o log mostra os dois), mas não depende de o anfitrião cobrir exatamente o ecrã virtual.
- **DPI**: o manifesto declara **PerMonitorV2**, por isso trabalhamos sempre em píxeis físicos. As janelas **filhas não recebem `WM_DPICHANGED`**, por isso reagimos a `WM_DISPLAYCHANGE`, `WM_SETTINGCHANGE`, `WM_DEVICECHANGE (DBT_DEVNODES_CHANGED)` e relemos `GetDpiForMonitor` em cada monitor.
- **Debounce + passagem tardia**: estas mensagens chegam em rajada. Esperamos 250 ms e sincronizamos; e voltamos a sincronizar aos 1500 ms, porque o Explorer redimensiona o seu `WorkerW`/`Progman` ao seu ritmo e pode fazê-lo depois de nós.
- **Sincronizar** = remover superfícies de monitores desligados, criar as de monitores novos e, para as restantes: `ResizeBuffers` → reposicionar janela → **desenhar logo um frame** no tamanho novo. Os monitores são identificados pelo nome do dispositivo (`\\.\DISPLAY1`).
- Na Fase 1 cada monitor mostra uma cor animada com **moldura de 6 px lógicos** e um **quadrado central de 48 px lógicos**: se a moldura aparecer cortada ou o quadrado mudar de tamanho aparente ao mudar a escala, há um bug de layout/DPI.

## 7. Render

### Porquê o flip model (`DXGI_SWAP_EFFECT_FLIP_DISCARD`)
No modelo antigo ("blt"), o DWM **copia** o nosso back buffer para a sua própria superfície a cada frame. No flip model o DWM **usa diretamente** os nossos buffers: menos uma cópia de ecrã inteiro por frame e por monitor, menos VRAM e menos latência. Funciona em janelas filhas no Windows 10/11 (confirmado no layout 24H2). Se a criação falhar, caímos para `DXGI_SWAP_EFFECT_DISCARD` (blt) e registamos no log.

### Outras decisões
- **`DXGI_SCALING_NONE`**: se por um instante a janela e os buffers tiverem tamanhos diferentes, o frame aparece alinhado ao canto em vez de **esticado**.
- **Um só `ID3D11Device`** para todos os monitores (um adaptador). Monitores ligados a GPUs diferentes funcionam (o DWM faz a cópia entre GPUs), com um pequeno custo; otimizar isso fica para depois, se for preciso.
- **`Present(0, 0)`**: o ritmo vem do nosso timer. Com `Present(1, 0)` (vsync) cada swapchain bloquearia à espera do vsync — com 3 monitores, 3 esperas por frame.
- **Sem flash preto**: a janela é criada **escondida**, recebe a swapchain e o **primeiro frame é apresentado antes** de `ShowWindow(SW_SHOWNA)`.
- **Sem GDI**: `WM_ERASEBKGND` devolve 1, `WM_PAINT` só valida, a classe não tem pincel de fundo.
- **Device lost** (`DXGI_ERROR_DEVICE_REMOVED/RESET`, ex.: atualização ou crash do driver): libertar todas as swapchains e o dispositivo, recriar tudo; se falhar, tentar de novo a cada segundo. As janelas não são destruídas.

## 7b. Render ASCII (Fase 2)

```
compute shader do wallpaper ──escreve──▶ cell buffer (1 célula = 12 bytes) ──lê──▶ grid.hlsl ──▶ swapchain
         ▲ lê prev()                       { glifo, fg RGBA8, bg RGBA8 }            ▲ lê
   state A ◀──troca──▶ state B                                                 atlas R8 (glifos)
```

### GlyphAtlas: porquê rasterizar por DPI
Um glifo rasterizado a 12 px e esticado para 18 px fica desfocado; encolhido fica com serrilha. Por isso **nunca escalamos o atlas**: para cada monitor, rasterizamos todos os caracteres no tamanho exato em píxeis para o seu DPI (`cell` em px lógicos × escala). Se o DPI muda, reconstruímos (é rápido: ~200 glifos).
- **DirectWrite** dá-nos a cobertura de cada glifo em escala de cinza (`IDWriteGlyphRunAnalysis`, 1 byte por píxel). Guardamos numa textura **R8** (só cobertura); a cor vem do cell buffer.
- **Métricas inteiras**: a altura da célula é exatamente `ascent + descent` da fonte escalada; a largura é o avanço do `0` arredondado; a linha de base fica no mesmo píxel inteiro em todas as células. Meio píxel = blur ou costuras.
- O atlas contém o **charset** (slots 0..N-1, na mesma ordem: assim o shader escolhe o caractere só com um índice), o **ASCII** e o **Latin-1** (para as mensagens de erro em português) e `?` para o que faltar.
- O pixel shader lê o atlas com `Load` (o píxel exato), nunca `Sample` (filtragem = blur).

### Glifos procedurais
Blocos (`░▒▓█▀▄…`), linhas de caixa (`─│┌┐…`) e Braille vêm **desenhados por nós**, píxel a píxel, em vez da fonte. As fontes desenham-nos com pequenas folgas ou sobreposições que, numa grelha, aparecem como **riscas entre células**; desenhados à mão ocupam exatamente a célula inteira. É o que os terminais modernos (Windows Terminal, kitty, WezTerm) fazem. `▀` e `▄` dividem a célula no mesmo píxel, por isso juntos dão exatamente `█` (há um teste para isso).

### grid.hlsl: um triângulo
Para cada monitor há **uma draw call**: um triângulo que cobre o ecrã inteiro (mais barato que dois, que processariam duas vezes os píxeis da diagonal). O pixel shader calcula em que célula está, lê a célula, lê a cobertura no atlas e mistura `bg → fg`. A grelha é centrada; a margem (quando o ecrã não é múltiplo da célula) fica com a cor de fundo.

## 7c. Backend shader (Fase 3)

O `main.hlsl` do wallpaper define só `CellOut cell(CellIn i)`. A app monta o código final:

```
#line 1 "prelude.hlsl"   ← constantes do frame, rato, palette(), noise(), prev()...
#line 1 "main.hlsl"      ← o código do wallpaper, com param("x") trocado por _param(índice)
#line 1 "epilogue.hlsl"  ← [numthreads(8,8,1)] CSMain: chama cell() e escreve no cell buffer
```

- **Compute shader, uma thread por célula**: ~30 000 células por monitor 1080p são trivialidade para uma GPU, e o resultado já fica no cell buffer sem passar pela CPU.
- **`param("speed")`**: o HLSL não tem strings. Antes de compilar, cada `param("nome")` é trocado por `_param(i)`, onde `i` é a posição do parâmetro no `[params]` do manifest; os valores vão num constant buffer. Um nome desconhecido é erro com número da linha (lógica pura, testada).
- **`#line`**: os erros do compilador apontam para a linha certa do `main.hlsl`, não do ficheiro montado.
- **`prev()` com double buffering**: dois buffers de estado (`float4` por célula). Num frame lê-se de A e escreve-se em B; no seguinte trocam. Assim cada célula vê o estado *anterior* de todas as vizinhas, sem corridas.
- O `FrameConstants` em C++ é o espelho exato do `cbuffer Frame` do prelude (regras de empacotamento de 16 bytes); um `static_assert` no tamanho apanha discrepâncias.
- **Compilação fora do thread de UI**: o `D3DCompile` pode demorar dezenas de ms. Como as nossas janelas são filhas de uma janela do Explorer (filas de input ligadas), bloquear o thread de UI bloquearia o desktop. O hot reload compila com `std::async` e avisa o thread de UI com uma mensagem. Só o **primeiro** carregamento é síncrono, *antes* de as janelas existirem, para o primeiro frame já ser o wallpaper (28 ms medidos).
- **Erros**: manifest inválido ou shader que não compila → mantém-se o último estado bom; a mensagem aparece num canto do ecrã (escrita diretamente no cell buffer) e no log.

### Hot reload
`ReadDirectoryChangesW` **assíncrono** na pasta do wallpaper: o evento do `OVERLAPPED` entra na lista de handles do `MsgWaitForMultipleObjectsEx`. Não há thread nem polling. Ao sinalizar, esperamos 150 ms (os editores gravam em vários passos) e recarregamos.

## 7d. Rato (Fase 3)

- **Raw Input com `RIDEV_INPUTSINK`** numa janela *message-only*: recebemos os botões e o scroll mesmo sem foco. Um hook `WH_MOUSE_LL` poria o nosso processo no caminho de **cada** evento do rato do sistema (se demorarmos, o cursor de toda a gente fica lento, e o Windows remove hooks lentos); o Raw Input é só uma cópia entregue na nossa fila.
- A **posição** vem de `GetCursorPos` uma vez por frame (o Raw Input só dá movimentos relativos); a velocidade é suavizada exponencialmente.
- **Em pausa, o Raw Input é desregistado**: sem isso o sistema acordar-nos-ia a cada movimento do rato (com ratos de 1000 Hz, mil vezes por segundo).
- **DesktopHitTest**: `WindowFromPoint` sob o cursor é `SysListView32`/`SHELLDLL_DefView`/`WorkerW`/`Progman`? Só então o rato conta (`mouse.inside`) e os cliques são aceites — clicar num browser por cima do desktop não faz ondas. Configurável com `interaction = "always"`.
- Cada monitor recebe a posição convertida para as **suas** células (origem do monitor + margem da grelha).

## 8. Pausas (pausar = 0% de CPU)

As razões são **bits** (`PauseReason`), avaliados **por monitor** pela função pura `evaluatePause` (testada):

| Razão | Fonte | Âmbito |
|---|---|---|
| `user` | Menu da tray "Pausar" | global |
| `locked` | `WTSRegisterSessionNotification` → `WTS_SESSION_LOCK/UNLOCK`, desligar sessão | global |
| `suspended` | `WM_POWERBROADCAST` → `PBT_APMSUSPEND` / `PBT_APMRESUME*` | global |
| `battery-saver` | `RegisterPowerSettingNotification(GUID_POWER_SAVING_STATUS)` | global (configurável) |
| `fullscreen` | `SHQueryUserNotificationState` (jogos D3D exclusivos, apresentações, *busy*) **ou** janela em primeiro plano não maximizada que cobre o monitor inteiro (vídeo no browser, jogos borderless) | por monitor |
| `maximized` | Qualquer janela visível, não *cloaked*, maximizada nesse monitor | por monitor (configurável) |

Detalhes:
- Ignoramos as janelas do shell (`Progman`, `WorkerW`, barras de tarefas), as nossas e as **cloaked** (`DwmGetWindowAttribute(DWMWA_CLOAKED)`: janelas noutro ambiente de trabalho virtual ou apps UWP suspensas, que o `IsWindowVisible` diz serem visíveis).
- **Eventos de janela**: `SetWinEventHook` (`WINEVENT_OUTOFCONTEXT`, sem injetar nada noutros processos) para `EVENT_SYSTEM_FOREGROUND`, `MOVESIZEEND`, `MINIMIZESTART/END`, com debounce de 100 ms.
- **Desvio consciente da spec**: algumas mudanças (ex.: Win+↓ numa janela maximizada, um vídeo a sair de ecrã inteiro) não geram nenhum destes eventos. Para não as perder, reavaliamos **1× por segundo**, mas **só** enquanto houver um monitor a desenhar ou em pausa por causa de uma janela. Em pausa por bloqueio/suspensão/utilizador **não há timer nenhum**. O custo medido de uma reavaliação é desprezável (CPU média total 0,05% com dois monitores). A alternativa sem polling (`EVENT_OBJECT_LOCATIONCHANGE`) acordaria o thread a **cada movimento do rato** no sistema inteiro, o que é pior.
- Ao **retomar**, desenhamos logo um frame em vez de esperar pelo timer.

## 9. Repor o wallpaper e instância única

- No arranque guardamos `SPI_GETDESKWALLPAPER`; à saída chamamos `SPI_SETDESKWALLPAPER` com o mesmo caminho e **só `SPIF_SENDCHANGE`** (o Explorer repinta, mas não reescrevemos o registo — assim não estragamos um slideshow ou o Windows Spotlight).
- Também em `WM_ENDSESSION` (encerramento do Windows) e num `SetUnhandledExceptionFilter` (crash).
- Testado no layout 24H2: mesmo um `taskkill /f` deixa o wallpaper original visível, porque as nossas janelas simplesmente desaparecem de cima do `WorkerW`.
- **Instância única**: mutex `Local\AsciiPaper.SingleInstance` (só no modo wallpaper; o `--windowed` pode correr ao lado para desenvolver).

## 10. Build

- **C++20, Win32 puro, CMake + vcpkg (manifest)**, triplet `x64-windows-static` e CRT estático (`/MT`): um `.exe` sem DLLs para instalar.
- `/W4 /WX /permissive-`: warnings são erros. Os headers de terceiros entram como *SYSTEM* (`/external:W0`), por isso não nos afetam.
- RAII: `wil::unique_*` para handles Win32, `Microsoft::WRL::ComPtr` para COM. Sem `new`/`delete`.
- Presets: `msvc` (gerador Visual Studio, não precisa de Developer Shell) e `ninja-msvc` / `ninja-clang-cl` (precisam de Developer Shell; o clang-cl precisa do componente LLVM do Visual Studio).

## 11. Números medidos (v0.2.0, Release)

Máquina: Windows 11 (build 26200, layout 24H2+), RTX 4060, 2 monitores (1920×1080 + 1080×1920 em coordenadas negativas), 12 threads. Comando: `asciipaper.exe --bench 6` com o `ripple` (60 FPS, células de 6×12 px, 28 800 células por monitor).

| Métrica | Valor | Alvo |
|---|---|---|
| Binário | 909 KB | < 5 MB |
| CPU do processo (2 monitores a 60 FPS) | 0,17% | < 2% |
| Frame time em CPU (compute + draw + Present, 2 monitores) | médio 0,41 ms, p99 0,97 ms | — |
| RAM (2 monitores) | working set 51,5 MB, private bytes 57,4 MB (~26 MB/monitor) | < 30 MB por monitor |
| Arranque até ao primeiro frame (inclui compilar o wallpaper) | 239 ms | < 300 ms |
| Carregar + compilar um wallpaper | 28 ms | — |

> Notas: o frame time é medido em CPU; o trabalho da GPU é assíncrono e não entra (com esta carga, é desprezável). Uma parte grande da RAM são as DLLs do driver da GPU carregadas no processo, que não controlamos. A Fase 1 (só cor) media 0,05% de CPU com um monitor ativo a 30 FPS.

## 12. Decisões em aberto

- **clang-cl**: os presets existem, mas o componente LLVM do Visual Studio ainda não está instalado nesta máquina, por isso o build com clang-cl ainda não foi verificado.
- **Monitores em GPUs diferentes**: um único dispositivo D3D; avaliar um dispositivo por adaptador se o custo da cópia entre GPUs se notar.
- **Texto a 200% de escala**: o atlas é sempre rasterizado para o DPI real, mas o critério da Fase 2 ("células de 6 px nítidas a 200%") ainda não foi verificado num monitor a 200%.
- **Glow/bloom** (opcional na spec, desligado por defeito): ainda não implementado.
- **Cache de shaders compilados**: o arranque cumpre o orçamento sem cache (28 ms a compilar). Se wallpapers maiores o ultrapassarem, guardar o bytecode em `%LOCALAPPDATA%`.
