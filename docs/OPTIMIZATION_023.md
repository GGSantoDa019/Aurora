# Aurora 0.2.3 — otimização e composição da interface

## Alterações

- Removidos os contextos OpenGL independentes dos osciladores. Os gráficos são componentes comuns, com repintura por mudança e polling a 30 Hz. Não existem mais superfícies nativas sobrepondo os painéis irmãos.
- All parameters agora é um componente interno, sem registrar outro AudioProcessorEditor. Ao abrir, o conteúdo principal e seu teclado/controles inferiores são ocultados. Navegar nas abas fecha o painel. A composição conserva um único teclado visível.
- A configuração de rotas, matriz, macros e curvas é atualizada apenas quando sua família muda, sem a segunda atualização redundante por trecho.
- Consultas de parâmetros usam uma tabela de nomes previamente construída, com acesso para literais sem criação temporária de String. Os nomes das famílias e campos também são indexados.
- Caches por voz para configuração de osciladores, detune/unison, envelopes e LFOs; a matriz de áudio percorre somente rotas ativas. As fases e tempos de todos os LFOs continuam evoluindo, preservando seu comportamento posterior; o cálculo da forma de onda é omitido quando o LFO não possui rota ativa. Curvas de envelopes não utilizados também deixam de ser avaliadas.
- Buffers são limpos somente no trecho usado. Rampas de volume/cutoff/ressonância são preparadas em buffers de controle e aplicadas por amostra, sem repetir toda a cadeia para cada amostra.
- Casos mistos que exigem alteração por voz conservam o caminho de trechos de uma amostra. Portamento ativo também usa o caminho conservador. A precisão temporal não foi removida para ganhar desempenho.
- Coeficientes do SVF são recalculados quando frequência, ressonância ou sample rate mudam.
- O leitor wavetable acessa taps contíguos diretamente no interior do período; o buffer temporário fica restrito às bordas. O kernel e a ordem de operações recuperados permanecem inalterados.
- Samples publicados são compartilhados nas capturas de estado, evitando sua cópia integral ao salvar. Base64/XML/CBOR/Zstd são produzidos após liberar o bloqueio do callback. Ainda há uma região curta de captura de parâmetros/metadados e cópia dos bytes de IR; não se afirma ausência total de bloqueios.
- Samples são analisados antes da publicação. Estado e IRs são decodificados/preparados antes do commit sob bloqueio. A troca usa recursos prontos e o descarte dos antigos ocorre fora da região de commit quando possível.
- Os quatro slots de um rack compartilham os coeficientes imutáveis da convolução, mantendo históricos separados. A preparação FFT da mesma IR não é repetida para cada slot. O FIR direto usa o comprimento efetivo da cabeça.

## Comparativo local

O arquivo `optimization/benchmark.json` registra 16.384 frames por cenário, 48 kHz, blocos de 512, interface fechada. Os casos com notas usam quatro notas simultâneas e unison 4. A automação alterna cutoff com offset 37. Neste fixture o destino inicial é Main; há um teste de regressão separado com filtros audíveis e delay.

| Cenário | Antes, ms de processamento | Depois, ms de processamento |
| --- | ---: | ---: |
| Parado | 72,47 | 1,60 |
| Notas sustentadas | 1.635,51 | 17,30 |
| Automação de cutoff | 19.720,72 | 18,13 |

As três saídas float32 foram idênticas byte a byte. Esses tempos são do processador isolado, em uma execução antes/depois, sujeitos à carga do Windows; não são percentuais de CPU do FL Studio, nem uma promessa para todos os presets. A validação final repetiu a suíte e o comparativo no build entregue: 79 verificações aprovadas.

## Memória e limites

Foram eliminados os três contextos gráficos e a duplicação de coeficientes de IR entre slots. O tamanho da redução de RAM depende do driver, dos assets e das IRs. Não foi atribuída ao plugin a memória total mostrada pela DAW, nem foi comprovado vazamento de memória na versão anterior. Os buffers de delay continuam previamente reservados para permitir trocas de efeito sem alocação na thread de áudio.

A versão não troca os motores por aproximações mais baratas, não reduz automaticamente a polifonia e não descarta caudas de efeitos. Granular/espectral e combinações intensas de modulação ainda podem exigir mais CPU. Otimizações futuras nesses caminhos dependem de perfis e testes específicos.

## Verificação visual

A suíte abre e fecha All parameters, troca para Global, verifica exclusividade do painel/teclado e gera `interface-all-parameters.png`. A captura foi inspecionada sem sobreposição. Há teste de composição sem contextos OpenGL nativos. Ainda é necessária a confirmação visual na DAW do usuário; as verificações automatizadas usam o host de teste próprio.
