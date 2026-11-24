# Ideias para efeitos mais avançados

> Brainstorm para o futuro do OpenASCII, **ordenado do melhor para o pior**. O critério combina três coisas:
> **impacto** (quanto o público vai gostar e partilhar), **esforço** (dias de trabalho, risco técnico) e
> **encaixe** (quão bem cabe na arquitetura atual: compute shader por célula, canvas estendido, `prev()`,
> launcher). Cada ideia diz *como* se faria aqui, não só *o quê*.
>
> Legenda: impacto ★ (1 a 5) · esforço ● (1 = horas, 5 = semanas)

## Estado (v0.5.0)

As ideias com 2★ ou mais foram implementadas como **tech demos**: um wallpaper por ideia, comentado para
servir de exemplo a quem quiser criar os seus. Ver `docs/CREATING_WALLPAPERS.md`, secção 6.

| # | Estado | Onde |
|---|---|---|
| 1 | ✅ feito | `photo` (imagem, GIF, vídeo, o wallpaper do Windows) + "Escolher imagem ou vídeo" no launcher |
| 2 | ✅ feito | `snow` (neve que assenta nas janelas e cai quando as mexes) |
| 3 | ✅ feito | `spectrum` + tema `synthwave/beat` |
| 4 | ✅ cenas raymarched (`temple`) · ⏳ modelos `.obj` por fazer | `temple` |
| 5 | ⏳ por fazer | precisa de uma chave da API de IA e de um fluxo de revisão do código gerado |
| 6 | ✅ feito | `hud` (CPU, RAM, tempo real, sol) + tema `valley/weather`; cidade nas Definições |
| 7 | ✅ feito | `sandbox` (areia, água, pedra; pintar com o rato) |
| 8 | ✅ feito | `fluid` (24 passos por frame) e `render.substeps` para qualquer wallpaper |
| 9 | ✅ feito | secção `[post]` (glow, CRT, vinheta, aberração, grão) em qualquer wallpaper; ver `terminal`, `hud` |
| 10 | ⏳ por fazer | precisa de um servidor e de moderação |
| 11 | ✅ feito | `coral` (Gray-Scott) e `life` (Conway) |
| 12 | ✅ feito | `terminal` + `[inputs] text` (literal, `file:`, `folder:`, `lines:`) |
| 13 | ⏳ por fazer | backend Lua (Fase 4 da spec); hoje a lógica com estado faz-se com células reservadas (`arcade`, `bonsai`) |
| 14 | ✅ playlist (minutos, ordem aleatória) · ⏳ horários por hora do dia | Definições + "Incluir na playlist" |
| 15 | ⏳ por fazer | exige um programa/atlas por monitor (refatorar o render) |
| 16 | ✅ feito | `fractal` (Mandelbrot com zoom contínuo) |
| 17 | ✅ feito (planeta procedural) | `planet` |
| 18 | ✅ feito | `bonsai` |
| 19 | ✅ feito | `mirror` (só liga com autorização) |
| 20 | ✅ feito | tema `matrix/code` |
| 21 | ⏳ por fazer | captura de janelas (Windows.Graphics.Capture) |
| 22 | ✅ feito | `arcade` (Breakout que joga sozinho) |
| 23 | ✅ ícones como obstáculos (`[inputs] icons`, a neve assenta neles) | `snow` |
| 24 | ⏳ por fazer (há a fonte grande 5x7: `bigFontPixel`) | `hud`, `clock` |

## Resumo

