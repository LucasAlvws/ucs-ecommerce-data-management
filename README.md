# Sistema de Gerenciamento de Dados de E-commerce com Organização Sequencial-Indexada

## 1. Descrição dos Arquivos de Dados

### 1.1. Arquivo de Produtos (`joias.dat`)

O arquivo `joias.dat` armazena o cadastro de produtos (joias) em formato binário, com registros de tamanho fixo. Os registros estão ordenados pelo campo chave `id_produto`, campo não repetido que identifica cada produto no sistema.

A estrutura de dados utilizada para representar cada produto é definida como:

```c
typedef struct {
    int64_t id_produto;      // Campo CHAVE (não repetido)
    char categoria[64];      // Campo com repetição
    char marca[64];          // Campo com repetição
    char nome[128];          // Campo com repetição
    double preco;            // Campo com repetição
} Produto;                   
```

A estrutura possui os seguintes campos:

- `id_produto` (int64_t): Identificador único do produto. Este é o campo chave do arquivo, não admitindo valores repetidos.
- `categoria` (char[64]): Categoria da joia (exemplo: "jewelry.earring"). Campo com valores repetidos.
- `marca` (char[64]): Marca ou tipo de metal (exemplo: "gold", "silver"). Campo com valores repetidos.
- `nome` (char[128]): Nome descritivo do produto (exemplo: "earring red gold diamond"). Campo com valores repetidos.
- `preco` (double): Preço em dólares americanos. Campo com valores repetidos.

Strings menores que o tamanho alocado são preenchidas com caracteres nulos.

Para este arquivo foi construído um índice parcial denominado `joias.idx`, que contém uma entrada para cada 256 registros do arquivo de dados.

### 1.2. Arquivo de Pedidos (`pedidos.dat`)

O arquivo `pedidos.dat` armazena os registros de pedidos (compras) realizados na loja online. Cada registro possui tamanho fixo e os registros estão ordenados pelo campo chave `id_pedido`, que identifica cada pedido no sistema.

A estrutura de dados para representação dos pedidos é:

```c
#define MAX_ITENS_PEDIDO 50

typedef struct {
    int64_t id_pedido;                      // Campo CHAVE (não repetido)
    int32_t n_itens;                        // Quantidade de itens
    int64_t ids_produtos[MAX_ITENS_PEDIDO]; // Array fixo de IDs
} Pedido;                                   
```

Os campos da estrutura são:

- `id_pedido` (int64_t): Identificador único do pedido. Campo chave, não admitindo valores repetidos. Ocupa 8 bytes.
- `n_itens` (int32_t): Quantidade de itens incluídos no pedido, com valor máximo de 50. Campo com valores repetidos. Ocupa 4 bytes.
- `ids_produtos[50]` (int64_t): Array de tamanho fixo contendo os identificadores dos produtos incluídos no pedido. As posições não utilizadas permanecem zeradas. Campo com valores repetidos. Ocupa 400 bytes (50 posições × 8 bytes).

A estrutura de tamanho fixo permite que o sistema armazene até 50 itens por pedido. Pedidos com quantidade de itens superior a este limite são truncados durante a importação. O tamanho fixo facilita o cálculo de offsets e a implementação de operações de busca.

Para este arquivo foi construído um índice denominado `pedidos.idx`, contendo uma entrada para cada pedido armazenado no arquivo de dados.

## 2. Descrição dos Arquivos de Índice

### 2.1. Índice de Produtos (`joias.idx`)

O arquivo `joias.idx` implementa um índice parcial (esparso) para o arquivo de produtos. A estrutura de cada entrada do índice é:

```c
typedef struct {
    int64_t id_base;    // id_produto do registro amostrado
    uint64_t offset;    // byte offset no joias.dat
} JoiasIdxEntry;        
```

Cada entrada do índice contém:

- `id_base` (int64_t): Valor do campo `id_produto` do registro amostrado. Ocupa 8 bytes.
- `offset` (uint64_t): Posição em bytes (byte offset) onde o registro está localizado no arquivo `joias.dat`. Ocupa 8 bytes.

O índice é construído mostrando um registro a cada 256 registros do arquivo de dados (constante `JOIAS_INDEX_STEP = 256`). Desta forma o indice se torna menor e acabou deixando mais rápida a busca por um item.

### 2.2. Índice de Pedidos (`pedidos.idx`)

O arquivo `pedidos.idx` implementa um índice para o arquivo de pedidos. A estrutura de cada entrada é:

```c
typedef struct {
    int64_t id_pedido;  // id_pedido
    uint64_t offset;    // byte offset no pedidos.dat
} PedidosIdxEntry;      
```

Cada entrada contém:

- `id_pedido` (int64_t): Identificador único do pedido. Ocupa 8 bytes.
- `offset` (uint64_t): Posição em bytes onde o registro do pedido inicia no arquivo `pedidos.dat`. Ocupa 8 bytes.

O índice denso mantém uma entrada para cada pedido existente no arquivo de dados. Esta abordagem permite acesso direto aos registros através de busca binária no índice, sem necessidade de varredura adicional no arquivo de dados.

## 3. Métodos de Ordenação Implementados

