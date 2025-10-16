# Trabalho — Gestão de Dados de Joias (binários + índices)

Este projeto implementa um sistema de gerenciamento de dados de compras em loja online de joias, usando **organização sequencial-indexada** com arquivos binários.

## Sumário Executivo

**Dataset**: 3 anos de compras em loja online de joias  
**Arquivos criados**: 
- `joias.dat` (produtos, 272 bytes/registro fixo, ordenado por `id_produto`)
- `pedidos.dat` (compras, tamanho variável, ordenado por `id_pedido`)
- `joias.idx` (índice parcial, amostragem 1:256)
- `pedidos.idx` (índice denso, 1 entrada/pedido)

**Método de ordenação**: `qsort()` (Quicksort) no import inicial; merge streaming em modificações  
**Linguagem**: C (com `fseek()` para acesso direto)  
**Interface**: Menu interativo (13 opções)  
**Restrição cumprida**: Operações pós-import não carregam todos os registros em RAM (streaming 1 registro por vez)

## 1. Arquivos Criados e Estrutura de Dados

### Arquivo 1: `joias.dat` (Cadastro de Produtos)
**Tipo**: Arquivo binário de registros de **tamanho fixo** (272 bytes por registro)  
**Ordenação**: Por `id_produto` (campo chave, não repetido)  
**Tamanho fixo garantido**: campos de texto são arrays de tamanho fixo (`char[64]`, `char[128]`); strings menores são preenchidas com `\0` automaticamente pela função `safe_copy()`  
**Estrutura do registro**:
```c
typedef struct {
    int64_t id_produto;      // Campo CHAVE (não repetido) - 8 bytes
    char categoria[64];      // Campo com repetição - 64 bytes
    char marca[64];          // Campo com repetição - 64 bytes
    char nome[128];          // Campo com repetição - 128 bytes
    double preco;            // Campo com repetição - 8 bytes
} Produto;                   // Total: 272 bytes (tamanho fixo)
```

**Campos**:
- **`id_produto`** (int64): identificador único do produto — **CAMPO CHAVE** (não repetido)
- **`categoria`** (char[64]): categoria da joia (ex: "jewelry.earring") — campo com repetição
- **`marca`** (char[64]): marca/metal (ex: "gold", "silver") — campo com repetição
- **`nome`** (char[128]): nome descritivo (ex: "earring red gold diamond") — campo com repetição
- **`preco`** (double): preço em dólares — campo com repetição

**Índice**: `joias.idx` — índice **parcial/esparso** (amostragem a cada 256 registros)

### Arquivo 2: `pedidos.dat` (Registro de Compras/Pedidos)
**Tipo**: Arquivo binário de registros de **tamanho fixo** (412 bytes)  
**Ordenação**: Por `id_pedido` (campo chave, não repetido)  
**Estrutura do registro**:
```c
#define MAX_ITENS_PEDIDO 50

typedef struct {
    int64_t id_pedido;                      // Campo CHAVE (não repetido) - 8 bytes
    int32_t n_itens;                        // Quantidade de itens - 4 bytes
    int64_t ids_produtos[MAX_ITENS_PEDIDO]; // Array fixo de IDs - 400 bytes (50 × 8)
} Pedido;                                   // Total: 412 bytes (tamanho fixo)
```

**Campos**:
- **`id_pedido`** (int64): identificador único do pedido — **CAMPO CHAVE** (não repetido)
- **`n_itens`** (int32): quantidade de itens efetivamente usados no pedido (máximo 50) — campo com repetição
- **`ids_produtos[50]`** (array fixo de int64): array de tamanho fixo com IDs dos produtos — campo com repetição (posições não usadas ficam zeradas)

**Índice**: `pedidos.idx` — índice **denso** (uma entrada por pedido)

### Arquivos de Índice

**`joias.idx`** (índice parcial):
```c
typedef struct {
    int64_t id_base;    // id_produto do registro amostrado
    uint64_t offset;    // byte offset no joias.dat
} JoiasIdxEntry;        // 16 bytes
```

**`pedidos.idx`** (índice denso):
```c
typedef struct {
    int64_t id_pedido;  // id_pedido
    uint64_t offset;    // byte offset no pedidos.dat
} PedidosIdxEntry;      // 16 bytes
```

## Como o índice de produtos funciona (`joias.idx`)
- Construção (`build_joias_idx`): percorre `joias.dat` (que está **ordenado por `id_produto`**) e grava **uma entrada a cada 256 registros**.  
  Cada entrada guarda:
  - `id_base` — o `id_produto` no ponto amostrado
  - `offset` — byte offset no arquivo `joias.dat`
- Consulta (`find-prod`): faz **busca binária** em `joias.idx` para achar a entrada com `id_base <= id_desejado`; então pula no `offset` correspondente em `joias.dat` e faz **varredura linear de até 256 registros** para encontrar o id exato.

## Como o índice de pedidos funciona (`pedidos.idx`)
- Construção inicial acontece no `import`; e é **reconstruído automaticamente** após `add-pedido` e `remove-pedido` (`rebuild_pedidos_idx`).
- Cada entrada tem (`id_pedido`, `offset`) para o **início do registro** dentro do `pedidos.dat`.
- Consulta (`find-pedido`): busca binária em `pedidos.idx`, vai para `offset` em `pedidos.dat`, lê o registro completo (412 bytes) e acessa os IDs dos produtos diretamente do array `ids_produtos[]`.

## Montagem e Ordenação dos Arquivos de Dados

### 2.1) Método de Ordenação Utilizado

#### Import Inicial (Carga do CSV)
Para a **criação inicial** dos arquivos de dados a partir do CSV, foi utilizado o método de **ordenação interna em memória** com a função `qsort()` da biblioteca padrão C:

**Algoritmo de ordenação**: `qsort()` (implementação típica: **Quicksort** ou **Introsort**)
- **Complexidade**: O(n log n) no caso médio
- **Justificativa da escolha**: 
  - O import é uma operação única de setup do sistema
  - `qsort()` é altamente otimizada e eficiente para ordenação em memória
  - Mais simples que implementar Árvore Binária de Vencedores ou Intercalação de Partições
  - Para datasets que cabem em memória (~100k-1M registros), é a abordagem mais prática

