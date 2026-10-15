# Aurora 0.2.3 — sprint de infraestrutura (15/10/2026)

O anexo original era uma investigação estática parcial; a atualização de 15/10 acrescenta consultas de interfaces públicas e renders offline em processo próprio. A investigação continua parcial, não uma especificação integral nem o código-fonte original. “Concluído” abaixo se refere ao contrato implementado e testado no Aurora; não significa equivalência sonora/binária com o plugin investigado. A sprint de infraestrutura não alterou o layout; a revisão posterior 0.2.2 adiciona a tela principal descrita em [UI_REFERENCE.md](docs/UI_REFERENCE.md), por solicitação explícita do usuário.

| Tarefa | Estado | Implementação e limite |
| --- | --- | --- |
| 1. Automação e catálogo | Automação implementada; catálogo estrangeiro parcial | O wrapper JUCE entrega todos os pontos VST3 com sampleOffset. Fila fixa de 8.192 pontos, rejeições contadas, ordem estável e rampas lineares de 10 ms por amostra, inclusive rampas interrompidas. Pontos de flush sem áudio atualizam o parâmetro sem vazar offsets ao bloco seguinte. Catálogo de todos os parâmetros Aurora com range/default/unidade e catálogo estático dos 2.637 IDs estrangeiros distintos. Correção da interpretação anterior: os 2.637 IDs estáticos não são uma lista de parâmetros expostos. O catálogo runtime possui 2.623 IDs únicos: 2.080 controladores MIDI por canal e 543 demais registros. Apenas 420 IDs coincidem numericamente com o inventário estático; isso não demonstra mapeamento. As cinco amostras de conversão são preservadas como observações, sem inventar fórmulas. Os 35 descritores RoutingSlot continuam sendo os mapeamentos internos confirmados, não uma medida de quanto falta no host. |
| 2. SVF | Trecho confirmado concluído | Limitação em ±1,5 e polinômio float x−0,14814815x³, estados recursivos persistentes e integração ao roteamento. A topologia TPT/SVF do Aurora é independente; o documento não especifica todos os modelos/coefs/ganhos do original, portanto “SVF exato completo” continua não demonstrado. |
| 3. Sete RoutingSlots | Concluído para os descritores e o grafo Aurora | Instâncias Osc A/B/C, Noise, Sub, Filter 1/2. Balance −100..100 (default −100), buses 0..100 (default 0), destino plain 0..3 (default Main=1), ViaEnv1 bool (default 1). Cortes n=0,25/0,50/0,75 e 0,50; inversa do destino p/3. As rotas dos filtros aplicam Env1/sends inclusive quando alimentam o parceiro. Destinos parceiros preservados; ciclos usam Main. A lei 50/50 e a prevenção de ciclos são políticas próprias, não fatos recuperados do original. |
| 4. Efeitos | Núcleos confirmados implementados; equivalência integral pendente | Anel auxiliar 0x1fff/8.192 com interpolação, mistura estéreo e suavização; o delay longo permanece separado. Detector do compressor com 0,00025. BodePhasePair: duas cadeias de 18, seção extra e segunda saída negada. FIR direto + cauda FFT particionada B=128/N=256, zero-padding e overlap-add sem latência adicional; produto com ganho 1/N, compensando a IFFT normalizada do JUCE. M/S em grupos SIMD de quatro. Os 37 coeficientes Bode agora são recuperados com igualdade dos 74 valores L/R no construtor e instalados por bits IEEE-754. Foi removida a aproximação por FIR Hilbert; a rotação usa diretamente o par all-pass recuperado. Curva completa do compressor, mistura/tempos originais e agendamento/kernel FFT original não foram recuperados; esses pontos continuam sendo implementações independentes. |
| 5. Estado e presets | Formato Aurora v2 e ferramentas implementados | AuroraState\0 + u64LE comprimento JSON + JSON + u32LE tamanho descomprimido + u32LE seletor 2 + frame Zstd(CBOR). Estado é árvore estruturada, sem XML opaco no payload v2. Metadados, campos desconhecidos suportados e dados binários preservados. Migração de XML/AuroraJson antigos, migração estruturada v1→v2, rejeição de versão futura e entradas inválidas. O marcador Aurora é próprio; não pretende escrever presets Serum equivalentes nem recalcular hash estrangeiro desconhecido. Banco de 626 presets originais gerados, sem copiar assets/presets comerciais. |
| 6. Expressão por voz/MPE | Implementação funcional; conformidade completa pendente | Note on/off, pressão polifônica, IDs VST3 independentes, volume/pan/tuning/expressão/brilho por nota; MPE lower/upper e RPN por MPEZoneLayout, bend de membro/mestre, pressão, CC74 e propagação de pedais do mestre. Quatro envelopes e dez LFOs com fase/tempo/valor independentes por voz. Filtro continua um bus compartilhado, usando média da modulação das vozes. INoteExpressionController agora anuncia Volume/Pan/Tuning/Brightness no bus 0/canal 0 com MPE desligado; a lista fica vazia com MPE ligado. Há teste real do controller e aviso kNoteExpressionChanged. O parâmetro MPE enabled preserva default ligado para compatibilidade com a 0.2.0. IDs nativos duplicados substituem a voz antiga. Não anunciar MPE “completo”: ainda faltam suíte oficial/hardware/FL, mapeamento físico de expressão, vibrato/texto/fonemas nativos e integração completa de IDs nativos ao arp/Mono/Legato. |
| 7. Numérico/SIMD | Implementado nos núcleos definidos | MXCSR salvo/restaurado por RAII com DAZ/FTZ. MAC complexo FFT e M/S usam quatro lanes SSE, com fallback escalar e tratamento da cauda. Não se paralelizam quatro instantes de um SVF recursivo como se fossem independentes. Não há alegação de ganho de CPU medido nem de processamento GPU. |

