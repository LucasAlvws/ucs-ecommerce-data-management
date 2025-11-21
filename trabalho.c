#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <time.h>

#define PATH_JOIAS        "joias.dat"
#define PATH_JOIAS_IDX    "joias.idx"
#define PATH_PEDIDOS      "pedidos.dat"
#define PATH_PEDIDOS_IDX  "pedidos.idx"

#define CAT_MAX   64
#define MARCA_MAX 64
#define NOME_MAX  128
#define JOIAS_INDEX_STEP 256
#define MAX_ITENS_PEDIDO 50

// ========================================================================
// TRABALHO II - DEFINIÇÕES PARA ÍNDICES EM MEMÓRIA
// ========================================================================
#define BLOCK_SIZE 4096
#define BTREE_ORDER ((BLOCK_SIZE - sizeof(int) - sizeof(int)) / (sizeof(int64_t) + sizeof(uint64_t) + sizeof(void*)))
#define BTREE_MIN_KEYS ((BTREE_ORDER - 1) / 2)
#define HASH_TABLE_SIZE 10007

typedef struct {
    int64_t id_produto;
    char categoria[CAT_MAX];
    char marca[MARCA_MAX];
    char nome[NOME_MAX];
    double preco;
} Produto;

typedef struct {
    int64_t id_pedido;
    int32_t n_itens;
    int64_t ids_produtos[MAX_ITENS_PEDIDO];
} Pedido;

typedef struct {
    int64_t id_base;
    uint64_t offset;
} JoiasIdxEntry;

typedef struct {
    int64_t id_pedido;
    uint64_t offset;
} PedidosIdxEntry;

// ========================================================================
// TRABALHO II - ESTRUTURAS DA ÁRVORE B
// ========================================================================
typedef struct BTreeNode {
    int n_keys;
    int is_leaf;
    int64_t keys[BTREE_ORDER - 1];
    uint64_t offsets[BTREE_ORDER - 1];
    struct BTreeNode* children[BTREE_ORDER];
} BTreeNode;

typedef struct {
    BTreeNode* root;
    int total_nodes;
    int total_keys;
} BTree;

// ========================================================================
// TRABALHO II - ESTRUTURAS DA TABELA HASH
// ========================================================================
typedef struct HashNode {
    int64_t id_pedido;
    uint64_t offset;
    struct HashNode* next;
} HashNode;

typedef struct {
    HashNode** buckets;
    int size;
    int total_entries;
    int collisions;
} HashTable;

static void die(const char* m){ perror(m); exit(1); }

// Gerar ID único baseado em timestamp
static int64_t gerar_id(void) {
    return (int64_t)time(NULL) * 1000000 + (int64_t)(rand() % 1000000);
}

// Forward declarations
static void build_joias_idx(void);
static void rebuild_pedidos_idx(void);
static void construir_indice_parcial(void) { build_joias_idx(); }
static void construir_indice_exaustivo(void) { rebuild_pedidos_idx(); }
static size_t fsize(FILE* f){
    long p=ftell(f); if(p<0) die("ftell");
    if(fseek(f,0,SEEK_END)!=0) die("fseek end");
    long e=ftell(f); if(e<0) die("ftell end");
    if(fseek(f,p,SEEK_SET)!=0) die("fseek restore");
    return (size_t)e;
}
static void rstrip(char* s){
    size_t n=strlen(s);
    while(n>0 && (s[n-1]=='\n'||s[n-1]=='\r')) s[--n]='\0';
}
static int parse_csv_line(const char* line, char** out, int max, char* buf, size_t bufsz){
    size_t L=strlen(line), w=0; int nf=0, i=0;
    while(i<(int)L){
        if(nf>=max) break;
        while(i<(int)L && (line[i]==' '||line[i]=='\t')) i++;
        if(i>=(int)L) break;
        out[nf]=buf+w;
        int q=0; if(line[i]=='"'){ q=1; i++; }
        while(i<(int)L){
            char c=line[i++];
            if(q){
                if(c=='"'){
                    if(i<(int)L && line[i]=='"'){ if(w+1>=bufsz){ return -1; } buf[w++]='"'; i++; }
                    else break;
                }else{
                    if(w+1>=bufsz){ return -1; } buf[w++]=c;
                }
            }else{
                if(c==',') break;
                if(c=='\r'||c=='\n'){ i=(int)L; break; }
                if(w+1>=bufsz){ return -1; } buf[w++]=c;
            }
        }
        if(w+1>=bufsz){ return -1; } buf[w++]='\0'; nf++;
    }
    return nf;
}
static int try_i64(const char* s, int64_t* out){
    if(!s||!*s){ return 0; } char*e=0; errno=0; long long v=strtoll(s,&e,10);
    if(errno||e==s){ return 0; } *out=(int64_t)v; return 1;
}
static int try_i32(const char* s, int32_t* out){
    if(!s||!*s){ return 0; } char*e=0; errno=0; long v=strtol(s,&e,10);
    if(errno||e==s){ return 0; } *out=(int32_t)v; return 1;
}
static int try_f64(const char* s, double* out){
    if(!s||!*s){ return 0; } char*e=0; errno=0; double v=strtod(s,&e);
    if(errno||e==s){ return 0; } *out=v; return 1;
}
static void safe_copy(char* dst, size_t cap, const char* src){
    if(!src) src="";
    size_t n=strlen(src); if(n>=cap) n=cap-1;
    memcpy(dst, src, n); dst[n]='\0';
}

typedef struct { int64_t id_produto; char categoria[CAT_MAX]; char marca[MARCA_MAX]; char nome[NOME_MAX]; double preco; } ProdutoTmp;
typedef struct { int64_t id_pedido, id_produto; int32_t quantidade; } LinhaTmp;

static int cmp_produto_id(const void* a, const void* b){
    const ProdutoTmp* x=a; const ProdutoTmp* y=b;
    if(x->id_produto<y->id_produto) return -1;
    if(x->id_produto>y->id_produto) return 1;
    return 0;
}
static int cmp_linha_by_pedido_then_prod(const void* a, const void* b){
    const LinhaTmp* x=a; const LinhaTmp* y=b;
    if(x->id_pedido<y->id_pedido) return -1;
    if(x->id_pedido>y->id_pedido) return 1;
    if(x->id_produto<y->id_produto) return -1;
    if(x->id_produto>y->id_produto) return 1;
    return 0;
}

static void build_nome(char* out, size_t cap, const char* categoria, const char* cor, const char* metal, const char* pedra){
    char tmp[256]="";
    const char* cat = categoria? categoria:"";
    const char* c = cor? cor:"";
    const char* m = metal? metal:"";
    const char* p = pedra? pedra:"";
    const char* cat_clean = strncmp(cat,"jewelry.",8)==0? cat+8 : cat;
    int first=1;
    if(cat_clean && *cat_clean){ strncat(tmp, cat_clean, sizeof(tmp)-1); first=0; }
    if(c && *c){ if(!first) strncat(tmp," ",sizeof(tmp)-1); strncat(tmp,c,sizeof(tmp)-1); first=0; }
    if(m && *m){ if(!first) strncat(tmp," ",sizeof(tmp)-1); strncat(tmp,m,sizeof(tmp)-1); first=0; }
    if(p && *p){ if(!first) strncat(tmp," ",sizeof(tmp)-1); strncat(tmp,p,sizeof(tmp)-1); }
    safe_copy(out, cap, tmp);
}

static void write_joias(ProdutoTmp* v, size_t n){
    if(!n) return;
    qsort(v,n,sizeof *v, cmp_produto_id);
    size_t w=0;
    for(size_t i=0;i<n;i++){
        if(w==0 || v[i].id_produto!=v[w-1].id_produto){
            if(w!=i) v[w]=v[i];
            w++;
        }
    }
    FILE* f=fopen(PATH_JOIAS,"wb"); if(!f) die("joias.dat");
    Produto p;
    for(size_t i=0;i<w;i++){
        memset(&p,0,sizeof p);
        p.id_produto=v[i].id_produto;
        safe_copy(p.categoria,CAT_MAX,v[i].categoria);
        safe_copy(p.marca,MARCA_MAX,v[i].marca);
        safe_copy(p.nome,NOME_MAX,v[i].nome);
        p.preco=v[i].preco;
        if(fwrite(&p,sizeof p,1,f)!=1) die("w joias");
    }
    fclose(f);
    printf("joias.dat: %zu produtos únicos\n", w);
}

