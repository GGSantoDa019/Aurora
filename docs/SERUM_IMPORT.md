# Importação experimental de .SerumPreset — Aurora 0.1.2

O Aurora importa uma parte do estado de presets do Serum 2 e adapta esse estado aos seus próprios motores. **Importar não significa reproduzir o som original com fidelidade.** O Aurora continua sendo um sintetizador independente.

## Como usar

1. Abra o instrumento **Aurora** e clique em **Load**.
2. Selecione um arquivo `.SerumPreset` ou `.serumpreset`. O navegador **Presets** também lista essas extensões.
3. Leia o relatório apresentado ao final. Ele informa parâmetros, rotas, assets e limitações encontradas.
4. Salve em `.aurora` para guardar a conversão. Wavetables, samples e IRs carregados são incorporados ao estado nativo.

A versão atual aceita o contêiner **XferJson**, com metadados JSON e payload **CBOR comprimido com Zstandard, encoding 2**. Não lê `.fxp` do Serum 1 nem encodings antigos 0/1.

## Tradução disponível

| Recurso | Comportamento |
| --- | --- |
| Osciladores A/B/C | Seleção de motor, nível, pan, afinação, unison e parâmetros básicos; escalas e limites são adaptados |
| Wavetable | Carrega WAV externo, com períodos de 2.048 amostras, até 256 quadros e oito níveis de limitação harmônica; tabelas maiores são reduzidas |
| Sample | Lê WAV, FLAC e AIFF; converte para mono e assume referência MIDI 60 |
| Granular/Spectral | Carrega o sample referenciado e utiliza o motor simplificado do Aurora |
| Envelopes | Attack, decay, sustain e release; curvas e limites diferem |
| LFOs | Até dez curvas de oito pontos, rate, sync, phase, delay e rise; LFOs importados usam saída unipolar, preservada no `.aurora` |
| Macros/modulação | Até oito macros e 64 rotas; traduz destinos de nível, posição, pitch, pan e cutoff e fontes compatíveis |
| Filtros | Aproximação por SVF low/band/high-pass; modelos, drive e keytrack não são reproduzidos |
| Roteamento | Destino, balanço entre filtros, sends e Via Env 1; rack 0 do preset é adaptado ao rack principal do Aurora |
| FX | Primeiros quatro slots de cada rack, com tipos e parâmetros aproximados; IRs externas e arrays `embeddedIR` compatíveis são carregados |

## Arquivos externos

O importador procura os assets na pasta do preset e em suas pastas ancestrais, nas subpastas `Tables`, `Samples` e `Impulses`, na biblioteca indicada pelas preferências locais do Serum 2 e na biblioteca irmã `Serum Presets`. Também aceita referências absolutas locais existentes e o campo `samplePathRelative` dos presets.

Se faltar uma wavetable, usa a tabela gerada do Aurora e avisa. Se faltar um sample, desativa o oscilador afetado. Uma IR ausente desativa a convolução correspondente. Quando todos os motores utilizáveis dependem de assets ausentes ou não suportados, a importação é recusada e o som anterior é preservado.

**Multisamples SFZ internos ainda não são importados**, mesmo que a biblioteca esteja instalada. Também ficam fora da tradução: warp/FM, áudio de noise sample, formas de sub diferentes da senoide, destinos avançados de modulação, fontes auxiliares, arp/clip patterns e algoritmos específicos do Serum. Os modos de LFO/retrigger, curvas por segmento, divisões dotted/triplet e topologias complexas podem diferir.

Em presets importados, o destino `None` mantém os sends FX e remove o caminho dry. Essa adaptação é preservada no `.aurora`; os presets nativos anteriores conservam o comportamento original de roteamento.

O relatório apresenta essas diferenças; uma importação concluída não é uma indicação de equivalência sonora.

## Validação

A suíte inclui fixtures sintéticos próprios para leitura do contêiner, mapeamento, limites de CBOR, rejeição de arquivos inválidos sem alteração do som, carregamento de wavetable externa, áudio e restauração nativa após remover o arquivo externo. O teste de FFT verifica a amplitude da fundamental da tabela importada.

Também foram examinados presets locais reais de fábrica e do usuário. Uma amostra inicial de três presets importou, produziu áudio finito e restaurou seu estado nativo; uma amostra adicional de doze teve dez conversões com áudio e duas recusas esperadas por multisamples não suportados. Há sobreposição de um preset entre essas amostras. Esses resultados não medem semelhança sonora com o Serum nem cobertura de todas as versões e bibliotecas.

Os presets e assets reais usados nesses testes locais não fazem parte do repositório ou dos pacotes. Os testes públicos usam apenas dados sintéticos próprios.

## Formato consultado

O leitor C++ é independente. As referências públicas usadas para conferir o contêiner e o esquema são:

- [serum-preset-packager: File Format](https://github.com/KennethWussmann/serum-preset-packager#file-format)
- [serum2vital: formatos e fontes de modulação](https://github.com/btesser/serum2vital/blob/main/docs/FORMATS.md)

Os dados de presets locais foram lidos para conferir os campos e testar a implementação. Não foi incorporado código executável do Serum.
