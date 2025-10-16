# Mudanças para Tamanho Fixo em Pedidos

## ✅ Alteração Realizada

Conforme o requisito do trabalho:
> "Os registros dos arquivos de dados devem ser de tamanho fixo"

Modifiquei a estrutura de pedidos de **tamanho variável** para **tamanho fixo**.

## Antes (Tamanho Variável)

```c
typedef struct {
    int64_t id_pedido;  // 8 bytes
    int32_t n_itens;    // 4 bytes
} PedidoHdr;            // 12 bytes

// Seguido por n_itens valores int64_t (tamanho variável)
```

**Problema:** Violava o requisito de tamanho fixo.

## Depois (Tamanho Fixo)

```c
#define MAX_ITENS_PEDIDO 50

typedef struct {
    int64_t id_pedido;                      // 8 bytes
    int32_t n_itens;                        // 4 bytes
    int64_t ids_produtos[MAX_ITENS_PEDIDO]; // 400 bytes (50 × 8)
} Pedido;                                   // Total: 412 bytes FIXO
```

**Solução:** Array de tamanho fixo com 50 posições.

## Mudanças no Código

### 1. Estrutura de Dados
- Renomeado `PedidoHdr` → `Pedido`
- Adicionado array fixo `ids_produtos[50]`
- Tamanho: 412 bytes (sempre o mesmo)

### 2. Escrita de Pedidos
**Antes:**
```c
fwrite(&hdr, sizeof hdr, 1, f);
for(int i=0; i<n_itens; i++){
    fwrite(&id_produto, sizeof(int64_t), 1, f);
}
```

**Depois:**
```c
Pedido ped;
memset(&ped, 0, sizeof(Pedido));
ped.id_pedido = ...;
ped.n_itens = ...;
for(int i=0; i<n_itens; i++){
    ped.ids_produtos[i] = ...;
}
fwrite(&ped, sizeof ped, 1, f);  // Escreve 412 bytes de uma vez
```

### 3. Leitura de Pedidos
**Antes:**
```c
fread(&hdr, sizeof hdr, 1, f);
for(int i=0; i<hdr.n_itens; i++){
    fread(&id_produto, sizeof(int64_t), 1, f);
}
```

**Depois:**
```c
Pedido ped;
fread(&ped, sizeof ped, 1, f);  // Lê 412 bytes de uma vez
for(int i=0; i<ped.n_itens; i++){
    int64_t id = ped.ids_produtos[i];  // Acesso direto ao array
}
```

### 4. Reconstrução de Índice
**Antes:**
```c
fread(&hdr, sizeof hdr, 1, f);
fseek(f, hdr.n_itens * sizeof(int64_t), SEEK_CUR);  // Pula IDs
```

**Depois:**
```c
fread(&ped, sizeof ped, 1, f);  // Já leu tudo, não precisa pular
```

## Vantagens

1. ✅ **Atende o requisito:** registros de tamanho fixo
2. ✅ **Simplifica o código:** não precisa ler IDs separadamente
3. ✅ **Facilita cálculo de offsets:** `offset = posição × 412`
4. ✅ **Acesso direto:** `fseek()` funciona perfeitamente

## Desvantagens (Trade-offs)

1. ⚠️ **Desperdício de espaço:**
   - Pedido com 1 item: usa 412 bytes (400 bytes desperdiçados)
   - Pedido com 50 itens: usa 412 bytes (sem desperdício)
   - Média: ~20 itens por pedido → ~240 bytes desperdiçados por pedido

2. ⚠️ **Limitação de tamanho:**
   - Máximo 50 itens por pedido
   - Pedidos maiores são truncados (com aviso no import)

3. ⚠️ **Arquivo maior:**
   - Antes: tamanho variável (eficiente)
   - Depois: sempre 412 bytes (pode ser maior)

## Justificativa

Apesar das desvantagens, a mudança é **necessária** porque:
- O requisito do trabalho **exige** tamanho fixo
- É uma decisão de design comum em sistemas de arquivos
- Facilita implementação de índices e acesso direto
- Trade-off aceitável para fins acadêmicos

## Arquivos Atualizados

1. ✅ `trabalho.c` - Código ajustado
2. ✅ `README.md` - Documentação atualizada
3. ✅ `PERGUNTAS_RESPOSTAS.md` - Perguntas atualizadas
4. ✅ `MEMORIA.md` - Análise de memória (ainda válida)

## Teste

Para testar a mudança:
```bash
make clean
make
./trabalho
# Escolha opção 1 (import)
# Escolha opção 3 (buscar pedido)
```

O sistema agora trabalha com registros de tamanho fixo em ambos os arquivos! 🎯
