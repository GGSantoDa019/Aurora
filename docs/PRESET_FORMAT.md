# AuroraState v2

Ordem little-endian: marcador `AuroraState\0` (12 bytes), u64 comprimento dos metadados, JSON UTF-8, u32 tamanho CBOR descomprimido, u32 seletor `2`, um frame Zstandard com CBOR. Sem comprimento comprimido redundante: o frame ocupa os bytes restantes e trailing bytes são rejeitados. Limites: arquivo/payload até 128 MiB, metadados até 8 MiB, profundidade CBOR 64 e teto de itens do leitor. Entradas inválidas não substituem o preset atual.

O objeto CBOR contém `schema: 2` e `tree`, uma árvore com `type`, `properties` e `children`. Propriedades de parâmetros mantêm IDs Aurora estáveis. `routingPlainVersion: 2` distingue destino plain 0..3 dos formatos antigos. Metadados JSON e campos adicionais aceitos no objeto de estado são preservados. O subconjunto nativo contém strings, números finitos, bool, null, arrays, mapas de chaves string e bytes; não se promete round-trip de todos os tags/tipos CBOR externos. O formato não é um escritor de SerumPreset.

A leitura mantém compatibilidade com XML binário JUCE e AuroraJson schema 1. A escrita usa v2. A migração estruturada v1→v2 é aditiva; versões futuras são rejeitadas. O XML intermediário é apenas um adaptador para o estado APVTS e o legado; não é o payload opaco do novo contêiner.

O leitor offline aceita AuroraJson, AuroraState e XferJson seletor 2, preservando metadados e estado em JSON. Bytes usam `{"$bytes_base64":"..."}`. O comando pack escreve apenas AuroraState e migra o XML legado. Hashes estrangeiros não são recalculados, porque o algoritmo não foi recuperado. A descompressão é limitada e a ferramenta não executa o plugin nem busca assets externos.