static void write_pedidos_and_index(LinhaTmp* v, size_t n){
    if(!n) return;
    qsort(v,n,sizeof *v, cmp_linha_by_pedido_then_prod);
    FILE* f = fopen(PATH_PEDIDOS, "wb"); if(!f) die("pedidos.dat");
    FILE* idx = fopen(PATH_PEDIDOS_IDX, "wb"); if(!idx) die("pedidos.idx");

    size_t i=0;
    while(i<n){
        int64_t cur_ped = v[i].id_pedido;
        int64_t total = 0;
        size_t j=i;
        while(j<n && v[j].id_pedido==cur_ped){
            if(v[j].quantidade>0) total += v[j].quantidade;
            j++;
        }
        if(total > MAX_ITENS_PEDIDO) { fprintf(stderr, "Pedido %lld tem %lld itens (max=%d). Truncando.\n", (long long)cur_ped, (long long)total, MAX_ITENS_PEDIDO); total = MAX_ITENS_PEDIDO; }
        long off = ftell(f);
        Pedido ped;
        memset(&ped, 0, sizeof(Pedido));
        ped.id_pedido=cur_ped;
        ped.n_itens=(int32_t)total;
        int idx_item=0;
        for(size_t k=i; k<j && idx_item<MAX_ITENS_PEDIDO; ++k){
            for(int q=0; q<v[k].quantidade && idx_item<MAX_ITENS_PEDIDO; ++q){
                ped.ids_produtos[idx_item++] = v[k].id_produto;
            }
        }
        if(fwrite(&ped, sizeof ped, 1, f)!=1){ fclose(f); fclose(idx); die("w pedido"); }
        PedidosIdxEntry e; e.id_pedido = cur_ped; e.offset = (uint64_t)off;
        if(fwrite(&e, sizeof e, 1, idx)!=1) { fclose(f); fclose(idx); die("w idx"); }
        i = j;
    }
    fclose(f); fclose(idx);
    printf("pedidos.dat: gravado e indexado.\n");
}

static void build_joias_idx(void){
    FILE* f=fopen(PATH_JOIAS,"rb"); if(!f) die("open joias.dat");
    FILE* idx=fopen(PATH_JOIAS_IDX,"wb"); if(!idx) die("open joias.idx");
    size_t nrec=fsize(f)/sizeof(Produto);
    Produto p; JoiasIdxEntry e;
    for(size_t i=0;i<nrec;i+=JOIAS_INDEX_STEP){
        if(fseek(f,(long)(i*sizeof(Produto)),SEEK_SET)!=0) die("seek joias");
        if(fread(&p,sizeof p,1,f)!=1) break;
        e.id_base=p.id_produto; e.offset=(uint64_t)(i*sizeof(Produto));
        if(fwrite(&e,sizeof e,1,idx)!=1) die("w joias.idx");
    }
    fclose(f); fclose(idx);
    printf("joias.idx: ok (step=%d)\n", JOIAS_INDEX_STEP);
}

static void rebuild_pedidos_idx(void){
    FILE* f=fopen(PATH_PEDIDOS,"rb"); if(!f) die("open pedidos.dat");
    FILE* idx=fopen(PATH_PEDIDOS_IDX,"wb"); if(!idx) die("open pedidos.idx");
    while(1){
        long off=ftell(f);
        Pedido ped; size_t rd=fread(&ped, sizeof ped, 1, f);
        if(rd!=1) break;
        PedidosIdxEntry e; e.id_pedido=ped.id_pedido; e.offset=(uint64_t)off;
        if(fwrite(&e,sizeof e,1,idx)!=1) die("w pedidos.idx");
    }
    fclose(f); fclose(idx);
    printf("pedidos.idx: reconstruído.\n");
}

static void cmd_import(const char* csv){
    FILE* in=fopen(csv,"r"); if(!in) die("open CSV");
    size_t capP=2048,nP=0, capL=4096,nL=0;
    ProdutoTmp* prods=malloc(capP*sizeof *prods); if(!prods) die("malloc prods");
    LinhaTmp*   linhas=malloc(capL*sizeof *linhas); if(!linhas) die("malloc linhas");

    char line[8192], cell[16384]; char* col[32];
    size_t ok=0, skip=0;
    while(fgets(line,sizeof line,in)){
        rstrip(line); if(!line[0]) continue;
        int nf=parse_csv_line(line,col,32,cell,sizeof cell);
        if(nf<0){ skip++; continue; }
        if(nf<8){ skip++; continue; }

        int64_t order_id=0, prod_id=0; int32_t qty=0; double price=0.0;
        const char* s_order = (nf>1? col[1]:"");
        const char* s_prod  = (nf>2? col[2]:"");
        const char* s_qty   = (nf>3? col[3]:"");
        const char* s_cat   = (nf>5? col[5]:"");
        const char* s_price = (nf>7? col[7]:"");
        const char* s_cor   = (nf>10? col[10]:"");
        const char* s_metal = (nf>11? col[11]:"");
        const char* s_pedra = (nf>12? col[12]:"");

        if(!try_i64(s_order,&order_id) || !try_i64(s_prod,&prod_id) || !try_i32(s_qty,&qty) || !try_f64(s_price,&price)){
            skip++; continue;
        }

        if(nP==capP){ capP*=2; prods=realloc(prods,capP*sizeof *prods); if(!prods) die("realloc prods"); }
        prods[nP].id_produto=prod_id;
        safe_copy(prods[nP].categoria,CAT_MAX, s_cat);
        safe_copy(prods[nP].marca,MARCA_MAX, s_metal?s_metal:"");
        build_nome(prods[nP].nome,NOME_MAX, s_cat,s_cor,s_metal,s_pedra);
        prods[nP].preco=price; nP++;

        if(nL==capL){ capL*=2; linhas=realloc(linhas,capL*sizeof *linhas); if(!linhas) die("realloc linhas"); }
        linhas[nL].id_pedido=order_id; linhas[nL].id_produto=prod_id; linhas[nL].quantidade=qty; nL++;

        ok++;
    }
    fclose(in);
    printf("CSV lido: %zu válidas, %zu puladas\n", ok, skip);

    write_joias(prods,nP);
    write_pedidos_and_index(linhas,nL);
    build_joias_idx();
    free(prods); free(linhas);
}

static void cmd_find_prod(const char* s){
    int64_t target; if(!try_i64(s,&target)){ fprintf(stderr,"id_produto inválido\n"); return; }
    
    // TRABALHO II: Medição de tempo adicionada
    clock_t start_time = clock();
    
    FILE* fidx=fopen(PATH_JOIAS_IDX,"rb"); FILE* fdat=fopen(PATH_JOIAS,"rb");
    if(!fidx||!fdat) die("abrir joias.*");
    size_t sz=fsize(fidx), n=sz/sizeof(JoiasIdxEntry);
    if(n==0){ printf("Índice vazio.\n"); goto done; }
    
    size_t lo=0, hi=n;
    while(lo<hi){
        size_t mid=(lo+hi)/2;
        JoiasIdxEntry entry;
        if(fseek(fidx,(long)(mid*sizeof(JoiasIdxEntry)),SEEK_SET)!=0) die("seek idx mid");
        if(fread(&entry,sizeof(JoiasIdxEntry),1,fidx)!=1) die("read idx mid");
        if(entry.id_base<=target) lo=mid+1; else hi=mid;
    }
    size_t base=(lo==0?0:lo-1);
    
    JoiasIdxEntry entry;
    if(fseek(fidx,(long)(base*sizeof(JoiasIdxEntry)),SEEK_SET)!=0) die("seek idx base");
    if(fread(&entry,sizeof(JoiasIdxEntry),1,fidx)!=1) die("read idx base");
    uint64_t start=entry.offset;

    if(fseek(fdat,(long)start,SEEK_SET)!=0) die("seek data");
    Produto p; size_t cnt=0; int found=0;
    while(cnt<JOIAS_INDEX_STEP && fread(&p,sizeof p,1,fdat)==1){
        if(p.id_produto==target){
            printf("Produto: id=%lld nome=\"%.*s\" cat=\"%.*s\" marca=\"%.*s\" preco=%.2f\n",
                   (long long)p.id_produto, (int)NOME_MAX, p.nome, (int)CAT_MAX, p.categoria, (int)MARCA_MAX, p.marca, p.preco);
            found=1; break;
        }
        cnt++;
    }
    if(!found) printf("Produto %lld não encontrado.\n", (long long)target);
    
    // TRABALHO II: Exibir tempo de busca
    clock_t end_time = clock();
    double time_spent = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    printf("Tempo de busca (indice parcial): %.6f segundos\n", time_spent);
    
done: if(fidx) fclose(fidx); if(fdat) fclose(fdat);
}

