#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

typedef struct Nodo {
    int vertice;
    struct Nodo* prox;
} Nodo;

typedef struct Grafo {
    int V;
    Nodo** adj;
} Grafo;

typedef struct Lista {
    int* vertices;
    int tamanho;
    struct Lista* prox;
} Lista;

typedef struct ThreadData {
    Grafo* g;
    int k;
    int* contador;
    Lista** cliques;
    pthread_mutex_t* mutex;
    Lista** trabalhos_geral;
    int thread_id;
    bool ativo;
    Lista* fila_trabalhos;
} ThreadData;

bool conexao_completa(Grafo* g, int* clique, int tamanho, int vizinho);

pthread_mutex_t mutex_roubo = PTHREAD_MUTEX_INITIALIZER;

Nodo* criar_nodo(int vertice) {
    Nodo* novo_nodo = (Nodo*)malloc(sizeof(Nodo));
    novo_nodo->vertice = vertice;
    novo_nodo->prox = NULL;
    return novo_nodo;
}

Grafo* criar_grafo(int V) {
    Grafo* g = (Grafo*)malloc(sizeof(Grafo));
    g->V = V;
    g->adj = (Nodo**)malloc(V * sizeof(Nodo*));
    for (int i = 0; i < V; i++) {
        g->adj[i] = NULL;
    }
    return g;
}

void adicionar_aresta(Grafo* g, int u, int v) {
    Nodo* novo_nodo_v = criar_nodo(v);
    novo_nodo_v->prox = g->adj[u];
    g->adj[u] = novo_nodo_v;

    Nodo* novo_nodo_u = criar_nodo(u);
    novo_nodo_u->prox = g->adj[v];
    g->adj[v] = novo_nodo_u;
}

Lista* roubar_trabalho(ThreadData* dados, int maxv) {
    pthread_mutex_lock(&mutex_roubo);
    Lista* item = dados->fila_trabalhos;
    if (item) {
        dados->fila_trabalhos = item->prox;
    }
    pthread_mutex_unlock(&mutex_roubo);
    return item;
}


void* contagem_de_cliques_thread(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    Grafo* g = data->g;
    int k = data->k;
    int* contador = data->contador;
    Lista** cliques = data->cliques;
    pthread_mutex_t* mutex = data->mutex;

    while (true) {
        if (*cliques == NULL) {
            *cliques = roubar_trabalho(data, 5); // Tenta roubar 5 itens de outra thread
            if (*cliques == NULL) {
                break;
            }
        }

        Lista* clique_atual = *cliques;
        *cliques = (*cliques)->prox;

        if (clique_atual->tamanho == k) {
            (*contador)++;
            free(clique_atual->vertices);
            free(clique_atual);
            continue;
        }

        int ultimo_vertice = clique_atual->vertices[clique_atual->tamanho - 1];

        for (int vizinho = ultimo_vertice + 1; vizinho < g->V; vizinho++) {
            if (conexao_completa(g, clique_atual->vertices, clique_atual->tamanho, vizinho)) {
                int* nova_clique = malloc((clique_atual->tamanho + 1) * sizeof(int));
                memcpy(nova_clique, clique_atual->vertices, clique_atual->tamanho * sizeof(int));
                nova_clique[clique_atual->tamanho] = vizinho;

                Lista* nova_lista = malloc(sizeof(Lista));
                nova_lista->vertices = nova_clique;
                nova_lista->tamanho = clique_atual->tamanho + 1;
                nova_lista->prox = *cliques;
                *cliques = nova_lista;
            }
        }

        free(clique_atual->vertices);
        free(clique_atual);
    }
    return NULL;
}

