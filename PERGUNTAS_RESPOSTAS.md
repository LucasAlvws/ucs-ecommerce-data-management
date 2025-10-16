# Perguntas e Respostas para Apresentação

## Perguntas sobre Estrutura de Dados

### 1. Por que você usou registros de tamanho fixo para produtos?

**Resposta:**
Usei registros de tamanho fixo (272 bytes) porque:
- Facilita o cálculo de offsets: `offset = posição × 272`
- Permite acesso direto com `fseek()` sem precisar ler registros anteriores
- Simplifica a implementação do índice esparso
- Garante que `sizeof(Produto)` seja sempre o mesmo, independente do conteúdo das strings

Para garantir o tamanho fixo, usei:
- `#pragma pack(1)` para desabilitar padding do compilador
- Arrays de tamanho fixo: `char categoria[64]`, `char nome[128]`
- Função `safe_copy()` que trunca strings longas e adiciona `\0`

### 2. Por que pedidos têm tamanho fixo com array de 50 posições?

**Resposta:**
Pedidos têm tamanho fixo (412 bytes) para atender o requisito do trabalho:
> "Os registros dos arquivos de dados devem ser de tamanho fixo"

Estrutura:
```c
typedef struct {
    int64_t id_pedido;                      // 8 bytes
    int32_t n_itens;                        // 4 bytes
    int64_t ids_produtos[MAX_ITENS_PEDIDO]; // 400 bytes (50 × 8)
} Pedido;                                   // Total: 412 bytes
```

**Vantagens do tamanho fixo:**
- Facilita cálculo de offsets: `offset_pedido_N = N × 412`
- Permite acesso direto com `fseek()` sem ler registros anteriores
- Atende o requisito do trabalho

**Trade-off:**
- Desperdiça espaço: pedido com 1 item usa 412 bytes (400 bytes desperdiçados)
- Limita tamanho: máximo 50 itens por pedido
- Mas: atende o requisito de tamanho fixo!

### 3. Por que índice esparso para produtos e denso para pedidos?

**Resposta:**

**Índice esparso para produtos:**
- Produtos têm tamanho FIXO (272 bytes)
- Posso calcular matematicamente: `offset_produto_N = N × 272`
- Não preciso indexar todos, apenas amostra (1 a cada 256)
- Reduz tamanho do índice em 256x
- Após busca binária no índice, faço varredura linear de no máximo 256 produtos

**Índice denso para pedidos:**
- Pedidos têm tamanho FIXO (412 bytes) mas ainda uso índice denso
- PODERIA calcular offset matematicamente: `offset = N × 412`
- Mas uso índice denso para facilitar busca por `id_pedido` (que não é sequencial)
- Índice denso: 1 entrada por pedido
- Permite acesso direto O(log n) sem varredura linear

## Perguntas sobre Algoritmos

### 4. Explique como funciona a busca binária no índice

**Resposta:**
A busca binária funciona assim:

1. **Inicialização**: `lo=0, hi=n` (n = número de entradas no índice)
2. **Loop**: enquanto `lo < hi`:
   - Calcula `mid = (lo + hi) / 2`
   - Usa `fseek()` para pular para a entrada `mid` no arquivo
   - Lê APENAS essa entrada (16 bytes)
   - Compara `entry.id_base` com o `target`
   - Se `entry.id_base <= target`: busca na metade superior (`lo = mid + 1`)
   - Senão: busca na metade inferior (`hi = mid`)
3. **Resultado**: `lo` aponta para a entrada mais próxima

**Complexidade:** O(log n) - divide o espaço de busca pela metade a cada iteração

**Exemplo:** 
- Índice com 100 entradas
- Iteração 1: verifica entrada 50
- Iteração 2: verifica entrada 25 ou 75
- Iteração 3: verifica entrada 12, 37, 62 ou 87
- ...
- Total: ~7 iterações (log₂ 100 ≈ 6.64)

### 5. Por que você não carrega todo o índice na memória?