static void cmd_find_pedido(const char* s){
    int64_t target; if(!try_i64(s,&target)){ fprintf(stderr,"id_pedido inválido\n"); return; }
    
    // TRABALHO II: Medição de tempo adicionada
    clock_t start_time = clock();
    
    FILE* idx=fopen(PATH_PEDIDOS_IDX,"rb"); if(!idx){ printf("Índice de pedidos ausente. Faça import/insert/remove primeiro.\n"); return; }
    size_t sz=fsize(idx), n=sz/sizeof(PedidosIdxEntry);
    
    size_t lo=0, hi=n;
    while(lo<hi){
        size_t mid=(lo+hi)/2;
        PedidosIdxEntry entry;
        if(fseek(idx,(long)(mid*sizeof(PedidosIdxEntry)),SEEK_SET)!=0) die("seek ped.idx mid");
        if(fread(&entry,sizeof(PedidosIdxEntry),1,idx)!=1) die("read ped.idx mid");
        if(entry.id_pedido<target) lo=mid+1; else hi=mid;
    }
    
    if(lo==n){
        printf("Pedido %lld não encontrado.\n",(long long)target);
        fclose(idx);
        // TRABALHO II: Exibir tempo mesmo quando não encontrado
        clock_t end_time = clock();
        double time_spent = (double)(end_time - start_time) / CLOCKS_PER_SEC;
        printf("Tempo de busca (indice exaustivo): %.6f segundos\n", time_spent);
        return;
    }
    
    PedidosIdxEntry found_entry;
    if(fseek(idx,(long)(lo*sizeof(PedidosIdxEntry)),SEEK_SET)!=0) die("seek ped.idx found");
    if(fread(&found_entry,sizeof(PedidosIdxEntry),1,idx)!=1) die("read ped.idx found");
    
    if(found_entry.id_pedido!=target){
        printf("Pedido %lld não encontrado.\n",(long long)target);
        fclose(idx);
        // TRABALHO II: Exibir tempo mesmo quando não encontrado
        clock_t end_time = clock();
        double time_spent = (double)(end_time - start_time) / CLOCKS_PER_SEC;
        printf("Tempo de busca (indice exaustivo): %.6f segundos\n", time_spent);
        return;
    }
    
    uint64_t off=found_entry.offset;
    fclose(idx);

    FILE* f=fopen(PATH_PEDIDOS,"rb"); if(!f) die("open pedidos.dat");
    if(fseek(f,(long)off,SEEK_SET)!=0) die("seek ped");
    Pedido ped; if(fread(&ped,sizeof ped,1,f)!=1){ fclose(f); die("read pedido"); }
    printf("Pedido %lld — n_itens=%d\n", (long long)ped.id_pedido, ped.n_itens);
    for(int32_t i=0;i<ped.n_itens;i++){
        printf("  item%03d -> id_produto=%lld\n", i+1, (long long)ped.ids_produtos[i]);
    }
    fclose(f);
    
    // TRABALHO II: Exibir tempo de busca
    clock_t end_time = clock();
    double time_spent = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    printf("Tempo de busca (indice exaustivo): %.6f segundos\n", time_spent);
}

static void cmd_list_prod_n(long n){
    if(n<=0){ printf("N inválido.\n"); return; }
    FILE* f=fopen(PATH_JOIAS,"rb"); if(!f){ printf("Não foi possível abrir %s\n", PATH_JOIAS); return; }
    Produto p; long i=0; printf("Primeiros %ld produtos:\n", n);
    while(i<n && fread(&p,sizeof p,1,f)==1){
        printf("%3ld) id=%lld nome=\"%.*s\" cat=\"%.*s\" marca=\"%.*s\" preco=%.2f\n",
               i+1,(long long)p.id_produto,(int)NOME_MAX,p.nome,(int)CAT_MAX,p.categoria,(int)MARCA_MAX,p.marca,p.preco);
        i++;
    }
    if(i==0) printf("(arquivo vazio)\n");
    fclose(f);
}
static void cmd_list_pedidos_n(long n){
    if(n<=0){ printf("N inválido.\n"); return; }
    FILE* f=fopen(PATH_PEDIDOS,"rb"); if(!f){ printf("Não foi possível abrir %s\n", PATH_PEDIDOS); return; }
    long printed=0;
    while(printed<n){
        Pedido ped; size_t rd=fread(&ped,sizeof ped,1,f);
        if(rd!=1) break;
        printf("%3ld) id_pedido=%lld  n_itens=%d\n", printed+1, (long long)ped.id_pedido, ped.n_itens);
        printed++;
    }
    if(printed==0) printf("(arquivo vazio)\n");
    fclose(f);
}

static void cmd_add_produto(const char* s_cat, const char* s_marca, const char* s_nome, const char* s_preco){
    double preco;
    if(!try_f64(s_preco,&preco)){
        fprintf(stderr,"preco inválido.\n"); return;
    }
    if(!s_cat||!s_nome){ fprintf(stderr,"categoria e nome são obrigatórios.\n"); return; }
    
    int64_t id_produto = 1;
    FILE* fin = fopen(PATH_JOIAS, "rb");
    
    if(fin){
        Produto p;
        while(fread(&p, sizeof p, 1, fin)==1){
            if(p.id_produto >= id_produto){
                id_produto = p.id_produto + 1;
            }
        }
        fclose(fin);
        fin = fopen(PATH_JOIAS, "rb");
    }
    
    Produto novo;
    memset(&novo, 0, sizeof(Produto));
    novo.id_produto = id_produto;
    safe_copy(novo.categoria, CAT_MAX, s_cat);
    safe_copy(novo.marca, MARCA_MAX, s_marca?s_marca:"");
    safe_copy(novo.nome, NOME_MAX, s_nome);
    novo.preco = preco;
    
    FILE* fout = fopen("joias.tmp", "wb");
    if(!fout) die("joias.tmp");
    
    int inserted = 0;
    if(!fin){
        if(fwrite(&novo, sizeof(Produto), 1, fout)!=1){ fclose(fout); die("w novo produto"); }
        inserted = 1;
    } else {
        Produto p;
        while(fread(&p, sizeof p, 1, fin)==1){
            if(!inserted && id_produto < p.id_produto){
                if(fwrite(&novo, sizeof(Produto), 1, fout)!=1){ fclose(fin); fclose(fout); die("w novo"); }
                inserted = 1;
            }
            if(fwrite(&p, sizeof(Produto), 1, fout)!=1){ fclose(fin); fclose(fout); die("w produto"); }
        }
        if(!inserted){
            if(fwrite(&novo, sizeof(Produto), 1, fout)!=1){ fclose(fout); die("w novo fim"); }
        }
        fclose(fin);
    }
    fclose(fout);
    
    if(remove(PATH_JOIAS)!=0 && errno!=ENOENT) die("rm joias.dat");
    if(rename("joias.tmp", PATH_JOIAS)!=0) die("mv joias.tmp");
    
    build_joias_idx();
    printf("Produto %lld adicionado com sucesso.\n", (long long)id_produto);
}