int contagem_de_cliques_paralela(Grafo* g, int k, int t, int maxv) {
    Lista* cliques = NULL;
    pthread_t threads[t];
    ThreadData dados[t];
    int contadores[t];
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    Lista* trabalho_por_thread[t];

    for (int i = 0; i < t; i++) {
        contadores[i] = 0;
        trabalho_por_thread[i] = NULL;
    }

    for (int v = 0; v < g->V; v++) {
        int* clique_inicial = malloc(sizeof(int));
        clique_inicial[0] = v;
        Lista* nova_lista = malloc(sizeof(Lista));
        nova_lista->vertices = clique_inicial;
        nova_lista->tamanho = 1;
        nova_lista->prox = cliques;
        cliques = nova_lista;
    }

    Lista* atual = cliques;
    int indice = 0;
    while (atual != NULL) {
        Lista* proximo = atual->prox;
        atual->prox = trabalho_por_thread[indice];
        trabalho_por_thread[indice] = atual;
        indice = (indice + 1) % t;
        atual = proximo;
    }

    for (int i = 0; i < t; i++) {
        dados[i].g = g;
        dados[i].k = k;
        dados[i].contador = &contadores[i];
        dados[i].cliques = &trabalho_por_thread[i];
        dados[i].mutex = &mutex;
        dados[i].trabalhos_geral = &trabalho_por_thread[i];
        dados[i].fila_trabalhos = NULL;  // Fila inicial para cada thread
        dados[i].thread_id = i;
        dados[i].ativo = true;

        pthread_create(&threads[i], NULL, contagem_de_cliques_thread, &dados[i]);
    }

    for (int i = 0; i < t; i++) {
        pthread_join(threads[i], NULL);
    }

    int total_contador = 0;
    for (int i = 0; i < t; i++) {
        total_contador += contadores[i];
    }

    return total_contador;
}


bool conexao_completa(Grafo* g, int* clique, int tamanho, int vizinho) {
    for (int i = 0; i < tamanho; i++) {
        int vertice = clique[i];
        Nodo* adj = g->adj[vertice];
        bool conectado = false;

        while (adj != NULL) {
            if (adj->vertice == vizinho) {
                conectado = true;
                break;
            }
            adj = adj->prox;
        }

        if (!conectado) {
            return false;
        }
    }
    return true;
}

void carregar_grafo_de_arquivo(Grafo* g, const char* nome_arquivo) {
    FILE* arquivo = fopen(nome_arquivo, "r");
    if (arquivo == NULL) {
        printf("Erro ao abrir o arquivo!\n");
        exit(EXIT_FAILURE);
    }

    int u, v;
    while (fscanf(arquivo, "%d %d", &u, &v) != EOF) {
        if (u < 0 || u >= g->V || v < 0 || v >= g->V) {
            continue;
        }
        adicionar_aresta(g, u, v);
    }
    fclose(arquivo);
}

void liberar_grafo(Grafo* g) {
    for (int i = 0; i < g->V; i++) {
        Nodo* nodo_atual = g->adj[i];
        while (nodo_atual != NULL) {
            Nodo* prox = nodo_atual->prox;
            free(nodo_atual);
            nodo_atual = prox;
        }
    }
    free(g->adj);
    free(g);
}


int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <dataset> <k> <num_threads> <maxv_roubado>\n", argv[0]);
        return 1;
    }

    char dataset[30];
    int k, num_threads, maxv_roubado;
    strcpy(dataset, argv[1]);
    k = atoi(argv[2]);
    num_threads = atoi(argv[3]);
    maxv_roubado = atoi(argv[4]);
    struct timespec start_time, end_time;;
    int num_vertices;

    if (strcmp(dataset, "citeseer") == 0) {
        num_vertices = 3312;
    } else if (strcmp(dataset, "ca_astroph") == 0) {
        num_vertices = 18772;
    } else if (strcmp(dataset, "dblp") == 0) {
        num_vertices = 317080;
    } else {
        return 1;
    }

    Grafo* g = criar_grafo(num_vertices);

    char arquivo[110];
    snprintf(arquivo, sizeof(arquivo), "%s.edgelist", dataset);

    carregar_grafo_de_arquivo(g, arquivo);

    clock_gettime(CLOCK_MONOTONIC, &start_time);

    int resultado = contagem_de_cliques_paralela(g, k, num_threads,maxv_roubado);

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    
    double time_spent = (end_time.tv_sec - start_time.tv_sec) + (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

    printf("Tempo de execução: %.6f segundos\n", time_spent);
    printf("Número de cliques de tamanho %d: %d\n", k, resultado);

    liberar_grafo(g);

    return 0;
}