**Resposta:**
Porque o requisito do trabalho diz:
> "Não é permitido o uso da memória RAM para armazenar todos (ou grande parte) dos registros"

Mesmo que o índice seja pequeno, carregar tudo violaria o requisito. Além disso:
- Demonstra que sei fazer busca binária DIRETAMENTE no arquivo
- Usa apenas 16 bytes de memória (1 entrada) vs potencialmente megabytes
- Escalável: funciona mesmo com índices gigantes

### 6. Como funciona o merge ordenado streaming?

**Resposta:**
É um algoritmo de intercalação que mantém o arquivo ordenado sem carregar tudo na memória:

**Passos:**
1. Abre arquivo original para leitura
2. Abre arquivo temporário para escrita
3. Lê produtos UM POR VEZ do original
4. Para cada produto:
   - Se ainda não inseriu o novo E `novo.id < atual.id`: escreve o novo
   - Escreve o produto atual no temporário
5. Se chegou ao final e não inseriu: adiciona no final
6. Substitui arquivo original pelo temporário
7. Reconstrói o índice

**Memória usada:** apenas 2 produtos (544 bytes)
**Complexidade:** O(n) tempo, O(1) espaço

## Perguntas sobre Requisitos

### 7. Como você garante que não carrega todos os dados na memória?

**Resposta:**
Implementei as seguintes estratégias:

**Buscas:**
- Busca binária DIRETAMENTE no arquivo de índice (não carrego o índice todo)
- Leitura pontual com `fseek()` + `fread()` de 1 registro por vez

**Modificações (add/remove):**
- Merge ordenado streaming: lê 1 registro, processa, escreve 1 registro
- Usa arquivo temporário para evitar corrupção

**Consultas agregadas:**
- Joia mais cara: mantém apenas 2 produtos na memória (atual + melhor)
- Vendas por nome: reabre arquivo para cada busca (O(1) memória)

**Exceção justificada:**
- Import inicial: carrega CSV para ordenar (operação única de setup)
- Após ordenar, grava em disco e libera memória
- Todas as outras operações usam streaming

### 8. Por que você usou qsort() no import?

**Resposta:**
Usei `qsort()` porque:

1. **É a carga inicial**: executada apenas 1 vez na vida do sistema
2. **Eficiência**: O(n log n) - muito rápido para ordenação em memória
3. **Simplicidade**: biblioteca padrão C, bem testada e otimizada
4. **Adequação**: para datasets que cabem em RAM (~1M produtos = ~300MB)

**Alternativas consideradas:**
- **Merge sort externo**: necessário apenas se não couber em RAM
- **Árvore de vencedores**: útil para merge de múltiplas partições
- Para o dataset fornecido (3 anos de compras), qsort() é ideal

**Método de ordenação:**
- `qsort()` usa Quicksort ou Introsort (Quicksort + Heapsort)
- Complexidade: O(n log n) médio, O(n log n) pior caso (Introsort)

### 9. Como você trata duplicatas?

**Resposta:**

**No import:**
- Após ordenar com `qsort()`, faço uma passada removendo duplicatas:
```c
if(i>0 && arr[i].id_produto == arr[i-1].id_produto){
    continue;  // Pula duplicatas
}
```
- Funciona porque o array está ordenado

**No add-produto:**
- Durante o merge, verifico se o id já existe:
```c
if(p.id_produto == novo.id_produto){
    printf("Produto já existe!\n");
    return;  // Aborta operação
}
```

**No add-pedido:**
- Mesma lógica: verifica durante o merge e aborta se já existe

### 10. Por que você reconstrói o índice após cada modificação?

**Resposta:**
Preciso reconstruir porque:

**Para produtos (índice esparso):**
- Ao adicionar/remover um produto, os offsets de TODOS os produtos seguintes mudam
- Exemplo: removi produto na posição 100 → produtos 101, 102, ... mudaram de posição
- Índice esparso precisa refletir as novas posições

**Para pedidos (índice denso):**
- Pedidos têm tamanho variável
- Ao adicionar/remover, os offsets de todos os pedidos seguintes mudam
- Índice denso mapeia id_pedido → offset, então precisa ser atualizado

