#include "radiogeddon_dsp.h"
#include <stdlib.h>

void radiogeddon_dsp_cluster_add(
    RadioGeddonCluster* clusters,
    size_t* n,
    size_t max_clusters,
    uint32_t value,
    uint32_t tol_pct) {
    for(size_t i = 0; i < *n; i++) {
        uint32_t c = clusters[i].center;
        uint32_t tol = (c * tol_pct) / 100 + 10;
        uint32_t diff = (value > c) ? (value - c) : (c - value);
        if(diff <= tol) {
            clusters[i].count++;
            clusters[i].sum += value;
            clusters[i].center = (uint32_t)(clusters[i].sum / clusters[i].count);
            return;
        }
    }
    if(*n < max_clusters) {
        clusters[*n].center = value;
        clusters[*n].count = 1;
        clusters[*n].sum = value;
        (*n)++;
    }
}

size_t radiogeddon_dsp_parse_line(
    const char* line,
    size_t* count,
    uint32_t* min_us,
    uint32_t* max_us,
    RadioGeddonCluster* clusters,
    size_t* cluster_n,
    size_t max_clusters) {
    size_t parsed = 0;
    const char* p = line;
    char* end = NULL;
    while(*p) {
        long v = strtol(p, &end, 10);
        if(end == p) break;
        p = end;
        uint32_t a = (uint32_t)((v < 0) ? -v : v);
        if(a == 0) continue;
        parsed++;
        if(count) (*count)++;
        if(min_us && (*min_us == 0 || a < *min_us)) *min_us = a;
        if(max_us && a > *max_us) *max_us = a;
        if(clusters && cluster_n) {
            radiogeddon_dsp_cluster_add(
                clusters, cluster_n, max_clusters, a, RADIOGEDDON_CLUSTER_TOL);
        }
    }
    return parsed;
}

void radiogeddon_dsp_sort_clusters(RadioGeddonCluster* clusters, size_t n) {
    for(size_t i = 1; i < n; i++) {
        RadioGeddonCluster key = clusters[i];
        size_t j = i;
        while(j > 0 && clusters[j - 1].center > key.center) {
            clusters[j] = clusters[j - 1];
            j--;
        }
        clusters[j] = key;
    }
}

void radiogeddon_dsp_key_stats(uint64_t key, int* nonzero, int* distinct) {
    uint8_t bytes[8];
    for(size_t i = 0; i < 8; i++) bytes[i] = (uint8_t)(key >> (8 * (7 - i)));
    int nz = 0, dist = 0;
    for(size_t i = 0; i < 8; i++) {
        if(bytes[i]) nz++;
    }
    for(int v = 0; v < 256; v++) {
        for(size_t i = 0; i < 8; i++) {
            if(bytes[i] == v) {
                dist++;
                break;
            }
        }
    }
    if(nonzero) *nonzero = nz;
    if(distinct) *distinct = dist;
}
