# Aurora 0.1 — sintetizador e FX VST3 para Windows x64

Implementação independente em C++ baseada nas funcionalidades e equações dos dois textos fornecidos. O projeto oferece um instrumento **Aurora** e um processador estéreo **Aurora FX**. Não foi demonstrada equivalência sonora com o Serum; não há compatibilidade com seus presets nem reutilização da interface, tabelas, imagens ou presets comerciais.

## Instalação e uso

1. Extraia o pacote de binários. Copie as pastas completas `Aurora.vst3` e `Aurora FX.vst3` para uma pasta VST3 reconhecida pela DAW, por exemplo `C:\Program Files\Common Files\VST3` (pode exigir privilégio administrativo). Também é possível selecionar uma pasta própria na DAW.
2. Faça uma nova varredura de plugins. Use Aurora numa pista MIDI; Aurora FX numa pista de áudio.
3. O instrumento abre com um oscilador ativo e sub. Toque pelo MIDI ou teclado virtual. O executável Standalone permite configurar o dispositivo de áudio.
4. Use as abas OSC, MIX, FX, MATRIX e GLOBAL; Presets abre o navegador. All parameters abre todos os controles por seção/instância. Em OSC, escolha o motor e importe WAV, AIFF, FLAC ou SFZ. A referência padrão do sample é MIDI 60 (C4 pela convenção usada aqui).
5. Os racks 1 e 2 são buses paralelos; o rack 3 processa o bus principal após a soma dos dois primeiros. Cada rack tem quatro slots em série. Na seção Routing, aumente FX bus 1/2 para alimentar os racks paralelos. No Aurora FX, Routing 1 representa a entrada estéreo.
6. Em FX rack, Import IR carrega uma resposta ao impulso para o rack selecionado. Todos os slots de convolução desse rack compartilham essa IR. Escolha Convolution no slot desejado. IRs devem ter no máximo 10 segundos e 32 MB.
7. Sequencer oferece arpejador e clip de 16 passos, com BPM do host durante transporte ou BPM de fallback. O piano roll cobre MIDI 48–71: clique para ativar/trocar a nota; clique direito desativa. Os controles abaixo permitem notas 0–127. O teclado virtual fica no rodapé.
8. Save/Load preset usa arquivos `.aurora`; o estado da DAW inclui parâmetros, zonas, samples e IRs incorporados. `Demo.aurora` e `demo.wav` demonstram uma configuração com Hyper e reverb.

## Implementado

| Área | Implementação |
|---|---|
| Síntese | 16 vozes, três osciladores com seleção de motor, ruído e sub, velocidade MIDI, pitch bend ±2 semitons e pressão por nota/canal |
| Wavetable | Quatro quadros gerados matematicamente, 2.048 pontos, oito níveis de limitação harmônica, interpolação de posição e morph |
| Sample | Reprodução mono, transposição por referência, loop opcional, importação de arquivos até 120 segundos |
| Multisample | Zonas por faixa MIDI; importador SFZ básico: global/group/region, sample, key, lokey, hikey, pitch_keycenter. Não é um parser SFZ completo |
| Granular | Dois grains sobrepostos, janelas Hann, posição e duração de 10–250 ms, reprodução com transposição |
| Spectral | Análise FFT de um quadro de 2.048 amostras do sample; ressíntese aditiva com até 64 harmônicos |
| Modulação | Quatro ADSRs por voz, dez LFOs com tempo sync/fase/curva custom de oito pontos, oito macros e 64 rotas, com fontes velocity/note/pressure/env/LFO/macro/mod wheel e curvas de resposta |
| Destinos de modulação | Níveis e posições dos três osciladores, pitch, pan e frequências dos dois filtros. Modulação dos filtros é agregada pela média das vozes ativas |
| Filtros | Dois SVFs com low/band/high pass e saturação `x − 0,14814815 x³`, com entrada limitada |
| Roteamento | Sete slots; balanço −100…100; sends 0…100; destino normalizado com cortes 0,25/0,50/0,75 e Via Env1 com corte 0,50 |
| FX | Distortion, flanger, phaser, chorus, delay, compressor, reverb, EQ, filtro, Hyper, Bode aproximado, convolução, largura, Dimension, mid/side e divisões de duas/três bandas |
| Convolução | Convolução por FFT delegada ao JUCE; carregamento assíncrono de IR após decodificação e transferência de propriedade do buffer |
| Estado | Contêiner próprio AuroraJson, metadados JSON, payload CBOR comprimido por Zstandard. Carrega também a versão inicial com XML binário JUCE |
| Interface | Interface própria redimensionável, knobs, gráficos, drag-and-drop de modulação e slots FX, matriz virtualizada, navegador de presets, undo/redo, MIDI learn, teclado e piano roll |

