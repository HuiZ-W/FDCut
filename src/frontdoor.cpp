#include "frontdoor.hpp"
#include "graph_io.hpp"
#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <iomanip>
#include <istream>
#include <iterator>
#include <limits>
#include <memory>
#include <numeric>
#include <ostream>
#include <random>
#include <stdexcept>
#include <tuple>
#include <utility>

extern "C" {
void* fd_compile(int,int,const int*,const int*,const int*,const int64_t*);
int fd_compile_info(void*,int*);
const int* fd_compile_ints(void*,int);
const int64_t* fd_compile_cost(void*);
void fd_compile_free(void*);
int fd_sweep(int,int,const int*,const int*,int,const int*,const int*,const int*,const int64_t*,int*,int*,int*,int*,int*,int*,int64_t*);
int fd_component_sweep(int,int,const int*,const int*,int,const int*,const int*,const int*,const int64_t*,int*,int*,int*,int*,int*,int*,int64_t*,int*,int*,int64_t*,int*);
int fd_hpf_cut(int,int,const int*,const int*,const unsigned int*,int,int,int*,unsigned long long*,double*);
}
namespace fd {
namespace {
using Clock = std::chrono::steady_clock;
double seconds(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double>(b-a).count(); }
void need(bool b, const char* msg) { if (!b) throw std::runtime_error(msg); }
struct Term { int tail; std::vector<int> heads, variables; i64 cost; };
struct Model { int n=0; std::vector<int> u,v,constants; std::vector<Term> terms; };
struct NumericEdge { int u, v; unsigned int capacity; };
using PackedEdge = std::uint64_t;

PackedEdge pack_edge(int u,int v) {
  return (static_cast<PackedEdge>(static_cast<std::uint32_t>(u))<<32)|
         static_cast<std::uint32_t>(v);
}
int packed_tail(PackedEdge edge) { return static_cast<int>(edge>>32); }
int packed_head(PackedEdge edge) { return static_cast<int>(edge&0xffffffffULL); }

bool normalize_edge(int u,int v,int source_gate,int sink_gate,int& out_u,int& out_v) {
  if(u==v||u==1||v==0)return false;
  out_u=u==0?source_gate:u;out_v=v==1?sink_gate:v;
  return out_u!=out_v;
}

std::uint64_t run_numeric_cut(int nodes,std::vector<NumericEdge> edges,
                              unsigned int hard_capacity,std::vector<int>& left,
                              unsigned long long& value,double& hpf_seconds) {
  std::sort(edges.begin(),edges.end(),[](const NumericEdge& a,const NumericEdge& b){
    return a.u<b.u||(a.u==b.u&&(a.v<b.v||(a.v==b.v&&a.capacity<b.capacity)));
  });
  std::size_t write=0;
  for(std::size_t read=0;read<edges.size();){
    const int u=edges[read].u,v=edges[read].v;std::uint64_t capacity=0;
    do {
      capacity=std::min<std::uint64_t>(hard_capacity,capacity+edges[read].capacity);
      ++read;
    } while(read<edges.size()&&edges[read].u==u&&edges[read].v==v);
    edges[write++]={u,v,static_cast<unsigned int>(capacity)};
  }
  edges.resize(write);
  need(edges.size()<=static_cast<std::size_t>(INT_MAX),
       "materialized arc count exceeds the signed 32-bit HPF limit");
  std::vector<int> row;std::vector<int> col;std::vector<unsigned int> cap;
  row.reserve(edges.size());col.reserve(edges.size());cap.reserve(edges.size());
  for(const auto& edge:edges){row.push_back(edge.u);col.push_back(edge.v);cap.push_back(edge.capacity);}
  std::vector<NumericEdge>().swap(edges);
  left.assign(nodes,0);double timing[3];
  need(fd_hpf_cut(nodes,static_cast<int>(row.size()),row.data(),col.data(),cap.data(),
                  0,1,left.data(),&value,timing)==0,"HPF failed");
  hpf_seconds+=timing[2];
  return row.size();
}
std::vector<int> copyints(void* ptr,int field,int length) {
  if (!length) return {};
  auto p=fd_compile_ints(ptr,field);return {p,p+length};
}
bool compile(const Query& q, Model& m) {
  std::unique_ptr<void,decltype(&fd_compile_free)> ptr(
    fd_compile(q.n,int(q.u.size()),q.u.data(),q.v.data(),q.marks.data(),q.weights.data()),fd_compile_free);
  need(bool(ptr),"native compiler allocation failed");
  int info[8];int status=fd_compile_info(ptr.get(),info);
  if (status==1) return false;
  need(status!=3,"compiled structure exceeds the signed 32-bit backend limit");
  need(status==0,"invalid DAG or compiler error");
  m.n=info[1];m.u=copyints(ptr.get(),0,info[3]);m.v=copyints(ptr.get(),1,info[3]);
  auto tail=copyints(ptr.get(),2,info[4]),off=copyints(ptr.get(),3,info[4]+1);
  auto heads=copyints(ptr.get(),4,info[5]),po=copyints(ptr.get(),5,info[4]+1),prov=copyints(ptr.get(),6,info[6]);
  m.constants=copyints(ptr.get(),7,info[7]);auto cost=fd_compile_cost(ptr.get());
  for(int j=0;j<info[4];++j) m.terms.push_back({tail[j],{heads.begin()+off[j],heads.begin()+off[j+1]},
    {prov.begin()+po[j],prov.begin()+po[j+1]},cost[j]});
  return true;
}
void prune(Model& m,const std::string& method,Answer& answer) {
  if(method=="base")return;
  int n=m.n,nt=int(m.terms.size());
  std::vector<int> tail,off{0},heads;std::vector<i64> cost;
  for(auto& t:m.terms){tail.push_back(t.tail);heads.insert(heads.end(),t.heads.begin(),t.heads.end());off.push_back(int(heads.size()));cost.push_back(t.cost);}
  std::vector<int> mapping(n),state(n),rank(n),flags(nt),eu(heads.size()),ev(heads.size());
  i64 stats[12]={};int status;
  if(method=="sweep") status=fd_sweep(n,int(m.u.size()),m.u.data(),m.v.data(),nt,tail.data(),off.data(),heads.data(),cost.data(),mapping.data(),state.data(),rank.data(),flags.data(),eu.data(),ev.data(),stats);
  else {
    std::vector<int> component(n),tc(nt),witness(n);std::vector<i64> limit(nt);
    status=fd_component_sweep(n,int(m.u.size()),m.u.data(),m.v.data(),nt,tail.data(),off.data(),heads.data(),cost.data(),mapping.data(),state.data(),rank.data(),flags.data(),eu.data(),ev.data(),stats,component.data(),tc.data(),limit.data(),witness.data());
  }
  if(status!=0)throw std::runtime_error("pruning kernel failed with status "+std::to_string(status));
  answer.heavy_terms=stats[4];
  if(!stats[4])return;
  auto reps=mapping;std::sort(reps.begin(),reps.end());reps.erase(std::unique(reps.begin(),reps.end()),reps.end());
  need(reps.size()>=2&&reps[0]==0&&reps[1]==1,"invalid terminal projection");
  std::vector<int> renumber(n,-1);for(size_t i=0;i<reps.size();++i)renumber[reps[i]]=int(i);
  for(auto& v:mapping)v=renumber[v];
  Model reduced;reduced.n=int(reps.size());reduced.constants=std::move(m.constants);
  auto arc=[&](int u,int v){u=mapping[u];v=mapping[v];if(u!=v&&u!=1&&v!=0){reduced.u.push_back(u);reduced.v.push_back(v);}};
  for(size_t e=0;e<m.u.size();++e)arc(m.u[e],m.v[e]);
  for(int e=0;e<stats[9];++e)arc(eu[e],ev[e]);
  for(int j=0;j<nt;++j){auto& t=m.terms[j];
    if(flags[j]==1)reduced.constants.insert(reduced.constants.end(),t.variables.begin(),t.variables.end());
    if(flags[j]>=0)continue;
    Term z;z.tail=mapping[t.tail];z.cost=t.cost;z.variables=std::move(t.variables);
    for(int v:t.heads){int h=mapping[v];if(h!=0&&h!=z.tail)z.heads.push_back(h);}
    need(z.tail!=1&&!z.heads.empty(),"invalid projected term");reduced.terms.push_back(std::move(z));
  }
  m=std::move(reduced);
}
}

std::string input_capacity_error(std::uint64_t nodes, std::uint64_t edges) {
  if(nodes<2)return "node count must be at least two";
  if(nodes>static_cast<std::uint64_t>(INT_MAX)-2)
    return "node count exceeds the signed 32-bit compiler limit";
  if(edges>static_cast<std::uint64_t>(INT_MAX))
    return "edge count exceeds the signed 32-bit compiler limit";
  return {};
}

void validate(const Query& q) {
  need(q.n>=2&&q.marks.size()==size_t(q.n)&&q.weights.size()==size_t(q.n)&&q.u.size()==q.v.size(),"invalid dimensions");
  auto capacity_error=input_capacity_error(static_cast<std::uint64_t>(q.n),q.u.size());
  need(capacity_error.empty(),capacity_error.c_str());
  bool x=false,y=false;i64 sum=0;
  for(int a=0;a<q.n;++a){int m=q.marks[a];
    need(m>=0&&m<16,"invalid mark bits");x|=bool(m&1);y|=bool(m&2);
    need(!((m&1)&&(m&2))&&!((m&3)&&(m&12))&&(!(m&4)||(m&8)),"require disjoint X/Y and I subset R disjoint X/Y");
    need(q.weights[a]>=0&&q.weights[a]<(1LL<<30),"weight outside nonnegative integer fast path");
    if(m&8)sum+=q.weights[a];
  }
  need(x&&y&&sum+1<(1LL<<30),"empty X/Y or total cost exceeds fast path");
  for(size_t e=0;e<q.u.size();++e)need(q.u[e]>=0&&q.v[e]>=0&&q.u[e]<q.n&&q.v[e]<q.n,"edge endpoint out of range");
}
Query read(std::istream& in) {
  Query q;long long n,m;
  need(bool(in>>n>>m)&&n>=0&&m>=0,"invalid FDG header");
  auto capacity_error=input_capacity_error(static_cast<std::uint64_t>(n),static_cast<std::uint64_t>(m));
  need(capacity_error.empty(),capacity_error.c_str());
  q.n=int(n);q.marks.resize(n);q.weights.resize(n);q.u.resize(m);q.v.resize(m);
  for(int a=0;a<n;++a)need(bool(in>>q.marks[a]>>q.weights[a]),"truncated vertex records");
  for(int e=0;e<m;++e)need(bool(in>>q.u[e]>>q.v[e]),"truncated edge records");
  std::string extra;need(!(in>>extra),"trailing input");validate(q);return q;
}
Query read_binary_full_domain(const std::string& path,int x,int y,i64 cost) {
  Query q;
  need(x>=0&&y>=0&&x!=y,"require distinct nonnegative X/Y vertices");
  need(cost>=0&&cost<(1LL<<30),"weight outside nonnegative integer fast path");
  fdio::read_graph_bin(path,[&](fdio::Id nodes,fdio::Id edges){
    auto capacity_error=input_capacity_error(nodes,edges);
    need(capacity_error.empty(),capacity_error.c_str());
    need(static_cast<fdio::Id>(x)<nodes&&static_cast<fdio::Id>(y)<nodes,
         "X/Y vertex outside binary graph");
    const auto candidates=nodes-2;
    need(cost==0||candidates<=static_cast<fdio::Id>(((1LL<<30)-2)/cost),
         "total cost exceeds fast path");
    q.n=static_cast<int>(nodes);
    q.marks.assign(q.n,8);q.weights.assign(q.n,cost);
    q.marks[x]=1;q.marks[y]=2;q.weights[x]=q.weights[y]=0;
    q.u.reserve(static_cast<std::size_t>(edges));
    q.v.reserve(static_cast<std::size_t>(edges));
  },[&](fdio::Id u,fdio::Id v){
    q.u.push_back(static_cast<int>(u));q.v.push_back(static_cast<int>(v));
  });
  validate(q);return q;
}
void write(std::ostream& out,const Query& q) {
  out<<q.n<<' '<<q.u.size()<<'\n';for(int a=0;a<q.n;++a)out<<q.marks[a]<<' '<<q.weights[a]<<'\n';
  for(size_t e=0;e<q.u.size();++e)out<<q.u[e]<<' '<<q.v[e]<<'\n';
}
Answer solve(const Query& q,const std::string& method) {
  auto start=Clock::now();validate(q);need(method=="base"||method=="sweep"||method=="component","unknown method");
  Answer a;Model m;
  if(!compile(q,m)){a.compile_s=a.total_s=seconds(start,Clock::now());return a;}
  auto compiled=Clock::now();a.cores_before=m.n;a.terms_before=int(m.terms.size());
  prune(m,method,a);auto pruned=Clock::now();a.cores_after=m.n;a.terms_after=int(m.terms.size());
  i64 bound=1;for(int v=0;v<q.n;++v)if(q.marks[v]&8)bound+=q.weights[v];
  need(m.terms.size()<=static_cast<std::size_t>(INT_MAX-m.n),
       "residual node count exceeds the signed 32-bit backend limit");
  // HPF stores node excess in a 32-bit word. A valid network can nevertheless
  // have many hard arcs incident to a terminal whose capacities sum past that
  // limit. Route terminal arcs through two gates so the source and sink each
  // see only one arc of capacity `bound`. Since every feasible finite cut costs
  // at most bound-1, neither gate arc can belong to an optimum cut.
  need(m.n+static_cast<int>(m.terms.size())<=INT_MAX-2,
       "residual node count exceeds the signed 32-bit backend limit");
  int source_gate=m.n+static_cast<int>(m.terms.size());
  int sink_gate=source_gate+1;
  a.numeric_nodes=sink_gate+1;
  const auto hard_capacity=static_cast<unsigned int>(bound);
  std::uint64_t logical_arcs=2+m.u.size();
  for(const auto& t:m.terms){
    need(logical_arcs<=std::numeric_limits<std::uint64_t>::max()-2-t.heads.size(),
         "logical arc count overflow");
    logical_arcs+=2+t.heads.size();
  }
  a.logical_arcs=logical_arcs;
  const char* force_lazy_text=std::getenv("FD_FORCE_LAZY_CUT");
  const bool lazy=(force_lazy_text&&std::string(force_lazy_text)=="1")||
                  logical_arcs>static_cast<std::uint64_t>(INT_MAX);
  a.implicit_cut=lazy;
  std::vector<int> left;unsigned long long value=0;double hpf_seconds=0;
  if(!lazy){
    std::vector<NumericEdge> edges;edges.reserve(static_cast<std::size_t>(logical_arcs));
    auto arc=[&](int u,int v,i64 capacity){int a_u,a_v;
      if(capacity>0&&normalize_edge(u,v,source_gate,sink_gate,a_u,a_v))
        edges.push_back({a_u,a_v,static_cast<unsigned int>(std::min<i64>(capacity,bound))});
    };
    for(std::size_t e=0;e<m.u.size();++e)arc(m.u[e],m.v[e],bound);
    for(std::size_t j=0;j<m.terms.size();++j){auto& t=m.terms[j];int aux=m.n+static_cast<int>(j);
      arc(t.tail,aux,t.cost);arc(aux,t.tail,bound);for(int h:t.heads)arc(aux,h,bound);
    }
    edges.push_back({0,source_gate,hard_capacity});
    edges.push_back({sink_gate,1,hard_capacity});
    a.numeric_arcs=run_numeric_cut(a.numeric_nodes,std::move(edges),hard_capacity,left,value,hpf_seconds);
    a.cut_rounds=1;
  }else{
    // Exact constraint generation for networks whose full arc set cannot be
    // indexed by the signed 32-bit HPF backend. Every omitted arc has capacity
    // `bound`, strictly larger than the cost of any finite cut. We repeatedly
    // solve a relaxation and add every omitted implication u in S => v in S
    // that its cut violates. If a relaxation optimum violates none, it is
    // feasible for the full network; its lower bound and feasible upper bound
    // coincide, so the returned cut is globally optimal.
    std::vector<NumericEdge> finite;
    finite.reserve(m.terms.size()+2);finite.push_back({0,source_gate,hard_capacity});
    finite.push_back({sink_gate,1,hard_capacity});
    for(std::size_t j=0;j<m.terms.size();++j){const auto& t=m.terms[j];int u,v;
      if(t.cost>0&&normalize_edge(t.tail,m.n+static_cast<int>(j),source_gate,sink_gate,u,v))
        finite.push_back({u,v,static_cast<unsigned int>(t.cost)});
    }
    std::vector<PackedEdge> materialized;
    for(;;){
      need(finite.size()<=static_cast<std::size_t>(INT_MAX)-materialized.size(),
           "implicit cut working set exceeds the signed 32-bit HPF limit");
      std::vector<NumericEdge> working;working.reserve(finite.size()+materialized.size());
      working.insert(working.end(),finite.begin(),finite.end());
      for(PackedEdge edge:materialized)
        working.push_back({packed_tail(edge),packed_head(edge),hard_capacity});
      a.numeric_arcs=run_numeric_cut(a.numeric_nodes,std::move(working),hard_capacity,left,value,hpf_seconds);
      ++a.cut_rounds;
      std::vector<PackedEdge> violated;
      auto inspect=[&](int raw_u,int raw_v){int u,v;
        if(normalize_edge(raw_u,raw_v,source_gate,sink_gate,u,v)&&left[u]&&!left[v])
          violated.push_back(pack_edge(u,v));
      };
      for(std::size_t e=0;e<m.u.size();++e)inspect(m.u[e],m.v[e]);
      for(std::size_t j=0;j<m.terms.size();++j){const auto& t=m.terms[j];int aux=m.n+static_cast<int>(j);
        inspect(aux,t.tail);for(int h:t.heads)inspect(aux,h);
      }
      std::sort(violated.begin(),violated.end());
      violated.erase(std::unique(violated.begin(),violated.end()),violated.end());
      std::vector<PackedEdge> fresh;fresh.reserve(violated.size());
      std::set_difference(violated.begin(),violated.end(),materialized.begin(),materialized.end(),
                          std::back_inserter(fresh));
      if(fresh.empty())break;
      need(fresh.size()<=static_cast<std::size_t>(INT_MAX)-finite.size()&&
           materialized.size()<=static_cast<std::size_t>(INT_MAX)-finite.size()-fresh.size(),
           "implicit cut working set exceeds the signed 32-bit HPF limit");
      std::vector<PackedEdge> merged;merged.reserve(materialized.size()+fresh.size());
      std::merge(materialized.begin(),materialized.end(),fresh.begin(),fresh.end(),
                 std::back_inserter(merged));
      materialized.swap(merged);
    }
    a.materialized_hard_arcs=materialized.size();
  }
  auto solved=Clock::now();need(left[0]&&!left[1],"invalid cut terminals");
  i64 paid=0;a.selected=std::move(m.constants);
  for(auto& t:m.terms)if(left[t.tail]&&std::any_of(t.heads.begin(),t.heads.end(),[&](int h){return !left[h];})){
    paid+=t.cost;a.selected.insert(a.selected.end(),t.variables.begin(),t.variables.end());
  }
  need(paid==i64(value),"cut cost disagrees with original terms");
  std::sort(a.selected.begin(),a.selected.end());need(std::adjacent_find(a.selected.begin(),a.selected.end())==a.selected.end(),"duplicate provenance");
  for(int v:a.selected)a.value+=q.weights[v];
  a.feasible=true;
  auto recovered=Clock::now();a.compile_s=seconds(start,compiled);a.prune_s=seconds(compiled,pruned);
  a.solve_s=hpf_seconds;a.build_s=std::max(0.0,seconds(pruned,solved)-hpf_seconds);
  a.recover_s=seconds(solved,recovered);a.total_s=seconds(start,recovered);
  return a;
}

Query generate(int variables,int width,unsigned seed,const std::string& family) {
  need(variables>=2&&width>=1&&variables%width==0&&variables/width>=2,"variables must be a multiple of width with >=2 layers");
  need(variables<(1<<22),"generator size limit");
  need(family=="random"||family=="middle"||family=="corridor"||family=="joined","unknown family");
  Query q;q.n=variables+3;q.marks.assign(q.n,8);q.weights.assign(q.n,0);
  q.marks[0]=1;q.marks[1]=2;q.marks[2]=0;std::mt19937 rng(seed);int depth=variables/width;
  auto node=[&](int d,int c){return 3+d*width+c;};auto arc=[&](int u,int v){q.u.push_back(u);q.v.push_back(v);};
  bool corridors=family=="corridor"||family=="joined";
  need(!corridors||width%4==0,"corridor width must be divisible by four");
  arc(2,0);arc(2,1);
  for(int c=0;c<width;++c){arc(0,node(0,c));arc(node(depth-1,c),1);}
  for(int d=0;d<depth;++d)for(int c=0;c<width;++c){
    bool cheap=family=="middle"?d==depth/2:corridors&&d==(c/4+1)*depth/(width/4+1);
    q.weights[node(d,c)]=cheap?1+rng()%10:(family=="random"?1+rng()%100:100+rng()%900);
    if(d+1<depth){arc(node(d,c),node(d+1,c));
      if(width>1){int next=corridors?(c/4)*4+(c+1)%4:(c+1)%width;arc(node(d,c),node(d+1,next));}
    }
  }
  if(family=="joined"&&width>=8)for(int d=1;d<depth;++d)for(int b=0;b+1<width/4;++b){
    int h=q.n++;q.marks.push_back(0);q.weights.push_back(0);arc(h,node(d,4*b));arc(h,node(d,4*(b+1)));
  }
  validate(q);return q;
}

// Independent original-graph criterion via ancestral moralization. This does
// not inspect the compiler or its cut. Pairwise marriage can be quadratic in
// indegree; verification is deliberately outside benchmark query timing.
bool valid_fd(const Query& q,const std::vector<int>& selected) {
  std::vector<int> z(q.n),x(q.n),y(q.n);
  for(int v:selected){if(v<0||v>=q.n||z[v]||!(q.marks[v]&8))return false;z[v]=1;}
  for(int v=0;v<q.n;++v){x[v]=bool(q.marks[v]&1);y[v]=bool(q.marks[v]&2);if((q.marks[v]&4)&&!z[v])return false;}
  std::vector<std::vector<int>> out(q.n);
  for(size_t e=0;e<q.u.size();++e)out[q.u[e]].push_back(q.v[e]);
  auto seen=x;std::vector<int> stack;for(int v=0;v<q.n;++v)if(x[v])stack.push_back(v);
  while(!stack.empty()){int v=stack.back();stack.pop_back();if(y[v])return false;
    for(int w:out[v])if(!z[w]&&!seen[w]){seen[w]=1;stack.push_back(w);}}
  auto separated=[&](const std::vector<int>& a,const std::vector<int>& b,const std::vector<int>& cond,const std::vector<int>& deleted){
    std::vector<std::vector<int>> parents(q.n);for(size_t e=0;e<q.u.size();++e)if(!deleted[q.u[e]])parents[q.v[e]].push_back(q.u[e]);
    std::vector<int> ancestor(q.n),todo;for(int v=0;v<q.n;++v)if(a[v]||b[v]||cond[v]){ancestor[v]=1;todo.push_back(v);}
    for(size_t at=0;at<todo.size();++at)for(int p:parents[todo[at]])if(!ancestor[p]){ancestor[p]=1;todo.push_back(p);}
    std::vector<std::vector<int>> moral(q.n);auto link=[&](int u,int v){if(!cond[u]&&!cond[v]){moral[u].push_back(v);moral[v].push_back(u);}};
    for(int v:todo){for(int p:parents[v])link(v,p);for(size_t i=0;i<parents[v].size();++i)for(size_t j=i+1;j<parents[v].size();++j)link(parents[v][i],parents[v][j]);}
    std::vector<int> visited(q.n),pending;for(int v=0;v<q.n;++v)if(a[v]&&!cond[v]){visited[v]=1;pending.push_back(v);}
    for(size_t at=0;at<pending.size();++at){int v=pending[at];if(b[v])return false;for(int w:moral[v])if(!visited[w]){visited[w]=1;pending.push_back(w);}}
    return true;
  };
  return separated(x,z,std::vector<int>(q.n),x)&&separated(z,y,x,z);
}
void print(std::ostream& out,const Answer& a,const std::string& method,bool witness) {
  out<<std::setprecision(10)<<"{\"method\":\""<<method<<"\",\"feasible\":"<<(a.feasible?"true":"false")
    <<",\"value\":"<<(a.feasible?std::to_string(a.value):"null")<<",\"selected_size\":"<<a.selected.size()
    <<",\"cores_before\":"<<a.cores_before<<",\"terms_before\":"<<a.terms_before<<",\"cores_after\":"<<a.cores_after
    <<",\"terms_after\":"<<a.terms_after<<",\"numeric_nodes\":"<<a.numeric_nodes<<",\"numeric_arcs\":"<<a.numeric_arcs
    <<",\"logical_arcs\":"<<a.logical_arcs<<",\"implicit_cut\":"<<(a.implicit_cut?"true":"false")
    <<",\"cut_rounds\":"<<a.cut_rounds<<",\"materialized_hard_arcs\":"<<a.materialized_hard_arcs
    <<",\"heavy_terms\":"<<a.heavy_terms<<",\"compile_s\":"<<a.compile_s<<",\"prune_s\":"<<a.prune_s
    <<",\"build_s\":"<<a.build_s<<",\"solve_s\":"<<a.solve_s<<",\"recover_s\":"<<a.recover_s<<",\"total_s\":"<<a.total_s;
  out<<",\"complete\":"<<(a.complete?"true":"false")<<",\"proven_optimal\":"<<(a.complete&&a.feasible?"true":"false")
    <<",\"status\":\""<<a.status<<"\",\"states\":"<<a.states<<",\"fd_checks\":"<<a.fd_checks
    <<",\"oracle_calls\":"<<a.oracle_calls<<",\"pruned_branches\":"<<a.pruned_branches;
  if(witness){out<<",\"selected\":[";for(size_t i=0;i<a.selected.size();++i){if(i)out<<',';out<<a.selected[i];}out<<']';}
  out<<'}';
}
}
