# Resultados dos Testes de Desempenho - Índices

## 1. Tempo de Criação dos Índices em Memória

| Índice                                 | Tempo (segundos) |
|----------------------------------------|------------------|
| Índice de Produtos (produtos Arvore B) | 0.001510         |
| Índice de Pedidos (pedidos Hash)       | 0.016698         |

## 2. Comparação de Consultas - Índices de Arquivo vs Índices em Memória

| Consulta Produtos | Índice de Arquivo (segundos) | Índice em Memória Árvore B (segundos) | Linha no Arquivo |
|-------------------|------------------------------|---------------------------------------|------------------|
| Consulta 1:       | 0.000068                     | 0.000040                              | 1                |
| Consulta 2:       | 0.000128                     | 0.000032                              | 250              |
| Consulta 3:       | 0.000072                     | 0.000037                              | 1000             |
| Consulta 4:       | 0.000068                     | 0.000036                              | 1500             |
| Consulta 5:       | 0.000083                     | 0.000038                              | 2000             |

| Consulta Pedidos | Índice de Arquivo (segundos) | Índice em Memória Hash (segundos) | Linha no Arquivo |
|------------------|------------------------------|-----------------------------------|------------------|
| Consulta 1:      | 0.000095                     | 0.000002                          | 1                |
| Consulta 2:      | 0.000074                     | 0.000001                          | 250              |
| Consulta 3:      | 0.000073                     | 0.000002                          | 1000             |
| Consulta 4:      | 0.000078                     | 0.000003                          | 1500             |
| Consulta 5:      | 0.000081                     | 0.000004                          | 2000             |

## 3. Testes de Inserção/Remoção com Recriação de Índices

| Operação            | Tempo de Execução (segundos) |
|---------------------|------------------------------|
| Inserção de Produto | 0.000091                     |
| Remoção de Produto  | 0.000091                     |
| Inserção de Pedido  | 0.000091                     |
| Remoção de Pedido   | 0.000091                     |