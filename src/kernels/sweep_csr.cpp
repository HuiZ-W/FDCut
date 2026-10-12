// Same exact sweep/incumbent rule as sweep_coarsen.cpp. Compact incidence
// arrays and SCC only on unforced cores reduce implementation overhead.
#include <algorithm>
#include <cstdint>
#include <numeric>
#include <utility>
#include <vector>
using i64 = int64_t;

struct SweepCSR {
 std::vector<int> off, item;
 SweepCSR() = default;
 SweepCSR(int n,int m,const int* from,const int* to):off(n+1,0),item(m) {
  for(int e=0;e<m;++e)++off[from[e]+1];
  std::partial_sum(off.begin(),off.end(),off.begin());
  auto cursor=off;
  for(int e=0;e<m;++e)item[cursor[from[e]]++]=to[e];
 }
};

extern "C" int fd_sweep(int n,int nh,const int* hu,const int* hv,
                        int nt,const int* tail,const int* off,const int* heads,const i64* cost,
                        int* mapping,int* state,int* rank,int* flags,
                        int* extra_u,int* extra_v,i64* stats) {
 try {
  if(n<2)return 2;
  std::fill(stats,stats+12,0);std::fill(rank,rank+n,-1);std::fill(flags,flags+nt,-1);
  std::vector<int> degree(n,0),remaining(nt,0),left(n,0),term_id(nt),owner(off[nt]);
  left[0]=1;rank[0]=0;
  for(int e=0;e<nh;++e){
   if((hu[e]==0&&hv[e]!=0)||(hv[e]==1&&hu[e]!=1))return 2;
   if(hu[e]>1&&hv[e]>1)++degree[hu[e]];
  }
  std::iota(term_id.begin(),term_id.end(),0);
  for(int j=0;j<nt;++j)for(int e=off[j];e<off[j+1];++e)owner[e]=j;
  SweepCSR in(n,nh,hv,hu),bytail(n,nt,tail,term_id.data()),byhead(n,off[nt],heads,owner.data());
  i64 current=0;
  for(int j=0;j<nt;++j){
   for(int e=off[j];e<off[j+1];++e)if(heads[e]!=0)++remaining[j];
   if(tail[j]==0&&remaining[j])current+=cost[j];
  }
  i64 upper=current;stats[1]=upper;int best=0,processed=0;
  std::vector<int> queue;queue.reserve(n);
  for(int v=2;v<n;++v)if(!degree[v])queue.push_back(v);
  for(size_t at=0;at<queue.size();++at){
   int v=queue[at];left[v]=1;rank[v]=++processed;++stats[0];
   for(int e=bytail.off[v];e<bytail.off[v+1];++e){int j=bytail.item[e];++stats[0];if(remaining[j])current+=cost[j];}
   for(int e=byhead.off[v];e<byhead.off[v+1];++e){int j=byhead.item[e];++stats[0];if(!--remaining[j]&&left[tail[j]])current-=cost[j];}
   if(current<upper){upper=current;best=processed;}
   for(int e=in.off[v];e<in.off[v+1];++e){int u=in.item[e];++stats[0];if(u>1&&!--degree[u])queue.push_back(u);}
  }
  if(processed!=n-2||upper<0)return 3;
  stats[2]=upper;stats[3]=best;
  std::vector<int> heavy;
  for(int j=0;j<nt;++j)if(cost[j]>upper)heavy.push_back(j);
  stats[4]=heavy.size();
  if(heavy.empty()){
   for(int v=0;v<n;++v){mapping[v]=v;state[v]=v==0?1:v==1?0:-1;}
   stats[5]=n;return 0;
  }
  std::vector<int> au(hu,hu+nh),av(hv,hv+nh);
  au.reserve(nh+off[nt]);av.reserve(nh+off[nt]);
  for(int j:heavy)for(int e=off[j];e<off[j+1];++e){au.push_back(tail[j]);av.push_back(heads[e]);++stats[0];}
  SweepCSR out(n,int(au.size()),au.data(),av.data());
  in=SweepCSR(n,int(au.size()),av.data(),au.data());
  std::fill(state,state+n,-1);
  auto reach=[&](int root,const SweepCSR& adj,int value)->bool {
   queue.clear();queue.push_back(root);state[root]=value;
   for(size_t at=0;at<queue.size();++at){int v=queue[at];
    for(int e=adj.off[v];e<adj.off[v+1];++e){int u=adj.item[e];++stats[0];
     if(state[u]>=0&&state[u]!=value)return false;
     if(state[u]<0){state[u]=value;queue.push_back(u);}
    }
   }return true;
  };
  if(!reach(0,out,1)||state[1]==1||!reach(1,in,0))return 4;
  // Forced source cores have no edge to a free core; no free core has an
  // edge to forced sink. An SCC of free cores therefore cannot traverse a
  // forced core, so the remaining SCC computation can omit both sets.
  std::vector<int> visited(n,0),order;
  std::vector<std::pair<int,int>> dfs;
  for(int v=0;v<n;++v)mapping[v]=state[v]==1?0:state[v]==0?1:-1;
  for(int root=2;root<n;++root)if(state[root]<0&&!visited[root]){
   visited[root]=1;dfs.push_back({root,out.off[root]});
   while(!dfs.empty()){
    int v=dfs.back().first;int& next=dfs.back().second;
    if(next==out.off[v+1]){order.push_back(v);dfs.pop_back();}
    else {int u=out.item[next++];++stats[0];if(state[u]<0&&!visited[u]){visited[u]=1;dfs.push_back({u,out.off[u]});}}
   }
  }
  int groups=2;
  for(auto it=order.rbegin();it!=order.rend();++it)if(mapping[*it]<0){
   int root=*it;++groups;queue.clear();queue.push_back(root);mapping[root]=root;
   for(size_t at=0;at<queue.size();++at){int v=queue[at];
    for(int e=in.off[v];e<in.off[v+1];++e){int u=in.item[e];++stats[0];
     if(state[u]<0&&mapping[u]<0){mapping[u]=root;queue.push_back(u);}
    }
   }
  }
  for(int v=2;v<n;++v){stats[7]+=state[v]==1;stats[8]+=state[v]==0;}
  stats[5]=groups;stats[6]=n-groups;
  int ne=0;
  for(int j:heavy)for(int e=off[j];e<off[j+1];++e){int a=mapping[tail[j]],b=mapping[heads[e]];
   if(a!=b&&a!=1&&b!=0){extra_u[ne]=tail[j];extra_v[ne]=heads[e];++ne;}
  }
  stats[9]=ne;
  for(int j=0;j<nt;++j){
   if(cost[j]>upper){flags[j]=0;continue;}
   int t=mapping[tail[j]];bool unpaid=true,sink=false;
   for(int e=off[j];e<off[j+1];++e){int h=mapping[heads[e]];if(h!=t&&h!=0)unpaid=false;if(h==1)sink=true;}
   flags[j]=(t==1||unpaid)?0:(t==0&&sink)?1:-1;
  }
  return 0;
 }catch(...){return 1;}
}
