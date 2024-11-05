#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <omp.h>

int contador_total = 0;

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

void carregar_grafo(Grafo* g, const char* nome_arquivo) {
    FILE* arquivo = fopen(nome_arquivo, "r");
    if (arquivo == NULL) {
        perror("Erro ao abrir o arquivo");
        exit(EXIT_FAILURE);
    }

    int u, v;
    while (fscanf(arquivo, "%d %d", &u, &v) != EOF) {
        if (u >= 0 && u < g->V && v >= 0 && v < g->V) {
            adicionar_aresta(g, u, v);
        }
    }
    fclose(arquivo);
}

Lista* criar_lista(int* clique, int tamanho) {
    Lista* nova_lista = (Lista*)malloc(sizeof(Lista));
    nova_lista->vertices = (int*)malloc(tamanho * sizeof(int));
    memcpy(nova_lista->vertices, clique, tamanho * sizeof(int));
    nova_lista->tamanho = tamanho;
    nova_lista->prox = NULL;
    return nova_lista;
}

void empilhar(Lista** cliques, int* clique, int tamanho) {
    Lista* nova_clique = criar_lista(clique, tamanho);
    nova_clique->prox = *cliques;
    *cliques = nova_clique;
}

Lista* desempilhar(Lista** cliques) {
    if (*cliques == NULL) return NULL;
    Lista* topo = *cliques;
    *cliques = (*cliques)->prox;
    return topo;
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

bool contem(int* array, int tamanho, int elemento) {
    for (int i = 0; i < tamanho; i++) {
        if (array[i] == elemento) {
            return true;
        }
    }
    return false;
}

int contagem_de_cliques_parallel(Grafo* g, int k, int num_threads, int maxv_roubado) {
    Lista** cliques = malloc(num_threads * sizeof(Lista*));
    if (cliques == NULL) {
        fprintf(stderr, "Erro ao alocar memória para cliques\n");
        exit(EXIT_FAILURE);
    }
    
    for (int i = 0; i < num_threads; i++) {
        cliques[i] = NULL;
    }

    #pragma omp parallel num_threads(num_threads)
    {
        int id = omp_get_thread_num();
        int local_contador = 0;
        Lista* thread_cliques = NULL;

        // Inicializa a pilha local com os vértices
        #pragma omp for
        for (int v = 0; v < g->V; v++) {
            int clique_inicial[] = {v};
            empilhar(&thread_cliques, clique_inicial, 1);
        }

        while (true) {
            Lista* clique_atual = desempilhar(&thread_cliques);

            if (clique_atual == NULL) {
                // Tentar roubar trabalho de outras threads
                bool roubou_trabalho = false;
                for (int i = 0; i < num_threads; i++) {
                    if (i != id && cliques[i] != NULL) {
                        #pragma omp critical
                        {
                            // Checa se a lista de outra thread tem cliques para roubar
                            if (cliques[i] != NULL) {
                                Lista* roubado = desempilhar(&cliques[i]);
                                if (roubado != NULL) {
                                    empilhar(&thread_cliques, roubado->vertices, roubado->tamanho);
                                    roubou_trabalho = true;
                                    // Não liberamos 'roubado' aqui
                                }
                            }
                        }
                    }
                    if (roubou_trabalho) break;
                }

                if (thread_cliques == NULL) {
                    break; // Se ainda não tiver cliques, sair
                }
            }

            if (clique_atual != NULL) {
                if (clique_atual->tamanho == k) {
                    local_contador++;
                } else {
                    int ultimo_vertice = clique_atual->vertices[clique_atual->tamanho - 1];
                    for (int vizinho = ultimo_vertice + 1; vizinho < g->V; vizinho++) {
                        if (!contem(clique_atual->vertices, clique_atual->tamanho, vizinho) &&
                            conexao_completa(g, clique_atual->vertices, clique_atual->tamanho, vizinho)) {
                            int nova_clique[clique_atual->tamanho + 1];
                            memcpy(nova_clique, clique_atual->vertices, clique_atual->tamanho * sizeof(int));
                            nova_clique[clique_atual->tamanho] = vizinho;

                            empilhar(&thread_cliques, nova_clique, clique_atual->tamanho + 1);
                        }
                    }
                }

                free(clique_atual->vertices);
                free(clique_atual);
            }
        }

        // Somar a contagem local na variável global
        #pragma omp atomic
        contador_total += local_contador;
    }

    free(cliques);
    return contador_total;
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

void exibir_uso(const char *nome_programa) {
    fprintf(stderr, "Uso: %s <dataset> <k> <num_threads> <maxv_roubado>\n", nome_programa);
}

int obter_num_vertices(const char *dataset) {
    const char *datasets[] = {"citeseer", "ca_astroph", "dblp"};
    const int num_vertices[] = {3312, 18772, 317080};
    for (int i = 0; i < 3; i++) {
        if (strcmp(dataset, datasets[i]) == 0) {
            return num_vertices[i];
        }
    }
    fprintf(stderr, "Dataset não suportado: %s\n", dataset);
    return -1;
}

Grafo* inicializar_grafo(const char *dataset) {
    int num_vertices = obter_num_vertices(dataset);
    if (num_vertices == -1) {
        return NULL; // Retorna NULL em caso de erro
    }

    char arquivo[110];
    snprintf(arquivo, sizeof(arquivo), "%s.edgelist", dataset);

    Grafo *g = criar_grafo(num_vertices);
    carregar_grafo(g, arquivo); // Chamada direta à nova função de carregar o grafo

    return g;
}

void medir_tempo_execucao(Grafo *g, int k, int num_threads, int maxv_roubado) {
    struct timespec start_time, end_time;

    clock_gettime(CLOCK_MONOTONIC, &start_time);
    int total_cliques = contagem_de_cliques_parallel(g, k, num_threads, maxv_roubado);
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    double tempo_total = (end_time.tv_sec - start_time.tv_sec) +
                         (end_time.tv_nsec - start_time.tv_nsec) / 1e9;

    printf("Total de cliques de tamanho %d: %d\n", k, total_cliques);
    printf("Tempo total de execução: %.2f segundos\n", tempo_total);
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        exibir_uso(argv[0]);
        return EXIT_FAILURE;
    }

    const char *dataset = argv[1];
    int k = atoi(argv[2]);
    int num_threads = atoi(argv[3]);
    int maxv_roubado = atoi(argv[4]);

    Grafo *g = inicializar_grafo(dataset);
    if (g == NULL) {
        fprintf(stderr, "Falha ao inicializar o grafo.\n");
        return EXIT_FAILURE;
    }

    medir_tempo_execucao(g, k, num_threads, maxv_roubado);
    liberar_grafo(g);

    return EXIT_SUCCESS;
}