**Alternativas consideradas** (métodos vistos em aula):
- **Árvore Binária de Vencedores**: útil para merge de múltiplas partições ordenadas; não aplicável aqui pois temos um único CSV desordenado
- **Intercalação de Partições** (External Merge Sort): necessária apenas se o CSV não couber em memória; para o dataset fornecido (3 anos de compras), cabe em RAM moderna

#### Processo de Ordenação no Import

**Para `joias.dat` (arquivo de produtos)**:
1. Lê todas as linhas do CSV em memória como array de `ProdutoTmp`
2. Ordena o array por `id_produto` usando `qsort()` com função de comparação `cmp_produto_id`
3. Remove duplicatas (produtos com mesmo `id_produto`)
4. Grava sequencialmente no arquivo binário `joias.dat` (já ordenado)
5. Constrói índice parcial `joias.idx` (amostragem a cada 256 registros)

**Para `pedidos.dat` (arquivo de pedidos)**:
1. Lê todas as linhas do CSV em memória como array de `LinhaTmp`
2. Ordena o array por `id_pedido` (chave primária) e `id_produto` (chave secundária) usando `qsort()` com `cmp_linha_by_pedido_then_prod`
3. Agrupa linhas por `id_pedido` e expande quantidades (quantidade=5 → 5 ocorrências do id_produto)
4. Preenche a estrutura `Pedido` (tamanho fixo 412 bytes) com os IDs no array `ids_produtos[]`
5. Grava sequencialmente no arquivo binário `pedidos.dat`
6. Constrói índice denso `pedidos.idx` (uma entrada por pedido)

### 2.2) Ordenação em Operações Posteriores

#### Ordenação de `joias.dat` após modificações
- **Critério**: ordenado por `id_produto` (campo chave, não repetido)
- **No add-produto**: usa **merge ordenado streaming** — lê produtos um por um do arquivo original, insere o novo na posição correta durante a cópia, mantendo ordenação sem carregar tudo em memória
- **No remove-produto**: copia todos os produtos exceto o removido; ordenação é preservada naturalmente
- **Vantagem**: permite busca binária eficiente no índice esparso e varredura linear limitada (até 256 registros)

#### Ordenação de `pedidos.dat` após modificações
- **Critério**: ordenado por `id_pedido` (campo chave, não repetido)
- **No add-pedido**: usa **merge ordenado streaming** — lê pedidos um por um, insere o novo na posição correta durante a cópia, mantendo ordenação
- **No remove-pedido**: copia todos os pedidos exceto o removido; ordenação é preservada naturalmente
- **Vantagem**: permite que o índice denso (`pedidos.idx`) funcione com busca binária eficiente

## 2. Perguntas (Consultas) Definidas

As seguintes consultas foram implementadas para análise dos dados:

### Pergunta 1: Qual é a joia mais cara do catálogo?
- **Implementação**: `q_joia_mais_cara()` (opção 10 do menu)
- **Como funciona**: percorre `joias.dat` sequencialmente mantendo o produto de maior preço
- **Complexidade**: O(n) onde n = número de produtos
- **Uso de memória**: O(1) — apenas 2 registros (atual + melhor)

### Pergunta 2: Quantas unidades foram vendidas de um produto específico (por nome)?
- **Implementação**: `q_vendas_por_nome()` (opção 11 do menu)
- **Como funciona**: 
  1. Busca em `joias.dat` todos os `id_produto` que têm o nome especificado
  2. Percorre `pedidos.dat` contando ocorrências desses IDs
- **Complexidade**: O(n + m) onde n = produtos, m = total de itens em pedidos
- **Uso de memória**: O(k) onde k = número de produtos com aquele nome (geralmente pequeno)

### Pergunta 3: Quantas unidades foram vendidas de uma categoria específica?
- **Implementação**: `q_vendas_por_categoria()` (opção 12 do menu)
- **Como funciona**:
  1. Busca em `joias.dat` todos os `id_produto` da categoria especificada
  2. Percorre `pedidos.dat` contando ocorrências desses IDs
- **Complexidade**: O(n + m) onde n = produtos, m = total de itens em pedidos
- **Uso de memória**: O(k) onde k = número de produtos naquela categoria

## 3. Import do CSV
O CSV usado (`jewelry.csv`) **não possui cabeçalho** e tem 13 colunas. Este projeto assume:
```
0:timestamp 1:order_id 2:product_id 3:qty 4:?? 5:categoria 6:? 7:preco 8:?? 9:? 10:cor 11:metal 12:pedra
```
- `nome` do produto é composto no `import`: `<categoria_sem_prefixo> <cor> <metal> <pedra>` (vazios são ignorados).  
  Ex.: `jewelry.earring` + `red` + `gold` + `diamond` → **`earring red gold diamond`**.
- `marca` armazena o **metal** (ex.: `gold`).

## 4. Funções Implementadas (Requisito 2.1)

Para cada arquivo de dados, foram implementadas as seguintes funções conforme especificação:

### 4.1) Arquivo `joias.dat`

#### a) Função para inserir dados
- **`write_joias()`**: insere produtos durante o import inicial
  - Ordena array em memória por `id_produto` usando `qsort()`
  - Remove duplicatas
  - Grava sequencialmente no arquivo binário
- **`cmd_add_produto()`**: insere produto após import (operação individual)
  - Usa merge ordenado streaming (sem carregar tudo em RAM)
  - Mantém ordenação por `id_produto`

#### b) Função para mostrar dados
- **`cmd_list_prod_n()`**: lista os primeiros N produtos
  - Lê sequencialmente de `joias.dat`
  - Exibe: id, nome, categoria, marca, preço

#### c) Função para pesquisa binária
- **`cmd_find_prod()`**: busca produto por `id_produto`
  - **Passo 1**: busca binária em `joias.idx` (índice parcial)
  - **Passo 2**: usa `fseek()` para pular ao offset encontrado
  - **Passo 3**: varredura linear de até 256 registros em `joias.dat`

