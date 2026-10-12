/* Array adapter to the official HPF 3.23 implementation. Solver routines are
 * included unchanged; this adapter replaces DIMACS input and requests only the
 * minimum cut (no unnecessary maximum-flow recovery). Not thread-safe: HPF has
 * process-global state. The original arc ordering is preserved.
 *
 * Copyright (c) 2001. The Regents of the University of California (Regents).
 * All Rights Reserved.
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for educational, research, and not-for-profit purposes, without
 * fee and without a signed licensing agreement, is hereby granted, provided that
 * the above copyright notice, this paragraph and the following two paragraphs
 * appear in all copies, modifications, and distributions.
 * Contact The Office of Technology Licensing, UC Berkeley, 2150 Shattuck Avenue,
 * Suite 510, Berkeley, CA 94720-1620, (510) 643-7201, for commercial licensing
 * opportunities. Created by Bala Chandran and Dorit S. Hochbaum, Department of
 * Industrial Engineering and Operations Research, University of California,
 * Berkeley.
 * IN NO EVENT SHALL REGENTS BE LIABLE TO ANY PARTY FOR DIRECT, INDIRECT, SPECIAL,
 * INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS, ARISING OUT OF
 * THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF REGENTS HAS BEEN
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * REGENTS SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE. THE SOFTWARE AND ACCOMPANYING DOCUMENTATION, IF ANY, PROVIDED
 * HEREUNDER IS PROVIDED "AS IS". REGENTS HAS NO OBLIGATION TO PROVIDE MAINTENANCE,
 * SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 */
#define main upstream_hpf_cli_main
#include FD_HPF_SOURCE
#undef main

int fd_hpf_cut(int n, int m, const int* row, const int* col,
               const unsigned int* cap, int s, int t,
               int* left, unsigned long long* value, double* timing) {
    int i, first=0, last=m-1;
    double start=timer(), initialized, solved;
    if(n<2 || m<0 || s<0 || s>=n || t<0 || t>=n || s==t) return 1;
    numNodes=n; numArcs=m; source=s+1; sink=t+1; highestStrongLabel=1;
    adjacencyList=(Node*)calloc(n,sizeof(Node));
    strongRoots=(Root*)calloc(n,sizeof(Root));
    labelCount=(uint*)calloc(n,sizeof(uint));
    arcList=(Arc*)calloc(m ? m : 1,sizeof(Arc));
    if(!adjacencyList || !strongRoots || !labelCount || !arcList) return 2;
    for(i=0;i<n;++i) {
        initializeRoot(&strongRoots[i]); initializeNode(&adjacencyList[i],i+1);
    }
    for(i=0;i<m;++i) initializeArc(&arcList[i]);
    for(i=0;i<m;++i) {
        int u=row[i],v=col[i],j=((u+v)%2) ? first++ : last--;
        if(u<0 || u>=n || v<0 || v>=n) return 3;
        arcList[j].from=&adjacencyList[u]; arcList[j].to=&adjacencyList[v];
        arcList[j].capacity=cap[i];
        ++adjacencyList[u].numAdjacent; ++adjacencyList[v].numAdjacent;
    }
    for(i=0;i<n;++i) createOutOfTree(&adjacencyList[i]);
    for(i=0;i<m;++i) {
        Arc* a=&arcList[i];
        uint u=a->from->number,v=a->to->number;
        if(source==v || sink==u || u==v) continue;
        if(source==u && v==sink) a->flow=a->capacity;
        else if(u==source) addOutOfTreeNode(&adjacencyList[u-1],a);
        else if(v==sink) addOutOfTreeNode(&adjacencyList[v-1],a);
        else addOutOfTreeNode(&adjacencyList[u-1],a);
    }
    initialized=timer();
    simpleInitialization(); pseudoflowPhase1();
    solved=timer();
    for(i=0;i<n;++i) left[i]=(adjacencyList[i].label>=numNodes);
    *value=0;
    for(i=0;i<m;++i) if(left[row[i]] && !left[col[i]]) *value+=cap[i];
    timing[0]=initialized-start; timing[1]=solved-initialized;
    freeMemory();
    timing[2]=timer()-start;
    return (!left[s] || left[t]) ? 4 : 0;
}
