// Direct FD-to-packed compilation. The causal criteria and normalization are
// identical to filtered_compile.py + normalize + merge_signatures.
#include <algorithm>
#include <climits>
#include <cstdint>
#include <map>
#include <numeric>
#include <utility>
#include <vector>
using i64 = int64_t;
struct CompileCSR {
 std::vector<int> off, to;
 CompileCSR(int n,int m,const int* u,const int* v):off(static_cast<size_t>(n)+1,0),to(m){
  for(int e=0;e<m;++e)++off[u[e]+1];
  std::partial_sum(off.begin(),off.end(),off.begin());
  auto cursor=off;
  for(int e=0;e<m;++e)to[cursor[u[e]]++]=v[e];
 }
 CompileCSR(int n,const std::vector<int>& u,const std::vector<int>& v):
  CompileCSR(n,static_cast<int>(v.size()),u.data(),v.data()){}
};
struct CompileResult {
 int status=0,n=0,domain=0;
 std::vector<int> hu,hv,tail,off{0},heads,prov_off{0},prov,constants,mapping;
 std::vector<i64> cost;
};
struct CompileCapacityError {};

struct ScreenGraph {
 int n;
 CompileCSR out,in;
 bool dag;
 std::vector<int> topological_position;
 ScreenGraph(int nodes,int edges,const int* u,const int* v):
  n(nodes),out(nodes,edges,u,v),in(nodes,edges,v,u),dag(false),
  topological_position(n){
  std::vector<int> degree(n),queue;
  for(int a=0;a<n;++a)degree[a]=in.off[a+1]-in.off[a];
  for(int a=0;a<n;++a)if(!degree[a])queue.push_back(a);
  for(size_t at=0;at<queue.size();++at){int a=queue[at];
   for(int e=out.off[a];e<out.off[a+1];++e)if(!--degree[out.to[e]])queue.push_back(out.to[e]);
  }
  dag=queue.size()==static_cast<size_t>(n);
  if(dag)for(int position=0;position<n;++position)topological_position[queue[position]]=position;
 }
};

static void extend(const CompileCSR& adj,std::vector<int>& seen,std::vector<int> pending,
                   const int* marks,int exclude=0,bool stop_x=false){
 std::vector<int> todo;
 for(int v:pending)if(!(marks[v]&exclude)&&!seen[v]){seen[v]=1;todo.push_back(v);}
 while(!todo.empty()){
  int v=todo.back();todo.pop_back();if(stop_x&&(marks[v]&1))continue;
  for(int e=adj.off[v];e<adj.off[v+1];++e){int u=adj.to[e];
   if(!seen[u]&&!(marks[u]&exclude)){seen[u]=1;todo.push_back(u);}
  }
 }
}

static void screen_extend(const CompileCSR& adj,std::vector<unsigned char>& seen,
                          std::vector<int> pending,const int* marks,
                          int exclude=0,bool stop_x=false){
 std::vector<int> todo;
 for(int v:pending)if(!(marks[v]&exclude)&&!seen[v]){seen[v]=1;todo.push_back(v);}
 while(!todo.empty()){
  int v=todo.back();todo.pop_back();if(stop_x&&(marks[v]&1))continue;
  for(int e=adj.off[v];e<adj.off[v+1];++e){int u=adj.to[e];
   if(!seen[u]&&!(marks[u]&exclude)){seen[u]=1;todo.push_back(u);}
  }
 }
}

extern "C" void* fd_screen_prepare(int n,int m,const int* u,const int* v){
 try {
  if(n<2||n>INT_MAX-2||m<0)return nullptr;
  for(int e=0;e<m;++e)if(u[e]<0||u[e]>=n||v[e]<0||v[e]>=n)return nullptr;
  auto* graph=new ScreenGraph(n,m,u,v);
  if(!graph->dag){delete graph;return nullptr;}
  return graph;
 }catch(...){return nullptr;}
}

