/* Export nets.h + nets_extra.h as JSON (index order = firmware net index). */
#include <stdio.h>
#include "nets_extra.h"

static void dump(const net_t *n, int last)
{
    printf("{\"name\":\"%s\",\"klass\":\"%s\",\"m0\":[", n->name, n->klass);
    for (int p = 0; p < n->np; ++p) printf("%s%d", p ? "," : "", n->m0[p]);
    printf("],\"pname\":[");
    for (int p = 0; p < n->np; ++p) printf("%s\"%s\"", p ? "," : "", n->pname[p]);
    printf("],\"T\":[");
    for (int t = 0; t < n->nt; ++t) {
        printf("%s{\"name\":\"%s\",\"pre\":[", t ? "," : "", n->tname[t]);
        for (int k = 0; k < n->npre[t]; ++k) printf("%s%d", k ? "," : "", n->pre[t][k]);
        printf("],\"post\":[");
        for (int k = 0; k < n->npost[t]; ++k) printf("%s%d", k ? "," : "", n->post[t][k]);
        printf("]}");
    }
    printf("]}%s\n", last ? "" : ",");
}

int main(void)
{
    printf("[\n");
    for (int i = 0; i < N_NETS; ++i) dump(&NETS[i], 0);
    for (int i = 0; i < N_EXTRA; ++i) dump(&EXTRA[i], i == N_EXTRA - 1);
    printf("]\n");
    return 0;
}