| # | Ideia | Impacto | Esforço | Porquê nesta posição |
|---|---|---|---|---|
| 1 | Imagens, GIFs e vídeos teus → ASCII (com contornos) | ★★★★★ | ●●● | o pedido nº 1 de qualquer app de wallpaper: o *teu* conteúdo |
| 2 | Wallpaper que "vê" as janelas (chuva e neve em cima delas) | ★★★★★ | ●● | ninguém mais faz isto; o Wallpaper Engine não consegue |
| 3 | Reage à música (áudio do sistema) | ★★★★★ | ●●● | visualizadores estão entre os wallpapers mais usados |
| 4 | Biblioteca 3D: cenas raymarched + modelos .obj | ★★★★☆ | ●●● | "3D em ASCII" é hipnótico e partilhável |
| 5 | Wallpaper gerado por IA a partir de uma frase | ★★★★☆ | ●●● | efeito "uau"; transforma qualquer pessoa em criadora |
| 6 | Dados ao vivo: tempo real, sol, aurora, sistema | ★★★★☆ | ●● | o wallpaper passa a ser útil, não só bonito |
| 7 | Areia e água que caem (sandbox de partículas) | ★★★★☆ | ●● | brinquedo viciante, ótimo para vídeos curtos |
| 8 | Fluido a sério (fumo/tinta) + multi-pass | ★★★★☆ | ●●●● | o preset "fluid" da spec; desbloqueia muitos efeitos |
| 9 | Pós-processamento: brilho (bloom), CRT, glitch | ★★★☆☆ | ●● | melhora *todos* os wallpapers de uma vez |
| 10 | Galeria da comunidade dentro do launcher | ★★★★☆ | ●●● | crescimento: as pessoas partilham e descobrem |
| 11 | Reação-difusão, Lenia e autómatos orgânicos | ★★★☆☆ | ● | padrões vivos lindíssimos com poucas linhas |
| 12 | Texto vivo: citações, letras de música, manchetes | ★★★☆☆ | ●● | personalização sem programar |
| 13 | Backend Lua + bandos (boids), partículas, física | ★★★☆☆ | ●●●● | é a Fase 4 da spec; abre lógica com estado |
| 14 | Playlists e horários (muda com a hora/tempo) | ★★★☆☆ | ● | conveniência; "manhã calma, noite synthwave" |
| 15 | Wallpaper diferente por monitor | ★★★☆☆ | ●● | pedido clássico de multi-monitor (Fase 5 da spec) |
| 16 | Fractais com zoom infinito e atratores | ★★★☆☆ | ● | clássico de demoscene, muito bonito em ASCII |
| 17 | Globo terrestre com dia/noite e nuvens reais | ★★★☆☆ | ●● | "olhar pela janela da estação espacial" |
| 18 | Crescimento lento (bonsai, cidade, floresta em horas) | ★★★☆☆ | ●● | arte que muda ao longo do dia |
| 19 | Espelho de webcam em ASCII | ★★☆☆☆ | ●● | divertido, mas privacidade e CPU |
| 20 | Chuva de código a partir dos teus ficheiros | ★★☆☆☆ | ● | nicho, mas adorado por programadores |
| 21 | Captura de outra janela → ASCII (ex.: vídeo do YouTube) | ★★☆☆☆ | ●●● | impressionante, mas confuso como wallpaper |
| 22 | Mini-jogos no ambiente de trabalho | ★★☆☆☆ | ●●● | novidade, distrai do trabalho |
| 23 | Interagir com os ícones do ambiente de trabalho | ★★☆☆☆ | ●●●● | frágil: exige ler memória do Explorer |
| 24 | Fontes FIGlet / letras gigantes personalizadas | ★★☆☆☆ | ● | pequeno extra útil (relógio, nomes) |
| 25 | Cor que acompanha a app em foco | ★☆☆☆☆ | ● | subtil demais para valer o custo |
| 26 | Parallax com a cabeça (webcam) | ★☆☆☆☆ | ●●●● | caro e estranho no dia a dia |
| 27 | Tela partilhada com amigos (multiplayer) | ★☆☆☆☆ | ●●●●● | precisa de servidor, moderação |
| 28 | Integração com jogos (vida, munição) | ★☆☆☆☆ | ●●●● | cada jogo é um caso; pausamos em ecrã inteiro |

---