// Return 0 when no directed x-y path exists, 1 when a non-direct path exists,
// 2 when x->y is an edge (which makes FD1 impossible), and -1 on invalid input.
extern "C" int fd_screen_pair_type(void* ptr,int x,int y){
 if(!ptr)return -1;
 try {
  auto& graph=*static_cast<ScreenGraph*>(ptr);
  if(x<0||x>=graph.n||y<0||y>=graph.n||x==y)return -1;
  if(graph.topological_position[x]>=graph.topological_position[y]||
     graph.out.off[x]==graph.out.off[x+1]||graph.in.off[y]==graph.in.off[y+1])return 0;
  for(int e=graph.out.off[x];e<graph.out.off[x+1];++e)if(graph.out.to[e]==y)return 2;
  thread_local std::vector<unsigned char> reach_seen;
  thread_local std::vector<int> stack,touched;
  if(reach_seen.size()!=static_cast<size_t>(graph.n))reach_seen.assign(graph.n,0);
  stack.clear();touched.clear();stack.push_back(x);touched.push_back(x);reach_seen[x]=1;
  auto finish=[&](int answer){for(int v:touched)reach_seen[v]=0;return answer;};
  while(!stack.empty()){
   int v=stack.back();stack.pop_back();
   for(int e=graph.out.off[v];e<graph.out.off[v+1];++e){int w=graph.out.to[e];
    if(w==y)return finish(1);
    if(graph.topological_position[w]>=graph.topological_position[y]||reach_seen[w])continue;
    reach_seen[w]=1;touched.push_back(w);stack.push_back(w);
   }
  }
  return finish(0);
 }catch(...){return -1;}
}

// Status matches fd_compile: 0 means a finite-cost FD exists, 1 means the
// hard constraints prove infeasibility, and 2 means invalid query marks.
extern "C" int fd_screen_check(void* ptr,const int* marks){
 if(!ptr||!marks)return 2;
 try {
  const auto& graph=*static_cast<ScreenGraph*>(ptr);int n=graph.n;
  std::vector<int> x,y,required;
  for(int a=0;a<n;++a){
   int mark=marks[a];
   if(mark<0||mark>=16||((mark&1)&&(mark&2))||((mark&3)&&(mark&12))||
      ((mark&4)&&!(mark&8)))return 2;
   if(mark&1)x.push_back(a);
   if(mark&2)y.push_back(a);
   if(mark&4)required.push_back(a);
  }
  if(x.size()!=1||y.size()!=1)return 2;
  std::vector<unsigned char> forbidden(n),ay(n),domain(n),proper(n),candidate(n);
  screen_extend(graph.in,forbidden,x,marks);
  std::vector<int> queue;
  for(int a=0;a<n;++a)if(forbidden[a])queue.push_back(a);
  std::fill(forbidden.begin(),forbidden.end(),0);
  screen_extend(graph.out,forbidden,queue,marks,0,true);
  screen_extend(graph.in,ay,y,marks);domain=ay;queue=x;
  queue.insert(queue.end(),required.begin(),required.end());
  screen_extend(graph.in,domain,queue,marks);for(int a:x)domain[a]=0;
  screen_extend(graph.in,proper,y,marks,1);
  for(int a=0;a<n;++a){
   candidate[a]=(marks[a]&8)&&!forbidden[a]&&((marks[a]&4)||ay[a]);
   if((marks[a]&4)&&!candidate[a])return 1;
  }
  std::vector<int> xparents;
  std::vector<unsigned char> is_xparent(n);
  for(int e=graph.in.off[x[0]];e<graph.in.off[x[0]+1];++e){int p=graph.in.to[e];
   if(domain[p]){xparents.push_back(p);is_xparent[p]=1;}
  }
  std::vector<unsigned char> source(n),source_seed(n),sink(n);
  queue.clear();
  auto source_push=[&](int a){if(!source[a]){source[a]=1;source_seed[a]=1;queue.push_back(a);}};
  for(int a:x)for(int e=graph.out.off[a];e<graph.out.off[a+1];++e){
   int h=graph.out.to[e];if(proper[h])source_push(h);
  }
  for(int a:required)source_push(a);
  bool source_parents_expanded=false;
  for(size_t at=0;at<queue.size();++at){int a=queue[at];
   if(marks[a]&2)return 1;
   if(is_xparent[a]&&!source_parents_expanded){source_parents_expanded=true;
    for(int p:xparents)if(!source[p]){source[p]=1;queue.push_back(p);}
   }
   if(!domain[a])continue;
   for(int e=graph.in.off[a];e<graph.in.off[a+1];++e){int p=graph.in.to[e];
    if(domain[p]&&!source[p]){source[p]=1;queue.push_back(p);}
   }
   if(!candidate[a])for(int e=graph.out.off[a];e<graph.out.off[a+1];++e){int b=graph.out.to[e];
    if(domain[b]&&!source[b]){source[b]=1;queue.push_back(b);}
   }
  }
  queue=y;for(int a:y){if(source[a])return 1;sink[a]=1;}
  bool sink_parents_expanded=false;
  for(size_t at=0;at<queue.size();++at){int a=queue[at];
   if(source_seed[a])return 1;
   if(is_xparent[a]&&!sink_parents_expanded){sink_parents_expanded=true;
    for(int p:xparents){if(source[p])return 1;if(!sink[p]){sink[p]=1;queue.push_back(p);}}
   }
   if(!domain[a])continue;
   for(int e=graph.out.off[a];e<graph.out.off[a+1];++e){int b=graph.out.to[e];
    if(!domain[b])continue;
    if(source[b])return 1;
    if(!sink[b]){sink[b]=1;queue.push_back(b);}
   }
   for(int e=graph.in.off[a];e<graph.in.off[a+1];++e){int p=graph.in.to[e];
    if(!domain[p]||candidate[p])continue;
    if(source[p])return 1;
    if(!sink[p]){sink[p]=1;queue.push_back(p);}
   }
  }
  return 0;
 }catch(...){return 2;}
}