### 3.1. Manutenção da Ordenação em Operações de Modificação

Após a carga inicial, operações de inserção e remoção de registros devem preservar a ordenação dos arquivos de dados. Para atender à restrição de não carregar todos os registros em memória RAM, foi implementada a técnica de merge ordenado usando um arquivo .tmp.

**Inserção de Produtos**

A operação de inserção de um novo produto no arquivo `joias.dat` é realizada através de merge ordenado:

1. Abertura do arquivo original para leitura e criação de arquivo temporário para escrita;
2. Leitura sequencial dos produtos existentes, um registro por vez;
3. Inserção do novo produto na posição correta (quando seu `id_produto` é menor que o produto corrente);
4. Cópia dos demais produtos subsequentes;
5. Substituição do arquivo original pelo arquivo temporário;
6. Reconstrução do índice `joias.idx`.

**Remoção de Produtos**

A remoção é realizada copiando todos os registros exceto o que possui o `id_produto` a ser removido. A ordenação é naturalmente preservada pois não há alteração na ordem relativa dos registros remanescentes.

**Inserção e Remoção de Pedidos**

As operações sobre o arquivo `pedidos.dat` seguem a mesma estratégia de merge ordenado, mantendo a ordenação por `id_pedido`. Após cada operação, o índice denso `pedidos.idx` é completamente reconstruído através da função `rebuild_pedidos_idx()`.

## 4. Consultas Implementadas

Foram definidas três consultas para análise dos dados armazenados no sistema:

### 4.1. Consulta 1: Produto de Maior Preço

Esta consulta identifica qual produto possui o maior valor de preço no catálogo.

**Implementação**: Função `q_joia_mais_cara()`

**Algoritmo**: Varredura sequencial completa do arquivo `joias.dat`, mantendo em memória o produto de maior preço encontrado até o momento. A cada leitura, compara-se o preço do produto corrente com o melhor preço armazenado, atualizando-o quando necessário.

### 4.2. Consulta 2: Volume de Vendas por Nome de Produto

Esta consulta contabiliza o número total de unidades vendidas de produtos que possuem um determinado nome.

**Implementação**: Função `q_vendas_por_nome()`

**Algoritmo**: Para cada item presente nos pedidos, verifica-se no arquivo de produtos se o nome corresponde ao termo buscado. A comparação é realizada de forma case-insensitive. O contador é incrementado para cada ocorrência encontrada.

### 4.3. Consulta 3: Volume de Vendas por Categoria

Esta consulta contabiliza o número total de unidades vendidas de uma categoria específica de produtos.

**Implementação**: Função `q_vendas_por_categoria()`

**Algoritmo**: Similar à consulta anterior, mas a comparação é realizada sobre o campo categoria. Para cada item nos pedidos, busca-se o produto correspondente e verifica-se se sua categoria corresponde à categoria especificada. A comparação ignora o prefixo "jewelry." presente no dataset.

## 5. Operações do Sistema

O sistema oferece as seguintes operações através de um menu interativo:

### 5.1. Operações de Busca

**Busca de Produto**: Localiza um produto pelo seu `id_produto` utilizando busca binária no índice parcial `joias.idx`, seguida de varredura linear limitada (máximo 256 registros) no arquivo `joias.dat`. Utiliza a função `fseek()` para acesso direto ao offset indicado pelo índice.

**Busca de Pedido**: Localiza um pedido pelo seu `id_pedido` através de busca binária no índice denso `pedidos.idx`, com acesso direto via `fseek()` ao registro no arquivo `pedidos.dat`.

### 5.2. Operações de Modificação

**Inserção**: Implementadas as funções `cmd_add_produto()` e `cmd_add_pedido()` que utilizam merge ordenado com streaming para inserir novos registros mantendo a ordenação. Os índices são reconstruídos após cada operação.

**Remoção**: Implementadas as funções `cmd_remove_produto()` e `cmd_remove_pedido()` que copiam todos os registros exceto o removido para um arquivo temporário. Os índices são reconstruídos após cada operação.

### 5.3. Operações de Listagem

**Listagem de Produtos**: Função `cmd_list_prod_n()` que exibe os primeiros N produtos do arquivo através de leitura sequencial.

**Listagem de Pedidos**: Função `cmd_list_pedidos_n()` que exibe os primeiros N pedidos através de leitura sequencial.

## 6. Formato do Dataset

O arquivo CSV utilizado (`jewelry.csv`) não possui cabeçalho e contém 13 colunas. O sistema extrai as seguintes informações:

- Colunas 1 (`order_id`), 2 (`product_id`), 3 (`qty`): utilizadas para construir os pedidos
- Coluna 5 (`categoria`): categoria do produto (ex: "jewelry.earring")
- Coluna 7 (`preco`): preço do produto em dólares
- Colunas 10 (`cor`), 11 (`metal`), 12 (`pedra`): utilizadas para compor o nome descritivo do produto

O campo `nome` do produto é composto pela concatenação de categoria (sem prefixo "jewelry."), cor, metal e pedra. O campo `marca` armazena o metal especificado na coluna 11.

## 7. Compilação e Execução

O sistema pode ser compilado e executado através dos seguintes comandos:

```bash
make
```