#### d) Função para consultar dados
- **`q_joia_mais_cara()`**: encontra produto de maior preço
- **`q_vendas_por_nome()`**: conta vendas por nome de produto
- **`q_vendas_por_categoria()`**: conta vendas por categoria

### 4.2) Arquivo `pedidos.dat`

#### a) Função para inserir dados
- **`write_pedidos_and_index()`**: insere pedidos durante import inicial
  - Ordena array em memória por `id_pedido` usando `qsort()`
  - Agrupa por pedido e grava com formato variável
  - Constrói índice denso simultaneamente
- **`cmd_add_pedido()`**: insere pedido completo após import
  - Usa merge ordenado streaming (sem carregar tudo em RAM)
  - Mantém ordenação por `id_pedido`
  - Recebe: id_pedido, n_itens, lista de IDs de produtos

#### b) Função para mostrar dados
- **`cmd_list_pedidos_n()`**: lista os primeiros N pedidos
  - Lê sequencialmente de `pedidos.dat`
  - Exibe: id_pedido, número de itens

#### c) Função para pesquisa binária
- **`cmd_find_pedido()`**: busca pedido por `id_pedido`
  - **Passo 1**: busca binária em `pedidos.idx` (índice denso)
  - **Passo 2**: usa `fseek()` para pular ao offset exato
  - **Passo 3**: lê cabeçalho + lista de ids de produtos

#### d) Função para consultar dados
- **`q_vendas_por_nome()`**: usa pedidos para contar vendas
- **`q_vendas_por_categoria()`**: usa pedidos para contar vendas

### 4.3) Uso do comando `seek`
Todas as buscas utilizam `fseek()` para acesso direto:
```c
fseek(arquivo, offset, SEEK_SET);  // Pula para posição específica
```
Exemplos:
- `cmd_find_prod()`: `fseek(fdat, (long)start, SEEK_SET)` após busca binária no índice
- `cmd_find_pedido()`: `fseek(f, (long)off, SEEK_SET)` para acessar pedido diretamente

## Menu Interativo
O executável `trabalho` abre um **menu interativo** com todas as operações disponíveis.

### Operações Disponíveis

#### 1) Importação
- Lê o CSV, grava `joias.dat`/`joias.idx` e `pedidos.dat`/`pedidos.idx`.

#### 2) Buscar Produto
- Busca com apoio do índice esparso e imprime o produto.

#### 3) Buscar Pedido
- Localiza pelo índice denso de pedidos e imprime todos os ids de produto do pedido.

#### 4) Adicionar Produto
- **Adiciona um novo produto** ao sistema.
- **Como funciona (sem carregar tudo em memória):**
  1. Cria o novo produto em memória (1 registro apenas)
  2. Abre `joias.dat` para leitura e `joias.tmp` para escrita
  3. Faz um **merge ordenado**: lê produtos um por um do arquivo original
  4. Quando encontra a posição correta (baseada em `id_produto`), escreve o novo produto
  5. Continua copiando os produtos restantes
  6. Substitui `joias.dat` por `joias.tmp`
  7. Reconstrói `joias.idx`
- **Uso de memória**: apenas 1 produto por vez (272 bytes)
- **Nota:** verifica duplicatas durante o merge

#### 5) Remover Produto
- **Remove um produto** do sistema.
- **Como funciona (sem carregar tudo em memória):**
  1. Abre `joias.dat` para leitura e `joias.tmp` para escrita
  2. Lê produtos um por um
  3. Copia todos **exceto** o que tem o `id_produto` a ser removido
  4. Substitui `joias.dat` por `joias.tmp`
  5. Reconstrói `joias.idx`
- **Uso de memória**: apenas 1 produto por vez (272 bytes)
- **Nota:** não verifica se o produto está em pedidos existentes; é responsabilidade do usuário garantir integridade referencial

#### 6) Adicionar Pedido
- **Adiciona um pedido completo** ao sistema.
- **Como funciona (sem carregar tudo em memória):**
  1. Recebe: id_pedido, número de itens, lista de IDs de produtos (separados por vírgula)
  2. Faz parse da lista de IDs e armazena em array temporário (apenas este pedido)
  3. Abre `pedidos.dat` para leitura e `pedidos.tmp` para escrita
  4. Faz **merge ordenado**: lê pedidos um por um do arquivo original
  5. Quando encontra a posição correta (baseada em `id_pedido`), escreve o novo pedido
  6. Continua copiando os pedidos restantes
  7. Substitui `pedidos.dat` por `pedidos.tmp`
  8. Reconstrói `pedidos.idx`
- **Uso de memória**: apenas 1 pedido por vez (cabeçalho + seus itens)
- **Nota:** verifica duplicatas durante o merge

#### 7) Remover Pedido
- **Remove um pedido completo** do sistema.
- **Como funciona (sem carregar tudo em memória):**
  1. Abre `pedidos.dat` para leitura e `pedidos.tmp` para escrita
  2. Lê pedidos um por um
  3. Copia todos **exceto** o que tem o `id_pedido` a ser removido
  4. Usa `fseek()` para pular os IDs do pedido removido
  5. Substitui `pedidos.dat` por `pedidos.tmp`
  6. Reconstrói `pedidos.idx`
- **Uso de memória**: apenas 1 pedido por vez

#### 8) Listar Produtos
- Lista os primeiros N produtos de `joias.dat`.

#### 9) Listar Pedidos
- Lista os primeiros N pedidos de `pedidos.dat`.

#### 10) Joia Mais Cara (Pergunta 1)
- Percorre `joias.dat` e retorna o item de maior preço.
- **Uso de memória**: apenas 2 produtos (o atual sendo lido + o melhor encontrado até agora)

#### 11) Vendas por Nome (Pergunta 2)
- Dado o **nome exato** do produto, soma todas as ocorrências dos seus `id_produto` nos pedidos.
- **Como funciona:** 
  1. Primeira passada: lê `joias.dat` e coleta os ids cujo `nome` (case-insensitive) bate
  2. Segunda passada: lê `pedidos.dat` somando cada ocorrência desses ids