static void cmd_remove_produto(const char* s_id){
    int64_t id_produto;
    if(!try_i64(s_id,&id_produto)){ fprintf(stderr,"id_produto inválido.\n"); return; }
    
    FILE* fin = fopen(PATH_JOIAS, "rb");
    if(!fin){ printf("Arquivo joias.dat não existe.\n"); return; }
    
    FILE* fout = fopen("joias.tmp", "wb");
    if(!fout){ fclose(fin); die("joias.tmp"); }
    
    Produto p;
    int found = 0;
    while(fread(&p, sizeof p, 1, fin)==1){
        if(p.id_produto == id_produto){
            found = 1;
            continue;
        }
        if(fwrite(&p, sizeof(Produto), 1, fout)!=1){ fclose(fin); fclose(fout); die("w produto"); }
    }
    fclose(fin);
    fclose(fout);
    
    if(!found){
        printf("Produto %lld não encontrado.\n", (long long)id_produto);
        remove("joias.tmp");
        return;
    }
    
    if(remove(PATH_JOIAS)!=0) die("rm joias.dat");
    if(rename("joias.tmp", PATH_JOIAS)!=0) die("mv joias.tmp");
    
    build_joias_idx();
    printf("Produto %lld removido com sucesso.\n", (long long)id_produto);
}

static void cmd_add_pedido(const char* s_n_itens, const char* s_ids_produtos){
    int32_t n_itens;
    if(!try_i32(s_n_itens,&n_itens) || n_itens<=0){
        fprintf(stderr,"n_itens inválido.\n"); return;
    }
    
    int64_t id_pedido = 1;
    FILE* ftmp = fopen(PATH_PEDIDOS, "rb");
    if(ftmp){
        Pedido p;
        while(fread(&p, sizeof p, 1, ftmp)==1){
            if(p.id_pedido >= id_pedido){
                id_pedido = p.id_pedido + 1;
            }
        }
        fclose(ftmp);
    }
    
    int64_t* ids_produtos = (int64_t*)malloc((size_t)n_itens * sizeof(int64_t));
    if(!ids_produtos) die("malloc ids_produtos");
    
    size_t len = strlen(s_ids_produtos);
    char* str_copy = (char*)malloc(len + 1);
    if(!str_copy){ free(ids_produtos); die("malloc str_copy"); }
    strcpy(str_copy, s_ids_produtos);
    
    int32_t count = 0;
    char* token = strtok(str_copy, ", ");
    while(token && count < n_itens){
        int64_t id;
        if(try_i64(token, &id)){
            ids_produtos[count++] = id;
        }
        token = strtok(NULL, ", ");
    }
    free(str_copy);
    
    if(count != n_itens){
        fprintf(stderr,"Número de IDs fornecidos (%d) não corresponde a n_itens (%d).\n", count, n_itens);
        free(ids_produtos);
        return;
    }
    
    FILE* fin = fopen(PATH_PEDIDOS, "rb");
    FILE* fout = fopen("pedidos.tmp", "wb");
    if(!fout){ free(ids_produtos); die("pedidos.tmp"); }
    
    int inserted = 0;
    if(!fin){
        Pedido novo;
        memset(&novo, 0, sizeof(Pedido));
        novo.id_pedido = id_pedido;
        novo.n_itens = n_itens;
        for(int32_t i=0; i<n_itens; i++){
            novo.ids_produtos[i] = ids_produtos[i];
        }
        if(fwrite(&novo, sizeof novo, 1, fout)!=1){ free(ids_produtos); fclose(fout); die("w pedido"); }
        inserted = 1;
    } else {
        Pedido ped;
        while(fread(&ped, sizeof ped, 1, fin)==1){
            if(ped.id_pedido == id_pedido){
                printf("Pedido %lld já existe. Use remove-pedido antes de adicionar novamente.\n", (long long)id_pedido);
                fclose(fin); fclose(fout); remove("pedidos.tmp"); free(ids_produtos);
                return;
            }
            
            if(!inserted && id_pedido < ped.id_pedido){
                Pedido novo;
                memset(&novo, 0, sizeof(Pedido));
                novo.id_pedido = id_pedido;
                novo.n_itens = n_itens;
                for(int32_t i=0; i<n_itens; i++){
                    novo.ids_produtos[i] = ids_produtos[i];
                }
                if(fwrite(&novo, sizeof novo, 1, fout)!=1){ fclose(fin); fclose(fout); free(ids_produtos); die("w novo pedido"); }
                inserted = 1;
            }
            
            if(fwrite(&ped, sizeof ped, 1, fout)!=1){ fclose(fin); fclose(fout); free(ids_produtos); die("w pedido copy"); }
        }
        
        if(!inserted){
            Pedido novo;
            memset(&novo, 0, sizeof(Pedido));
            novo.id_pedido = id_pedido;
            novo.n_itens = n_itens;
            for(int32_t i=0; i<n_itens; i++){
                novo.ids_produtos[i] = ids_produtos[i];
            }
            if(fwrite(&novo, sizeof novo, 1, fout)!=1){ fclose(fout); free(ids_produtos); die("w novo pedido fim"); }
        }
        fclose(fin);
    }
    fclose(fout);
    free(ids_produtos);
    
    if(remove(PATH_PEDIDOS)!=0 && errno!=ENOENT) die("rm pedidos.dat");
    if(rename("pedidos.tmp", PATH_PEDIDOS)!=0) die("mv pedidos.tmp");
    
    rebuild_pedidos_idx();
    printf("Pedido %lld adicionado com sucesso (%d itens).\n", (long long)id_pedido, n_itens);
}

static void cmd_remove_pedido(const char* s_id){
    int64_t id_pedido;
    if(!try_i64(s_id,&id_pedido)){ fprintf(stderr,"id_pedido inválido.\n"); return; }
    
    FILE* fin = fopen(PATH_PEDIDOS, "rb");
    if(!fin){ printf("Arquivo pedidos.dat não existe.\n"); return; }
    
    FILE* fout = fopen("pedidos.tmp", "wb");
    if(!fout){ fclose(fin); die("pedidos.tmp"); }
    
    Pedido ped;
    int found = 0;
    while(fread(&ped, sizeof ped, 1, fin)==1){
        if(ped.id_pedido == id_pedido){
            found = 1;
            continue;
        }
        
        if(fwrite(&ped, sizeof ped, 1, fout)!=1){ fclose(fin); fclose(fout); die("w pedido"); }
    }
    fclose(fin);
    fclose(fout);
    
    if(!found){
        printf("Pedido %lld não encontrado.\n", (long long)id_pedido);
        remove("pedidos.tmp");
        return;
    }
    
    if(remove(PATH_PEDIDOS)!=0) die("rm pedidos.dat");
    if(rename("pedidos.tmp", PATH_PEDIDOS)!=0) die("mv pedidos.tmp");
    
    rebuild_pedidos_idx();
    printf("Pedido %lld removido com sucesso.\n", (long long)id_pedido);
}

static void q_joia_mais_cara(void){
    FILE* f=fopen(PATH_JOIAS,"rb"); if(!f){ printf("Abra primeiro com import.\n"); return; }
    Produto best; int ok=0;
    Produto p;
    while(fread(&p,sizeof p,1,f)==1){
        if(!ok || p.preco>best.preco){ best=p; ok=1; }
    }
    fclose(f);
    if(ok){
        printf("Joia mais cara: id=%lld nome=\"%.*s\" cat=\"%.*s\" marca=\"%.*s\" preco=%.2f\n",
            (long long)best.id_produto, (int)NOME_MAX, best.nome, (int)CAT_MAX, best.categoria, (int)MARCA_MAX, best.marca, best.preco);
    }else{
        printf("joias.dat vazio.\n");
    }
}

static int equals_ignore_case(const char* a, const char* b){
    for(; *a && *b; ++a, ++b){
        char ca=(char)tolower((unsigned char)*a);
        char cb=(char)tolower((unsigned char)*b);
        if(ca!=cb) return 0;
    }
    return *a=='\0' && *b=='\0';
}

