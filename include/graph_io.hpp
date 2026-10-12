#pragma once
#include <array>
#include <cstdint>
#include <cctype>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
namespace fdio {
using Id = std::uint64_t;
struct GraphInfo { Id nodes, edges; };
inline Id read_u64(std::istream& in) {
    unsigned char b[8];
    if (!in.read(reinterpret_cast<char*>(b), 8)) throw std::runtime_error("truncated binary graph");
    Id x=0; for (int i=0;i<8;++i) x |= Id(b[i])<<(8*i); return x;
}
inline void write_u64(std::ostream& out, Id x) {
    char b[8]; for(int i=0;i<8;++i) b[i]=char(x>>(8*i)); out.write(b,8);
}
inline Id read_decimal(std::istream& in) {
    in >> std::ws; Id x=0; int c=in.peek();
    if(c<'0'||c>'9') throw std::runtime_error("expected unsigned decimal integer");
    while((c=in.peek())>='0'&&c<='9') {
        in.get(); Id d=c-'0';
        if(x>(std::numeric_limits<Id>::max()-d)/10) throw std::runtime_error("integer overflow");
        x=x*10+d;
    }
    if(c!=std::char_traits<char>::eof() && !std::isspace(static_cast<unsigned char>(c)))
        throw std::runtime_error("invalid integer delimiter");
    return x;
}
// Bulk reads keep per-byte parsing in memory rather than in iostream sentries.
class BufferedInput {
    std::ifstream in;
    std::array<unsigned char,1<<20> buffer{};
    std::size_t pos=0,end=0;
public:
    explicit BufferedInput(const std::string& path):in(path,std::ios::binary) {
        if(!in) throw std::runtime_error("cannot open "+path);
    }
    int peek() {
        if(pos==end) {
            in.read(reinterpret_cast<char*>(buffer.data()),buffer.size());
            end=static_cast<std::size_t>(in.gcount());pos=0;
            if(in.bad()) throw std::runtime_error("graph read error");
        }
        return pos==end ? -1 : buffer[pos];
    }
    int get() {int c=peek();if(c>=0)++pos;return c;}
    static bool space(int c) {return c==' '||(c>=9&&c<=13);}
    void whitespace() {while(space(peek()))++pos;}
    Id decimal() {
        whitespace();int c=peek();Id x=0;
        if(c<'0'||c>'9') throw std::runtime_error("expected unsigned decimal integer");
        do {
            Id d=c-'0';
            constexpr Id max=std::numeric_limits<Id>::max();
            if(x>max/10||(x==max/10&&d>max%10)) throw std::runtime_error("integer overflow");
            x=x*10+d;++pos;c=peek();
        } while(c>='0'&&c<='9');
        if(c!=-1&&!space(c)) throw std::runtime_error("invalid integer delimiter");
        return x;
    }
    Id u64() {
        Id x=0;
        if(end-pos>=8) {
            for(int i=0;i<8;++i)x|=Id(buffer[pos+i])<<(8*i);
            pos+=8;
        } else {
            for(int i=0;i<8;++i) {
                int c=get();if(c<0)throw std::runtime_error("truncated binary graph");
                x|=Id(c)<<(8*i);
            }
        }
        return x;
    }
};
// Streaming callbacks keep extra memory bounded even for billion-edge graphs.
// on_header(nodes, edges) runs before on_edge(source, target).
template<class Header, class Edge>
GraphInfo read_graph_txt(const std::string& path, Header on_header, Edge on_edge) {
    BufferedInput in(path);
    GraphInfo g{in.decimal(),in.decimal()}; on_header(g.nodes,g.edges);
    for(Id i=0;i<g.edges;++i) {
        Id u=in.decimal(),v=in.decimal();
        if(u>=g.nodes||v>=g.nodes) throw std::runtime_error("endpoint out of range");
        on_edge(u,v);
    }
    in.whitespace();if(in.peek()!=-1) throw std::runtime_error("trailing graph data");
    return g;
}
template<class Header, class Edge>
GraphInfo read_graph_bin(const std::string& path, Header on_header, Edge on_edge) {
    BufferedInput in(path);
    for(char c:std::string("FDGBIN01"))if(in.get()!=c)throw std::runtime_error("invalid binary magic");
    GraphInfo g{in.u64(),in.u64()};
    if(g.edges>(std::numeric_limits<Id>::max()-24)/16 || std::filesystem::file_size(path)!=24+16*g.edges)
        throw std::runtime_error("binary size mismatch");
    on_header(g.nodes,g.edges);
    for(Id i=0;i<g.edges;++i) {
        Id u=in.u64(),v=in.u64();
        if(u>=g.nodes||v>=g.nodes) throw std::runtime_error("endpoint out of range");
        on_edge(u,v);
    }
    return g;
}
} // namespace fdio
