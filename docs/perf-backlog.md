# Backlog de performance: estratégia por item

> **Estado em 2026-11-05 (noite):** feitos A1, A2, B2, B3, B4, C3, D1 e a
> parte barata de C2 (move do vetor de itens); B1 tem a medição no lugar
> (`flush=`/`gpuwait=` na linha de perf), falta o flush parcial depois do
> número do console. Ficam A3 (só medir), B1, B5 (não vale o risco de
> trava), C1 (a tela inferior é double-buffered: redraw parcial precisa de
> dois frames ou flicker) e o resto de C2 (reconstrução das abas). O texto
> abaixo é o plano original, mantido como registro do raciocínio.

> **2026-11-13:** a seção E levanta as oportunidades do core para o Old
> 3DS (nada medido ainda; começa pelo E0).

Levantamento de 2026-11-05 (quatro auditorias de leitura: menu, core,
renderização, plataforma). O que já entrou está no CHANGELOG (Unreleased) e
no journal do mesmo dia. Este documento cobre o que FALTA, com a estratégia
de correção, como validar e o que pode dar errado. Ordem: por ganho esperado
dentro de cada grupo. Os números de ganho são estimativas de leitura de
código, não medições; a linha `[perf][frame]` do console (campo
`core=`/`ppu=`/`draw=`) é a régua final para todos.

Regra geral: um item por commit, medido antes e depois com a mesma ROM e o
mesmo savestate, e as cenas douradas do Azahar (`tools/azahar-validate`)
rodadas sempre que a mudança toca a renderização.

---

## A. Core da emulação (63-77% do frame)

### A1. CPU_SHUTDOWN por jogo: pular os loops de espera por NMI

**Situação.** `CPU_SHUTDOWN` não é definido em lugar nenhum. Os restos do
mecanismo do Snes9x 1.43 estão em `getset.h` (WaitAddress), `ppu.cpp`
(leitura de `$4212` grava `WaitAddress`), `cpuexec.cpp:364` e nos 23
`#ifdef` de `cpuexec-ops.h`; `BranchCheck0/1()` são macros vazias. Os
speedhacks `Op42` (`cpuexec-ops.h:7222`, `memmap.cpp:3888-4030`) cobrem
cerca de 22 títulos patcheados à mão, e MMX3 não está entre eles. Todo jogo
que espera o vblank num `LDA $4210 / BPL` executa esse loop instrução por
instrução.

**Estratégia.**
1. Reativar a detecção na ramificação, não no `#ifdef` antigo: nos opcodes
   de branch tomado, comparar `CPU_PC` com `CPU.WaitAddress`; se igual e
   `CPU.WaitCounter` passou de 1, avançar `CPU_Cycles` para `CPU.NextEvent`
   (exatamente o que `Op42` já faz em `cpuexec-ops.h:7283-7290`).
2. `WaitAddress` é gravado pelas leituras de `$4210`, `$4212`, `$2140-$2143`
   e dos contadores H/V, e limpado por qualquer escrita de memória
   (`CpuSetByte`). Sem a limpeza na escrita, um loop que altera RAM vira
   falso positivo.
3. Toggle por jogo no `.cfg` (`IDLELOOP=0/1`), default OFF na primeira
   versão; picker "Idle Loop Skip" na aba Emulator. Default ON só depois de
   uma rodada de campo.
4. Não tocar o caminho SA-1 (`S9xMainLoopWithSA1`) nesta etapa.

**Validação.** Azahar: todas as cenas douradas (bit a bit); as cenas de MSU-1
(o handshake de áudio é o primeiro a quebrar). Console: `core=` da linha de
perf em MMX3, Chrono Trigger (IRQ pesado), Super Mario World, e um jogo com
APU em lockstep (Earthworm Jim). Teste de host: um loop sintético que lê
`$4212` e ramifica, confirmando que `CPU_Cycles` salta para `NextEvent` e
que uma escrita de RAM dentro do loop desarma o salto.

**Ganho / risco.** 10-30% do core em jogos que esperam o NMI; zero em jogos
que ocupam o frame inteiro. Risco médio: efeitos sincronizados por IRQ e o
handshake com a APU podem desincronizar, por isso é por jogo e começa OFF.
Esforço médio (branches + hooks de leitura + limpeza na escrita + menu).

### A2. SPC700 idle: dormir enquanto o driver faz poll do timer

**Situação.** `APUShutdown()` é macro vazia (`spc700.cpp:70`),
`apu_get_reg` não grava `WaitAddress` (`apumem.h:26-48`) e
`S9xUpdateAPUTimer` roda o SPC até `CPU.Cycles` sempre (`apu.cpp:943-1001`).
Cerca de 6 mil opcodes de SPC por frame, boa parte lendo `$FD`/`$F4` em
loop.

**Estratégia.**
1. Dois endereços de espera (como no 1.43): a leitura de `$F4-$F7` e de
   `$FD-$FF` grava `IAPU.WaitAddress1/2 = IAPU.PC`; a escrita em RAM da APU
   limpa os dois.