static int buscar_produto_por_id(int64_t id_produto, Produto* resultado) {
    FILE* fidx = fopen(PATH_JOIAS_IDX, "rb");
    FILE* fdat = fopen(PATH_JOIAS, "rb");
    if (!fidx || !fdat) {
        if (fidx) fclose(fidx);
        if (fdat) fclose(fdat);
        return 0;
    }
    
    size_t n = fsize(fidx) / sizeof(JoiasIdxEntry);
    if (n == 0) {
        fclose(fidx);
        fclose(fdat);
        return 0;
    }
    
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        JoiasIdxEntry entry;
        if (fseek(fidx, (long)(mid * sizeof(JoiasIdxEntry)), SEEK_SET) != 0) {
            fclose(fidx); fclose(fdat);
            return 0;
        }
        if (fread(&entry, sizeof(JoiasIdxEntry), 1, fidx) != 1) {
            fclose(fidx); fclose(fdat);
            return 0;
        }
        if (entry.id_base <= id_produto)
            lo = mid + 1;
        else
            hi = mid;
    }
    size_t base = (lo == 0 ? 0 : lo - 1);
    
    JoiasIdxEntry entry;
    if (fseek(fidx, (long)(base * sizeof(JoiasIdxEntry)), SEEK_SET) != 0) {
        fclose(fidx); fclose(fdat);
        return 0;
    }
    if (fread(&entry, sizeof(JoiasIdxEntry), 1, fidx) != 1) {
        fclose(fidx); fclose(fdat);
        return 0;
    }
    
    if (fseek(fdat, (long)entry.offset, SEEK_SET) != 0) {
        fclose(fidx); fclose(fdat);
        return 0;
    }
    
    Produto p;
    size_t cnt = 0;
    while (cnt < JOIAS_INDEX_STEP && fread(&p, sizeof p, 1, fdat) == 1) {
        if (p.id_produto == id_produto) {
            *resultado = p;
            fclose(fidx);
            fclose(fdat);
            return 1;
        }
        cnt++;
    }
    
    fclose(fidx);
    fclose(fdat);
    return 0;
}

static void q_vendas_por_nome(const char* nome){
    if(!nome||!*nome){ printf("Forneça um nome.\n"); return; }
    
    FILE* fp=fopen(PATH_PEDIDOS,"rb"); 
    if(!fp){ printf("Precisa do pedidos.dat (rode import).\n"); return; }
    
    long long count=0;
    Pedido ped;
    
    while(fread(&ped,sizeof ped,1,fp)==1){
        for(int32_t i=0;i<ped.n_itens;i++){
            Produto p;
            
            if(buscar_produto_por_id(ped.ids_produtos[i], &p)){
                if(equals_ignore_case(p.nome,nome)){
                    count++;
                }
            }
        }
    }
    fclose(fp);
    printf("Vendas (itens) do nome \"%s\": %lld\n", nome, count);
}


static int has_category(const char* cat_full, const char* want_clean){
    const char* base = (strncmp(cat_full,"jewelry.",8)==0? cat_full+8 : cat_full);
    while(*base && *want_clean){
        char a=(char)tolower((unsigned char)*base);
        char b=(char)tolower((unsigned char)*want_clean);
        if(a!=b) return 0;
        base++; want_clean++;
    }
    return *base=='\0' && *want_clean=='\0';
}

static void q_vendas_por_categoria(const char* categoria){
    if(!categoria||!*categoria){ printf("Forneça a categoria (ex: earring, pendant, necklace).\n"); return; }
    
    FILE* fp=fopen(PATH_PEDIDOS,"rb"); 
    if(!fp){ printf("Precisa do pedidos.dat (rode import).\n"); return; }
    
    long long count=0;
    Pedido ped;
    
    while(fread(&ped,sizeof ped,1,fp)==1){
        for(int32_t i=0;i<ped.n_itens;i++){
            Produto p;
            
            if(buscar_produto_por_id(ped.ids_produtos[i], &p)){
                if(has_category(p.categoria, categoria)){
                    count++;
                }
            }
        }
    }
    fclose(fp);
    printf("Total de itens vendidos na categoria \"%s\": %lld\n", categoria, count);
}

static void rstrip2(char *s){ size_t n=strlen(s); while(n>0 && (s[n-1]=='\n'||s[n-1]=='\r')) s[--n]='\0'; }
static void trim2(char *s){
    size_t i=0, j=strlen(s);
    while(i<j && isspace((unsigned char)s[i])) i++;
    while(j>i && isspace((unsigned char)s[j-1])) j--;
    if(i>0) memmove(s, s+i, j-i);
    s[j-i]='\0';
}
static void read_line(const char *prompt, char *buf, size_t n){
    printf("%s", prompt); fflush(stdout);
    if(fgets(buf, (int)n, stdin) == NULL){ buf[0]='\0'; clearerr(stdin); return; }
    rstrip2(buf); trim2(buf);
}
static void press_enter(void){
    printf("\n(Pressione ENTER para continuar) "); fflush(stdout);
    int c; while((c=getchar())!='\n' && c!=EOF);
}

// ========== FUNCOES DA ARVORE B ==========

// Criar novo nodo da árvore B
static BTreeNode* btree_create_node(int is_leaf) {
    BTreeNode* node = (BTreeNode*)malloc(sizeof(BTreeNode));
    if (!node) die("malloc btree node");
    node->n_keys = 0;
    node->is_leaf = is_leaf;
    for (size_t i = 0; i < BTREE_ORDER; i++) {
        node->children[i] = NULL;
    }
    return node;
}

// Criar árvore B vazia
static BTree* btree_create(void) {
    BTree* tree = (BTree*)malloc(sizeof(BTree));
    if (!tree) die("malloc btree");
    tree->root = btree_create_node(1);
    tree->total_nodes = 1;
    tree->total_keys = 0;
    return tree;
}

// Buscar chave na árvore B
static int btree_search(BTreeNode* node, int64_t key, uint64_t* offset) {
    if (!node) return 0;
    
    int i = 0;
    while (i < node->n_keys && key > node->keys[i]) {
        i++;
    }
    
    if (i < node->n_keys && key == node->keys[i]) {
        *offset = node->offsets[i];
        return 1;
    }
    
    if (node->is_leaf) {
        return 0;
    }
    
    return btree_search(node->children[i], key, offset);
}

// Dividir filho cheio
static void btree_split_child(BTreeNode* parent, int index, BTreeNode* child) {
    int mid = (BTREE_ORDER - 1) / 2;
    BTreeNode* new_node = btree_create_node(child->is_leaf);
    new_node->n_keys = BTREE_ORDER - 1 - mid - 1;
    
    // Copiar segunda metade das chaves para o novo nodo
    for (int i = 0; i < new_node->n_keys; i++) {
        new_node->keys[i] = child->keys[mid + 1 + i];
        new_node->offsets[i] = child->offsets[mid + 1 + i];
    }
    
    // Copiar ponteiros dos filhos se não for folha
    if (!child->is_leaf) {
        for (int i = 0; i <= new_node->n_keys; i++) {
            new_node->children[i] = child->children[mid + 1 + i];
        }
    }
    
    child->n_keys = mid;
    
    // Inserir novo filho no pai
    for (int i = parent->n_keys; i > index; i--) {
        parent->children[i + 1] = parent->children[i];
    }
    parent->children[index + 1] = new_node;
    
    // Mover chaves do pai
    for (int i = parent->n_keys - 1; i >= index; i--) {
        parent->keys[i + 1] = parent->keys[i];
        parent->offsets[i + 1] = parent->offsets[i];
    }
    
    parent->keys[index] = child->keys[mid];
    parent->offsets[index] = child->offsets[mid];
    parent->n_keys++;
}

