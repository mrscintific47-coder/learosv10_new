#include <unistd.h>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <fstream>
#include <chrono>
#include <algorithm>
#include <cmath>

struct LLNode { int value; LLNode* next=nullptr; explicit LLNode(int v):value(v){} };
struct BSTNode { int value; BSTNode* left=nullptr,*right=nullptr; explicit BSTNode(int v):value(v){} };
struct HashEntry { int key,value; HashEntry* next=nullptr; HashEntry(int k,int v):key(k),value(v){} };

enum class DSType { Stack, Queue, List, HashMap, BST };
DSType currentType = DSType::Stack;

LLNode* stackTop=nullptr, *queueHead=nullptr, *queueTail=nullptr, *listHead=nullptr;
static constexpr int BUCKET_COUNT=13;
std::vector<HashEntry*> hashBuckets(BUCKET_COUNT,nullptr);
BSTNode* bstRoot=nullptr;
int nodeCount=0;

static long readRSS() {
    std::ifstream f("/proc/self/status"); std::string line;
    while(std::getline(f,line)) if(line.rfind("VmRSS:",0)==0){long v;std::istringstream ss(line.substr(6));ss>>v;return v;}
    return 0;
}
static std::string heapRange(bool start) {
    std::ifstream f("/proc/self/maps"); std::string line;
    while(std::getline(f,line)) if(line.find("[heap]")!=std::string::npos){
        auto dash=line.find('-');
        return "0x"+(start?line.substr(0,dash):line.substr(dash+1,line.find(' ')-dash-1));
    }
    return "0x0";
}
static std::string addr(const void* p){if(!p)return "0x0";char b[32];snprintf(b,32,"0x%lx",(unsigned long)p);return b;}

static void freeChain(LLNode* n){while(n){auto*x=n->next;delete n;n=x;}}
static void freeBST(BSTNode* n){if(!n)return;freeBST(n->left);freeBST(n->right);delete n;}
static void freeHash(){for(auto&b:hashBuckets){HashEntry*e=b;while(e){auto*x=e->next;delete e;e=x;}b=nullptr;}}
static void clearAll(){freeChain(stackTop);stackTop=nullptr;freeChain(queueHead);queueHead=queueTail=nullptr;freeChain(listHead);listHead=nullptr;freeHash();freeBST(bstRoot);bstRoot=nullptr;nodeCount=0;}

static void printState(const std::string& op="",int val=0,const void* naddr=nullptr,const std::string& res="ok") {
    if(!op.empty()) std::cout<<"OP "<<op<<" "<<val<<" "<<addr(naddr)<<" "<<res<<"\n";
    int size=0;
    if(currentType==DSType::Stack){for(LLNode*n=stackTop;n;n=n->next){std::cout<<"NODE "<<addr(n)<<" "<<n->value<<" "<<addr(n->next)<<"\n";size++;}}
    else if(currentType==DSType::Queue){for(LLNode*n=queueHead;n;n=n->next){std::cout<<"NODE "<<addr(n)<<" "<<n->value<<" "<<addr(n->next)<<"\n";size++;}}
    else if(currentType==DSType::List){for(LLNode*n=listHead;n;n=n->next){std::cout<<"NODE "<<addr(n)<<" "<<n->value<<" "<<addr(n->next)<<"\n";size++;}}
    else if(currentType==DSType::HashMap){for(int i=0;i<BUCKET_COUNT;i++){for(HashEntry*e=hashBuckets[i];e;e=e->next){std::cout<<"HNODE "<<addr(e)<<" "<<i<<" "<<e->key<<" "<<e->value<<" "<<addr(e->next)<<"\n";size++;}}}
    else if(currentType==DSType::BST){std::vector<BSTNode*>q;if(bstRoot)q.push_back(bstRoot);int qi=0;while(qi<(int)q.size()){BSTNode*n=q[qi++];std::cout<<"BNODE "<<addr(n)<<" "<<n->value<<" "<<addr(n->left)<<" "<<addr(n->right)<<"\n";if(n->left)q.push_back(n->left);if(n->right)q.push_back(n->right);size++;}}
    std::string typeName=currentType==DSType::Stack?"stack":currentType==DSType::Queue?"queue":currentType==DSType::List?"list":currentType==DSType::HashMap?"hashmap":"bst";
    std::cout<<"SUMMARY "<<typeName<<" "<<size<<" "<<readRSS()<<" "<<heapRange(true)<<" "<<heapRange(false)<<"\n";
}

static BSTNode* bstInsert(BSTNode* n,int val){if(!n){nodeCount++;return new BSTNode(val);}if(val<n->value)n->left=bstInsert(n->left,val);else if(val>n->value)n->right=bstInsert(n->right,val);return n;}
static BSTNode* bstMin(BSTNode* n){while(n->left)n=n->left;return n;}
static BSTNode* bstRemove(BSTNode* n,int val,bool& removed){if(!n)return nullptr;if(val<n->value)n->left=bstRemove(n->left,val,removed);else if(val>n->value)n->right=bstRemove(n->right,val,removed);else{removed=true;nodeCount--;if(!n->left){auto*r=n->right;delete n;return r;}if(!n->right){auto*l=n->left;delete n;return l;}BSTNode*m=bstMin(n->right);n->value=m->value;bool d=false;n->right=bstRemove(n->right,m->value,d);}return n;}