2. No dispatch (`apu.cpp:961`), se `IAPU.PC` bate com um dos dois e nenhum
   tick de timer está pendente, avançar `APU.Cycles` para
   `nextEventCycles` em vez de executar.
3. Manter os casos especiais já existentes (Terranigma, Clock Tower em
   `apu.cpp:812-840`), que leem de volta a porta.
4. Mesmo toggle por jogo de A1 (`IDLELOOP` cobre CPU e APU), ou um segundo
   bit se um jogo precisar de só um dos dois.

**Validação.** Como A1, com ênfase nas cenas de MSU-1 e em jogos com música
de streaming pela APU (Tales of Phantasia, Star Ocean). Áudio comparado por
captura do Azahar (o harness já grava WAV nos probes de MSU-1).

**Ganho / risco.** 2-5% do frame. Risco médio (o 1.43 tinha relatos de
dessincronia de áudio). Esforço médio.

### A3. Experimento -O2 vs -O3 nos arquivos do core

**Decidido (2026-11-10):** o Jorge rodou as builds -O2 no console e ficaram
melhores; `-O2` virou o padrão do Makefile (tudo, não só o core). Ele não
disse qual das duas variantes nem mandou números: falta um log com `core=`
para registrar o ganho. Próxima comparação: `make 3dsx CORE_OPT=-Os`
(`output/snes9x_3ds_Oscore.3dsx`). Azahar SBS em três cenas: bit-idêntico
à base em -O3.

