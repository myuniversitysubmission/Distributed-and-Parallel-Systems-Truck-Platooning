#ifndef LOGICAL_CLOCK_H
#define LOGICAL_CLOCK_H

#include <pthread.h>

#define LOGICAL_MAX_CLIENTS 7
#define LOGICAL_MAX_NODES (LOGICAL_MAX_CLIENTS + 1) // server + clients

typedef struct {
    int size; // total nodes
    int id;   // 0 = server, 1..n clients
    int M[LOGICAL_MAX_NODES][LOGICAL_MAX_NODES];
    pthread_mutex_t mutex;
} MatrixClock;

void lc_init(MatrixClock *lc, int size, int id);
void lc_inc_local(MatrixClock *lc); // increment own clock (M[id][id]++)
// Merge a parsed row array into matrix (row from src_id)
void lc_merge_row(MatrixClock *lc, int src_id, const int *row);
// Parse a "a,b,c..." string and merge into matrix as src_id
void lc_merge_row_from_str(MatrixClock *lc, int src_id, const char *str);
void lc_set_node_diag(MatrixClock *lc, int node_id, int val); // set M[node][node]
void lc_reset_node(MatrixClock *lc, int node_id); // set row & col to 0
void lc_print(MatrixClock *lc); // print

// Serialize/merge entire matrix 
char *lc_serialize_matrix(MatrixClock *lc);    
void lc_merge_matrix_from_str(MatrixClock *lc, const char *str); 

#endif // LOGICAL_CLOCK_H