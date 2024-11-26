#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <mpi.h>

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

Lista* criar_lista(int* clique, int tamanho) {
    Lista* nova_lista = (Lista*)malloc(sizeof(Lista));
    nova_lista->vertices = (int*)malloc(tamanho * sizeof(int));
    for (int i = 0; i < tamanho; i++) {
        nova_lista->vertices[i] = clique[i];
    }
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

int contagem_de_cliques_parallel(Grafo* g, int k, int rank, int num_procs) {
    Lista* cliques = NULL;
    int contador_local = 0;

    for (int v = rank; v < g->V; v += num_procs) {
        int clique_inicial[] = {v};
        empilhar(&cliques, clique_inicial, 1);
    }

    while (cliques != NULL) {
        Lista* clique_atual = desempilhar(&cliques);

        if (clique_atual->tamanho == k) {
            contador_local++;
            free(clique_atual->vertices);
            free(clique_atual);
            continue;
        }

        int ultimo_vertice = clique_atual->vertices[clique_atual->tamanho - 1];

        for (int vizinho = ultimo_vertice + 1; vizinho < g->V; vizinho++) {
            if (!contem(clique_atual->vertices, clique_atual->tamanho, vizinho) 
                && conexao_completa(g, clique_atual->vertices, clique_atual->tamanho, vizinho)) {
                int nova_clique[clique_atual->tamanho + 1];
                for (int j = 0; j < clique_atual->tamanho; j++) {
                    nova_clique[j] = clique_atual->vertices[j];
                }
                nova_clique[clique_atual->tamanho] = vizinho;
                empilhar(&cliques, nova_clique, clique_atual->tamanho + 1);
            }
        }

        free(clique_atual->vertices);
        free(clique_atual);
    }

    int total_cliques = 0;
    MPI_Reduce(&contador_local, &total_cliques, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

    return total_cliques;
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

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank, num_procs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &num_procs);

    char dataset[30];
    int k;

    if (rank == 0) {
        scanf("%s %d", dataset, &k);
    }

    MPI_Bcast(&k, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(dataset, 30, MPI_CHAR, 0, MPI_COMM_WORLD);

    int num_vertices;
    if (strcmp(dataset, "citeseer") == 0) {
        num_vertices = 3312;
    } else if (strcmp(dataset, "ca_astroph") == 0) {
        num_vertices = 18772;
    } else if (strcmp(dataset, "dblp") == 0) {
        num_vertices = 317080;
    } else {
        if (rank == 0) {
            printf("Dataset desconhecido!\n");
        }
        MPI_Finalize();
        return 1;
    }
    
    Grafo* g = criar_grafo(num_vertices);

    char arquivo[110];
    snprintf(arquivo, sizeof(arquivo), "%s.edgelist", dataset);

    carregar_grafo_de_arquivo(g, arquivo);

    int resultado = contagem_de_cliques_parallel(g, k, rank, num_procs);

    if (rank == 0) {
        printf("Número de cliques de tamanho %d: %d\n", k, resultado);
    }

    liberar_grafo(g);

    MPI_Finalize();
    return 0;
}