extern "C" void fd_screen_free(void* ptr){delete static_cast<ScreenGraph*>(ptr);}

extern "C" void* fd_compile(int n,int m,const int* input_u,const int* input_v,
                             const int* marks,const i64* weights){
 CompileResult* r=nullptr;
 try {
  r=new CompileResult();r->mapping.assign(n,-1);
  if(n<2||m<0){r->status=2;return r;}
  if(n>INT_MAX-2){r->status=3;return r;}
  std::vector<int> u(input_u,input_u+m),v(input_v,input_v+m),degree(n,0),queue;
  for(int e=0;e<m;++e){if(u[e]<0||u[e]>=n||v[e]<0||v[e]>=n){r->status=2;return r;}++degree[v[e]];}
  CompileCSR out(n,u,v),in(n,v,u);
  for(int i=0;i<n;++i)if(!degree[i])queue.push_back(i);
  for(size_t at=0;at<queue.size();++at){int a=queue[at];
   for(int e=out.off[a];e<out.off[a+1];++e)if(!--degree[out.to[e]])queue.push_back(out.to[e]);
  }
  if(int(queue.size())!=n){r->status=2;return r;}
  std::vector<int> x,y,required;
  for(int i=0;i<n;++i){if(marks[i]&1)x.push_back(i);if(marks[i]&2)y.push_back(i);if(marks[i]&4)required.push_back(i);}
  std::vector<int> forbidden(n,0),ay(n,0),domain(n,0),proper(n,0),candidate(n,0);
  extend(in,forbidden,x,marks);
  // Descendant propagation must start at ALL forbidden ancestors; extend
  // seeds normally skips visited roots, so clear them before this traversal.
  queue.clear();for(int i=0;i<n;++i)if(forbidden[i])queue.push_back(i);
  std::fill(forbidden.begin(),forbidden.end(),0);extend(out,forbidden,queue,marks,0,true);
  extend(in,ay,y,marks);domain=ay;queue=x;queue.insert(queue.end(),required.begin(),required.end());
  extend(in,domain,queue,marks);for(int a:x)domain[a]=0;
  extend(in,proper,y,marks,1);
  for(int i=0;i<n;++i){
   candidate[i]=(marks[i]&8)&&!forbidden[i]&&((marks[i]&4)||ay[i]);
   if((marks[i]&4)&&!candidate[i]){r->status=1;return r;}
   r->domain+=domain[i];
  }
  std::vector<int> hu,hv;
  auto arc=[&](int a,int b){
   if(hu.size()>=static_cast<size_t>(INT_MAX))throw CompileCapacityError{};
   hu.push_back(a);hv.push_back(b);
  };
  for(int e=0;e<m;++e)if(domain[u[e]]&&domain[v[e]]){
   arc(v[e]+2,u[e]+2);if(!candidate[u[e]])arc(u[e]+2,v[e]+2);
  }
  for(int a:x){
   int first=-1;
   for(int e=in.off[a];e<in.off[a+1];++e){int p=in.to[e];if(!domain[p])continue;
    if(first<0)first=p;else{arc(first+2,p+2);arc(p+2,first+2);}
   }
   for(int e=out.off[a];e<out.off[a+1];++e){int h=out.to[e];if(proper[h])arc(0,h+2);}
  }
  for(int a:required)arc(0,a+2);
  for(int a:y)arc(a+2,1);
  int raw_n=n+2;CompileCSR hard_out(raw_n,hu,hv),hard_in(raw_n,hv,hu);
  std::vector<int> state(raw_n,-1),mapping(raw_n,-1);
  auto reach=[&](int root,const CompileCSR& adj,int value)->bool{
   queue.clear();queue.push_back(root);state[root]=value;
   for(size_t at=0;at<queue.size();++at){int a=queue[at];
    for(int e=adj.off[a];e<adj.off[a+1];++e){int b=adj.to[e];
     if(state[b]>=0&&state[b]!=value)return false;
     if(state[b]<0){state[b]=value;queue.push_back(b);}
    }
   }return true;
  };
  if(!reach(0,hard_out,1)||state[1]==1||!reach(1,hard_in,0)){r->status=1;return r;}
  std::vector<int> visited(raw_n,0),order;
  std::vector<std::pair<int,int>> stack;
  for(int a=0;a<raw_n;++a)if(state[a]>=0)mapping[a]=state[a]==1?0:1;
  for(int root=2;root<raw_n;++root)if(domain[root-2]&&state[root]<0&&!visited[root]){
   visited[root]=1;stack.push_back({root,hard_out.off[root]});
   while(!stack.empty()){
    int a=stack.back().first;int& next=stack.back().second;
    if(next==hard_out.off[a+1]){order.push_back(a);stack.pop_back();}
    else {int b=hard_out.to[next++];if(state[b]<0&&!visited[b]){visited[b]=1;stack.push_back({b,hard_out.off[b]});}}
   }
  }
  r->n=2;
  for(auto it=order.rbegin();it!=order.rend();++it)if(mapping[*it]<0){
   int group=r->n++;queue.clear();queue.push_back(*it);mapping[*it]=group;
   for(size_t at=0;at<queue.size();++at){int a=queue[at];
    for(int e=hard_in.off[a];e<hard_in.off[a+1];++e){int b=hard_in.to[e];
     if(state[b]<0&&mapping[b]<0){mapping[b]=group;queue.push_back(b);}
    }
   }
  }
  for(int a=0;a<n;++a)if(domain[a])r->mapping[a]=mapping[a+2];
  std::vector<std::pair<int,int>> edges;edges.reserve(hu.size());
  for(size_t e=0;e<hu.size();++e){int a=mapping[hu[e]],b=mapping[hv[e]];
   if(a!=b)edges.push_back({a,b});
  }
  std::sort(edges.begin(),edges.end());edges.erase(std::unique(edges.begin(),edges.end()),edges.end());
  for(auto e:edges){r->hu.push_back(e.first);r->hv.push_back(e.second);}
  using Key=std::pair<int,std::vector<int>>;
  std::map<Key,std::vector<int>> terms;
  r->constants=required;
  for(int a=0;a<n;++a)if(candidate[a]&&!(marks[a]&4)){
   int t=mapping[a+2];if(t==1)continue;
   std::vector<int> heads;bool sink=false;
   for(int e=out.off[a];e<out.off[a+1];++e){int b=out.to[e];if(!domain[b])continue;
    int h=mapping[b+2];if(h==1)sink=true;if(h!=t&&h!=0)heads.push_back(h);
   }
   if(sink)heads={1};
   else {std::sort(heads.begin(),heads.end());heads.erase(std::unique(heads.begin(),heads.end()),heads.end());}
   if(heads.empty())continue;
   if(t==0&&sink){r->constants.push_back(a);continue;}
   terms[{t,heads}].push_back(a);
  }
  for(const auto& item:terms){
   if(item.first.second.size()>static_cast<size_t>(INT_MAX)-r->heads.size()||
      item.second.size()>static_cast<size_t>(INT_MAX)-r->prov.size())
    throw CompileCapacityError{};
   r->tail.push_back(item.first.first);r->heads.insert(r->heads.end(),item.first.second.begin(),item.first.second.end());
   r->off.push_back(int(r->heads.size()));i64 c=0;
   for(int a:item.second){r->prov.push_back(a);c+=weights[a];}
   r->prov_off.push_back(int(r->prov.size()));r->cost.push_back(c);
  }
  return r;
 }catch(const CompileCapacityError&){
  if(!r)return nullptr;
  r->status=3;return r;
 }catch(...){delete r;return nullptr;}
}

extern "C" int fd_compile_info(void* ptr,int* info){
 if(!ptr)return 3;
 auto& r=*static_cast<CompileResult*>(ptr);
 int values[]={r.status,r.n,r.domain,int(r.hu.size()),int(r.tail.size()),int(r.heads.size()),
               int(r.prov.size()),int(r.constants.size())};std::copy(values,values+8,info);return r.status;
}
extern "C" const int* fd_compile_ints(void* ptr,int field){
 auto& r=*static_cast<CompileResult*>(ptr);
 const std::vector<int>* fields[]={&r.hu,&r.hv,&r.tail,&r.off,&r.heads,&r.prov_off,&r.prov,&r.constants,&r.mapping};
 return field>=0&&field<9?fields[field]->data():nullptr;
}
extern "C" const i64* fd_compile_cost(void* ptr){return static_cast<CompileResult*>(ptr)->cost.data();}
extern "C" void fd_compile_free(void* ptr){delete static_cast<CompileResult*>(ptr);}