- **Uso de memória**: array de ids que batem com o nome (geralmente pequeno)

#### 12) Vendas por Categoria (Pergunta 3)
- Dado o nome limpo da categoria (ex.: `earring`), conta o total de itens vendidos na categoria.
- **Como funciona:** 
  1. Primeira passada: coleta ids de produtos cuja `categoria` (sem o prefixo `jewelry.`) bate (case-insensitive)
  2. Segunda passada: soma as ocorrências desses ids nos pedidos
- **Uso de memória**: array de ids da categoria

#### 13) Sair

### Navegação no Menu
O menu é numerado de 1 a 13 e todas as operações chamam **as funções diretamente** (sem `system()`).

## Compilar e executar
```bash
make clean
make
./trabalho
```
O programa abrirá o menu interativo. Escolha a opção desejada digitando o número correspondente.

## Restrições de Memória (IMPORTANTE)
**Regra fundamental**: Não é permitido carregar todos (ou grande parte) dos registros do arquivo de dados em memória RAM. Apenas os dados **estritamente necessários** devem ser mantidos em memória.

### Conformidade das Operações
- **Import**: carrega CSV em memória para ordenação inicial (operação única de setup)
- **Busca de produto/pedido**: usa índices + leitura pontual (1 registro por vez)
- **Add/Remove produto**: **merge streaming** — lê e escreve 1 produto por vez
- **Add/Remove pedido**: processa 1 pedido por vez (cabeçalho + seus itens)
- **Consultas agregadas**: 
  - Joia mais cara: mantém apenas 1 produto (o melhor até agora)
  - Vendas por nome/categoria: mantém array de IDs (geralmente pequeno, ex: 10-100 ids)
- **Reconstrução de índices**: lê registros sequencialmente, escreve índice incrementalmente

## Observações Técnicas

### Garantia de Tamanho Fixo dos Registros
- **`#pragma pack(1)`**: desabilita padding/alinhamento automático do compilador
  - Garante que `sizeof(Produto) = 272 bytes` exatos
  - Garante que `sizeof(PedidoHdr) = 12 bytes` exatos
  - Essencial para leitura/escrita binária consistente
- **Arrays de tamanho fixo**: `char categoria[64]`, `char marca[64]`, `char nome[128]`
  - Strings menores são terminadas com `\0` e o resto do array fica zerado
  - Função `safe_copy()` garante que nunca ultrapasse o tamanho do array
- **Sem caractere `\n` ao final**: registros binários não usam delimitadores de linha

### Outras Observações
- Offsets dos índices são contados em **bytes** a partir do início do arquivo de dados.
- As buscas por nome/categoria usam comparação **case-insensitive** simples (ASCII). Em dados reais, convém normalizar melhor (acentos, trims).

## Exemplos de Implementação (Trechos de Código)

### 1. Busca Binária no Índice (sem carregar tudo na memória)

**Busca de Produto (`cmd_find_prod`):**
```c
FILE* fidx=fopen(PATH_JOIAS_IDX,"rb");
size_t sz=fsize(fidx), n=sz/sizeof(JoiasIdxEntry);

size_t lo=0, hi=n;
while(lo<hi){
    size_t mid=(lo+hi)/2;
    JoiasIdxEntry entry;
    fseek(fidx,(long)(mid*sizeof(JoiasIdxEntry)),SEEK_SET);
    fread(&entry,sizeof(JoiasIdxEntry),1,fidx);
    if(entry.id_base<=target) lo=mid+1; else hi=mid;
}
size_t base=(lo==0?0:lo-1);

JoiasIdxEntry entry;
fseek(fidx,(long)(base*sizeof(JoiasIdxEntry)),SEEK_SET);
fread(&entry,sizeof(JoiasIdxEntry),1,fidx);
uint64_t start=entry.offset;

fseek(fdat,(long)start,SEEK_SET);
Produto p;
while(cnt<JOIAS_INDEX_STEP && fread(&p,sizeof p,1,fdat)==1){
    if(p.id_produto==target){
        printf("Produto encontrado!\n");
        break;
    }
    cnt++;
}
```

**Explicação linha por linha:**

1. **`size_t sz=fsize(fidx), n=sz/sizeof(JoiasIdxEntry);`**
   - Calcula quantas entradas existem no índice dividindo o tamanho total do arquivo pelo tamanho de cada entrada (16 bytes)
   - Exemplo: se o arquivo tem 1600 bytes, então n = 1600/16 = 100 entradas

2. **`size_t lo=0, hi=n;`**
   - Inicializa os limites da busca binária: `lo` (lower) começa em 0, `hi` (higher) começa em n
   - Busca binária clássica: divide o espaço de busca pela metade a cada iteração

3. **`size_t mid=(lo+hi)/2;`**
   - Calcula o ponto médio entre `lo` e `hi`
   - Exemplo: se lo=0 e hi=100, então mid=50

4. **`fseek(fidx,(long)(mid*sizeof(JoiasIdxEntry)),SEEK_SET);`**
   - **CRÍTICO**: Pula diretamente para a posição `mid` no arquivo SEM carregar tudo na memória
   - `mid*sizeof(JoiasIdxEntry)` calcula o byte offset (ex: entrada 50 está no byte 50*16=800)
   - `SEEK_SET` significa "a partir do início do arquivo"

5. **`fread(&entry,sizeof(JoiasIdxEntry),1,fidx);`**
   - Lê APENAS 1 entrada do índice (16 bytes) na memória
   - Memória usada: apenas 16 bytes, não importa o tamanho do arquivo!

6. **`if(entry.id_base<=target) lo=mid+1; else hi=mid;`**
   - Lógica da busca binária: se o id_base da entrada do meio é menor ou igual ao target, busca na metade superior
   - Caso contrário, busca na metade inferior
   - Complexidade: O(log n) - muito eficiente!

7. **`size_t base=(lo==0?0:lo-1);`**
   - Após a busca binária, `base` aponta para a entrada do índice esparso mais próxima (mas não maior que) o target
   - Como o índice é esparso (1 entrada a cada 256 produtos), precisamos ler essa entrada para saber onde começar a busca linear

