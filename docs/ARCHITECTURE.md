# Arquitetura do AsciiPaper

> Documento vivo: explica **porquê** o código é como é. Atualizado no fim de cada fase.
> Estado atual: **Fase 0 (esqueleto) + Fase 1 (integração com o desktop)**.

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
| `src/core/` | Lógica **pura** sem Win32 (geometria, política de pausas, argumentos). É a única parte testada por unit tests. |
| `src/util/` | Log, helpers de Win32. |
| `src/desktop/` | `WorkerW` (ligar ao Explorer), `MonitorManager` (monitores + DPI), `PauseManager` (quem tapa o quê), `Wallpaper` (guardar/repor). |
| `src/render/` | `Device` (D3D11 partilhado), `MonitorSurface` (janela + swapchain por monitor). |
| `src/app/` | `App` (cola tudo e corre o loop), `Tray`. |
| `res/` | Manifesto (PerMonitorV2), ícone, `.rc` com versão. |
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

## 11. Números medidos (Fase 1, Release)

Máquina: Windows 11 (build 26200, layout 24H2+), RTX 4060, 2 monitores (1920×1080 + 1080×1920 em coordenadas negativas), 12 threads.

| Métrica | Valor | Alvo |
|---|---|---|
| Binário | 494 KB | < 5 MB |
| CPU média (1 monitor a desenhar a 30 FPS, outro em pausa) | 0,05% | < 2% |
| Working set (2 monitores) | 39 MB | < 30 MB por monitor |
| Arranque até ao primeiro frame | ~240 ms (inclui criar o dispositivo D3D) | < 300 ms |

> Nota sobre RAM: uma boa parte do working set são as DLLs do driver da GPU carregadas no processo, que não controlamos. O `--bench` (Fase 2) vai reportar *private bytes* e working set separadamente.

## 12. Decisões em aberto

- **clang-cl**: os presets existem, mas o componente LLVM do Visual Studio ainda não está instalado nesta máquina, por isso o build com clang-cl ainda não foi verificado.
- **Monitores em GPUs diferentes**: um único dispositivo D3D; avaliar um dispositivo por adaptador se o custo da cópia entre GPUs se notar.