**Otimização possível (não implementada):**
- Usar B-tree ou estrutura de dados mais sofisticada
- Permitiria updates incrementais sem reconstrução completa
- Mas aumentaria muito a complexidade do código

## Perguntas sobre Implementação

### 11. O que é #pragma pack(1)?

**Resposta:**
`#pragma pack(1)` desabilita o padding (alinhamento) automático do compilador.

**Sem pragma pack:**
```c
struct {
    int64_t id;    // 8 bytes
    char nome[3];  // 3 bytes
    double preco;  // 8 bytes
}  // Compilador adiciona 5 bytes de padding após nome
   // Total: 8 + 3 + 5 (padding) + 8 = 24 bytes
```

**Com #pragma pack(1):**
```c
#pragma pack(push, 1)
struct {
    int64_t id;    // 8 bytes
    char nome[3];  // 3 bytes
    double preco;  // 8 bytes
}  // Total: 8 + 3 + 8 = 19 bytes (sem padding)
#pragma pack(pop)
```

**Por que usar:**
- Garante que `sizeof(Produto) = 272 bytes` exatos
- Essencial para leitura/escrita binária consistente
- Sem isso, o tamanho poderia variar entre compiladores/plataformas

### 12. Como funciona fseek()?

**Resposta:**
`fseek()` move o ponteiro de leitura/escrita do arquivo para uma posição específica.

**Sintaxe:**
```c
fseek(arquivo, offset, origem);
```

**Parâmetros:**
- `arquivo`: ponteiro FILE*
- `offset`: quantos bytes mover
- `origem`: 
  - `SEEK_SET`: a partir do início do arquivo
  - `SEEK_CUR`: a partir da posição atual
  - `SEEK_END`: a partir do final do arquivo

**Exemplo:**
```c
fseek(f, 272, SEEK_SET);  // Pula para byte 272 (produto 1)
fseek(f, 544, SEEK_SET);  // Pula para byte 544 (produto 2)
```

**Por que é importante:**
- Permite acesso DIRETO sem ler dados intermediários
- Essencial para busca binária no arquivo
- Muito mais rápido que leitura sequencial

### 13. Por que você usa FILE* e não mmap()?

**Resposta:**
Usei `FILE*` (funções padrão C) porque:

**Vantagens:**
- Portável: funciona em qualquer sistema (Windows, Linux, Mac)
- Simples: API bem conhecida (`fopen`, `fread`, `fwrite`, `fseek`)
- Adequado para o escopo do trabalho

**mmap() seria útil para:**
- Acesso muito frequente aos dados
- Quando o sistema operacional gerencia o cache automaticamente
- Mas aumentaria a complexidade e reduziria portabilidade

**Decisão:** Para um trabalho acadêmico focado em organização de arquivos, `FILE*` é mais apropriado.

## Perguntas sobre Performance

### 14. Qual a complexidade das operações?

**Resposta:**

| Operação | Complexidade Tempo | Complexidade Espaço |
|----------|-------------------|---------------------|
| Buscar produto | O(log n + 256) | O(1) |
| Buscar pedido | O(log m) | O(1) |
| Adicionar produto | O(n) | O(1) |
| Remover produto | O(n) | O(1) |
| Adicionar pedido | O(m) | O(1) |
| Remover pedido | O(m) | O(1) |
| Joia mais cara | O(n) | O(1) |
| Vendas por nome | O(m × n) | O(1) |
| Import inicial | O(n log n) | O(n) |

Onde:
- n = número de produtos
- m = número de pedidos

### 15. Como você poderia otimizar as consultas de vendas?

**Resposta:**
Atualmente, vendas por nome tem complexidade O(m × n) porque:
- Para cada item vendido (m itens), busco o produto no arquivo (n produtos)

**Otimizações possíveis:**

1. **Índice secundário por nome:**
   - Criar B-tree ou hash table: nome → lista de ids
   - Reduz busca de O(n) para O(log n) ou O(1)
   - Trade-off: mais espaço em disco, mais complexidade