## Diferenças e limites importantes

- O relatório recupera apenas partes dos algoritmos. Hyper usa quatro leituras de atraso com trajetória triangular e dois SVFs de entrada; fases, coeficientes e ganhos são escolhas desta implementação. Dimension usa quatro taps com contribuição lateral de sinais opostos.
- Bode usa a recorrência all-pass fornecida como dispersor de fase e um FIR Hilbert de 127 taps para gerar quadratura. O controle time corresponde a um deslocamento de −1.000…1.000 Hz (0,5 = zero). É um frequency shifter próprio, sem equivalência calibrada com o original; wet introduz atraso de 63 amostras.
- O compressor tem detector de magnitude e suavização dependente da taxa de amostragem; a curva e os controles são próprios. O reverb usa o algoritmo JUCE. EQ e divisores são simplificados; os divisores recombinam bandas, sem sub-racks independentes por ramo.
- Sample e granular são motores básicos; não incluem editor de zonas completo, time-stretch avançado ou todos os opcodes SFZ. Spectral analisa um único quadro, sem reprodução de uma sequência espectral completa.
- Roteamento de filtros é acíclico: filtro 1 pode alimentar filtro 2; destino Filter no filtro 2 retorna ao principal. Não há ciclo de feedback entre os filtros. Nos slots dos filtros, Via Env1 recebe o envelope já aplicado nas fontes.
- Os sends são ganhos lineares (`100` significa ganho 1 nesta implementação), e pan usa potência constante. Essas leis são decisões próprias, pois o texto não recuperou a matemática original.
- MIDI note-on/off é distribuído por offsets dentro do bloco. Parâmetros do host e LFOs são atualizados por bloco; não há promessa de automação por amostra, MPE completo ou reprodução idêntica entre tamanhos de bloco. Parâmetros contínuos têm suavização de 15 ms por bloco; mix dos efeitos e master também são suavizados por amostra.
- Aurora usa saturação final tanh; Aurora FX preserva o caminho dry com master 1 e efeitos desligados. Efeitos fortes e sends paralelos podem aumentar o nível na variante FX.
- Não foram implementados os 2.637 IDs do produto original, a migração de presets XferJson, 123 templates VSTGUI, recursos comerciais ou interfaces GPU. As contagens e detalhes de Ghidra/REA do relatório são evidências da investigação, não funcionalidades de áudio a copiar.
- A interface e o host de testes são exercitados por código. A validação em sua DAW específica e uma comparação auditiva com o produto original não foram realizadas.

## Código e recompilação

O pacote de fontes contém JUCE 7.0.12 e Zstandard 1.5.7. Os binários foram compilados com LLVM-MinGW/Clang 22.1.8, CMake e Ninja. Use `build.ps1 -ToolchainBin <pasta contendo clang++, cmake e ninja>` ou CMake com MSVC em um Developer PowerShell.

Foi removido um construtor legado inutilizado em `third_party/JUCE/modules/juce_audio_processors/processors/juce_AudioPluginInstance.h`, incompatível com o diagnóstico do Clang 22. O restante do framework permanece como a versão 7.0.12. O arquivo `THIRD_PARTY.md` registra revisões e licenças.

O formato `.aurora` está documentado em `Source/Preset.h`. Seu marcador é AuroraJson, distinto de XferJson. Seu CBOR contém a árvore de estado Aurora serializada em XML binário; não implementa o esquema privado de outro produto.

Veja `validation.txt` para o resultado dos testes executados nesta entrega.


## Fluxo da interface

