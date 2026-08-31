# Fork history — releases and issue ledger

The dev journal's long-term memory (CODING_STANDARD §12): the release
timeline with what shipped, and every issue with how it was resolved (or
where it stands). Full release notes live in [CHANGELOG.md](../../CHANGELOG.md);
day-by-day detail lives in the monthly journal files. Update this ledger
whenever an issue opens or closes.

## Release timeline (fork)

Upstream (matbo87) history ends at v1.61 — see CHANGELOG.md. The fork's own line:

* **Stable 2026-08-13 (efb1096)** — Scene Profiles (PPU-signature capture,
  auto-switch with hysteresis + lerp, #23); Edge Cleanup Off/Trim/Zoom
  (#11); focus zone + Fade/Haze/Blur refinements (#8, #25); ghost blur vs
  color-math fix (dest alpha is the color-math mask); right-eye filter and
  Enhanced Resolution fixes (#9).
* **Stable 2026-08-14 (a9813db)** — ZIP ROM loading (#5); MSU-1 FLAC
  (#4); MSU packs as virtual entries + background dir refresh (#6, blue
  entries #29); 3D Tools block (#30–#33).
* **Stable v2.0 (2026-08-31, 21481ff)** — the nightly line promoted:
  rewind v2 (minutes of history via page deltas #37, hold-to-rewind +
  timeline, REWIND menu section, Enabled/Disabled frees the ring #39);
  MSU-1 audio read-ahead thread (#55) and the Old-3DS speed regression
  double-fix (#54); 3DS Mode (#16); lid-sleep seamless resume (#45);
  **self-updater** (#64) — first OTA in the fork's history.
* **Nightlies 2026-08-31 (f790cb6 → 0bad928)** — the 3D block: whole-pixel
  shifts + Slider Response option (#65); per-priority BG depth, shader-side
  (#60 A+B); the 3D Stereo tab + live editor: spotlight, real-time gauges,
  Y peek, pause-caption restore (#61); sprites split into four priorities
  (#60). Pending Jorge's hardware validation to close #60/#61/#65.

## Issue ledger

| # | Título (curto) | Estado | Resolução / situação |
|---|---|---|---|
| 1 | KI Arcade MSU-1 lento em hardware | ABERTA | Plano: probe caça-loops (histograma de PC nas duas CPUs), junto com #57 e #63 round 2 |
| 2 | Super Road Blaster: flicker residual de FMV | ABERTA | Wave 2 do MSU-1 corrigiu bit swap e gaps de áudio; os flickers de transição ficaram |
| 3 | Auditoria do Makefile (classe stale-object) | ABERTA | Fix pontual dos .d de subdiretórios já entrou (2f8736f); auditoria completa pendente |
| 4 | MSU-1 FLAC | FECHADA | Fallback `-N.flac` + tag `MSU1_LOOPPOINT`, decode em tempo real (Stable 2026-08-14) |
| 5 | ROMs em .zip | FECHADA | Loader descompacta o primeiro ROM; saves keyed pelo basename do zip (Stable 2026-08-14) |
| 6 | Cache de diretório esconde pastas novas | FECHADA | Refresh em background + pastas MSU viram entrada virtual única (Stable 2026-08-14) |
| 7 | Wallpaper deveria ficar no plano da tela | FECHADA | Fundo desenhado com depth 0 fixo (sem ±IOD) |
| 8 | Fade/Haze/Blur por camada | FECHADA | Efeitos atmosféricos relativos à zona de foco (Stable 2026-08-13) |
| 9 | 3D quebrava ao alternar Enhanced Resolution | FECHADA | SNES_MAIN_RIGHT faltava no dirty clear do render2x |
| 10 | Linha extra no menu (BG Opacity) | FECHADA | Espaçamento corrigido |
| 11 | Artefatos de borda do parallax | FECHADA | Edge Cleanup Off/Trim/Zoom; depois virou game-global (nightly 590d8d1) |
| 12 | Rewind | FECHADA | v1: snapshots (24s New/4s Old); v2 via #37 chegou a minutos |
| 13 | Fast forward | FECHADA | Toggle + hold hotkey |
| 14 | Macros de toque na segunda tela | ABERTA | Backlog |
| 15 | Revisitar defaults do 3D | FECHADA | Defaults ajustados (BG2=-2, BG3=-1) |
| 16 | Controle de overclock (New 3DS) | FECHADA | 3DS Mode: New 804MHz+L2 / Old 268MHz preview |
| 17 | Linhas extras na aba Controls | FECHADA | Corrigido |
| 18 | Verificar efeitos por camada | FECHADA | Auditoria confirmou corretos (por camada de fato) |
| 19 | Troca de porta P1/P2 | FECHADA | Swap no menu + notif |
| 20 | Mensagem de pause muito à frente | FECHADA | Overlay a 1 camada do maior pop-out (não IOD máximo) |
| 21 | Efeito 3D em degraus | FECHADA | Física do parallax: d+1 níveis (2d+1 com Enhanced 2x); documentado; #65 refinou |
| 22 | Força do 3D: níveis ou range maior | FECHADA | Escalas >1.0 dão artefatos em hardware; mantido 1.0 (constante de tuning fica) |
| 23 | Perfis 3D por cena | FECHADA | Assinatura PPU + word de VRAM + byte WATCH; captura com máscara aprendida (Stable 2026-08-13) |
| 24 | Crédito Tyler Sanders | FECHADA | Creditado no README |
| 25 | Depth Blur perdia força com 3D | FECHADA | Ghost passes com blending próprio (dest alpha preservado = máscara do color math) |
| 26 | Hacks widescreen | ABERTA | Backlog |
| 27 | Multiplayer local (2 consoles) | ABERTA | Backlog |
| 28 | FPS cap de FMV | FECHADA | Documentação: pacing pula só render/GPU; custo de emulação+streaming é irredutível |
| 29 | Packs MSU em azul + zip dentro do pack | FECHADA | Stable 2026-08-14 |
| 30 | Tools: Set as Global Default | FECHADA | Escreve default.3d (só o look global) |
| 31 | Tools: Copy 3D Settings From | FECHADA | Importa o look; perfis/WATCH ficam |
| 32 | Tools: Backup/Restore 3D | FECHADA | Snapshot oculto em .stereo3d-bak |
| 33 | Tools: diagnóstico do matcher | FECHADA | "Scene Matcher Info" ao vivo |
| 34 | Re-captura guiada de telas | ABERTA | Backlog |
| 35 | Overclock do SNES emulado | ABERTA | Backlog |
| 36 | Rewind v2: input-replay + greenzone | FECHADA | Experimento de determinismo passou (A=B=C), mas a rota de page-deltas (#37) entregou o objetivo; input-replay arquivado |
| 37 | Rewind: deltas de página | FECHADA | Deltas backward contra keyframes: ~1.5min New / ~1min Old na mesma memória |
| 38 | Cheat: prettify calculado e descartado | ABERTA | Bug menor de UI |
| 39 | Ring do rewind nunca liberado | FECHADA | rewind3dsFinalize + setting Enabled/Disabled (libera 24MB) |
| 40 | Ring de prefetch MSU nunca liberado | FECHADA | Liberado no msu3dsNdspUninstall |
| 41 | Pares copy-paste divergindo | ABERTA | Refactor pendente |
| 42 | Timeline: filmstrip auto-regenerável | ABERTA | Parcial: visual pass entregue; self-healing/materialize-on-A pendentes |
| 43 | Diálogo independente do menu | ABERTA | Refactor de UI (backdrop como parâmetro) |
| 44 | Framework de telas | ABERTA | Aguarda o 3º consumidor (YAGNI) |
| 45 | Old 3DS: tela escura pós-tampa | FECHADA | Resume seamless no APT sleep/wake (c949cf9) |
| 46 | msu2flac | FECHADA | Ferramenta de conversão de packs entregue |
| 47 | rom2zip | FECHADA | Ferramenta de compactação entregue |
| 48 | Auto updater (proposta original) | FECHADA | Duplicata: superada pela #64 entregue |
| 49 | Compactador unificado distribuível | ABERTA | msu2flac + rom2zip num pacote para usuários |
| 50 | Capture Patience default 8s no Old | FECHADA | Default ajustado |
| 51 | MMX3: ~10s de queda ao voltar do menu | FECHADA | Rodada de áudio #54/#55 (budget do syscore + capturas lock-free) |
| 52 | Áudio fora de ritmo (MMX3 sem MSU) | FECHADA | Mesma rodada: budget do syscore por jogo (30%/45% só com MSU-1) |
| 53 | Micro-stutter no loop FLAC | FECHADA | Read-ahead (#55): loop seek saiu do hot path |
| 54 | Regressão de velocidade no Old (SMK) | FECHADA | Duas causas por profiling: reuse de vértices M7 restaurado + budget do syscore por jogo |
| 55 | Calibrar syscore/decode MSU | FECHADA | Thread produtora com ring ~2s; mixer só copia; capturas sem lock |
| 56 | Zelda ALTTP MSU: FMV corrompido | ABERTA | Diagnóstico completo: HDMA ch6/7 → $2126/$2128 (letterbox por window) não aplicado no path de FMV; NÃO é tearing de DMA. Correção pendente (meio período) |
| 57 | DBZ Hyper Dimension lento (SA-1) | ABERTA | Speedhacks aplicam ("SKIPPED bytes differ" era alarme falso); falta idle-loop — probe caça-loops planejado |
| 58 | Edge Cleanup por cena com "Global" | ABERTA | Parcialmente absorvida: edge virou game-global; revisar se o por-cena ainda é desejado |
| 59 | Stuttering progressivo no Old | ABERTA | NÃO é memory leak; 3 causas: log com fflush por linha (+storms de [sig]), capturas de 20ms, autosave de 225-259ms. Fixes propostos |
| 60 | Profundidade por PRIORIDADE (rcmz) | ABERTA | ENTREGUE nas nightlies: cascata de 4 tiers no shader, BGs P0/P1 + sprites Prio 0-3, zero draws extras; aguarda validação em hardware p/ fechar |
| 61 | Editor 3D ao vivo (rcmz, harmônico) | ABERTA | ENTREGUE: aba 3D Stereo, spotlight por prioridade, gauges em tempo real, Y peek, caption restaurada; aguarda validação p/ fechar |
| 62 | Perspectiva real no Mode 7 | ABERTA | Planejada (depth por scanline via matriz; disclaimer Old; integrar crop/stretch) |
| 63 | SuperFX +10-20% | ABERTA | Round 1 merged (série Wyatt-James, polígonos do Star Fox ok); "melhorou mas ainda lento" → round 2 via caça-loops |
| 64 | Updater integrado | FECHADA | Entregue e validado em hardware (primeira OTA do fork): curl+mbedtls, worker thread, canais isolados, janela de 10min no auto-check |
| 65 | Deslocamento fracionário racha camadas | ABERTA | Causa-raiz: slider contínuo × arredondamento por seção; fix: roundf por camada + edge crop coerente + opção Discrete/Continuous; aguarda validação p/ fechar |
| 66 | Update falha com jogo carregado | ABERTA | Proposta na issue: savestate temporário no SD → liberar ROM → update → reload no fim/erro |