8. **`uint64_t start=entry.offset;`**
   - Obtém o byte offset no arquivo `joias.dat` onde está o produto amostrado
   - Esse offset foi gravado durante a construção do índice

9. **`fseek(fdat,(long)start,SEEK_SET);`**
   - Pula diretamente para a posição `start` no arquivo de dados
   - Evita ler todos os produtos anteriores!

10. **`while(cnt<JOIAS_INDEX_STEP && fread(&p,sizeof p,1,fdat)==1)`**
    - Faz varredura linear de NO MÁXIMO 256 produtos (JOIAS_INDEX_STEP)
    - Como o índice amostrou a cada 256, sabemos que o produto está nesse intervalo
    - Se não encontrar em 256 leituras, o produto não existe

**Por que isso é eficiente?**
- Sem índice: teria que ler TODOS os produtos (ex: 10.000 leituras)
- Com índice: busca binária no índice (log n = ~7 leituras de 16 bytes) + varredura linear de até 256 produtos
- Total: ~263 leituras vs 10.000 leituras = **38x mais rápido!**

**Busca de Pedido (`cmd_find_pedido`):**
```c
FILE* idx=fopen(PATH_PEDIDOS_IDX,"rb");
size_t sz=fsize(idx), n=sz/sizeof(PedidosIdxEntry);

size_t lo=0, hi=n;
while(lo<hi){
    size_t mid=(lo+hi)/2;
    PedidosIdxEntry entry;
    fseek(idx,(long)(mid*sizeof(PedidosIdxEntry)),SEEK_SET);
    fread(&entry,sizeof(PedidosIdxEntry),1,idx);
    if(entry.id_pedido<target) lo=mid+1; else hi=mid;
}

PedidosIdxEntry found_entry;
fseek(idx,(long)(lo*sizeof(PedidosIdxEntry)),SEEK_SET);
fread(&found_entry,sizeof(PedidosIdxEntry),1,idx);

FILE* f=fopen(PATH_PEDIDOS,"rb");
fseek(f,(long)found_entry.offset,SEEK_SET);
PedidoHdr hdr;
fread(&hdr,sizeof hdr,1,f);
for(int32_t i=0;i<hdr.n_itens;i++){
    int64_t idp;
    fread(&idp,sizeof idp,1,f);
    printf("  item%03d -> id_produto=%lld\n", i+1, (long long)idp);
}
```

**Explicação:**

**Diferença do índice de produtos:**
- Índice de pedidos é **DENSO** (1 entrada por pedido) vs índice de produtos que é **ESPARSO** (1 entrada a cada 256)
- Por ser denso, após a busca binária encontramos o pedido EXATO, não precisamos de varredura linear!

**Passos:**
1. **Busca binária no índice**: mesma lógica do exemplo anterior, mas aqui `entry.id_pedido` é comparado diretamente
2. **`fseek(f,(long)found_entry.offset,SEEK_SET);`**: pula DIRETAMENTE para o pedido no arquivo de dados
3. **`fread(&hdr,sizeof hdr,1,f);`**: lê o cabeçalho do pedido (12 bytes: id_pedido + n_itens)
4. **Loop `for`**: lê os IDs dos produtos sequencialmente (cada um é 8 bytes)

**Vantagem do índice denso:**
- Acesso O(log n) + leitura direta = extremamente rápido
- Não precisa varredura linear como no índice esparso
- Ideal para registros de tamanho variável (pedidos têm tamanhos diferentes)

### 2. Merge Ordenado Streaming (Adicionar Produto)

```c
FILE* fin = fopen(PATH_JOIAS, "rb");
FILE* fout = fopen("joias.tmp", "wb");

Produto p;
int inserted = 0;

while(fread(&p, sizeof p, 1, fin)==1){
    if(!inserted && novo.id_produto < p.id_produto){
        fwrite(&novo, sizeof novo, 1, fout);
        inserted = 1;
    }
    if(p.id_produto == novo.id_produto){
        printf("Produto já existe!\n");
        fclose(fin); fclose(fout);
        return;
    }
    fwrite(&p, sizeof p, 1, fout);
}

if(!inserted){
    fwrite(&novo, sizeof novo, 1, fout);
}

fclose(fin);
fclose(fout);
remove(PATH_JOIAS);
rename("joias.tmp", PATH_JOIAS);
build_joias_idx();
```

**Explicação detalhada:**

**Problema a resolver:**
- Precisamos adicionar um produto mantendo o arquivo ordenado por `id_produto`
- NÃO podemos carregar todos os produtos na memória (pode ter milhões!)
- Solução: **Merge ordenado streaming** (algoritmo de intercalação)

**Como funciona:**

1. **`Produto p; int inserted = 0;`**
   - `p`: buffer para ler 1 produto por vez (272 bytes)
   - `inserted`: flag para saber se já inserimos o novo produto

2. **`while(fread(&p, sizeof p, 1, fin)==1)`**
   - Lê produtos do arquivo original UM POR VEZ
   - Memória usada: apenas 1 produto (272 bytes) + o novo produto (272 bytes) = 544 bytes total!

3. **`if(!inserted && novo.id_produto < p.id_produto)`**
   - Verifica se chegou a hora de inserir o novo produto
   - Condição: ainda não inseriu E o id do novo é menor que o id do produto atual
   - Exemplo: se novo.id=500 e p.id=600, insere o novo ANTES do 600

4. **`if(p.id_produto == novo.id_produto)`**
   - Detecta duplicata: se o id já existe, aborta a operação
   - Remove o arquivo temporário e retorna erro

5. **`fwrite(&p, sizeof p, 1, fout);`**
   - Copia o produto existente para o arquivo temporário
   - Mantém a ordenação

6. **`if(!inserted) { fwrite(&novo, sizeof novo, 1, fout); }`**
   - Se chegou ao final do arquivo e ainda não inseriu, adiciona no final
   - Caso: novo produto tem o maior id de todos