// Inserir em nodo não cheio
static void btree_insert_non_full(BTreeNode* node, int64_t key, uint64_t offset) {
    int i = node->n_keys - 1;
    
    if (node->is_leaf) {
        // Inserir na posição correta
        while (i >= 0 && key < node->keys[i]) {
            node->keys[i + 1] = node->keys[i];
            node->offsets[i + 1] = node->offsets[i];
            i--;
        }
        node->keys[i + 1] = key;
        node->offsets[i + 1] = offset;
        node->n_keys++;
    } else {
        // Encontrar filho apropriado
        while (i >= 0 && key < node->keys[i]) {
            i--;
        }
        i++;
        
        if (node->children[i]->n_keys == BTREE_ORDER - 1) {
            btree_split_child(node, i, node->children[i]);
            if (key > node->keys[i]) {
                i++;
            }
        }
        btree_insert_non_full(node->children[i], key, offset);
    }
}

// Inserir chave na árvore B
static void btree_insert(BTree* tree, int64_t key, uint64_t offset) {
    BTreeNode* root = tree->root;
    
    if (root->n_keys == BTREE_ORDER - 1) {
        BTreeNode* new_root = btree_create_node(0);
        new_root->children[0] = root;
        btree_split_child(new_root, 0, root);
        tree->root = new_root;
        tree->total_nodes++;
        btree_insert_non_full(new_root, key, offset);
    } else {
        btree_insert_non_full(root, key, offset);
    }
    tree->total_keys++;
}

// Liberar memória da árvore B
static void btree_free_node(BTreeNode* node) {
    if (!node) return;
    if (!node->is_leaf) {
        for (int i = 0; i <= node->n_keys; i++) {
            btree_free_node(node->children[i]);
        }
    }
    free(node);
}

static void btree_free(BTree* tree) {
    if (!tree) return;
    btree_free_node(tree->root);
    free(tree);
}

static BTree* g_produtos_btree = NULL;
static HashTable* g_pedidos_hash = NULL;

// Construir índice da árvore B a partir do arquivo de produtos
static void btree_build_from_file(void) {
    clock_t start = clock();
    
    FILE* f = fopen(PATH_JOIAS, "rb");
    if (!f) {
        printf("Erro: arquivo %s não encontrado. Execute a importação primeiro.\n", PATH_JOIAS);
        return;
    }
    
    // Liberar árvore anterior se existir
    if (g_produtos_btree) {
        btree_free(g_produtos_btree);
    }
    
    g_produtos_btree = btree_create();
    
    Produto p;
    uint64_t offset = 0;
    int count = 0;
    
    while (fread(&p, sizeof(Produto), 1, f) == 1) {
        btree_insert(g_produtos_btree, p.id_produto, offset);
        offset += sizeof(Produto);
        count++;
    }
    
    fclose(f);
    
    clock_t end = clock();
    double time_spent = (double)(end - start) / CLOCKS_PER_SEC;
    
    printf("\n========== ARVORE B - INDICE EM MEMORIA ==========\n");
    printf("Arquivo: %s\n", PATH_JOIAS);
    printf("Ordem da arvore: %zu\n", (size_t)BTREE_ORDER);
    printf("Tamanho do bloco: %d bytes\n", BLOCK_SIZE);
    printf("Produtos indexados: %d\n", count);
    printf("Total de nodos: %d\n", g_produtos_btree->total_nodes);
    printf("Total de chaves: %d\n", g_produtos_btree->total_keys);
    printf("Tempo de criacao: %.6f segundos\n", time_spent);
    printf("==================================================\n");
}

// Buscar produto usando árvore B
static void btree_buscar_produto(const char* id_str) {
    if (!g_produtos_btree) {
        printf("Erro: Indice da arvore B nao foi criado. Use a opcao 14 primeiro.\n");
        return;
    }
    
    int64_t id = 0;
    if (!try_i64(id_str, &id)) {
        printf("ID invalido.\n");
        return;
    }
    
    clock_t start = clock();
    
    uint64_t offset;
    if (btree_search(g_produtos_btree->root, id, &offset)) {
        FILE* f = fopen(PATH_JOIAS, "rb");
        if (!f) {
            printf("Erro ao abrir arquivo de produtos.\n");
            return;
        }
        
        if (fseek(f, (long)offset, SEEK_SET) != 0) {
            printf("Erro ao posicionar no arquivo.\n");
            fclose(f);
            return;
        }
        
        Produto p;
        if (fread(&p, sizeof(Produto), 1, f) == 1) {
            clock_t end = clock();
            double time_spent = (double)(end - start) / CLOCKS_PER_SEC;
            
            printf("\n--- Produto encontrado (Arvore B) ---\n");
            printf("ID: %lld\n", (long long)p.id_produto);
            printf("Categoria: %s\n", p.categoria);
            printf("Marca: %s\n", p.marca);
            printf("Nome: %s\n", p.nome);
            printf("Preco: %.2f\n", p.preco);
            printf("Tempo de busca: %.6f segundos\n", time_spent);
        } else {
            printf("Erro ao ler produto.\n");
        }
        fclose(f);
    } else {
        clock_t end = clock();
        double time_spent = (double)(end - start) / CLOCKS_PER_SEC;
        printf("Produto ID %lld nao encontrado.\n", (long long)id);
        printf("Tempo de busca: %.6f segundos\n", time_spent);
    }
}

// ========== FUNCOES DA TABELA HASH ==========

static unsigned int hash_function(int64_t key) {
    return (unsigned int)(key % HASH_TABLE_SIZE);
}

static HashTable* hash_create(void) {
    HashTable* ht = (HashTable*)malloc(sizeof(HashTable));
    if (!ht) die("malloc hashtable");
    ht->buckets = (HashNode**)calloc(HASH_TABLE_SIZE, sizeof(HashNode*));
    if (!ht->buckets) die("calloc buckets");
    ht->size = HASH_TABLE_SIZE;
    ht->total_entries = 0;
    ht->collisions = 0;
    return ht;
}

static void hash_insert(HashTable* ht, int64_t id_pedido, uint64_t offset) {
    unsigned int index = hash_function(id_pedido);
    HashNode* new_node = (HashNode*)malloc(sizeof(HashNode));
    if (!new_node) die("malloc hashnode");
    new_node->id_pedido = id_pedido;
    new_node->offset = offset;
    new_node->next = NULL;
    
    if (ht->buckets[index] != NULL) {
        ht->collisions++;
        new_node->next = ht->buckets[index];
    }
    ht->buckets[index] = new_node;
    ht->total_entries++;
}

static int hash_search(HashTable* ht, int64_t id_pedido, uint64_t* offset) {
    unsigned int index = hash_function(id_pedido);
    HashNode* node = ht->buckets[index];
    
    while (node != NULL) {
        if (node->id_pedido == id_pedido) {
            *offset = node->offset;
            return 1;
        }
        node = node->next;
    }
    return 0;
}

static void hash_free(HashTable* ht) {
    if (!ht) return;
    for (int i = 0; i < ht->size; i++) {
        HashNode* node = ht->buckets[i];
        while (node != NULL) {
            HashNode* temp = node;
            node = node->next;
            free(temp);
        }
    }
    free(ht->buckets);
    free(ht);
}

static void hash_build_from_file(void) {
    clock_t start = clock();
    
    FILE* f = fopen(PATH_PEDIDOS, "rb");
    if (!f) {
        printf("Erro: arquivo %s não encontrado. Execute a importação primeiro.\n", PATH_PEDIDOS);
        return;
    }
    
    if (g_pedidos_hash) {
        hash_free(g_pedidos_hash);
    }
    
    g_pedidos_hash = hash_create();
    
    Pedido p;
    uint64_t offset = 0;
    int count = 0;
    
    while (fread(&p, sizeof(Pedido), 1, f) == 1) {
        hash_insert(g_pedidos_hash, p.id_pedido, offset);
        offset += sizeof(Pedido);
        count++;
    }
    
    fclose(f);
    
    clock_t end = clock();
    double time_spent = (double)(end - start) / CLOCKS_PER_SEC;
    
    printf("\n========== TABELA HASH - INDICE EM MEMORIA ==========\n");
    printf("Arquivo: %s\n", PATH_PEDIDOS);
    printf("Tamanho da tabela: %d\n", HASH_TABLE_SIZE);
    printf("Pedidos indexados: %d\n", count);
    printf("Total de entradas: %d\n", g_pedidos_hash->total_entries);
    printf("Colisoes: %d\n", g_pedidos_hash->collisions);
    printf("Fator de carga: %.2f%%\n", (g_pedidos_hash->total_entries * 100.0) / g_pedidos_hash->size);
    printf("Tempo de criacao: %.6f segundos\n", time_spent);
    printf("=====================================================\n");
}

