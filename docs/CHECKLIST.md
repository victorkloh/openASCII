# Checklist dos pedidos

Releitura de todos os pedidos da conversa de desenvolvimento, com o estado de cada um (v0.4.0, 2026-11-20).

Legenda: ✅ feito e verificado · 🟡 feito, falta verificação manual ou tem limitação · ⏳ por fazer

## 1. "Leia o ficheiro MD, fale comigo antes de prosseguir"
- ✅ `PROJECT.md` lido; resumo, pontos de atenção (RAM com drivers, `/external:W0`, `/MT` + vcpkg, flip model) e perguntas antes de começar.

## 2. "Documente o projeto num repositório, instale as dependências e lance a primeira release"
- ✅ Repositório git local, commits pequenos por passo lógico, tags `v0.1.0` → `v0.4.0`.
- ✅ Dependências instaladas sem admin em `%USERPROFILE%\dev-tools` (CMake, Ninja, vcpkg, GitHub CLI); `scripts/setup.ps1` reproduz.
- ✅ Documentação: `README.md`, `docs/ARCHITECTURE.md`, `docs/CREATING_WALLPAPERS.md`, `CHANGELOG.md`, `LICENSE` (MIT).
- ✅ Releases locais: `dist/AsciiPaper-<versão>-win64.zip` + SHA-256 (`scripts/package.ps1`).
- ⏳ Publicar no GitHub: precisa de `gh auth login` e de decidires se o repositório é público ou privado (pediste para ficar local por agora).
- ⏳ clang-cl: os presets existem, mas o componente LLVM do Visual Studio não está instalado (pede admin).

## 3. "Por enquanto local, como correr a app?" / "E os efeitos?"
- ✅ Instruções dadas; efeitos chegaram na Fase 3.

## 4. "Avança para a fase 2 e 3"
- ✅ Fase 2: atlas DirectWrite por DPI, cell buffer, `grid.hlsl`, blocos/caixa/Braille procedurais sem costuras, `--windowed`, `--bench`.
- ✅ Fase 3: rato, DesktopHitTest, `prelude.hlsl`, compute pass, `prev()`, params, manifest TOML, hot reload, erros no ecrã; `ripple`, `matrix`, `terrain`, `starfield`.
- 🟡 Critério "texto nítido com células de 6 px a 200%": o atlas é rasterizado para o DPI real, mas nenhum monitor desta máquina está a 200%.
- 🟡 Checklist manual da secção 9 da spec (mudar escala/resolução, ligar/desligar monitor, reiniciar o Explorer, Win+L, suspender): por correr por ti.

## 5. "Execute para eu dar uma olhada"
- ✅ A app foi aberta no ambiente de trabalho (e de novo agora, na v0.4.0).

## 6. Ripple sincronizado, terrain novo, menu da bandeja, opções por wallpaper, launcher, design
- ✅ **Efeito estendido entre monitores**: um só canvas para todos os ecrãs (500×161 células nesta máquina); a onda passa de um monitor para o outro; zonas fora dos monitores são paredes. Opção nas Definições.
- ✅ **Terrain vivo (valley)**: chuva, nuvens com sombra, animais (fogem do rato), pássaros, peixes, florestas, aldeias que acendem à noite, ciclo dia/noite, relâmpagos ao clicar.
- ✅ **Menu da bandeja próprio** (não o menu do Windows): miniaturas, variantes, velocidade, resolução, FPS, pausar, personalizar, sair.
- ✅ **Opções por wallpaper**: cores (seletor HSV), velocidades e parâmetros próprios, resolução da grelha, fonte, FPS, caracteres.
- ✅ **Resolução vs escala**: mudar a resolução da grelha não muda o tamanho do efeito (coordenadas de mundo); verificado com o tema `miniature`.
- ✅ **Launcher** bonito e minimalista: alterar, criar (a partir de modelo) e editar (abre o código; hot reload), guardar como tema, definições.
- ✅ **Skill de design** usada (apple-design): tokens de cor com contraste ≥ 4,5:1, tipografia, controlos ≥ 28 px, destaque tirado do wallpaper ativo, tema claro/escuro do Windows.
- 🟡 Interação real com o menu e o launcher: verificada por capturas fora do ecrã; falta o teu uso com o rato.

## 7. "Brainstorm de efeitos atrativos, faça, e crie a release"
- ✅ Pesquisa (temas populares de wallpapers animados, clássicos do terminal) e brainstorm.
- ✅ 8 efeitos novos com variante: `synthwave`, `aurora`, `rain`, `aquarium`, `clock`, `fire`, `fireworks`, `donut`.
- ✅ Release v0.4.0 (local; ver ponto 2 para o GitHub).

## 8. "A app trava ao passar o rato entre uma janela e o fundo; opção de FPS; benchmark e causas"
- ✅ **Benchmark e causa encontrada**: o Raw Input entregava uma mensagem por cada movimento do rato (≈370/s no teste, mais com ratos reais), numa fila de input ligada à do Explorer.
- ✅ **Corrigido**: rato lido uma vez por frame, sem mensagens. Medido com o rato a mexer: 500 → 31 despertares/s, 370 → 1 mensagem/s, 0 engasgos (antes: 5, até 244 ms).
- ✅ **FPS do fundo**: automático ou 15–144, no menu da bandeja e nas Definições.
- ✅ Diagnóstico permanente: `--bench` com GPU, Present, engasgos e despertares; `--verbose` regista a cada 5 s e avisa mensagens lentas.
- ✅ Otimizações extra de GPU (valley 0,30 → 0,19 ms por frame; donut).

## 9. "Só seguir o rato quando está no fundo; transição suave quando perde o rato"
- ✅ O rato só conta sobre o ambiente de trabalho (sobre janelas é ignorado e não custa nada).
- ✅ `mouse.presence`: entra em ~0,1 s, sai em ~0,5 s; todos os presets voltam suavemente à origem.

## 10. "Desativar o Wallpaper Engine enquanto a app corre, com confirmação; ao fechar, reabrir (com confirmação), sem perder definições"
- ✅ Deteção de Wallpaper Engine e Lively ao arrancar; pergunta com "lembrar a escolha".
- ✅ Pausa/retoma com os comandos oficiais deles (Wallpaper Engine `-control stop`/`play`; Lively `app --play false/true`): as definições e os wallpapers deles não mudam.
- ✅ Ao sair pela bandeja, pergunta se retoma; sem diálogo (taskkill, encerramento) retoma por defeito.
- ✅ Opções nas Definições (ao arrancar: perguntar/pausar/ignorar; ao sair: perguntar/retomar/deixar).
- 🟡 O Wallpaper Engine está a correr nesta máquina e a pergunta apareceu ao abrir a v0.4.0; o efeito real dos comandos (pausar/retomar) fica por confirmar contigo, porque não carreguei nos botões por ti.

## 11. "Brainstorm de efeitos avançados (3D, vídeo, imagens) em ideias.md, ordenado, com variedade"
- ✅ `ideias.md`: 28 ideias ordenadas com impacto, esforço, como fazer aqui, riscos e fontes; ordem de implementação sugerida.

## 12. "Releia os prompts e faça uma checklist"
- ✅ Este ficheiro.

## O que fica para depois (da spec original)
- ⏳ Fase 4: backend Lua + natives (`fluid`, `particles`, `life`).
- ⏳ Fase 5: wallpaper diferente por monitor; guia passo a passo no README já existe em `docs/CREATING_WALLPAPERS.md`.
- ⏳ Glow/bloom opcional; roda do rato (`mouse.scroll`) deixou de ser lida com a mudança para amostragem.