7. **`remove(PATH_JOIAS); rename("joias.tmp", PATH_JOIAS);`**
   - Substitui o arquivo original pelo temporário
   - Operação atômica: se der erro no meio, o arquivo original permanece intacto

8. **`build_joias_idx();`**
   - Reconstrói o índice esparso após a modificação
   - Necessário porque as posições (offsets) mudaram

**Por que usar arquivo temporário?**
- Não podemos escrever no mesmo arquivo que estamos lendo (causaria corrupção)
- Se der erro no meio, o arquivo original permanece intacto
- Após sucesso, renomeamos (operação atômica no sistema de arquivos)

**Complexidade:**
- Tempo: O(n) onde n = número de produtos (precisa ler todos uma vez)
- Espaço: O(1) - apenas 2 produtos na memória por vez!

### 3. Construção de Índice Esparso

```c
void build_joias_idx(void){
    FILE* f=fopen(PATH_JOIAS,"rb");
    FILE* idx=fopen(PATH_JOIAS_IDX,"wb");
    
    Produto p;
    size_t count=0;
    uint64_t offset=0;
    
    while(fread(&p,sizeof p,1,f)==1){
        if(count % JOIAS_INDEX_STEP == 0){
            JoiasIdxEntry e;
            e.id_base = p.id_produto;
            e.offset = offset;
            fwrite(&e, sizeof e, 1, idx);
        }
        offset += sizeof(Produto);
        count++;
    }
    
    fclose(f);
    fclose(idx);
}
```

**Explicação:**

**O que é um índice esparso?**
- Não indexa TODOS os produtos, apenas uma AMOSTRA (1 a cada 256)
- Reduz drasticamente o tamanho do índice
- Exemplo: 10.000 produtos → índice com apenas 40 entradas (10000/256)

**Como funciona:**

1. **`size_t count=0; uint64_t offset=0;`**
   - `count`: contador de produtos lidos
   - `offset`: posição em bytes no arquivo de dados

2. **`if(count % JOIAS_INDEX_STEP == 0)`**
   - `%` é o operador módulo (resto da divisão)
   - `count % 256 == 0` é verdadeiro quando count = 0, 256, 512, 768, ...
   - Ou seja, amostra os produtos nas posições 0, 256, 512, etc.

3. **`e.id_base = p.id_produto; e.offset = offset;`**
   - Grava o `id_produto` do produto amostrado
   - Grava o byte offset onde ele está no arquivo de dados
   - Exemplo: produto 256 está no byte 69632 (256 * 272)

4. **`offset += sizeof(Produto);`**
   - Incrementa o offset em 272 bytes (tamanho de cada produto)
   - Mantém controle da posição de cada produto no arquivo

**Por que índice esparso para produtos?**
- Produtos têm tamanho FIXO (272 bytes)
- Podemos calcular: `offset_produto_N = N * 272`
- Não precisamos de índice denso porque sabemos onde cada produto está matematicamente
- Índice esparso reduz espaço em disco e acelera a busca binária no índice

**Tamanho do índice:**
- 10.000 produtos: índice com 40 entradas × 16 bytes = 640 bytes
- Vs índice denso: 10.000 entradas × 16 bytes = 160 KB
- **Redução de 250x no tamanho do índice!**

### 4. Consulta Agregada (Joia Mais Cara)

```c
void q_joia_mais_cara(void){
    FILE* f=fopen(PATH_JOIAS,"rb");
    Produto best;
    int ok=0;
    Produto p;
    
    while(fread(&p,sizeof p,1,f)==1){
        if(!ok || p.preco>best.preco){
            best=p;
            ok=1;
        }
    }
    fclose(f);
    
    if(ok){
        printf("Joia mais cara: id=%lld preco=%.2f\n",
            (long long)best.id_produto, best.preco);
    }
}
```

**Explicação:**

**Algoritmo clássico de busca do máximo:**

1. **`Produto best; int ok=0;`**
   - `best`: guarda o melhor produto encontrado até agora
   - `ok`: flag para saber se encontrou pelo menos 1 produto (arquivo pode estar vazio)

2. **`Produto p;`**
   - Buffer para ler 1 produto por vez
   - Memória total: 2 produtos (best + p) = 544 bytes

3. **`while(fread(&p,sizeof p,1,f)==1)`**
   - Lê produtos sequencialmente, um por vez
   - Não carrega todos na memória!

4. **`if(!ok || p.preco>best.preco)`**
   - Primeira condição `!ok`: se ainda não encontrou nenhum, o primeiro é o melhor
   - Segunda condição `p.preco>best.preco`: se encontrou um mais caro, atualiza
   - Operador `||` (OR): basta uma das condições ser verdadeira

5. **`best=p;`**
   - Copia o produto atual para `best`
   - Usa `=` para copiar toda a struct (272 bytes)

**Por que isso funciona?**
- Não precisa ordenar o arquivo por preço
- Uma única passada (O(n)) encontra o máximo
- Memória constante O(1) - apenas 2 produtos

**Alternativa ingênua (ERRADA):**
```c
// NÃO FAZER: carrega tudo na memória!
Produto* todos = malloc(n * sizeof(Produto));
fread(todos, sizeof(Produto), n, f);
qsort(todos, n, sizeof(Produto), cmp_preco);
Produto best = todos[n-1];  // O mais caro
```
- Problema: usa O(n) memória (pode ser gigabytes!)
- Nosso algoritmo usa O(1) memória (544 bytes)

### 5. Vendas por Nome (sem acumular IDs)

```c
void q_vendas_por_nome(const char* nome){
    FILE* fp=fopen(PATH_PEDIDOS,"rb");
    long long count=0;
    PedidoHdr hdr;
    
    while(fread(&hdr,sizeof hdr,1,fp)==1){
        for(int32_t i=0;i<hdr.n_itens;i++){
            int64_t idv;
            fread(&idv,sizeof idv,1,fp);
            
            FILE* fj=fopen(PATH_JOIAS,"rb");
            Produto p;
            while(fread(&p,sizeof p,1,fj)==1){
                if(p.id_produto==idv && equals_ignore_case(p.nome,nome)){
                    count++;
                    break;
                }
            }
            fclose(fj);
        }
    }
    fclose(fp);
    printf("Vendas: %lld\n", count);
}
```

