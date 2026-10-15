# Wavetable e importação — 0.2.3

O motor antigo conserva seus parâmetros e comportamento. Os 57 controles novos são anexados ao layout, evitando deslocar os IDs/índices existentes. Presets importados ativam o perfil avançado; controles principais de unison, detune e warp passam a editar esse perfil.

A preparação periódica usa FFT double offline, 12 níveis (2.048 até 1 amostra), 8 guardas de cada lado e 4.287 floats por frame, em lugar de 16.384 floats de níveis completos. O nível zero mantém a entrada. O último nível mantém o par fundamental, removendo DC. Nenhuma FFT ou alocação desses níveis ocorre no render.

Pan/detune são preparados no cache de configuração por voz. Até 16 subvozes, RAND ao quadrado, dois slots de warp em sequência e taps anteriores ao volume/pan. Fontes habilitadas com volume zero continuam disponíveis. Dependências acíclicas são ordenadas; ciclos importados são rejeitados. A sequência do gerador é reproduzida; o agendamento global de reinicialização não é alegado equivalente.

A leitura suporta os carriers JSON/zlib, CBOR/zlib e CBOR/Zstandard, com limites de tamanho e verificação de integridade. Metadados, árvore de origem e flags desconhecidas ficam no estado `.aurora`. Isso permite preservar os dados; não significa que toda flag tenha efeito audível. Não há escritor do formato externo nem hash externo reconstruído.

Modos de warp sem algoritmo/curva confirmado são desabilitados na interface, preservados na origem e reportados no importador. Quantize, remap por curvas, odd/even, shapers e fontes de filtro não estão completos. Ruído e Sub usam os geradores próprios do Aurora. Envelopes, LFOs, efeitos, engines alternativos e defaults incompletos continuam limitações de fidelidade.

## Validação

90 verificações automatizadas, incluindo quatro sinais/48 níveis com erro máximo de 1e-6; 6.800 valores de pan, 768 inicializações de fase e 1.960 casos de modulação externa com igualdade de bits; sequência PRNG; transições; render completo com 16 subvozes e fonte silenciosa; três carriers e corrupção; unidades/defaults; round-trip de assets e origem; instrumento/FX em host VST3 próprio e três presets de fábrica.

O benchmark anterior continua com áudio byte a byte idêntico nos três cenários. O perfil novo tem custo proporcional a subvozes e fontes ativas; as medições anteriores não são promessa de custo para todo preset importado ou DAW.

Os fixtures numéricos são saídas de helpers e sinais sintéticos, sem incluir plugins ou presets comerciais. O código dos algoritmos é independente; material de investigação foi usado como evidência, não como código executável incorporado.