**`-Os` no core (2026-11-12): quebra.** O GCC 16.1 em `-Os` perde a escrita no
registrador global do PC (r9, `fastCPUPC`) nos opcodes de desvio de
cpuexec.cpp; o console dá data abort no primeiro desvio tomado (dump em
#80, log de sessão por console). Não medido. Para tentar de novo: `-Os` em
tudo do core menos cpuexec.cpp, ou PC fora do registrador global.

**Builds prontas (2026-11-07):** `make 3dsx OPT_FLAGS="-g -O2"` (tudo em
-O2) e `make 3dsx CORE_OPT=-O2` (só `source/Snes9x`). Comparar `core=` e
`ppu=` da linha `[perf][frame]` no console, mesma ROM e savestate.

**Situação.** O ARM11 tem 16 KB de I-cache e, no Old mode, sem L2. `-O3`
desenrola e inlina agressivamente `cpuexec`/`ppu`/`gfxhw`.

**Estratégia.** Nada de decidir por teoria: um alvo `make release
RELEASE_OPT_FLAGS="-O2"` e uma variante com `-O2` só em `source/Snes9x/*.cpp`
(regra por diretório no Makefile). Três builds, mesma ROM e savestate,
`[perf][frame]` por 300 frames cada, comparar `core=` e `ppu=`. Fica o que
medir menor. Sem mudança funcional.

**Ganho / risco.** Desconhecido (pode ser negativo). Esforço pequeno para
medir, pequeno para manter.

---

## B. Renderização (ppu ≈ 1,5 ms + draw ≈ 1,0 ms por frame)

### B1. Flush parcial do linear heap no fim do frame

**Feito (2026-11-07).** `gpu3dsFrameEnd` passa `GX_CMDLIST_FLUSH` e flusha
só o que o frame escreveu: cada `SVertexList` (`from + count` vértices da
metade atual, também antes de trocar de metade), o IBO das camadas
(`iboUsed`), o scratch das sombras no ponto de escrita, o ring do glow e
as linhas tocadas dos dois caches de tile (`tileCacheDirtyLo/Hi`,
`mode7CacheDirty`). `-DGPU_FULL_FLUSH` volta ao flush total. Medido no
console pelo `flush=` da linha de perf: antes 264-275 µs (New), 1144 µs
(Old).

**Seguimento (2026-11-12).** No modo Old o parcial ficou em 740 µs: o
custo é por chamada (uns 40 µs), não por byte. `flush_span.h` e as marcas
de escrita em `gpu3dsFlushFrameWrites` descarregam cada buffer uma vez, só
o que um desenho lê, e nada num frame sem desenho: de 21 chamadas por
frame desenhado para 7 a 13 (`fcalls=` na linha de perf).

**Situação.** `gpu3dsFrameEnd()` chama `C3D_FrameEnd(0)`
(`3dsimpl.cpp:1164`, default em `3dsgpu.h:669`). Com flags 0 o citro3d faz
`GSPGPU_FlushDataCache` do linear heap INTEIRO
(`libs/citro3d/source/renderqueue.c:212-217`): atlas de tiles de 2 MB, todos
os VBOs duplos, buffers de áudio e de MSU-1, tudo, escrito ou não.

**Estratégia.**
1. Medir primeiro: habilitar `TIMER_FLUSH` e `TIMER_GPU_WAIT`
   (`3dstimer.h`, hoje `PROFILING_DISABLED`) num build de console e ler os
   dois por 300 frames. Se o flush medir abaixo de 0,2 ms, parar aqui.
2. Passar `GX_CMDLIST_FLUSH` e flushar só o que o frame escreveu: a metade
   ativa de cada VBO de cena (`list->data`, `count × vertexSize`), o scratch
   de índices, o ring do glow, e as linhas do atlas tocadas (acompanhar
   min/max de `texturePosition` em `cache3dsCacheSnesTileToTexturePosition`,
   `3dsimpl_tilecache.cpp:30-38`).
3. Manter um `#define` que volta ao flush total, para diagnosticar.

**Validação.** Azahar não reproduz cache de CPU (sempre coerente), então a
prova é no console: tile fantasma ou lixo em tiles novos indica buffer
esquecido. Rodar MMX3, um jogo de Mode 7 (F-Zero) e um com muita troca de
VRAM (Yoshi's Island).

**Ganho / risco.** 0,2-1 ms por frame (o número real sai do TIMER_FLUSH).
Risco médio: um buffer esquecido rende tiles errados, visível mas não
fatal. Esforço pequeno.

### B2. allowDefer para escritas de OAM

**Situação.** Só `$2122` (CGRAM) marca `allowDefer`
(`ppu.h:822-824`, `gfxhw.cpp:4046-4049`); qualquer byte de OAM força
`FLUSH_REDRAW` de TODAS as camadas da seção. No Super Mario Kart são 8
seções por frame, 3 delas por OAM; uma seção de 3 linhas custa ~190 µs
porque a varredura de tiles de BG (33 × 4 BGs) é paga por linha de tiles
tocada, não por scanline.

**Estratégia.**
1. Nas escritas de `$2104` e `$2101`, setar `allowDefer` e marcar que só a
   camada OBJ mudou (`shouldRenderThisSegment` já tem o gate por camada em
   `gfxhw.cpp:4051-4066`).
2. O flush seguinte não-diferível força todas as camadas, como já acontece
   com a paleta: o caminho de catch-up existe, é só reutilizar.
3. Contar seções por frame no log de perf (`sections=`) antes e depois.

**Validação.** Azahar: cenas `layer-toggle`, `alttp-transition` e as de
MMX3, bit a bit contra as douradas; adicionar uma cena de SMK (OAM por
scanline) com fbdump.

**Ganho / risco.** 0,3-0,5 ms em jogos com muitas seções por OAM, zero nos
outros. Risco médio: sprite de uma seção aparecendo com a BG da seção
anterior se o catch-up falhar. Esforço pequeno-médio.

### B3. Lookup de tile empacotado

**Situação.** Cada tile faz três leituras aleatórias dependentes:
`vramCacheHashToTexturePosition[hash]` (256 KB),
`GFX.VRAMPaletteFrame[8192][16]` (512 KB, `gfx.h:89`) e `BG.Buffered[]`,
mais `paletteFrame[pal]`. Cerca de 2.900 tiles por frame por olho, logo
6-9 mil misses prováveis de cache, disputando a L2 com o core.

**Estratégia.**
1. Uma entrada de 32 bits por (tile, paleta): `{texturePos:14,
   paletteStamp:16, blank:1}`, uma única linha de cache por lookup.
2. `VRAMPaletteFrame` vira `u16` (os contadores só precisam diferir; o reset
   em `ppu.cpp:2697` já zera, tratar o wrap).
3. Manter o pareamento de frame alternado (`p^1`) e o pool da variante
   HDMA, que dependem desses campos.

**Validação.** Azahar: todas as douradas (qualquer erro aqui é visual e
imediato). Console: `ppu=` antes e depois.

**Ganho / risco.** 0,2-0,4 ms no `ppu=`. Risco baixo-médio. Esforço médio.

### B4. Sprites em seções pequenas emitidos por tile

**Situação.** `S9xDrawOBJSHardware` só usa o caminho de sprite inteiro
quando `EndY - startY >= 16` (`gfxhw.cpp:2846`); abaixo disso cada coluna
de sprite vira um quad 8×1 por linha (`gfxhw.cpp:2976-2983`), 8× os
vértices e 8× os lookups de B3.

**Estratégia.** Depende de B2: com as seções por OAM eliminadas, as seções
pequenas quase somem. O que sobrar: baixar o limiar de 16 linhas (o caminho
rápido limpa uma lista de 128 entradas, barato) ou percorrer runs de sprite
por linha de tile. Manter o caminho por linha para rotação de
FirstSprite+Y.

**Validação.** Cenas de MMX3 e SMK no Azahar. **Ganho:** 0,1-0,3 ms em
jogos com muitas seções. Risco baixo, esforço pequeno.

### B5. Índice sequencial estático

**Situação.** Quando uma camada tem mais de uma seção por alvo
(`useDrawArraysForTiledLayers = false`, `3dsimpl_gpu.cpp:1620`),
`gpu3dsDrawTiledLayer` grava um índice `u16` sequencial para cada vértice
(`:321-332`) e a passada de sombra repete (`:778-781`).

**Estratégia.** Um IBO identidade alocado uma vez e um ponteiro-base por
batch. Respeitar a regra tudo-ou-nada DrawArrays/DrawElements
(`developer-gotchas.md`: misturar trava o hardware).

**Validação.** Console obrigatório (a trava não reproduz no Azahar).
**Ganho:** 20-40 µs e menos bytes para B1 flushar. Risco baixo se a regra
for respeitada, esforço pequeno.

---

### B6. Quads pela CPU, sem geometry shader

**Estado: descartado (medido em 2026-11-10).** Passos 10 e 11 do GPU
Cost Probe no console: o geometry shader mais o setup custam 0,16 us por
quad; o vertex shader custa 0,3 us por vértice. Emitir quatro vértices
pela CPU tira o primeiro e paga dois vertex shaders a mais: 0,45 us por
quad mais caro. Não fazer. O vertex shader é o maior pedaço do custo por
tile: o que rende é cortar vértices (fantasmas do blur) ou encurtar o
vertex shader.

---

### B7. Imagem pronta da camada (layer scratch image)

**Estado: ligado por padrão (medido no console em 2026-11-10).** Um BG
desenhado muitas vezes no frame (main e sub, dois olhos, fantasmas do
blur) é desenhado uma vez numa imagem 512x256 e cada passada vira um
quad por plano de prioridade, com o mesmo shader, então shift, tinta,
alpha do fantasma, depth, stencil e alpha test valem igual. Lógica pura
em `source/layer_scratch.h` (varredura, modelo de custo com margem de
20%, tier), uso em `3dsimpl_gpu.cpp` (`layerScratchDecideAndBuild`,
`layerScratchDraw`).

- Sem VRAM (186 KB livres): as oito imagens são fatias de 256 KB da
  textura do Mode 7 (`SNES_SCRATCH`, alias de 512x256 RGBA5551). Fora de
  telas Mode 7, com um segundo de espera na saída; a volta ao Mode 7
  redesenha as quatro seções.
- Entra só quando paga: `passes * (verts * 380 + tilePx * 9)` contra a
  montagem mais `passes * bbox * 9` ns. Dois planos intercalados em tela
  cheia não pagam (duas caixas cheias de fill); camada com coverer do
  occlusion cull fica de fora; shift fracionário (modo Continuous) cai
  nos tiles.
- Depois das montagens o frame é flushado e dividido (`gpu3dsFrameSplit`):
  no console a unidade de textura lê o conteúdo velho de um alvo
  renderizado na mesma lista de comandos.

**Validação.** Azahar bit-idêntico com `-DLAYER_SCRATCH_FORCE` (todo
candidato pela imagem) em mmx3zp-dam-light, mmx3-sbs-fx1-full,
mmx3-pillar-blur6, smk-boot e shadow-smw-on, e sem o force na represa
(BG2, 1914 vértices, 8 passadas, um plano). 
**Medido** (New 3DS, MMX3 ZP fase pesada, perfil do Jorge, a cena
anda): 21.628 vértices por frame para 8.528; carga do frame 15.432 us
para 13.139 us; frames acima do orçamento 137 para 48; desenhos pulados
68 de 240 para 26. Ganho de 2,3 ms, menos que os 5 ms estimados: o quad
de cada passada preenche a caixa inteira da camada. `gpu=` não serve
para comparar (14.884 contra 15.096 us): com a imagem a lista é
submetida no começo do frame e a contagem inclui espera pela CPU.
Imagem limpa no console. O passo 12 do probe agora DESLIGA a imagem;
`-DLAYER_SCRATCH_OFF` compila sem ela.

---

## C. Menu (restos depois do lote de 2026-11-05)

### C1. Tela inferior repintada inteira por movimento de cursor

**Situação.** `menu3dsDrawEverything` (`3dsmenu.cpp:491-644`) faz três
preenchimentos de largura total, 14 linhas de texto com blend por pixel
(`3dsui.cpp:295-325` lê o framebuffer por glyph), abas, barra, bateria e
versão, a cada redraw; o tema RetroArch soma um xadrez de tela cheia.

**Estratégia.** Redesenhar só as duas linhas cuja seleção mudou (a anterior
e a nova), mantendo o fundo; redraw completo só em troca de aba, scroll e
diálogo. Precisa de um "dirty rows" no `SMenuTab`.

**Validação.** `menu_help.py` (probe de menu com o Mac destravado) para as
16 capturas; olho no console para rastro de cursor.

**Ganho / risco.** 1-3 ms por frame de menu. Risco médio de sujeira visual
(linha não limpa). Esforço médio. Só vale se o menu continuar lento depois
do lote atual.

### C2. Abas 3D/Effects reconstruídas a cada toggle

**Situação.** `showMenu` chama `stereo3dMarkTabsDirty()` sempre
(`3dsmain.cpp:4079`) e cada checkbox/radio/picker dessas abas devolve -1
(`3dsmenu.cpp:1108-1131`), reconstruindo `makeStereo3dMenu` +
`makeEffectsMenu`: três `std::string`, um `std::function` e um vetor de
picker por item, copiados por valor em `menu3dsAddTab`.

**Estratégia.** Reconstruir só quando o CONJUNTO de linhas muda (Hide
Unused, lista de perfis, Off escondendo um bloco); caso contrário apenas
atualizar `Value` dos itens; `std::move` dos vetores em `menu3dsAddTab`.

**Validação.** `menu_help.py`. **Ganho:** some o engasgo por toggle. Risco
baixo, esforço pequeno-médio.

### C3. Animações fixas

**Situação.** Troca de aba = 6 redraws + 6 vblanks (100 ms mínimo); abrir e
fechar diálogo = 16 frames cada com blend de tela cheia (270 ms).

**Estratégia.** `ANIMATE_DIALOG_STEPS` 8 → 4 e pular o redraw do fundo nos
frames em que nada se moveu. Gosto pessoal: decidir no console.

**Ganho:** sensação de resposta. Risco zero, esforço mínimo.

---

## D. Plataforma

### D1. Mixer em três passadas por bloco

**Situação.** `S9xMixSamplesIntoTempBuffer` → `MixBuffer` int32
(`soundux.cpp:112`), depois volume para L/R separados
(`impl3dsOutputSoundSamples`), depois o loop de intercalação em
`3dssound.cpp:131-135`. Três passadas sobre cada bloco de 256 frames.

**Estratégia.** Uma variante da rotina de volume que grava estéreo
intercalado direto em `wb->data_vaddr`, eliminando `stagingL/R`.
`ReverseStereo` já é tratado no mixer.

**Validação.** Captura WAV do Azahar comparada bit a bit com a anterior
(mesma ROM, mesmo savestate, 5 s).

**Ganho / risco.** 5-10 µs por bloco de 8 ms, mas no Old 3DS isso cai na
fatia de 30% do syscore que o FLAC divide. Risco baixo, esforço pequeno.

---

## E. Core no Old 3DS: levantamento de 2026-11-13

Leitura de código e de disassembly (build release `-O2`, GCC 16.1), sem
nenhuma mudança de código. **Nada aqui foi medido**: os ganhos são
estimativas e o E0 existe para trocá-las por número. Um item por commit,
como no resto deste documento.

### Fatos (base para todos os itens)

- Régua: Old mode, MMX3 Zero Project (MSU-1), represa, build 46a12c1:
  `core=` 12,7 ms (dos quais `ppu=` 1,9 a 2,1), carga 107%, 124 draws
  pulados de 300. Para a carga cair a 100% falta cerca de 1,2 ms.
- `core=` mede só `S9xMainLoop` (`source/3dsimpl.cpp`, `coreTick0`): 65816 +
  SPC700 + eventos de linha + DMA/HDMA + `ppu=`. O mixer do DSP roda em
  outra thread (core 1, fatia do syscore) e não entra. `S9xSyncSpeed` é
  vazio. É tempo de relógio: preempção da thread principal também conta.
- Nessa cena `idle=` é cerca de 0,5 por frame e `aidle=0`: nem o skip do
  65816 (A1) nem o do SPC700 (A2) atuam. O core roda cheio.
- Conta de padeiro: 10,6 ms fora do PPU = 2,85 M de ciclos do ARM por frame
  = cerca de 10,9 mil ciclos por linha do SNES (262 linhas). Uma linha tem
  1364 master cycles: de 50 a 75 instruções do 65816 e cerca de 20 do
  SPC700. O caminho rápido de uma instrução (`OpA5M1`, LDA dp) tem 28
  instruções ARM mais 11 do laço. Ou cada instrução ARM custa uns 3 ciclos
  (cache miss, salto indireto sem previsão), ou existe custo fixo por linha
  que ninguém mediu. A leitura não separa as duas coisas.
- Tamanho de código contra 16 KB de I-cache (4 vias, sem L2 no Old mode):
  `cpuexec.o` 212.772 B em 564 funções (handlers de 24 a 600 B),
  `S9xDoHBlankProcessing` 1.972 B, `S9xMainLoop` 1.680 B, `spc700.o`
  23.256 B, `soundux.o` 33.773 B. A cada linha o fluxo alterna três regiões
  de código: handlers do 65816, evento de linha, handlers do SPC700.
- Laço do 65816 por opcode (`EXECUTE_ONE_OPCODE` em `cpuexec.cpp`, 20
  cópias): `ldrb` do opcode, `ldr` do handler, `blx`, depois `ldr
  CPU.Flags` + `cmp` + `bne`, `ldr CPU.NextEvent` + `cmp` + salto, `ldr
  CPU.MemSpeed` + `add`. Registradores globais: r11 tabela de opcodes, r10
  ciclos, r9 PC. O r8 é salvo no prólogo e está livre.
- Laço do SPC700 por opcode (`S9xUpdateAPUTimer` em `apu.cpp`, 10 cópias):
  11 instruções com 5 loads e 1 store, todo o estado em memória
  (`APU.Cycles`, `IAPU.PC`), o byte do opcode lido duas vezes, a tabela
  `APU.S9xAPUCycles` com 4 bytes por entrada (1 KB). Os handlers ficam em
  outra unidade (`spc700.cpp`) e cada um recarrega os ponteiros de literal.
- `S9xUpdateAPUTimer` roda em todo `HBLANK_END_EVENT` (262 vezes por frame)
  e em todo acesso da CPU a $2140-$217F (`ppu.cpp`, leitura e escrita), que
  sincroniza o SPC700 até o ciclo atual antes de ler ou escrever a porta.
- Escritas do SPC700 no DSP não têm carimbo de tempo: entram numa fila
  (`S9xSetAPUDSPLater`) que o mixer esvazia a cada bloco de áudio.

### E0. Medir antes de mexer (obrigatório, primeiro)

**Estratégia.** Build de sonda (`EXTRA_DEFINES=-DPROBE_CORE`, com
`make clean` antes e depois) que acrescenta à linha `[perf][frame]`:

- `apu=` us por frame dentro de `S9xUpdateAPUTimer` (par de
  `svcGetSystemTick` dentro da função, cobre todos os chamadores);
- `hb=` us por frame em `S9xDoHBlankProcessing`, descontados `apu=` e
  `ppu=`; `dma=` us por frame em `S9xDoDMA` e `S9xDoHDMA`;
- contadores por frame: `ops=` (opcodes do 65816), `aops=` (opcodes do
  SPC700), `acalls=` (chamadas de `S9xUpdateAPUTimer`), `slow=` (acessos
  que caem em `S9xGetByteFromRegister`/`S9xSetByteToRegister`);
- `svc=` custo de um par de ticks, calibrado uma vez no boot com mil pares
  seguidos, para descontar a própria sonda (são mais de 500 pares por
  frame).

Segunda sonda, só para o Azahar (contagem é lógica, não depende de tempo):
histograma de opcodes do 65816 (por byte e por tabela M/X) e do SPC700 ao
longo de 600 frames da cena, escrito no log na saída. Cruzar com os
tamanhos de `nm -S --size-sort build/Snes9x/cpuexec.o` para saber quantos
bytes somam os handlers que cobrem 95% das execuções.

**Como ler.** `ops=` × custo plausível por opcode contra o tempo que sobra
depois de `apu=`, `hb=`, `dma=` e `ppu=` diz se o gasto é por instrução ou
por linha. O histograma diz se o conjunto quente cabe em 16 KB (se couber
folgado, o E2 vale pouco).

**Risco.** Nenhum para o produto: é build de sonda, não vai para nightly.

**Resultado (2026-11-13, Azahar, cena `mmx3zp-dam-light`, build
`-DPROBE_CORE_HIST`).** Por frame: `ops=` 13.736, `aops=` 4.441, `acalls=`
265, `hbcalls=` 525, `slowr=` 14, `sloww=` 197. O caminho lento de
registradores é desprezível nesta cena. O histograma mostrou que 61% dos
opcodes do 65816 eram dois: `LDA $09CE` (0xAD) e `BEQ` (0xF0) em 80:8103,
o laço em que o jogo espera o NMI, cerca de 4,2 mil voltas por frame. O
idle skip (A1) reconhecia o laço e não agia: 87,5% dos desvios tomados
eram barrados porque havia um IRQ pendente, e em todos esses casos o IRQ
estava mascarado pelo flag I (o jogo deixa um IRQ sem reconhecer atrás de
SEI). Corrigido em `idle_skip_interrupt_due` (source/idle_skip.h): só barra
o IRQ que a CPU iria atender. Depois: `ops=` 6.406 por frame (53% a menos),
258 pulos por frame. Tempo no console ainda não medido; a sonda de tempo
(`-DPROBE_CORE`, sem HIST) é a que vai para o console.

O SPC700 continua em laço de espera sem pulo (`aidle=` 0, cerca de 10
instruções por volta com CALL/RET: f0, e5, d0, 6f, 3f, 60, 65, 84, c4,
fd). É o próximo a investigar, antes do E1: se o A2 passar a pegar esse
laço, o E1 perde parte do motivo.

**Console (2026-11-14, nightly e580574, Old mode, represa do MMX3 ZP,
log em #80, log de sessão por console).** `core=` médio 11,5 ms em 17
janelas (10,3 a 12,9), contra 12,7 ms da base f5f2ac3 (12,3 a 13,2): cerca
de 1,2 ms a menos, 9%. Carga média perto de 100% (93 a 108), contra 106%.
`idle=` foi de cerca de 150 para cerca de 220 por frame; `skipped-draws`
não mudou (cerca de 130 de 300). O trecho jogado não é idêntico ao da
base, então o número vale como ordem de grandeza.

**O laço do SPC700 (Azahar, mesma sonda, histograma por endereço).** 93%
dos opcodes do SPC700 são 12 instruções do laço principal do driver de som
da Capcom, cerca de 345 voltas por frame:

    02f9 CALL $046a      046a MOV A,$00F4   0470 BNE $046a   048c RET
    02fc MOV A,$00FD     046d CMP A,$00F4   0472 MOV Y,A
    02ff CLRC            0300 ADC A,$B1     0473 BEQ $048c
    0302 MOV $B1,A       0304 BEQ $02f9

Lê a porta 0 (sem comando), lê o timer 0 (sem tique), soma em `$B1` e
grava o mesmo valor. O A2 não pega por dois motivos: o desvio volta para o
CALL, que não é a instrução de leitura marcada, e uma volta dura cerca de
40 ciclos do SPC contra 65 por linha, então mesmo pulando sobraria uma
volta por chamada de `S9xUpdateAPUTimer` (265 por frame). O que resolve é
dormir: provar que a volta é um ponto fixo (mesmo desvio tomado duas vezes
seguidas com os mesmos registradores, nenhuma escrita que mude RAM,
nenhuma escrita em $F0-$FF, nenhuma leitura do DSP, nenhum timer lido
diferente de zero) e não executar mais nada até um tique de timer ou uma
escrita da CPU nas portas, que já marcam `IAPU.APUExecuting = TRUE`. Vira
o item E1a, antes do E1.

### E1. SPC700 em lotes de N linhas

**Situação.** O SPC700 é chamado 262 vezes por frame para rodar cerca de 20
opcodes de cada vez. Cada chamada troca a região de código quente e os dados
(RAM do APU, tabelas) e depois volta para o 65816.

**Estratégia.** No `HBLANK_END_EVENT`, chamar `S9xUpdateAPUTimer` só quando
`(CPU.V_Counter & (N-1)) == 0`, com N = 4 e N = 8 para comparar. O rebase
por linha (`CPU.Cycles`, `APU.Cycles`, `IAPU.NextAPUTimerPos`) continua em
toda linha. Os acessos da CPU às portas já sincronizam até o ciclo exato,
então a conversa CPU/APU não muda; os timers do SPC700 avançam dentro do
próprio laço. Sem estado novo (a decisão sai do `V_Counter`), o que mantém
savestate e rewind como estão.

**Cuidados.** (1) `IAPU.NextAPUTimerPosDiv10000 = NextAPUTimerPos / 1000`
passa a ver valores negativos e a divisão do C trunca para zero: usar piso,
senão o tick do timer anda um ciclo. (2) Com o idle skip do SPC700 ativo
(`aidle>0`) o salto vai até o limite da chamada, que muda com N: o estado
final é equivalente, não necessariamente idêntico. (3) O atraso máximo das
escritas no DSP sobe de 1 para N linhas emuladas (0,5 ms com N = 8), bem
abaixo do bloco do mixer.

**Validação.** Cenas douradas idênticas no Azahar (o vídeo não pode mudar);
contagem de escritas no DSP e hash do estado do APU iguais à base depois de
600 frames numa cena com `aidle=0`; ouvir MMX3 (MSU-1 e normal), um jogo
com A2 ativo e um jogo de handshake pesado (Star Ocean ou Tales). No
console: `apu=` e `core=` contra a base.

**Ganho / risco.** Se a hipótese de cache estiver certa, de 0,3 a 1 ms; se
estiver errada, perto de zero, e isso também é resposta. Esforço pequeno,
risco médio (áudio). Por isso vem logo depois do E0.

### E2. Conjunto quente contíguo e caminho lento fora de linha

**Situação.** Todo handler que acessa memória carrega, inline, o caminho
lento de registrador (`CpuSaveFastRegisters`, chamada, `CpuLoadFastRegisters`):
cerca de 50 dos 164 B de `OpA5M1` (com o literal pool), bem mais nos indiretos (`OpB7M1` 600 B, `OpB1M1`
512 B). Os handlers ficam na ordem do fonte, quentes misturados com frios.

**Estratégia.** Duas partes, commits separados:

1. Caminho lento em funções `noinline` do próprio `cpuexec.cpp` (uma para
   cada acesso: byte e word, leitura e escrita), chamadas pelo ramo
   improvável de `CpuGetByte`/`CpuGetWord`/`CpuSetByte`/`CpuSetWord` em
   `cpuexec-ops.h`. **Não usar o atributo `cold`**: ele compila a função
   como `-Os`, e o GCC 16.1 em `-Os` perde escrita em registrador global
   (A3). Conferir no disassembly que r9/r10 são recarregados na volta.
2. Com o histograma do E0, marcar os handlers que cobrem 95% das execuções,
   mais `S9xMainLoop`, `S9xDoHBlankProcessingWithRegisters`,
   `S9xDoHBlankProcessing` e `S9xUpdateAPUTimer`, com
   `__attribute__((section(".text.core_hot")))`. O `3dsx.ld` usa
   `*(.text.*)`, então funções com a mesma seção ficam contíguas dentro de
   cada objeto, sem mexer no linker script. Conferir no `.map` o tamanho e
   a contiguidade de cada bloco. Não usar o atributo `hot` (muda a
   otimização da função).

**Validação.** Bit-idêntico por construção: cenas douradas do Azahar,
testes de host, `size` de `cpuexec.o` antes e depois. Console: `core=`.

**Ganho / risco.** Depende do E0: grande se o conjunto quente hoje passa de
16 KB, pequeno se já cabe. Esforço médio, risco baixo.

### E3. Base do estado em registrador (r8)

**Situação.** Cada handler abre carregando o endereço do struct da CPU do
literal pool (e o de `Memory` para `BlockIsRAM`): um ou dois loads por
opcode, e o literal pool de cada handler quente ocupa linha do D-cache.

**Estratégia.** `register ... asm("r8")` com o endereço do struct que já
junta `CPU`/`ICPU`/`Registers`, carregado em `CpuLoadFastRegisters`.
Constante, e r8 é callee-saved: não precisa de save/load em chamada para
fora. `BlockIsRAM` vira ponteiro dentro desse struct, como já são
`MemoryMap` e `MemorySpeed`.

**Ganho / risco.** 1 a 2 instruções por opcode de umas 40: de 0,1 a 0,3 ms.
Risco médio: é mais um registrador global num compilador que já errou com
eles; conferir o disassembly dos desvios (`OpF0`, `OpD0`, `Op80`) e rodar o
Azahar. Fazer depois do E2, que mexe nos mesmos arquivos.

### E4. Um limite só no laço principal

**Situação.** Depois de cada opcode o laço testa `CPU.Flags` e, em
seguida, `CPU.NextEvent`: dois loads, dois compares, dois saltos.

**Estratégia.** Um campo `CPU.EventLimit` = `NextEvent` quando `Flags` é
zero e um valor que sempre dispara quando não é; o laço compara só com ele
e o caminho de evento decide entre `S9xHandleFlags` e o HBlank. O campo é
recalculado em `S9xReschedule` e em todo ponto que escreve `CPU.Flags`
(levantar todos com grep antes de começar; um ponto esquecido é um NMI ou
IRQ perdido).

**Ganho / risco.** 3 instruções por opcode: cerca de 0,2 ms. Risco médio,
falha silenciosa (interrupção atrasada). Só depois de E1 a E3.

### E5. Custo fixo do laço do SPC700

**Estratégia.** No `EXECUTE_ONE_APU_OPCODE`: ler o byte do opcode uma vez;
`S9xAPUCycles` em `uint8` (256 B em vez de 1 KB) se os valores couberem.
Manter `APU.Cycles` e `IAPU.PC` em registrador exigiria levar os handlers
do SPC700 para a mesma unidade e usar registrador global: não vale antes
de o `apu=` do E0 mostrar que o SPC700 passa de 2 ms.

**Ganho / risco.** Cerca de 0,1 ms. Risco baixo, bit-idêntico.

### E6. Ganchos do idle skip em todo acesso (micro)

Toda leitura faz `ldrb BlockIsRAM[bloco]` e um store condicional em
`CPU.WaitAddress`; toda escrita faz `CPU.WaitAddress = NULL`. São 2 a 4
instruções das 28 do caminho rápido, pagas mesmo em jogo sem idle skip.
Possível: só armar os ganchos quando o A1 está ligado para o jogo (segunda
tabela de opcodes sem eles). Dobra o código, o que briga com o E2. Deixar
para o fim e só se o E0 mostrar que o gasto é por instrução.

### Não fazer agora

- **Despacho encadeado por tail call (`musttail`)**: economiza o retorno,
  mas põe o teste de evento em 564 handlers e apoia tudo num recurso novo
  do GCC junto com registradores globais. Só com o E0 provando que o
  retorno pesa.
- **SPC700 em outra thread**: o syscore do Old 3DS já é disputado pelo
  mixer e pelo FLAC, e a sincronia de portas viraria trava.
- **`-Os`, `-flto`, `-ffast-math`**: ver A3 e a lista abaixo.

---

## O que NÃO fazer (verificado)

- Divisão por 1000 no timer da APU: `-O3` já gera multiplicação; zero
  `idiv` em `apu.o` e `cpuexec.o`.
- Inline do dispatch de registradores em `CpuGetByte`: o custo é o
  save/load dos registradores globais r9-r11, obrigatório em qualquer
  chamada para fora do TU.
- `-flto`: o GCC não aceita variáveis globais em registrador entre
  unidades LTO. O equivalente manual é mover callees quentes para headers.
- `-ffast-math`: o experimento de determinismo do rewind depende de estado
  bit-exato.

## Ordem sugerida

1. Medir no console o lote de 2026-11-05 (menu, MSU-1 com FMV).
2. B1 (medição primeiro) e B2, juntos numa branch: validáveis no Azahar,
   risco baixo-médio.
3. A3, porque é só medir.
4. A1, a única mudança com chance de alterar a sensação de velocidade, com
   toggle por jogo e começando OFF.
5. O resto conforme a vontade.

**Core no Old 3DS (2026-11-13):** E0 (medir) primeiro e sozinho; depois E1,
E2 parte 1, E2 parte 2, E3, E4, E5, nessa ordem, reavaliando a cada número
do console. E6 só se o E0 apontar custo por instrução.
