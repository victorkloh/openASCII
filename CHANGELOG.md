# Changelog

Formato baseado em [Keep a Changelog](https://keepachangelog.com/pt-PT/1.1.0/); versões em [SemVer](https://semver.org/lang/pt-PT/).

## [0.5.0] — 2026-11-24

Primeira versão pública. As versões até à 0.4.0 foram de desenvolvimento e não chegaram a ser publicadas.

### Adicionado
- **15 tech demos**, um wallpaper por capacidade do motor, comentados para servirem de exemplo: `photo`, `spectrum`, `snow`, `temple`, `sandbox`, `fluid`, `coral`, `life`, `fractal`, `hud`, `planet`, `bonsai`, `mirror`, `terminal`, `arcade`. Temas novos: `synthwave/beat`, `valley/weather`, `matrix/code`.
- **Entradas (`[inputs]`)**, só ativas nos wallpapers que as pedem: janelas e ícones como obstáculos, áudio do sistema (loopback, 64 bandas + batida), imagem/GIF/vídeo/webcam como textura (WIC + Media Foundation), CPU/RAM, tempo real (Open-Meteo, só com cidade definida) e texto (`file:`, `folder:`, `lines:`). Os temas também as podem ligar.
- **Pós-processamento (`[post]`)**: glow pela "tinta" de cada glifo, scanlines, curvatura CRT, vinheta, aberração cromática e grão; caminho rápido sem custo quando desligado.
- **Multi-pass**: `render.substeps` (1–64) com `passIndex`/`passCount` no shader.
- Prelude: `asciiGlyph()` (qualquer caractere Latin-1), texto, áudio, imagem (`sampleSource`, `fitSource`, `sourceEdge`), obstáculos, fonte grande 5x7 (`bigFontPixel`), SDF 3D (`sdSphere`, `sdBox`, `sdTorus`, `cameraRay`...), `justClicked()`.
- **Launcher**: secção "entradas" por wallpaper (o que usa, "Escolher imagem ou vídeo", "Incluir na playlist"); Definições com cidade do tempo real, autorização da webcam e playlist (de N em N minutos, ordem aleatória).
- **Webcam com autorização explícita** (pergunta antes de ligar; "lembrar" opcional).
- `--screenshot-delay <s>`; o `--bench` já não pergunta pelo Wallpaper Engine.
- **Instalador** (`OpenASCII-<versão>-setup.exe`, Inno Setup): por utilizador e sem administrador. Pergunta se instala os **wallpapers predefinidos** (sem eles, a app instala-se vazia). Tem a opção **Iniciar com o Windows** e atalho no Menu Iniciar. Fecha a app antes de atualizar ou desinstalar, para o fundo original voltar. Na desinstalação, só apaga as tuas definições e wallpapers se disseres que sim. Continua a haver a versão portátil em zip.
- **Primeiros passos**: na primeira vez que a app arranca, o launcher mostra quatro passos curtos: boas-vindas e onde fica a app, desempenho (FPS, pausas, vários monitores), arranque com o Windows (opcional) e dicas. Cada escolha aplica-se logo e fica nas Definições.
- **Arranque rápido** (Definições e primeiros passos): em vez da lista de arranque do Windows, que atrasa as apps uns segundos depois de iniciares sessão, usa uma tarefa agendada só para a tua conta (sem administrador, prioridade normal, sem limite de tempo). Com o PC bloqueado, a app continua aberta em pausa, por isso ao desbloquear o wallpaper já lá está. O desinstalador apaga a tarefa.
- **Sem wallpapers instalados**, a app fica na bandeja sem tapar o fundo do Windows, e o launcher e o menu convidam a criar o primeiro.
- Abrir a app outra vez (o atalho, com ela já a correr desde o arranque) **mostra o launcher** da instância que já existe.
- Releases automáticas no GitHub: uma tag `vX.Y.Z` compila, testa, gera o instalador e o zip e cria um rascunho da release.

### Alterado
- **Novo nome: OpenASCII** (antes AsciiPaper). O executável passa a ser `openascii.exe` e as definições passam para `%APPDATA%\OpenASCII`. Na primeira vez, a app traz sozinha as definições, os ajustes e os teus wallpapers da pasta antiga, e passa o arranque com o Windows para o nome novo.
- **60 FPS por defeito** (antes: automático, a maioria a 30). As simulações que avançam por frame (fogo, ondas, neve, coral, vida, sandbox) usam `fixedStep()` e correm à mesma velocidade a qualquer FPS; configs antigas com `fps = 0` passam a 60.
- **Aplicar sem cliques perdidos**: um pedido feito enquanto outro wallpaper compila fica em fila (antes perdia-se e era preciso clicar outra vez); o botão e o cartão do menu mostram "A preparar" enquanto compila; o código compilado fica em cache (`%LOCALAPPDATA%\OpenASCII\shadercache`): o sandbox passou de 5,2 s para 21 ms na segunda vez.
- **Efeitos de ecrã em qualquer wallpaper** (launcher): Nenhum, CRT, Néon, Cinema ou à medida (brilho, linhas, curvatura, vinheta, aberração, grão).
- **Pesquisa e grupos no launcher**: procurar por nome, tipo ou descrição; lista agrupada por `meta.category` (Paisagens, Ambiente, Retro, 3D e espaço, Simulações, Dados e média, Jogos; os teus primeiro).
- Webcam e cidade do tempo real configuram-se na própria página do wallpaper que as usa.
- **Caracteres: "Usar só estes caracteres"** (`charset_only`), ligado ao escolher um conjunto. Antes, o conjunto só mudava a rampa: no aquário com "binário", os peixes continuavam com `< > ° ~`, e o texto e os sprites de todos os wallpapers ignoravam a escolha. Agora cada sprite e cada caractere de texto passa a ser desenhado com o caractere do conjunto mais parecido na forma e na densidade (um `(` vira `│` com o conjunto de caixas). Os conjuntos também se aplicam aos wallpapers que só desenham com caracteres próprios (neve, fogo de artifício), as mensagens de erro continuam legíveis e os conjuntos que não cabiam numa linha já não ficam escondidos.
- `fractal` sem interação com o rato (o modo Julia confundia).
- **Menu da bandeja mais compacto**: com 28 wallpapers saía do ecrã (2011 DIP de altura). Agora a grelha tem 3 colunas e scroll (no máximo ~3,5 linhas, com sombra nas bordas), a altura adapta-se sempre à área de trabalho, abre com o wallpaper em uso à vista, os ajustes rápidos ficam recolhidos, a pausa é um ícone no cabeçalho e há navegação por teclado (setas, Enter, Esc). Altura típica: ~490 DIP.

- Entradas mais robustas:
  - o áudio segue a troca de saída (colunas ↔ auscultadores);
  - o tempo real volta a tentar quando não há rede (ex.: no arranque, antes do Wi-Fi ligar);
  - a webcam desliga-se ao fim de 3 s em pausa e quando desligas o interruptor, e volta a tentar se estiver ocupada;
  - as simulações com `fixedStep()` aguentam dias seguidos ligadas (antes congelavam ao fim de ~4 dias);
  - a neve consulta as janelas e os ícones uma vez por célula, em vez de ~20.

### Corrigido
- **O que fica no fundo do ecrã já não se esconde atrás da barra de tarefas.** Os shaders recebem a área de trabalho de cada monitor (`primaryWorkRect()`, `workRectAt(c)`), e isto foi aplicado onde importa:
  - a raquete do `arcade`;
  - o vaso do `bonsai`;
  - a areia e as algas do `aquarium`;
  - a cidade e os lançamentos do `fireworks`;
  - as barras do `spectrum`;
  - os painéis do `hud` e do `terminal`.

  Muda sozinho se a barra mudar de sítio ou se esconder. Para testar: `--windowed --work-inset 48`.
- **O fundo do Windows aparecia por instantes por cima do wallpaper** (sobretudo com o Windows Spotlight, que troca o fundo sozinho). Quando troca o fundo, o Explorer reordena as janelas do ambiente de trabalho e a dele ficava por cima da nossa até à passagem de layout seguinte (até ~1,5 s). Agora a ordem é verificada a cada frame (no máximo a cada 50 ms) e reposta logo: medido, 16–47 ms.
- Cada `WM_SETTINGCHANGE` do Windows (são frequentes) reposicionava as janelas do wallpaper e refazia atlas, grelhas e canvas, mesmo sem nada ter mudado. Agora só se mexe no que mudou.
- **Quebra de FPS com o rato sobre o launcher e ao trocar de wallpaper**: o ciclo de mensagens ficava preso a alternar entre movimento do rato e pintura e nunca atendia o timer dos frames; o fundo passa a desenhar à hora certa entre mensagens.
- O fundo congelava enquanto se arrastava ou redimensionava o launcher, num menu ou num diálogo (medido: até 1,3 s); durante loops modais, um thread do threadpool espera pelo timer de alta resolução e o fundo continua a 30/60 fps.
- Os botões minimizar/maximizar/fechar já não congelam o fundo (~100 ms por clique).
- Interface mais leve: pinturas limitadas a ~70/s e de ~8 ms para ~2,7 ms (comparação de caminhos sem ir ao disco, larguras de texto em cache, registo lido só ao arrancar); previews descodificadas em segundo plano (primeira pintura de ~400 ms para ~40 ms); trocar de wallpaper não atualiza janelas escondidas.
- `taskkill` (sem `/f`) já não fica preso no diálogo de saída, e fecha a app mesmo com o launcher aberto (antes só o escondia).
- Neve: mexer o rato fazia os flocos todos "acelerarem" (o vento multiplicava o tempo); agora a deriva é o integral do vento e o rato só afasta os flocos à sua volta. Baixar a altura máxima apara logo as pilhas e a intensidade nota-se no ar.
- Escolher outra imagem/vídeo (ou outro texto) num ajuste já reconfigura as entradas ao vivo.
- `--screenshot` sai com erro se o wallpaper não carregar (em vez de ficar à espera).

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
- Ajustes do utilizador por wallpaper em `%APPDATA%\OpenASCII\overrides\` (formato de tema).
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
- Log em `%APPDATA%\OpenASCII\log.txt` com rotação; `--verbose` também na consola.
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
[0.5.0]: https://github.com/victorkloh/openASCII/releases/tag/v0.5.0
