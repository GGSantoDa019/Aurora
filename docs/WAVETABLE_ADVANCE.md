# Avanço do motor wavetable — preview sobre 0.2.2

Entrada: `serum2-motor-avanco.zip`, SHA-256 `cad81461a939de46f1c1e9488288ce6da3e8d43c9ae5807b7a0fdefb935b4f94`. Alvo histórico do relatório: Serum 2.1.5 x64, SHA-256 `33369e196ae66d05d20485f3c5e800707dc34a5ad89c27959250d60387b2ea55`. Nesta implementação não foi executado o Serum nem reaberto seu DLL; usamos os dados e capturas fornecidos. A identificação do ZIP foi conferida pelo REA; os resultados do kernel são registros históricos do pacote, não uma nova captura REA.

## Implementado

- Interpolação 16 taps, 512 fases, coeficientes base + residual × slope, com 8.192 floats por tabela. Valores incorporados por padrões IEEE-754 em `WavetableCoefficients.h`; hashes conferidos contra o relatório.
- Núcleo SSE2 em quatro lanes com a ordem de redução recuperada e operações separadas; fallback escalar com arredondamentos explícitos.
- Troca de níveis com a recorrência recuperada `new + (remaining*(old-new))*(1/64)`, preservando arredondamentos e decrementando o contador uma vez por avaliação.
- Leitura periódica com sete amostras de pre-roll por wrap; nenhuma leitura fora dos limites na borda da tabela. O estado de nível/transição pertence a cada oscilador e subvoz de unison, não ao recurso compartilhado.
- Integração tanto às tabelas geradas pelo Aurora quanto às wavetables importadas, inclusive as carregadas pelo importador `.serumpreset`.
- Sem alocação ou geração de coeficientes no processamento; inicialização dos coeficientes antecipada na construção das tabelas.

Os coeficientes têm soma aproximada 1,00316..1,00317; esse ganho foi preservado, sem normalização artificial. O teste da FFT verifica o dado antes da leitura e o teste de saída verifica separadamente o ganho conhecido do interpolador.

## Evidência e testes

O fixture usa os dois buffers sintéticos e as mesmas fases do script de referência fornecido. Os valores esperados vêm dos pares capturados na rotina original, não do código novo. São 1.028 avaliações estáveis e 64 transições. O arquivo original de pares tem SHA-256 `1185e2954ed994c98e02acee8109ad38097301d8f62468681bb47e4faa902882`. Os testes adicionais cobrem wrap de fase, ganho DC, transição de nível e reset ao trocar recurso. A suíte geral também cobre render do instrumento/FX, importação, estado e UI.

Os dois JSONs de verificação originais estão em `engine-evidence/`. Evidência histórica da revisão do leitor citada pelo relatório: `ev_00d976810c88f43856bced9b89233ee0115ac0490429ac0f58a8a45c90528ce9`.

## Limites

O resultado bitexato diz respeito ao núcleo nas entradas do fixture. O adaptador periódico e a integração por voz são código Aurora. Continuam próprios: construção FFT dos oito níveis, escolha de nível por frequência, interpolação entre frames, política de nova troca durante transição, limite próximo de Nyquist, warps, unison e ganhos da mistura. Não foi copiado pseudocódigo decompilado como implementação. Não foram incorporados wavetables musicais, samples, presets nem o binário comercial.

Ainda faltam o mapeamento completo de nomes/seletores dos warps, níveis originais, decimação, envelopes/vozes e motores Sample/Multisample/Granular/Spectral equivalentes. Portanto esta preview melhora o caminho wavetable do `.serumpreset`, mas não torna a importação integralmente fiel. Os metadados do plugin permanecem 0.2.2; o pacote é identificado como `engine-preview` para distingui-lo da release anterior.