static void hash_buscar_pedido(const char* id_str) {
    if (!g_pedidos_hash) {
        printf("Erro: Indice da tabela hash nao foi criado. Use a opcao 17 primeiro.\n");
        return;
    }
    
    int64_t id = 0;
    if (!try_i64(id_str, &id)) {
        printf("ID invalido.\n");
        return;
    }
    
    clock_t start = clock();
    
    uint64_t offset;
    if (hash_search(g_pedidos_hash, id, &offset)) {
        FILE* f = fopen(PATH_PEDIDOS, "rb");
        if (!f) {
            printf("Erro ao abrir arquivo de pedidos.\n");
            return;
        }
        
        if (fseek(f, (long)offset, SEEK_SET) != 0) {
            printf("Erro ao posicionar no arquivo.\n");
            fclose(f);
            return;
        }
        
        Pedido p;
        if (fread(&p, sizeof(Pedido), 1, f) == 1) {
            clock_t end = clock();
            double time_spent = (double)(end - start) / CLOCKS_PER_SEC;
            
            printf("\n--- Pedido encontrado (Tabela Hash) ---\n");
            printf("ID: %lld\n", (long long)p.id_pedido);
            printf("Numero de itens: %d\n", p.n_itens);
            printf("IDs dos produtos: ");
            for (int32_t i = 0; i < p.n_itens && i < 10; i++) {
                printf("%lld ", (long long)p.ids_produtos[i]);
            }
            if (p.n_itens > 10) printf("...");
            printf("\n");
            printf("Tempo de busca: %.6f segundos\n", time_spent);
        } else {
            printf("Erro ao ler pedido.\n");
        }
        fclose(f);
    } else {
        clock_t end = clock();
        double time_spent = (double)(end - start) / CLOCKS_PER_SEC;
        printf("Pedido ID %lld nao encontrado.\n", (long long)id);
        printf("Tempo de busca: %.6f segundos\n", time_spent);
    }
}

// ========== INSERCAO/REMOCAO INCREMENTAL - ARVORE B ==========

static void btree_inserir_produto_incremental(void) {
    if (!g_produtos_btree) {
        printf("Erro: Arvore B nao foi criada. Use a opcao 13 primeiro.\n");
        return;
    }
    
    char categoria[64], marca[64], nome[256], preco_str[32];
    read_line("Categoria: ", categoria, sizeof(categoria));
    if(categoria[0]=='\0'){ printf("Categoria vazia.\n"); return; }
    read_line("Marca: ", marca, sizeof(marca));
    if(marca[0]=='\0'){ printf("Marca vazia.\n"); return; }
    read_line("Nome: ", nome, sizeof(nome));
    if(nome[0]=='\0'){ printf("Nome vazio.\n"); return; }
    read_line("Preco: ", preco_str, sizeof(preco_str));
    double preco = atof(preco_str);
    if(preco <= 0){ printf("Preco invalido.\n"); return; }
    
    clock_t start = clock();
    
    // Adicionar no arquivo
    FILE* f = fopen("joias.dat", "ab");
    if(!f){ printf("Erro ao abrir joias.dat\n"); return; }
    
    uint64_t offset = ftell(f);
    Produto p;
    p.id_produto = gerar_id();
    strncpy(p.categoria, categoria, sizeof(p.categoria)-1);
    p.categoria[sizeof(p.categoria)-1] = '\0';
    strncpy(p.marca, marca, sizeof(p.marca)-1);
    p.marca[sizeof(p.marca)-1] = '\0';
    strncpy(p.nome, nome, sizeof(p.nome)-1);
    p.nome[sizeof(p.nome)-1] = '\0';
    p.preco = preco;
    
    if(fwrite(&p, sizeof(Produto), 1, f) != 1){
        printf("Erro ao escrever produto.\n");
        fclose(f);
        return;
    }
    fclose(f);
    
    // Inserir na Árvore B
    btree_insert(g_produtos_btree, p.id_produto, offset);
    
    clock_t end = clock();
    double time_spent = (double)(end - start) / CLOCKS_PER_SEC;
    
    printf("\n==================================================\n");
    printf("Produto adicionado com sucesso!\n");
    printf("ID gerado: %lld\n", (long long)p.id_produto);
    printf("Offset no arquivo: %llu\n", (unsigned long long)offset);
    printf("Tempo de insercao (arquivo + Arvore B): %.6f segundos\n", time_spent);
    printf("Total de produtos na Arvore B: %d\n", g_produtos_btree->total_keys);
    printf("==================================================\n");
    
    // Reconstruir índice parcial
    construir_indice_parcial();
}

static void btree_remover_produto_incremental(void) {
    if (!g_produtos_btree) {
        printf("Erro: Arvore B nao foi criada. Use a opcao 13 primeiro.\n");
        return;
    }
    
    printf("AVISO: Remocao incremental na Arvore B nao foi implementada.\n");
    printf("Use a opcao 5 (remover do arquivo) + opcao 13 (recriar indice).\n");
}

// ========== INSERCAO/REMOCAO INCREMENTAL - TABELA HASH ==========

static void hash_inserir_pedido_incremental(void) {
    if (!g_pedidos_hash) {
        printf("Erro: Tabela Hash nao foi criada. Use a opcao 15 primeiro.\n");
        return;
    }
    
    char n_str[32];
    read_line("Numero de itens: ", n_str, sizeof(n_str));
    int n = atoi(n_str);
    if(n <= 0 || n > MAX_ITENS_PEDIDO){
        printf("Numero invalido (1-%d).\n", MAX_ITENS_PEDIDO);
        return;
    }
    
    clock_t start = clock();
    
    // Adicionar no arquivo
    FILE* f = fopen("pedidos.dat", "ab");
    if(!f){ printf("Erro ao abrir pedidos.dat\n"); return; }
    
    uint64_t offset = ftell(f);
    Pedido ped;
    ped.id_pedido = gerar_id();
    ped.n_itens = n;
    
    for(int i=0; i<n; i++){
        char id_str[64];
        printf("  Item %d - id_produto: ", i+1);
        if(!fgets(id_str, sizeof(id_str), stdin)){ clearerr(stdin); i--; continue; }
        ped.ids_produtos[i] = atoll(id_str);
    }
    
    if(fwrite(&ped, sizeof(Pedido), 1, f) != 1){
        printf("Erro ao escrever pedido.\n");
        fclose(f);
        return;
    }
    fclose(f);
    
    // Inserir na Tabela Hash
    hash_insert(g_pedidos_hash, ped.id_pedido, offset);
    
    clock_t end = clock();
    double time_spent = (double)(end - start) / CLOCKS_PER_SEC;
    
    printf("\n=====================================================\n");
    printf("Pedido adicionado com sucesso!\n");
    printf("ID gerado: %lld\n", (long long)ped.id_pedido);
    printf("Offset no arquivo: %llu\n", (unsigned long long)offset);
    printf("Tempo de insercao (arquivo + Hash): %.6f segundos\n", time_spent);
    printf("Total de pedidos na Hash: %d\n", g_pedidos_hash->total_entries);
    printf("Colisoes: %d\n", g_pedidos_hash->collisions);
    printf("=====================================================\n");
    
    // Reconstruir índice exaustivo
    construir_indice_exaustivo();
}

static int hash_remove(HashTable* ht, int64_t id_pedido) {
    unsigned int index = hash_function(id_pedido);
    HashNode* node = ht->buckets[index];
    HashNode* prev = NULL;
    
    while (node != NULL) {
        if (node->id_pedido == id_pedido) {
            if (prev == NULL) {
                ht->buckets[index] = node->next;
            } else {
                prev->next = node->next;
            }
            free(node);
            ht->total_entries--;
            return 1;
        }
        prev = node;
        node = node->next;
    }
    return 0;
}

