#include "graph_io.hpp"
#include <iostream>
int main(int argc,char**argv) {try {
    if(argc!=3) throw std::runtime_error("usage: read_graph txt|bin FILE");
    fdio::Id count=0;
    auto header=[](fdio::Id n,fdio::Id m){std::cout<<"nodes="<<n<<" edges="<<m<<'\n';};
    auto edge=[&](fdio::Id u,fdio::Id v){
        // Consume directed edge u -> v here, or append it to your graph structure.
        (void)u;(void)v;++count;
    };
    std::string format=argv[1];
    if(format=="txt") fdio::read_graph_txt(argv[2],header,edge);
    else if(format=="bin") fdio::read_graph_bin(argv[2],header,edge);
    else throw std::runtime_error("expected txt or bin");
    std::cout<<"read_edges="<<count<<'\n';
} catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
