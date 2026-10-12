// Offline graph preparation; independent of the FD solver and its query semantics.
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
using U = uint64_t;
struct Edge { U u, v; bool operator<(const Edge& b) const {return u<b.u || (u==b.u && v<b.v);} bool operator==(const Edge& b) const{return u==b.u && v==b.v;} };
static void put(std::ostream& o,U x){char b[8];for(int i=0;i<8;++i)b[i]=char(x>>(8*i));o.write(b,8);}
int main(int argc,char**argv){try{
 if(argc!=2)throw std::runtime_error("usage: pack_graph OUTPUT_DIR < symbolic-edges.tsv (two JSON-string columns)");
 std::filesystem::path dir(argv[1]); std::filesystem::create_directories(dir);
 if(std::filesystem::exists(dir/"stats.json"))throw std::runtime_error("output already complete; choose a new directory");
 std::ofstream names(dir/"nodes.jsonl"); names.exceptions(std::ios::badbit|std::ios::failbit);
 std::unordered_map<std::string,U> ids; std::vector<Edge> es;
 auto id=[&](const std::string&s){auto p=ids.emplace(s,ids.size());if(p.second)names<<s<<'\n';return p.first->second;};
 std::string line; U records=0;
 while(std::getline(std::cin,line)){
  auto p=line.find('\t'); if(p==std::string::npos || line.find('\t',p+1)!=std::string::npos)throw std::runtime_error("expected exactly two tab-separated JSON strings");
  auto a=line.substr(0,p),b=line.substr(p+1);if(a.size()<2||b.size()<2||a.front()!='"'||b.front()!='"'||a.back()!='"'||b.back()!='"')throw std::runtime_error("invalid node token");
  U u=id(a),v=id(b); es.push_back({u,v});++records;
  if(records%1000000==0)std::cerr<<"records="<<records<<" nodes="<<ids.size()<<'\n';
 }
 if(!std::cin.eof())throw std::runtime_error("input read failed");
 U n=ids.size(); ids.clear();ids.rehash(0);names.close();
 std::sort(es.begin(),es.end());es.erase(std::unique(es.begin(),es.end()),es.end());
 std::vector<U> off(n+1),deg(n),parent(n),sz(n,1);for(U i=0;i<n;++i)parent[i]=i;
 auto root=[&](U x){while(parent[x]!=x){parent[x]=parent[parent[x]];x=parent[x];}return x;};
 U loops=0;
 std::ofstream edges(dir/"edges.u64le",std::ios::binary); edges.exceptions(std::ios::badbit|std::ios::failbit);
 for(auto e:es){put(edges,e.u);put(edges,e.v);++off[e.u+1];++deg[e.v];loops+=e.u==e.v;U a=root(e.u),b=root(e.v);if(a!=b){if(sz[a]<sz[b])std::swap(a,b);parent[b]=a;sz[a]+=sz[b];}}
 edges.close();for(U i=1;i<=n;++i)off[i]+=off[i-1];
 U wcc=0,largest=0;for(U i=0;i<n;++i)if(parent[i]==i){++wcc;largest=std::max(largest,sz[i]);}
 parent.clear();parent.shrink_to_fit();sz.clear();sz.shrink_to_fit();
 std::vector<U> queue;for(U i=0;i<n;++i)if(deg[i]==0)queue.push_back(i);
 for(U i=0;i<queue.size();++i){U u=queue[i];for(U j=off[u];j<off[u+1];++j)if(--deg[es[j].v]==0)queue.push_back(es[j].v);}
 // Iterative Kosaraju audit. SCCs are recorded, never contracted in the graph.
 U kahn_removed=queue.size(); queue.clear();queue.shrink_to_fit();
 deg.clear();deg.shrink_to_fit();
 std::vector<unsigned char> seen(n); std::vector<U> finish,stack,next=off;
 for(U start=0;start<n;++start){
  if(seen[start])continue;
  seen[start]=1;stack.push_back(start);
  while(!stack.empty()){
   U u=stack.back();
   if(next[u]<off[u+1]){U v=es[next[u]++].v;if(!seen[v]){seen[v]=1;stack.push_back(v);}}
   else{finish.push_back(u);stack.pop_back();}
  }
 }
 next.clear();next.shrink_to_fit();seen.clear();seen.shrink_to_fit();
 std::vector<U> rev(n+1);for(auto e:es)++rev[e.v+1];for(U i=1;i<=n;++i)rev[i]+=rev[i-1];
 std::vector<U> adj(es.size());next=rev;for(auto e:es)adj[next[e.v]++]=e.u;
 next.clear();next.shrink_to_fit();
 std::vector<U> comp(n,~U(0));U scc=0,largest_scc=0,cyclic_nodes=0;
 for(auto it=finish.rbegin();it!=finish.rend();++it){
  if(comp[*it]!=~U(0))continue;
  comp[*it]=scc;stack.push_back(*it);U size=0;
  while(!stack.empty()){U u=stack.back();stack.pop_back();++size;for(U j=rev[u];j<rev[u+1];++j){U v=adj[j];if(comp[v]==~U(0)){comp[v]=scc;stack.push_back(v);}}}
  if(size>1)cyclic_nodes+=size;
  else if(std::binary_search(es.begin(),es.end(),Edge{*it,*it}))++cyclic_nodes;
  largest_scc=std::max(largest_scc,size);++scc;
 }
 std::ofstream component_file(dir/"scc.u64le",std::ios::binary);component_file.exceptions(std::ios::badbit|std::ios::failbit);
 for(U c:comp){put(component_file,c);}
 component_file.close();
 std::ofstream stats(dir/"stats.json.tmp");stats.exceptions(std::ios::badbit|std::ios::failbit);
 stats<<"{\n  \"format\": \"directed-u64le-pairs-v1\",\n  \"node_id\": \"zero-based line number in nodes.jsonl\",\n  \"nodes\": "<<n<<",\n  \"input_edges\": "<<records<<",\n  \"edges\": "<<es.size()<<",\n  \"duplicate_edges_removed\": "<<records-es.size()<<",\n  \"self_loops_retained\": "<<loops<<",\n  \"weak_components\": "<<wcc<<",\n  \"largest_weak_component_nodes\": "<<largest<<",\n  \"is_dag\": "<<(kahn_removed==n?"true":"false")<<",\n  \"kahn_removed_nodes\": "<<kahn_removed<<",\n  \"kahn_residual_nodes_not_scc_count\": "<<n-kahn_removed<<",\n  \"strong_components\": "<<scc<<",\n  \"largest_strong_component_nodes\": "<<largest_scc<<",\n  \"nodes_on_directed_cycles\": "<<cyclic_nodes<<",\n  \"edge_direction_changed\": false,\n  \"complete\": true\n}\n";stats.close();std::filesystem::rename(dir/"stats.json.tmp",dir/"stats.json");
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