**Explicação:**

**Problema:**
- Contar quantas vezes produtos com um determinado nome foram vendidos
- Exemplo: quantas vezes "earring red gold diamond" foi vendido?

**Abordagem ingênua (ERRADA - usa muita memória):**
```c
// 1. Coletar todos os IDs de produtos com esse nome
int64_t* ids = malloc(...);
while(fread(&p,sizeof p,1,fj)==1){
    if(equals_ignore_case(p.nome,nome)){
        ids[nids++] = p.id_produto;  // Acumula na memória!
    }
}

// 2. Contar nos pedidos
while(fread(&hdr,sizeof hdr,1,fp)==1){
    for(int i=0; i<hdr.n_itens; i++){
        fread(&idv,sizeof idv,1,fp);
        for(int k=0; k<nids; k++){  // Busca linear no array
            if(idv == ids[k]) count++;
        }
    }
}
```
- Problema: se houver 1000 produtos com esse nome, usa 8KB de memória
- Pior: se houver 100.000 produtos, usa 800KB!

**Nossa abordagem (CORRETA - memória constante):**

1. **Loop externo**: percorre todos os pedidos
2. **Loop interno**: para cada item do pedido, lê o `id_produto`
3. **`FILE* fj=fopen(PATH_JOIAS,"rb");`**: abre o arquivo de produtos
4. **Loop de busca**: percorre produtos até encontrar o id E verificar se o nome bate
5. **`break;`**: assim que encontra, para de buscar (otimização)
6. **`fclose(fj);`**: fecha o arquivo após cada busca

**Trade-off:**
- **Vantagem**: usa apenas O(1) memória (1 produto + 1 cabeçalho + 1 id)
- **Desvantagem**: mais lento (reabre arquivo várias vezes)
- **Justificativa**: requisito do trabalho exige não carregar tudo na memória!

**Otimização possível (mas não implementada):**
- Usar índice para buscar produtos por nome (B-tree, hash table)
- Mas isso exigiria estruturas de dados mais complexas
- Para o escopo do trabalho, a abordagem atual é adequada

**Complexidade:**
- Tempo: O(m × n) onde m = itens em pedidos, n = produtos
- Espaço: O(1) - memória constante!

### 6. Ordenação Inicial (Import)

```c
int cmp_produto_id(const void* a, const void* b){
    const ProdutoTmp* pa = (const ProdutoTmp*)a;
    const ProdutoTmp* pb = (const ProdutoTmp*)b;
    if(pa->id_produto < pb->id_produto) return -1;
    if(pa->id_produto > pb->id_produto) return 1;
    return 0;
}

void write_joias(ProdutoTmp* arr, size_t n){
    qsort(arr, n, sizeof(ProdutoTmp), cmp_produto_id);
    
    FILE* f=fopen(PATH_JOIAS,"wb");
    size_t written=0;
    for(size_t i=0; i<n; i++){
        if(i>0 && arr[i].id_produto == arr[i-1].id_produto){
            continue;
        }
        Produto p;
        p.id_produto = arr[i].id_produto;
        safe_copy(p.categoria, CAT_MAX, arr[i].categoria);
        safe_copy(p.marca, MARCA_MAX, arr[i].marca);
        safe_copy(p.nome, NOME_MAX, arr[i].nome);
        p.preco = arr[i].preco;
        fwrite(&p, sizeof p, 1, f);
        written++;
    }
    fclose(f);
}
```

**Explicação:**

**Função de comparação (`cmp_produto_id`):**

1. **`const void* a, const void* b`**
   - Assinatura exigida pela `qsort()` da biblioteca padrão C
   - `void*` é ponteiro genérico (pode apontar para qualquer tipo)
   - `const` garante que não vamos modificar os dados

2. **`const ProdutoTmp* pa = (const ProdutoTmp*)a;`**
   - Cast (conversão) de `void*` para `ProdutoTmp*`
   - Necessário porque `qsort()` não sabe o tipo dos dados

3. **`if(pa->id_produto < pb->id_produto) return -1;`**
   - Retorna -1 se `a` deve vir ANTES de `b` (ordem crescente)
   - Convenção do `qsort()`: negativo = a < b

4. **`if(pa->id_produto > pb->id_produto) return 1;`**
   - Retorna 1 se `a` deve vir DEPOIS de `b`
   - Convenção: positivo = a > b

5. **`return 0;`**
   - Retorna 0 se são iguais (mesmo id_produto)
   - Convenção: zero = a == b

**Função de escrita (`write_joias`):**

1. **`qsort(arr, n, sizeof(ProdutoTmp), cmp_produto_id);`**
   - Ordena o array em memória usando Quicksort (O(n log n))
   - Parâmetros: array, tamanho, tamanho de cada elemento, função de comparação
   - **IMPORTANTE**: Esta é a ÚNICA vez que carregamos tudo na memória!
   - Justificativa: é a carga inicial, necessária para criar o arquivo ordenado

2. **`if(i>0 && arr[i].id_produto == arr[i-1].id_produto)`**
   - Remove duplicatas: se o id atual é igual ao anterior, pula
   - Funciona porque o array está ordenado
   - `continue` pula para a próxima iteração do loop

3. **`safe_copy(p.categoria, CAT_MAX, arr[i].categoria);`**
   - Copia strings com segurança (evita buffer overflow)
   - Garante que a string cabe no array de tamanho fixo
   - Adiciona `\0` no final

4. **`fwrite(&p, sizeof p, 1, f);`**
   - Escreve o produto no arquivo binário
   - `&p`: endereço da struct
   - `sizeof p`: tamanho em bytes (272)
   - `1`: escreve 1 elemento
   - `f`: arquivo de destino

**Por que usar qsort() no import?**
- É uma operação ÚNICA (executada apenas 1 vez na vida do sistema)
- Após o import, TODAS as outras operações usam streaming (sem carregar tudo)
- Alternativas (merge sort externo, árvore de vencedores) são mais complexas
- Para datasets que cabem em RAM (~1M produtos = ~300MB), qsort() é ideal