## 1. Imagens, GIFs e vídeos teus → ASCII ★★★★★ ●●●

**O quê.** Arrastas uma foto, um GIF ou um vídeo para o launcher e ele vira wallpaper ASCII. Com controlo de
rampa de caracteres, cores (originais, paleta, monocromático), contraste e um modo "contornos".

**Porquê funciona.** A conversão "luminância → caractere" sozinha dá aquele ASCII lavado. A técnica popularizada
pelo Acerola junta-lhe **deteção de contornos**: gradiente de Sobel para a *direção* e Difference-of-Gaussians para
a *força*; onde passa um contorno, a célula leva um caractere de linha orientado (`| / - \`). O resultado parece
desenhado à mão.

**Como aqui.**
- Imagem/GIF: descodificar com WIC (já usamos para as previews) para uma textura D3D11; um GIF são N texturas com
  tempos por frame.
- Vídeo: Media Foundation *Source Reader* com o mesmo dispositivo D3D11 (`MF_SOURCE_READER_D3D_MANAGER`), a
  descodificação acontece na GPU (NV12) e o frame já está numa textura. Pausa quando o wallpaper pausa.
- No shader: `sampleImage(uv)` no prelude; um pré-passe calcula luminância média por célula + Sobel + DoG numa
  textura pequena (uma por célula) e o `cell()` só escolhe o caractere.
- Escala: *cover*/*contain* por monitor; no modo estendido, uma imagem panorâmica atravessa os ecrãs.

**Riscos.** Vídeos 4K a 60 fps custam GPU: limitar a descodificação ao FPS do fundo, e por defeito 30.

## 2. Wallpaper que "vê" as janelas ★★★★★ ●●

**O quê.** As janelas abertas passam a fazer parte da cena: a chuva **bate no topo das janelas** e salpica, a neve
**acumula-se** em cima delas, os peixes do aquário nadam à volta, o fogo lambe as bordas, as estrelas do starfield
passam por trás.

**Porquê é a melhor ideia "nossa".** O Wallpaper Engine e o Lively desenham um vídeo/página atrás de tudo e não
sabem onde estão as janelas. Nós já sabemos: o `PauseManager` enumera as janelas a cada segundo para as pausas.

**Como aqui.** Passar até ~16 retângulos de janelas visíveis (em células do canvas) no constant buffer, com um
helper `obstacle(c)` / `windowTopAt(x)` no prelude. Atualizar ao mexer/redimensionar (eventos que já ouvimos).
A neve acumulada vive no `state` (com `prev()`), escorre quando a janela se mexe.

**Riscos.** Janelas maximizadas tapam tudo (mas aí o monitor pausa). Respeitar janelas *cloaked* (já tratamos).

## 3. Reage à música ★★★★★ ●●●

**O quê.** O fundo pulsa com o som do PC: barras de espectro ASCII, o sol do synthwave a bater com o grave, a
aurora a ondular com os médios, o ripple a largar uma gota a cada batida.

**Como aqui.** WASAPI em modo *loopback* captura a mistura do dispositivo de saída (sem drivers extra); FFT num
thread de trabalho (kissFFT, ~1 ms); passar `audio.spectrum[64]`, `audio.bass/mid/treble` e `audio.beat`
(deteção de batida simples por energia) no constant buffer. Opção por wallpaper: "reage ao som".

**Riscos.** Privacidade percebida (é só energia por frequência, nunca gravamos nada: dizê-lo claramente).
Algumas placas não suportam loopback. Silêncio = efeito em repouso, nunca parado.

## 4. Biblioteca 3D ★★★★☆ ●●●

**O quê.** Duas vias:
- **Cenas raymarched** (como o donut, mas fácil): o prelude ganha `sdSphere`, `sdBox`, `sdTorus`, `opUnion`,
  `opSmoothUnion`, uma câmara (`camera(pos, alvo)`), luz e sombra suave. Um "templo de colunas a girar" em 30 linhas.
- **Modelos .obj** que o utilizador escolhe: rasterizados pela GPU para uma textura de profundidade + normais e
  convertidos em ASCII com luz e contornos (mesma ideia do nº 1).

**Como aqui.** Raymarching já funciona (donut). Para .obj: um passe de rasterização D3D11 clássico (vertex +
pixel shader) para uma textura do tamanho do canvas em células × 2, antes do compute shader.

**Riscos.** Modelos pesados: reduzir triângulos ao carregar. Manter os FPS baixos por defeito.

## 5. Wallpaper gerado por IA ★★★★☆ ●●●

**O quê.** No launcher: "Descreve o teu wallpaper" → *"medusas a brilhar num mar escuro, fogem do rato"* →
aparece um wallpaper novo, editável.

**Como aqui.** Um modelo de linguagem gera o `wallpaper.toml` + `main.hlsl` a partir da API documentada em
`CREATING_WALLPAPERS.md`; compilamos e, se houver erros, devolvemos a mensagem do compilador para ele corrigir
(2-3 voltas). O hot reload mostra o resultado na hora.

**Riscos.** Precisa de chave de API e internet (opcional, nunca obrigatório). Rever o código gerado não é preciso:
um shader não tem acesso a ficheiros nem à rede, só à GPU.

## 6. Dados ao vivo ★★★★☆ ●●

**O quê.** O valley chove quando está a chover lá fora; o dia/noite segue o nascer e o pôr do sol reais da tua
cidade; a aurora fica mais forte quando o índice Kp sobe; um preset "sistema" mostra CPU/RAM/GPU em gráficos ASCII.

**Como aqui.** `world.weather` (chuva, nuvens, vento, temperatura), `world.sun` (elevação), via Open-Meteo (sem
chave), a cada 15 minutos, num thread. Localização escrita pelo utilizador (nunca automática sem pedir).

## 7. Areia e água que caem ★★★★☆ ●●

**O quê.** Uma caixa de areia no ambiente de trabalho: clicar larga areia, arrastar com o botão direito larga água,
o meio acende fogo que queima "madeira". A areia acumula no fundo de cada ecrã (e em cima das janelas, com o nº 2).

**Como aqui.** Autómato celular por blocos 2×2 (vizinhança de Margolus) no compute shader, que evita conflitos
entre threads; materiais no `state`. Já temos `prev()` e o canvas estendido.

## 8. Fluido a sério + multi-pass ★★★★☆ ●●●●

**O quê.** O preset "fluid" da spec: fumo/tinta que segue o rato, com redemoinhos.

**Como aqui.** "Stable Fluids" na GPU precisa de **vários passes por frame** (advecção, divergência, ~20 iterações
de pressão, projeção). Infraestrutura nova: `passes = [...]` no manifest, cada um com o seu shader e buffers.
Isto também serve o nº 1, o nº 4, o nº 9 e o nº 11.

## 9. Pós-processamento ★★★☆☆ ●●

**O quê.** Opções globais: brilho/bloom à volta dos caracteres claros (opcional na spec), "monitor CRT" (linhas
de varrimento, curvatura, fósforo), glitch ocasional, desfoque de fundo atrás dos caracteres.

**Como aqui.** Um passe extra depois do `grid.hlsl`, por monitor: bloom barato com 2 downsamples. Desligado por
defeito, como pede a spec.

## 10. Galeria da comunidade ★★★★☆ ●●●

**O quê.** Separador "Descobrir" no launcher: wallpapers de outras pessoas, com preview animada, instalar com um
clique, avaliar.

**Como aqui.** Um índice JSON num repositório GitHub (sem servidor nosso); cada wallpaper é um zip. Verificação:
só `.toml`, `.hlsl`, `.png`, `.ttf` (nada executável). Instalar = extrair para `%APPDATA%\OpenASCII\wallpapers`.

## 11. Reação-difusão, Lenia, autómatos ★★★☆☆ ●

**O quê.** Padrões que parecem coral, pele de animal, células a dividir-se. O Game of Life da spec, mas também
Gray-Scott e Lenia (a versão contínua do Life, com "criaturas" que nadam).

**Como aqui.** Cabem já hoje no backend shader com `prev()` (o Life é uma dúzia de linhas). Gray-Scott quer 4-8
iterações por frame: fica melhor depois do multi-pass (nº 8).

## 12. Texto vivo ★★★☆☆ ●●

**O quê.** Citações do dia, letra da música que está a tocar, manchetes RSS, a tua lista de tarefas: escritas em
ASCII com transições (máquina de escrever, decifrar "Matrix", dissolver).

**Como aqui.** Glifos já temos (`glyphs` + atlas). Faltam: texto dinâmico passado à GPU (um buffer de caracteres)
e fontes de dados (SMTC do Windows para "a tocar agora", RSS).

## 13. Backend Lua (Fase 4) ★★★☆☆ ●●●●

A spec prevê-o: lógica com estado (`frame`, `on_click`...) e *natives* em C++ (fluid, particles, life). Ótimo para
**bandos** (boids: pássaros e peixes com comportamento de grupo a sério), física de partículas, jogos simples.
Esforço grande (sandbox, orçamento por frame, API `g:*`), mas é a fundação de muitos efeitos com estado complexo.

## 14. Playlists e horários ★★★☆☆ ●

Rodar wallpapers a cada N minutos, por hora do dia ("07:00 aurora, 20:00 synthwave") ou pelo tempo real (nº 6).
Simples: uma lista na config e um timer (sem custo em pausa).

## 15. Wallpaper diferente por monitor ★★★☆☆ ●●

O canvas já suporta "um por monitor" (`span_monitors = false`); falta permitir um wallpaper e ajustes diferentes
em cada canvas, e a UI no launcher (escolher o monitor). É o que falta da Fase 5.

## 16. Fractais e atratores ★★★☆☆ ●

Mandelbrot/Julia com zoom infinito lento (precisão dupla nos shaders onde houver), o rato escolhe o ponto do
zoom; atrator de Lorenz desenhado como rasto. Muito bonito em ASCII e barato de fazer.

## 17. Globo terrestre ★★★☆☆ ●●

A Terra em ASCII a rodar devagar, com a linha dia/noite real (calcula-se da hora UTC), cidades acesas à noite,
nuvens (procedurais ou dados reais), a ISS a passar. Precisa de um mapa de continentes pequeno embebido.

## 18. Crescimento lento ★★★☆☆ ●●

Arte que evolui ao longo de horas: um bonsai que cresce durante o dia de trabalho, uma cidade que se constrói, uma
floresta que muda de estação com a data. O estado guarda-se em disco para continuar no dia seguinte.

## 19. Espelho de webcam ★★☆☆☆ ●●

A tua imagem em ASCII ao vivo (Media Foundation, como o nº 1). Divertido 5 minutos; a luz da webcam ligada o dia
todo assusta. Só como modo explícito e temporário.

## 20. Chuva de código dos teus ficheiros ★★☆☆☆ ●

Variante do matrix que mostra linhas reais de uma pasta escolhida (os teus projetos). Nicho, mas programadores
adoram. Fácil: ler texto num thread e passá-lo como buffer de caracteres (nº 12).

## 21. Outra janela → ASCII ★★☆☆☆ ●●●

Windows.Graphics.Capture para apanhar uma janela (um vídeo, um jogo) e mostrá-la convertida no fundo.
Impressionante como demo, mas confuso como wallpaper, e há questões de privacidade (pedir sempre).

## 22. Mini-jogos ★★☆☆☆ ●●●

Snake com o rato, "apanha as estrelas", pong contra o cursor. Novidade divertida, mas um wallpaper deve ser calmo;
melhor como easter egg.

## 23. Interagir com os ícones ★★☆☆☆ ●●●●

A água contorna os ícones, eles tremem ao clicar. Ler as posições dos ícones exige pedir memória dentro do
processo do Explorer (`LVM_GETITEMPOSITION` com `VirtualAllocEx`): frágil e com cheiro a hack. Fora de questão
sem uma API oficial.

## 24. Fontes FIGlet ★★☆☆☆ ●

Letras gigantes feitas de caracteres (o formato FIGlet tem centenas de fontes livres) para o relógio, nomes,
mensagens. Pequeno extra que combina com o nº 12.

## 25. Cor da app em foco ★☆☆☆☆ ●

Tingir o wallpaper com a cor dominante da janela ativa. Subtil demais; o destaque da UI já faz o inverso.

## 26. Parallax com a cabeça ★☆☆☆☆ ●●●●

Seguir a cabeça pela webcam para um efeito "janela 3D". Caro, impreciso e estranho no dia a dia.

## 27. Tela partilhada ★☆☆☆☆ ●●●●●

Desenhar no wallpaper e ver os desenhos dos amigos. Precisa de servidor, contas, moderação: não combina com
"um .exe pequeno sem dependências".

## 28. Integração com jogos ★☆☆☆☆ ●●●●

Mostrar a vida ou a munição de um jogo no fundo. Cada jogo é um caso diferente e, em ecrã inteiro, nós pausamos.

---

## Ordem sugerida de implementação

1. **Infraestrutura multi-pass + textura de entrada** (nº 8 e nº 1): desbloqueia imagens, vídeo, 3D .obj, fluido,
   reação-difusão e pós-processamento.
2. **Imagens e GIFs → ASCII com contornos** (nº 1, sem vídeo ainda), o maior impacto por dia de trabalho.
3. **Janelas como obstáculos** (nº 2), a característica única do OpenASCII.
4. **Áudio** (nº 3) e **dados ao vivo** (nº 6), que dão vida a todos os presets existentes.
5. **Vídeo** (resto do nº 1), **3D .obj** (nº 4) e **galeria** (nº 10).

## Fontes consultadas

- Técnica de ASCII com contornos (Sobel + DoG, caracteres direcionais), descrita em
  [marblestack — intro](https://marblestack.github.io/marblestack/posts/b_ascii/a_intro) e no
  [p5js-edge-detection-ascii-renderer](https://github.com/humanbydefinition/p5js-edge-detection-ascii-renderer);
  shader inspirado no Acerola: [ASCII-like shader (Modrinth)](https://modrinth.com/shader/K5CUBBea).
- Renderizadores 3D em ASCII: [terminal-raymarching](https://github.com/R3X-G1L6AME5H/terminal-raymarching),
  [rasterm](https://github.com/jabberwock/rasterm), [voxcii](https://codeberg.org/ash1sh/voxcii),
  [ASCII 3D Renderer (U. Toronto)](https://www.dgp.toronto.edu/~joonho/courses/csc317-2025-09/showcase/62).
- Áudio por WASAPI loopback: [spectrumcpp](https://awesome.ecosyste.ms/projects/github.com%2Fdiqezit%2Fspectrumcpp),
  [sysAudioSpectrogram](https://github.com/Harpseal/sysAudioSpectrogram),
  [lively-audio-visualizer](https://www.github.com/elias123tre/lively-audio-visualizer).
- Vídeo/webcam para ASCII: [Webcam-to-ASCII (Media Foundation)](https://github.com/XorcodeYt/Webcam-to-ASCII),
  [PDASC](https://pypi.org/project/PDASC/), [tplay](https://docs.rs/crate/tplay/0.6.3).
- Popularidade de temas de wallpaper animado: [Make Tech Easier](https://maketecheasier.com/best-wallpaper-engine-wallpapers),
  [Prima Games](https://primagames.com/gaming/best-animated-wallpapers), [PC Gamer](https://www.pcgamer.com/best-wallpaper-engine-wallpapers).