- Arraste a fonte selecionada no rodapé (DRAG SOURCE) para Level, Position, Tune, Pan ou Cutoff. Uma rota livre da matriz recebe a conexão. O anel dourado marca a profundidade atribuída.
- MATRIX permite escolher LFO, editar seus oito pontos e ativar tempo sync. A matriz tem source, target, amount e curve; selecione uma linha para editar. Curve = 1 é linear.
- Arraste o cabeçalho de um slot FX sobre outro do mesmo rack para trocar suas posições. As mudanças de tipo/amount/mix/time são automatizáveis.
- Clique direito num knob, selecione MIDI learn e mova um CC físico. A associação é guardada no preset; pode ser removida pelo mesmo menu. Pitch retorna ao centro ao soltar, e Mod envia CC 1.
- PRESETS varre uma pasta em thread separada, busca por nome e carrega por duplo clique. Tags, autor/categoria e favoritos ainda não possuem um modelo de catálogo.
- UI está em Source/Editor.h e Source/AdvancedEditor.h; DSP está em Source/Dsp.h e no processador em Source/Plugin.cpp. O gráfico de saída usa FIFO SPSC e peak atômico. Paths das curvas e arcos são preparados fora do paint; o timer de visualização roda a 60 Hz.
- O editor varia de 1060×650 até 1800×1200, com scroll quando necessário e desenho vetorial para HiDPI. A implementação combina Graphics 2D e visualização OpenGL; não inclui sub-racks independentes por banda.
- Importação e restauração de assets são operações de edição, com sincronização pelo callback do processador e interrupção das vozes durante a troca. Elas não são um pipeline de troca de assets sem interrupção do áudio.


## Interface compacta inspirada na referência

Esta revisão usa uma janela inicial de 1320×780, cabeçalhos cinza, módulos em grafite e knobs metálicos. A tela OSC exibe os três osciladores e os dois filtros na parte superior, macros/mixer na coluna esquerda e envelope/LFO na faixa inferior, sem precisar mudar de aba para vê-los. As curvas de wavetable usam visualização OpenGL sombreada em perspectiva, com fallback Graphics 2D. As respostas dos filtros são visualizações do modelo. Valores dos knobs compactos aparecem em popup ao arrastar ou passar o mouse. As conexões, parâmetros e estado do plugin foram preservados. Nenhuma imagem ou marca do plugin da referência foi incorporada.


## Revisão estrutural

- Router de views com OscPage, MixPage, FXPage, MatrixPage e GlobalPage. MIX contém as sete rotas; MATRIX mantém source/target/amount/curve; GLOBAL reúne arpejo, clip e performance.
- Na tela OSC, quatro abas ENV e dez abas LFO são fontes arrastáveis; não representam apenas uma instância. O painel Voicing oferece acesso rápido, e GLOBAL reúne os controles completos.
- Wavetable: unison de 1–8 vozes, detune até 50 cents, phase/random e modos próprios Bend/Sync/Fold. Esses controles são específicos de wavetable. Granular usa representação segmentada da amostra; Noise e Sub têm níveis e rotas próprios no mixer.
- Visualizador OpenGL com shader, textura de 256 amostras, sombreamento e profundidade em perspectiva. O DSP publica frames por fila SPSC; a UI consome a 60 Hz e envia por outra fila ao thread OpenGL. Há renderização por software para previews e contextos sem GPU. O teste de compilação do shader num contexto nativo passou nesta máquina; não é uma comparação com o renderer original.
- CurveEditor: até oito pontos, arraste, remoção de pontos internos por clique direito e inserção por duplo clique quando há espaço. O botão CURVE troca o envelope para curva multiponto. O knob Bezier ajusta os dois controles do segmento cúbico. A curva do envelope modula um gate ADSR, cuja release ainda controla o desligamento da voz; não é uma réplica de MSEG de outro produto.
- LFOs: forma custom multiponto, tensão Bézier, sync, phase, delay e rise. Delay/rise usam o relógio global iniciado em prepareToPlay; não são envelopes de LFO independentes por voz.
- Performance: Poly/Mono/Legato, 1–16 vozes, retorno à nota anterior mantida, portamento, transposição e escala Chromatic/Major/Minor. Arpejo Up/Down/Up-Down/Random, 1–4 oitavas e swing até 50%.
- Mappings MIDI passam a ser gravados por ID de parâmetro, com migração dos índices da versão anterior. Novos parâmetros ausentes em presets antigos recebem defaults.
- As seções anteriores descrevem a base histórica; esta revisão atualiza os pontos acima. Permanecem as diferenças de algoritmos, assets e compatibilidade com presets comerciais indicadas no início.


