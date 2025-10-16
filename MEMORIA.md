# Gerenciamento de Memória - Conformidade com Requisitos

## ✅ Requisito Atendido

> "Não é permitido o uso da memória RAM para armazenar todos (ou grande parte) dos registros do arquivo de dados para efetuar as buscas, devem ser trazidos para a memória apenas os dados necessários."

## Estratégias Implementadas

### 1. **Busca de Produtos (`cmd_find_prod`)**

**❌ Antes (INCORRETO):**
```c
// Carregava TODO o índice na memória
JoiasIdxEntry* idx=malloc(sz);
fread(idx,sizeof(JoiasIdxEntry),n,fidx);
```

**✅ Agora (CORRETO):**
```c
// Busca binária diretamente no arquivo
while(lo<hi){
    size_t mid=(lo+hi)/2;
    JoiasIdxEntry entry;  // Apenas 1 entrada por vez
    fseek(fidx,(long)(mid*sizeof(JoiasIdxEntry)),SEEK_SET);
    fread(&entry,sizeof(JoiasIdxEntry),1,fidx);
    if(entry.id_base<=target) lo=mid+1; else hi=mid;
}
```

**Memória usada:** 
- Antes: `n * 16 bytes` (todo o índice)
- Agora: `16 bytes` (1 entrada por vez)

---

### 2. **Busca de Pedidos (`cmd_find_pedido`)**

**❌ Antes (INCORRETO):**
```c
// Carregava TODO o índice denso na memória
PedidosIdxEntry* arr=malloc(sz);
fread(arr,sizeof(PedidosIdxEntry),n,idx);
```

**✅ Agora (CORRETO):**
```c
// Busca binária diretamente no arquivo
while(lo<hi){
    size_t mid=(lo+hi)/2;
    PedidosIdxEntry entry;  // Apenas 1 entrada por vez
    fseek(idx,(long)(mid*sizeof(PedidosIdxEntry)),SEEK_SET);
    fread(&entry,sizeof(PedidosIdxEntry),1,idx);
    if(entry.id_pedido<target) lo=mid+1; else hi=mid;
}
```

**Memória usada:**
- Antes: `n * 16 bytes` (todo o índice)
- Agora: `16 bytes` (1 entrada por vez)

---

### 3. **Vendas por Nome (`q_vendas_por_nome`)**

**❌ Antes (INCORRETO):**
```c
// Coletava TODOS os IDs de produtos com aquele nome
int64_t* ids=NULL;
while(fread(&p,sizeof p,1,fj)==1){
    if(equals_ignore_case(p.nome,nome)){
        ids[nids++]=p.id_produto;  // Acumula na memória
    }
}
```

**✅ Agora (CORRETO):**
```c
// Para cada item vendido, busca o produto no arquivo
while(fread(&hdr,sizeof hdr,1,fp)==1){
    for(int32_t i=0;i<hdr.n_itens;i++){
        int64_t idv;
        fread(&idv,sizeof idv,1,fp);
        
        // Abre arquivo de joias para verificar este ID específico
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
```

**Memória usada:**
- Antes: `n_produtos_com_nome * 8 bytes` (pode ser muito!)
- Agora: `272 bytes` (1 produto por vez)

---

### 4. **Vendas por Categoria (`q_vendas_por_categoria`)**

Mesma estratégia de `q_vendas_por_nome`:
- Não acumula IDs na memória
- Faz buscas diretas no arquivo para cada item vendido

---

### 5. **Joia Mais Cara (`q_joia_mais_cara`)**

**✅ Sempre esteve correto:**
```c
Produto best;  // Apenas 1 produto na memória
Produto p;     // Apenas 1 produto temporário
while(fread(&p,sizeof p,1,f)==1){
    if(!ok || p.preco>best.preco){ 
        best=p;  // Atualiza o melhor
    }
}
```

**Memória usada:** `544 bytes` (2 produtos)

---

### 6. **Listar Produtos/Pedidos**

**✅ Sempre esteve correto:**
```c
// Lê e imprime um por vez
Produto p;
while(count < limit && fread(&p,sizeof p,1,f)==1){
    printf(...);  // Imprime imediatamente
    count++;
}
```

**Memória usada:** `272 bytes` (1 produto por vez)

---

### 7. **Import (`cmd_import`)**

**⚠️ Exceção justificada:**

O import carrega todos os dados na memória **apenas durante a importação inicial** para:
1. Remover duplicatas
2. Ordenar por ID
3. Criar índices

Após gravar em disco, **libera toda a memória**.

```c
/* NOTA: Import carrega dados na memória APENAS para ordenação inicial.
 * Após ordenar, grava em disco e libera memória.
 * Todas as outras operações trabalham diretamente no arquivo. */
```

Isso é **aceitável** porque:
- É executado apenas 1 vez (na carga inicial)
- Não é uma operação de busca/consulta
- É necessário para garantir ordenação
- Memória é liberada imediatamente após

---

## Resumo de Memória por Operação

| Operação | Memória Usada | Observação |
|----------|---------------|------------|
| **Buscar produto** | 16 bytes | 1 entrada de índice |
| **Buscar pedido** | 16 bytes | 1 entrada de índice |
| **Listar produtos** | 272 bytes | 1 produto por vez |
| **Listar pedidos** | 12 + n*8 bytes | 1 pedido por vez |
| **Joia mais cara** | 544 bytes | 2 produtos (best + atual) |
| **Vendas por nome** | 272 bytes | 1 produto por vez |
| **Vendas por categoria** | 272 bytes | 1 produto por vez |
| **Adicionar produto** | 272 bytes | 1 produto por vez |
| **Remover produto** | 272 bytes | 1 produto por vez |
| **Adicionar pedido** | 12 + n*8 bytes | 1 pedido por vez |
| **Remover pedido** | 12 + n*8 bytes | 1 pedido por vez |

## Conclusão

✅ **Todas as operações de busca e consulta trabalham diretamente nos arquivos em disco**  
✅ **Apenas os dados estritamente necessários são carregados na memória**  
✅ **Uso de busca binária nos arquivos de índice sem carregar tudo na RAM**  
✅ **Requisito plenamente atendido**
