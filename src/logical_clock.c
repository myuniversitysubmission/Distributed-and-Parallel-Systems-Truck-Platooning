#include "logical_clock.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void lc_init(MatrixClock *lc, int size, int id)
{
    if (!lc) return;
    if (size > LOGICAL_MAX_NODES) size = LOGICAL_MAX_NODES;
    lc->size = size;
    lc->id   = id;
    pthread_mutex_init(&lc->mutex, NULL);
    pthread_mutex_lock(&lc->mutex);
    for (int i = 0; i < lc->size; ++i)
        for (int j = 0; j < lc->size; ++j)
            lc->M[i][j] = 0;
    pthread_mutex_unlock(&lc->mutex);
}

void lc_inc_local(MatrixClock *lc)
{
    if (!lc) return;
    pthread_mutex_lock(&lc->mutex);
    lc->M[lc->id][lc->id]++;
    pthread_mutex_unlock(&lc->mutex);
}

void lc_reset_node(MatrixClock *lc, int node_id)
{
    if (!lc || node_id < 0 || node_id >= lc->size) return;
    pthread_mutex_lock(&lc->mutex);
    for (int j = 0; j < lc->size; ++j) {
        lc->M[node_id][j] = 0; // zero row
        lc->M[j][node_id] = 0; // zero column
    }
    pthread_mutex_unlock(&lc->mutex);
}

void lc_print(MatrixClock *lc)
{
    if (!lc) return;
    pthread_mutex_lock(&lc->mutex);
    printf("MatrixClock (size=%d, id=%d):\n", lc->size, lc->id);
    for (int i = 0; i < lc->size; ++i) {
        for (int j = 0; j < lc->size; ++j) {
            printf("%3d ", lc->M[i][j]);
        }
        printf("\n");
    }
    pthread_mutex_unlock(&lc->mutex);
}

// void lc_merge_row(MatrixClock *lc, int src_id, const int *row)
// {
//     if (!lc || !row || src_id < 0 || src_id >= lc->size) return;
//     pthread_mutex_lock(&lc->mutex);
//     // updating our own records of sender truck.
//     for (int j = 0; j < lc->size; ++j) {
//         if (row[j] > lc->M[src_id][j])
//             lc->M[src_id][j] = row[j];
//     }
   
//     for (int j = 0; j < lc->size; ++j) {
//         // if the sender truck's val is > our truck's value, update in our truck's record
//         if (lc->M[src_id][j] > lc->M[lc->id][j])
//             lc->M[lc->id][j] = lc->M[src_id][j];
//     }
//     pthread_mutex_unlock(&lc->mutex);
// }

// void lc_merge_row_from_str(MatrixClock *lc, int src_id, const char *str)
// {
//     if (!lc || !str) return;
//     // str format: "a,b,c,..." & put it in our records.
//     int tmp[LOGICAL_MAX_NODES] = {0};
//     int idx = 0;
//     char *copy = strdup(str);
//     char *tok = strtok(copy, ",");
//     while (tok && idx < lc->size) {
//         tmp[idx++] = atoi(tok);
//         tok = strtok(NULL, ",");
//     }
//     lc_merge_row(lc, src_id, tmp);
//     free(copy);
// }

void lc_set_node_diag(MatrixClock *lc, int node_id, int val)
{
    // while initializing a client, make its nxn value as 1
    if (!lc || node_id < 0 || node_id >= lc->size) return;
    pthread_mutex_lock(&lc->mutex);
    lc->M[node_id][node_id] = val;
    pthread_mutex_unlock(&lc->mutex);
}

char *lc_serialize_matrix(MatrixClock *lc)
{
    if (!lc) return NULL;
    // Estimate buffer: each int up to ~5 chars + commas + semicolons + prefix
    int bufsz = lc->size * lc->size * 6 + lc->size + 16;
    char *buf = (char *)malloc(bufsz);
    if (!buf) return NULL;
    char *p = buf;
    int written = snprintf(p, bufsz, "MATRIX:");
    p += written;
    for (int i = 0; i < lc->size; ++i) {
        for (int j = 0; j < lc->size; ++j) {
            int n = snprintf(p, bufsz - (p - buf), "%d", lc->M[i][j]);
            p += n;
            if (j + 1 < lc->size) {
                *p++ = ',';
                *p = '\0';
            }
        }
        if (i + 1 < lc->size) {
            *p++ = ';';
            *p = '\0';
        }
    }
    return buf;
}

void lc_merge_matrix_from_str(MatrixClock *lc, const char *str)
{
    if (!lc || !str) return;

    char *copy = strdup(str);
    if (!copy) return;

    pthread_mutex_lock(&lc->mutex);

    char *saveptr1;
    char *rowTok = strtok_r(copy, ";", &saveptr1);
    int rowIdx = 0;

    while (rowTok && rowIdx < lc->size) {

        int tmp[LOGICAL_MAX_NODES] = {0};
        int idx = 0;

        /* Use a SECOND strtok_r for columns */
        char *saveptr2;
        char *cell = strtok_r(rowTok, ",", &saveptr2);

        while (cell && idx < lc->size) {
            tmp[idx++] = atoi(cell);
            cell = strtok_r(NULL, ",", &saveptr2);
        }

        for (int j = 0; j < lc->size; ++j){
            if(tmp[j] > lc->M[rowIdx][j])
            lc->M[rowIdx][j] = tmp[j];
        }
        rowIdx++;
        rowTok = strtok_r(NULL, ";", &saveptr1);
    }

    /* Column-wise max, update only my row */
    for (int j = 0; j < lc->size; ++j) {
        int maxcol = lc->M[0][j];
        for (int i = 1; i < lc->size; ++i)
            if (lc->M[i][j] > maxcol)
                maxcol = lc->M[i][j];

        lc->M[lc->id][j] = maxcol;
    }

    pthread_mutex_unlock(&lc->mutex);
    free(copy);
}