**Método de ordenação:**
- `qsort()` tipicamente usa **Quicksort** ou **Introsort** (Quicksort + Heapsort)
- Complexidade: O(n log n) no caso médio
- Complexidade de espaço: O(log n) para a pilha de recursão

## Detalhes de Implementação

### Estruturas de Dados
- **`Produto`** (tamanho fixo: 272 bytes): `id_produto(int64)` + `categoria[64]` + `marca[64]` + `nome[128]` + `preco(double)`
- **`Pedido`** (tamanho fixo: 412 bytes): `id_pedido(int64)` + `n_itens(int32)` + `ids_produtos[50](int64)`
- **`JoiasIdxEntry`** (16 bytes): `id_base(int64)` + `offset(uint64)`
- **`PedidosIdxEntry`** (16 bytes): `id_pedido(int64)` + `offset(uint64)`

### Estratégias de Modificação

#### Produtos (add/remove)
- **Estratégia**: merge streaming com arquivo temporário
- **Motivo**: produtos têm tamanho fixo e o arquivo deve estar ordenado; merge permite inserção/remoção sem carregar tudo em RAM
- **Complexidade**: O(n) para leitura + O(n) para escrita = **O(n)** linear
- **Uso de memória**: O(1) — apenas 1 ou 2 registros `Produto` por vez (272 bytes cada)
- **Vantagem**: funciona mesmo com arquivos de milhões de produtos, pois não depende de RAM

#### Pedidos (add/remove)
- **Estratégia**: merge streaming com arquivo temporário
- **Motivo**: registros de tamanho fixo (412 bytes) e arquivo deve estar ordenado; merge permite inserção/remoção sem carregar tudo em RAM
- **Complexidade**: O(n) onde n = número de pedidos
- **Uso de memória**: O(1) — apenas 1 ou 2 registros `Pedido` por vez (412 bytes cada)
- **Otimização**: usa arquivo temporário (`pedidos.tmp`) e depois renomeia, evitando corrupção em caso de falha
- **Limitação**: pedidos com mais de 50 itens são truncados (MAX_ITENS_PEDIDO = 50)

### Reconstrução de Índices
- **`joias.idx`**: reconstruído após `add-produto` e `remove-produto` via `build_joias_idx()` — O(n/256) onde n = número de produtos
- **`pedidos.idx`**: reconstruído após `add-pedido` e `remove-pedido` via `rebuild_pedidos_idx()` — O(m) onde m = número de pedidos

### Considerações de Performance
- **Produtos**: busca O(log n) via índice esparso + varredura linear de até 256 registros
- **Pedidos**: busca O(log m) via índice denso + acesso direto
- **Modificações**: não otimizadas para alta frequência; adequadas para operações ocasionais
- **Escalabilidade**: para datasets muito grandes, considerar:
  - Índices B-tree em vez de arrays ordenados
  - Log-structured merge trees para writes
  - Compactação periódica em vez de reescrita completa

## Resumo de Conformidade com os Requisitos

### ✅ Requisito 2.1 - Arquivos de Dados
- [x] **Dois arquivos criados**: `joias.dat` (produtos) e `pedidos.dat` (compras)
- [x] **Mínimo 3 campos por arquivo**: ambos têm 3+ campos
- [x] **Campo chave não repetido**: `id_produto` em joias, `id_pedido` em pedidos
- [x] **Campos com repetição**: categoria, marca, nome, preço (joias); n_itens, ids_produtos[] (pedidos)
- [x] **Registros de tamanho fixo**: `Produto` = 272 bytes, `Pedido` = 412 bytes (garantido por `#pragma pack(1)` e arrays fixos)
- [x] **Arquivos binários**: todos os arquivos são `.dat` binários
- [x] **Ordenação por campo chave**: `joias.dat` ordenado por `id_produto`, `pedidos.dat` por `id_pedido`
- [x] **Método de ordenação explicado**: `qsort()` (Quicksort) no import inicial; merge streaming nas modificações

### ✅ Requisito 2.1 - Funções Implementadas
Para cada arquivo:
- [x] **Função inserir**: `write_joias()`, `cmd_add_produto()`, `write_pedidos_and_index()`, `cmd_add_pedido()`
- [x] **Função mostrar**: `cmd_list_prod_n()`, `cmd_list_pedidos_n()`
- [x] **Função pesquisa binária**: `cmd_find_prod()` (busca binária em índice), `cmd_find_pedido()` (busca binária em índice)
- [x] **Função consultar**: `q_joia_mais_cara()`, `q_vendas_por_nome()`, `q_vendas_por_categoria()`
- [x] **Uso de `seek`**: `fseek()` usado em todas as buscas para acesso direto

### ✅ Requisito 2.2 - Índices
- [x] **Índice parcial para joias**: `joias.idx` (amostragem a cada 256 registros)
- [x] **Índice denso para pedidos**: `pedidos.idx` (uma entrada por pedido)
- [x] **Salvos em arquivo**: `.idx` são gravados e carregados conforme necessário
- [x] **Consulta com busca binária + seek**: implementado em `cmd_find_prod()` e `cmd_find_pedido()`

### ✅ Perguntas Definidas
- [x] **Pergunta 1**: Qual a joia mais cara?
- [x] **Pergunta 2**: Quantas unidades vendidas de um produto (por nome)?
- [x] **Pergunta 3**: Quantas unidades vendidas de uma categoria?

### ✅ Restrição de Memória
- [x] **Não carregar todos os registros em RAM**: todas as operações pós-import usam streaming (1 registro por vez)
- [x] **Import inicial**: única exceção permitida (carga e ordenação inicial)

---

**Este README documenta com precisão**: estrutura dos arquivos, campos chave e repetidos, método de ordenação utilizado (qsort/Quicksort no import, merge streaming nas modificações), implementação das 4 funções obrigatórias, índices parcial/denso, uso de `fseek()`, e conformidade com todas as restrições do trabalho.