int main() {
    std::cout<<"READY "<<getpid()<<"\n"<<std::flush;
    std::string line;
    while(std::getline(std::cin,line)){
        std::istringstream ss(line); std::string cmd; ss>>cmd;
        if(cmd=="PID"){std::cout<<"PID "<<getpid()<<"\nOK\n";}
        else if(cmd=="DS"){std::string t;ss>>t;clearAll();if(t=="stack")currentType=DSType::Stack;else if(t=="queue")currentType=DSType::Queue;else if(t=="list")currentType=DSType::List;else if(t=="hashmap")currentType=DSType::HashMap;else if(t=="bst")currentType=DSType::BST;printState();std::cout<<"OK\n";}
        else if(cmd=="PUSH"){int v;ss>>v;auto*n=new LLNode(v);n->next=stackTop;stackTop=n;nodeCount++;printState("PUSH",v,n);std::cout<<"OK\n";}
        else if(cmd=="POP"){if(!stackTop){std::cout<<"ERR stack is empty\n";}else{int v=stackTop->value;auto*n=stackTop;stackTop=n->next;delete n;nodeCount--;printState("POP",v,nullptr);std::cout<<"OK\n";}}
        else if(cmd=="ENQUEUE"){int v;ss>>v;auto*n=new LLNode(v);nodeCount++;if(!queueHead){queueHead=queueTail=n;}else{queueTail->next=n;queueTail=n;}printState("ENQUEUE",v,n);std::cout<<"OK\n";}
        else if(cmd=="DEQUEUE"){if(!queueHead){std::cout<<"ERR queue is empty\n";}else{int v=queueHead->value;auto*n=queueHead;queueHead=n->next;if(!queueHead)queueTail=nullptr;delete n;nodeCount--;printState("DEQUEUE",v,nullptr);std::cout<<"OK\n";}}
        else if(cmd=="INSERT"){int v;ss>>v;if(currentType==DSType::List){auto*n=new LLNode(v);n->next=listHead;listHead=n;nodeCount++;printState("INSERT",v,n);std::cout<<"OK\n";}else if(currentType==DSType::BST){bstRoot=bstInsert(bstRoot,v);printState("INSERT",v,nullptr);std::cout<<"OK\n";}else if(currentType==DSType::HashMap){int b=((v%BUCKET_COUNT)+BUCKET_COUNT)%BUCKET_COUNT;auto*e=new HashEntry(v,v*10);e->next=hashBuckets[b];hashBuckets[b]=e;nodeCount++;printState("INSERT",v,e);std::cout<<"OK\n";}else{std::cout<<"ERR wrong DS type\n";}}
        else if(cmd=="REMOVE"){int v;ss>>v;bool found=false;if(currentType==DSType::List){LLNode dummy(0);dummy.next=listHead;LLNode*prev=&dummy;while(prev->next){if(prev->next->value==v){auto*n=prev->next;prev->next=n->next;delete n;nodeCount--;found=true;break;}prev=prev->next;}listHead=dummy.next;if(!found){std::cout<<"ERR not found\n";}else{printState("REMOVE",v,nullptr);std::cout<<"OK\n";}}else if(currentType==DSType::BST){bstRoot=bstRemove(bstRoot,v,found);if(!found){std::cout<<"ERR not found\n";}else{printState("REMOVE",v,nullptr);std::cout<<"OK\n";}}else if(currentType==DSType::HashMap){int b=((v%BUCKET_COUNT)+BUCKET_COUNT)%BUCKET_COUNT;HashEntry dummy2(0,0);dummy2.next=hashBuckets[b];HashEntry*prev=&dummy2;while(prev->next){if(prev->next->key==v){auto*e=prev->next;prev->next=e->next;delete e;nodeCount--;found=true;break;}prev=prev->next;}hashBuckets[b]=dummy2.next;if(!found){std::cout<<"ERR not found\n";}else{printState("REMOVE",v,nullptr);std::cout<<"OK\n";}}else{std::cout<<"ERR wrong DS type\n";}}
        else if(cmd=="SEARCH"){int v;ss>>v;bool found=false;if(currentType==DSType::BST){BSTNode*n=bstRoot;while(n){if(v==n->value){found=true;break;}n=v<n->value?n->left:n->right;}}else if(currentType==DSType::HashMap){int b=((v%BUCKET_COUNT)+BUCKET_COUNT)%BUCKET_COUNT;for(HashEntry*e=hashBuckets[b];e;e=e->next)if(e->key==v){found=true;break;}}else{LLNode*n=currentType==DSType::Stack?stackTop:currentType==DSType::Queue?queueHead:listHead;while(n){if(n->value==v){found=true;break;}n=n->next;}}printState("SEARCH",v,nullptr,found?"found":"not_found");std::cout<<"OK\n";}
        else if(cmd=="STRESS"){int count;ss>>count;count=std::min(count,10000);auto start=std::chrono::steady_clock::now();for(int i=0;i<count;i++){if(currentType==DSType::BST){bstRoot=bstInsert(bstRoot,i);}else{auto*n=new LLNode(i);n->next=stackTop;stackTop=n;nodeCount++;}}auto end=std::chrono::steady_clock::now();long ms=std::chrono::duration_cast<std::chrono::milliseconds>(end-start).count();std::cout<<"STRESS_DONE "<<count<<" "<<ms<<" "<<readRSS()<<"\n";printState("STRESS",count,nullptr,"ok");std::cout<<"OK\n";}
        else if(cmd=="CLEAR"){clearAll();printState("CLEAR",0,nullptr);std::cout<<"OK\n";}
        else if(cmd=="STATUS"){printState();std::cout<<"OK\n";}
        else{std::cout<<"ERR unknown: "<<cmd<<"\n";}
        std::cout<<std::flush;
    }
    clearAll(); return 0;
}