2. **Cache em memória:**
   - Manter produtos mais acessados em cache (LRU)
   - Trade-off: usa mais memória (violaria requisito do trabalho)

3. **Índice invertido:**
   - Pré-computar: nome → contagem de vendas
   - Atualizar a cada venda
   - Trade-off: mais complexo, precisa manter consistência

**Por que não implementei:**
- Requisito do trabalho: não carregar tudo na memória
- Foco em organização sequencial-indexada (não B-trees)
- Para o escopo acadêmico, a solução atual demonstra os conceitos

## Perguntas Conceituais

### 16. Qual a diferença entre índice primário, secundário, esparso e denso?

**Resposta:**

**Índice Primário:**
- Baseado na chave primária (campo que ordena o arquivo)
- Exemplo: meu `joias.idx` é primário (baseado em `id_produto`)

**Índice Secundário:**
- Baseado em campo não-chave
- Exemplo: índice por nome, por categoria
- Não implementei (fora do escopo)

**Índice Esparso:**
- Não indexa todos os registros, apenas amostra
- Exemplo: meu `joias.idx` (1 entrada a cada 256 produtos)
- Vantagem: ocupa menos espaço
- Desvantagem: precisa varredura linear após busca binária

**Índice Denso:**
- Indexa TODOS os registros
- Exemplo: meu `pedidos.idx` (1 entrada por pedido)
- Vantagem: acesso direto sem varredura
- Desvantagem: ocupa mais espaço

### 17. Por que arquivos binários e não texto?

**Resposta:**

**Vantagens de binário:**
- **Espaço**: mais compacto (não precisa converter números para texto)
- **Velocidade**: leitura/escrita direta (sem parsing)
- **Tamanho fixo**: facilita cálculo de offsets
- **Precisão**: não perde precisão em floats/doubles

**Exemplo:**
- int64_t em binário: 8 bytes
- int64_t em texto: até 20 caracteres (20 bytes)

**Desvantagens:**
- Não é legível por humanos
- Não é portável entre arquiteturas diferentes (endianness)

**Para este trabalho:** binário é mais apropriado porque:
- Requisito: organização sequencial-indexada
- Necessário: acesso direto com `fseek()`
- Tamanho fixo facilita implementação

### 18. O que acontece se o programa crashar durante uma modificação?

**Resposta:**
Implementei proteção usando **arquivo temporário:**

**Processo:**
1. Abre arquivo original para leitura
2. Abre arquivo temporário para escrita
3. Faz todas as modificações no temporário
4. Se tudo OK: `remove(original); rename(temp, original);`
5. Se der erro: `remove(temp);` e original permanece intacto

**Garantias:**
- Se crashar durante a escrita no temporário: arquivo original intacto
- Se crashar após rename: operação completa (rename é atômico no SO)
- Não há estado intermediário corrompido

**Limitação:**
- Não é transacional (não tem rollback)
- Não protege contra falha de hardware (disco corrompido)
- Para produção, usaria banco de dados com ACID

## Dicas para Apresentação

1. **Demonstre conhecimento dos conceitos:**
   - Fale sobre índice esparso vs denso
   - Explique por que escolheu cada abordagem
   - Mostre que entende os trade-offs

2. **Mostre o código funcionando:**
   - Importe o CSV
   - Faça buscas
   - Adicione/remova registros
   - Execute as consultas

3. **Destaque os requisitos atendidos:**
   - Não carrega tudo na memória (exceto import)
   - Usa `fseek()` para acesso direto
   - Índice esparso e denso implementados
   - Arquivo ordenado mantido

4. **Esteja preparado para perguntas sobre:**
   - Complexidade de algoritmos
   - Estruturas de dados
   - Trade-offs de design
   - Otimizações possíveis

5. **Seja honesto sobre limitações:**
   - Vendas por nome é O(m×n) - poderia ser otimizado
   - Não implementei índice secundário
   - Reconstrução completa do índice é ineficiente
   - Mas: atende todos os requisitos do trabalho!