static void hash_remover_pedido_incremental(void) {
    if (!g_pedidos_hash) {
        printf("Erro: Tabela Hash nao foi criada. Use a opcao 15 primeiro.\n");
        return;
    }
    
    char id[64];
    read_line("id_pedido: ", id, sizeof(id));
    if(id[0]=='\0'){ printf("Valor invalido.\n"); return; }
    int64_t id_pedido = atoll(id);
    
    clock_t start = clock();
    
    // Remover da Tabela Hash
    int found = hash_remove(g_pedidos_hash, id_pedido);
    
    clock_t end = clock();
    double time_spent = (double)(end - start) / CLOCKS_PER_SEC;
    
    if (found) {
        printf("\n=====================================================\n");
        printf("Pedido ID %lld removido da Tabela Hash!\n", (long long)id_pedido);
        printf("Tempo de remocao: %.6f segundos\n", time_spent);
        printf("Total de pedidos na Hash: %d\n", g_pedidos_hash->total_entries);
        printf("=====================================================\n");
        printf("\nAVISO: O pedido ainda existe no arquivo pedidos.dat\n");
        printf("Use a opcao 7 para remover do arquivo tambem.\n");
    } else {
        printf("Pedido ID %lld nao encontrado na Tabela Hash.\n", (long long)id_pedido);
        printf("Tempo de busca: %.6f segundos\n", time_spent);
    }
}

static void menu_loop(void){
    char buf[512];
    for(;;){
        printf("\n=====================================\n");
        printf("   TRABALHO — MENU DE OPERACOES\n");
        printf("=====================================\n");
        printf("1) Importar CSV\n");
        printf("2) Buscar produto por ID\n");
        printf("3) Buscar pedido por ID\n");
        printf("4) Adicionar produto\n");
        printf("5) Remover produto\n");
        printf("6) Adicionar pedido\n");
        printf("7) Remover pedido\n");
        printf("8) Listar produtos\n");
        printf("9) Listar pedidos\n");
        printf("10) Joia mais cara\n");
        printf("11) Vendas por nome\n");
        printf("12) Vendas por categoria\n");
        printf("=====================================\n");
        printf("   TRABALHO II - INDICES EM MEMORIA\n");
        printf("=====================================\n");
        printf("13) Criar indice Arvore B (produtos)\n");
        printf("14) Buscar produto com Arvore B\n");
        printf("15) Inserir produto (Arvore B incremental)\n");
        printf("16) Remover produto (Arvore B incremental)\n");
        printf("17) Criar indice Tabela Hash (pedidos)\n");
        printf("18) Buscar pedido com Tabela Hash\n");
        printf("19) Inserir pedido (Hash incremental)\n");
        printf("20) Remover pedido (Hash incremental)\n");
        printf("=====================================\n");
        printf("21) Sair\n");
        printf("=====================================\n");
        printf("Escolha: "); fflush(stdout);
        if(!fgets(buf, sizeof(buf), stdin)) { clearerr(stdin); continue; }
        int opt = atoi(buf);

        if(opt == 1){
            char csv[256];
            read_line("Caminho do CSV [default: jewelry.csv]: ", csv, sizeof(csv));
            if(csv[0]=='\0') strcpy(csv, "jewelry.csv");
            cmd_import(csv);
            press_enter();
        } else if(opt == 2){
            char id[64];
            read_line("id_produto: ", id, sizeof(id));
            if(id[0]=='\0'){ printf("Valor invalido.\n"); press_enter(); continue; }
            cmd_find_prod(id);
            press_enter();
        } else if(opt == 3){
            char id[64];
            read_line("id_pedido: ", id, sizeof(id));
            if(id[0]=='\0'){ printf("Valor invalido.\n"); press_enter(); continue; }
            cmd_find_pedido(id);
            press_enter();
        } else if(opt == 4){
            char cat[128], marca[128], nome[256], preco[64];
            read_line("categoria: ", cat, sizeof(cat));
            read_line("marca: ", marca, sizeof(marca));
            read_line("nome: ", nome, sizeof(nome));
            read_line("preco: ", preco, sizeof(preco));
            if(cat[0]=='\0' || nome[0]=='\0' || preco[0]=='\0'){
                printf("Argumentos invalidos (categoria, nome e preco sao obrigatorios).\n"); press_enter(); continue;
            }
            cmd_add_produto(cat, marca, nome, preco);
            press_enter();
        } else if(opt == 5){
            char id[64];
            read_line("id_produto: ", id, sizeof(id));
            if(id[0]=='\0'){ printf("Valor invalido.\n"); press_enter(); continue; }
            cmd_remove_produto(id);
            press_enter();
        } else if(opt == 6){
            char n_itens[32], ids_produtos[512];
            read_line("numero de itens: ", n_itens, sizeof(n_itens));
            read_line("IDs dos produtos (separados por virgula): ", ids_produtos, sizeof(ids_produtos));
            if(n_itens[0]=='\0' || ids_produtos[0]=='\0'){
                printf("Argumentos invalidos.\n"); press_enter(); continue;
            }
            cmd_add_pedido(n_itens, ids_produtos);
            press_enter();
        } else if(opt == 7){
            char id[64];
            read_line("id_pedido: ", id, sizeof(id));
            if(id[0]=='\0'){ printf("Valor invalido.\n"); press_enter(); continue; }
            cmd_remove_pedido(id);
            press_enter();
        } else if(opt == 8){
            char n[32];
            read_line("Quantos produtos listar? ", n, sizeof(n));
            if(n[0]=='\0'){ printf("Valor invalido.\n"); press_enter(); continue; }
            long vn = strtol(n, NULL, 10);
            cmd_list_prod_n(vn);
            press_enter();
        } else if(opt == 9){
            char n[32];
            read_line("Quantos pedidos listar? ", n, sizeof(n));
            if(n[0]=='\0'){ printf("Valor invalido.\n"); press_enter(); continue; }
            long vn = strtol(n, NULL, 10);
            cmd_list_pedidos_n(vn);
            press_enter();
        } else if(opt == 10){
            q_joia_mais_cara();
            press_enter();
        } else if(opt == 11){
            char nome[256];
            read_line("Nome exato da joia (ex: \"earring red gold diamond\"): ", nome, sizeof(nome));
            if(nome[0]=='\0'){ printf("Nome vazio.\n"); press_enter(); continue; }
            q_vendas_por_nome(nome);
            press_enter();
        } else if(opt == 12){
            char cat[64];
            read_line("Categoria (ex: earring | pendant | necklace ...): ", cat, sizeof(cat));
            if(cat[0]=='\0'){ printf("Categoria vazia.\n"); press_enter(); continue; }
            q_vendas_por_categoria(cat);
            press_enter();
        } else if(opt == 13){
            btree_build_from_file();
            press_enter();
        } else if(opt == 14){
            char id[64];
            read_line("id_produto: ", id, sizeof(id));
            if(id[0]=='\0'){ printf("Valor invalido.\n"); press_enter(); continue; }
            btree_buscar_produto(id);
            press_enter();
        } else if(opt == 15){
            btree_inserir_produto_incremental();
            press_enter();
        } else if(opt == 16){
            btree_remover_produto_incremental();
            press_enter();
        } else if(opt == 17){
            hash_build_from_file();
            press_enter();
        } else if(opt == 18){
            char id[64];
            read_line("id_pedido: ", id, sizeof(id));
            if(id[0]=='\0'){ printf("Valor invalido.\n"); press_enter(); continue; }
            hash_buscar_pedido(id);
            press_enter();
        } else if(opt == 19){
            hash_inserir_pedido_incremental();
            press_enter();
        } else if(opt == 20){
            hash_remover_pedido_incremental();
            press_enter();
        } else if(opt == 21){
            printf("Saindo.\n");
            if(g_produtos_btree) {
                btree_free(g_produtos_btree);
                g_produtos_btree = NULL;
            }
            if(g_pedidos_hash) {
                hash_free(g_pedidos_hash);
                g_pedidos_hash = NULL;
            }
            break;
        } else {
            printf("Opcao invalida.\n");
        }
    }
}

int main(){
    menu_loop();
    return 0;
}