## Separação de domínios de parâmetros

- `docs/canonical-parameters.json`: inventário estático estrangeiro, provenance e descritores desconhecidos explícitos.
- `docs/reference-parameter-families.json`: encoders/instâncias e observações da investigação fornecida pelo usuário.
- `docs/aurora-parameter-catalog.json`: catálogo executável do Aurora exportado pelo gerador de fábrica. IDs de host antigos são preservados.
- IDs locais de preset estrangeiro são traduzidos por `SerumImport.h`; nunca são IDs de controles Aurora. O adaptador canonical→Aurora implementa os RoutingSlots confirmados. Não existe mapeamento inventado para parâmetros ainda sem equivalente.
- A fronteira da UI anterior traduz o antigo valor normalizado de RoutingDest; o processador/host/novo estado usam plain 0..3. `routingPlainVersion=2` distingue o novo estado e migra presets antigos antes de aplicá-los.

## Arquivos principais

`Source/CoreInfrastructure.h`, `Source/ExactKernels.h`, `Source/Preset.h`, `Source/Cbor.h`, `Source/CoreTests.h`, `Source/NativeVst3Tests.h`, `Source/Plugin.cpp`, `Source/Tests.cpp`. Patches JUCE em `juce_AudioProcessor.h` e `juce_audio_plugin_client_VST3.cpp` são parte obrigatória da distribuição de fonte. Usar JUCE 7 sem esses patches perde os offsets/IDs nativos.

## Validação

Validação concluída: 74 checks C++/VST3 aprovados (incluindo três presets de fábrica renderizados), 626 estados distintos decodificados e submetidos a round-trip offline, e migração/JSON/inputs inválidos validados pela ferramenta offline. Os resultados estão registrados em `validation.txt`, `docs/offline-validation.txt` e `docs/factory-validation.txt`. Os testes verificam contratos do Aurora e as equações confirmadas; não substituem comparação de áudio com o original. A sprint não será declarada integralmente concluída enquanto as lacunas acima permanecerem.

## Evidência runtime incorporada nesta revisão

`docs/runtime-host-parameters.json`: nomes, unidades vazias preservadas, passos, flags, padrões normalizados e cinco amostras plain/texto. `docs/parameter-domains.json` separa enumeração runtime de inventário interno. `docs/bode-original-coefficients.json` e `Source/BodeCoefficients.h` preservam os 37 doubles sem arredondamento decimal. SHA do alvo investigado: 33369e196ae66d05d20485f3c5e800707dc34a5ad89c27959250d60387b2ea55; evidência do construtor: ev_6cdc7bb472df72a379e6782150efb4826df06910121eca2a26863d63b6909a98. A série foi observada em 48 kHz; equivalência de todo Bode/todas as taxas continua não demonstrada.

A enumeração do original não substitui os parâmetros do Aurora por IDs estrangeiros. Os novos registros são referência documentada; preservar IDs Aurora mantém sessões existentes. Ainda faltam mapear os domínios internos/host/preset com evidência suficiente. Não há mais uma pendência artificial de “2.602 descritores host” calculada do modelo estático.

## Avanço wavetable após 0.2.2

Integrados o interpolador recuperado 16 taps/512 fases e a transição de 64 avaliações, com estado por subvoz. O fixture confronta 1.092 valores capturados no alvo histórico. Construção/seleção de níveis, warps e equivalência integral continuam pendentes. Veja [WAVETABLE_ADVANCE.md](docs/WAVETABLE_ADVANCE.md).

## Revisão 0.2.3 — desempenho e interface

Tratados os gargalos de configuração/limpeza por amostra, caches por voz, consultas de parâmetros, geração visual ociosa, cópia de samples no estado e preparação/duplicação de coeficientes de IR. All parameters usa composição comum e exclusividade de visibilidade. Os gráficos não criam mais contextos OpenGL. [Medições e limites](docs/OPTIMIZATION_023.md). A validação em FL Studio continua dependente de confirmação no host real.

Validação 0.2.3: 79 verificações aprovadas. Os três renders de comparação de desempenho coincidiram byte a byte. O teste adicional confronta automação de filtro/ressonância/volume com o caminho conservador, usando filtros audíveis e cauda de delay.


## Integração 0.2.3

Implementados e testados: níveis compactos periódicos (12 níveis/4.287 floats por frame), interpolação com guardas, transições sem reinício durante fade, morph/discreto, pan/detune/random/fase, até 16 subvozes, dois slots de warp confirmados e dependências acíclicas entre osciladores. Preservados os IDs e ranges anteriores, com novos parâmetros anexados ao catálogo.

Importador: JSON/zlib, CBOR/zlib e CBOR/Zstandard; conversões por parâmetro; defaults declarados; nomes/IDs locais sem correspondências inventadas; fonte/metadados desconhecidos preservados no estado nativo. A compatibilidade sonora integral permanece pendente. Quantize sem constantes confirmadas, curvas remap ausentes, shapers/filtros externos, diferenças em efeitos/envelopes e agendamento global do PRNG não são declarados equivalentes. Casos sem implementação são reportados ou rejeitados. Ver docs/WAVETABLE_PRESET_023.md. Validação final: 90 PASS, zero FAIL.
