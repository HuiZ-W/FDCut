#include "graph_io.hpp"
#include <array>
#include <charconv>
#include <cstring>
#include <iostream>
#include <system_error>
int main(int argc,char**argv) { try {
    if(argc<4) throw std::runtime_error("usage: graph_convert txt-to-bin|bin-to-txt INPUT OUTPUT OR raw-to-txt INPUT OUTPUT N [ascending]");
    std::string mode=argv[1],input=argv[2],output=argv[3],partial=output+".partial";
    if(std::filesystem::exists(output)||std::filesystem::exists(partial)) throw std::runtime_error("refusing existing output");
    if(mode!="txt-to-bin"&&mode!="bin-to-txt"&&mode!="raw-to-txt") throw std::runtime_error("unknown mode");
    std::ofstream out(partial,std::ios::binary); out.exceptions(std::ios::badbit|std::ios::failbit);
    auto header=[&](fdio::Id n,fdio::Id m){if(mode=="txt-to-bin") {out.write("FDGBIN01",8);fdio::write_u64(out,n);fdio::write_u64(out,m);} else out<<n<<' '<<m<<'\n';};
    std::array<char,1<<20> text_buffer{}; size_t used=0;
    auto flush=[&](){if(used)out.write(text_buffer.data(),used);used=0;};
    auto edge=[&](fdio::Id u,fdio::Id v){
        if(mode=="txt-to-bin"){
            if(text_buffer.size()-used<16)flush();
            for(int i=0;i<8;++i)text_buffer[used++]=char(u>>(8*i));
            for(int i=0;i<8;++i)text_buffer[used++]=char(v>>(8*i));
        }
        else {
            // A uint64 pair needs at most 20 + 1 + 20 + 1 bytes. Build the
            // record separately so every to_chars result is checked before
            // appending delimiters or copying into the large output buffer.
            std::array<char,42> line{};
            char* p=line.data();
            auto first=std::to_chars(p,line.data()+line.size(),u);
            if(first.ec!=std::errc{})throw std::runtime_error("integer formatting failed");
            p=first.ptr;*p++=' ';
            auto second=std::to_chars(p,line.data()+line.size(),v);
            if(second.ec!=std::errc{})throw std::runtime_error("integer formatting failed");
            p=second.ptr;*p++='\n';
            const size_t length=static_cast<size_t>(p-line.data());
            if(text_buffer.size()-used<length)flush();
            std::memcpy(text_buffer.data()+used,line.data(),length);used+=length;
        }
    };
    fdio::GraphInfo g{};
    if(mode=="txt-to-bin") g=fdio::read_graph_txt(input,header,edge);
    else if(mode=="bin-to-txt") g=fdio::read_graph_bin(input,header,edge);
    else {
        if(argc!=5&&argc!=6) throw std::runtime_error("raw-to-txt requires N [ascending]");
        std::istringstream ns(argv[4]); auto n=fdio::read_decimal(ns);
        auto bytes=std::filesystem::file_size(input); if(bytes%16) throw std::runtime_error("invalid raw byte count");
        g={n,bytes/16}; header(n,g.edges);
        std::ifstream in(input,std::ios::binary); fdio::Id pu=0,pv=0;
        for(fdio::Id i=0;i<g.edges;++i) {
            auto u=fdio::read_u64(in),v=fdio::read_u64(in);
            if(u>=n||v>=n||(i&&(u<pu||(u==pu&&v<=pv)))) throw std::runtime_error("invalid endpoint or nonunique/unsorted edges");
            if(argc==6&&(std::string(argv[5])!="ascending"||u>=v)) throw std::runtime_error("topological ID certificate failed");
            edge(u,v);pu=u;pv=v;
        }
    }
    flush();out.close();std::filesystem::rename(partial,output);
    std::cout<<g.nodes<<" nodes "<<g.edges<<" edges verified\n";
} catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;} }
