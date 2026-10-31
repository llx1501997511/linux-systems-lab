#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>

#define MAX_EVENTS 1024
#define BUFLEN 4096
#define SERV_PORT 8000
#define IDLE_TIMEOUT 300


struct myevent_s
{
    int fd;//cfd或是lfd
    int events;//读还是写EPOLLIN,EPOLLOUT
    void (*call_back)(int fd,int events,void *arg);//回调函数
    void *arg;//指针，指向自己
    int status;//状态在不在树上，他来决定add的op是使用ADD还是MOD
    char buf[BUFLEN];//read读到这里，send从这里发
    int len;//buf的实际长度
    int last_active;//最后活跃时间，根据这个进行踢进程。
    /* data */
};

static int g_efd=-1;
static struct myevent_s g_events[MAX_EVENTS+1];

static void acceptconn(int lfd,int events,void *arg);
static void recvdata(int fd,int events,void *arg);
static void senddata(int fd,int events,void *arg);


static void eventset(struct myevent_s* ev,int fd,void (*call_back)(int,int,void *),void *arg){
    //设置我盯谁，盯什么时间，调用谁，带上我。四个参数归位
    ev->fd=fd;
    ev->call_back=call_back;
    ev->arg=arg;

    ev->events=0;
    ev->status=0;
    //events和status交给add去管
    //不能清BUF，因为recv后就set，刚读到，给人清0，就什么都发不出去。
    //lastactive设置上，后面踢人用
    ev->last_active=(long)time(NULL);
}

static void eventadd(int efd,int events,struct myevent_s *ev){
    // 把事件挂到 epoll 上，或者改它正在监听的事件。
    // op 由 status 决定：已经在树上走 MOD，不在树上走 ADD。
    // data.ptr 存 ev 本身，epoll_wait 回来才能从事件反查回是哪个 fd。
    // 成功后要把 events 和 status 写回 ev，否则下一次判断会错。
   int op=(ev->status==1)?EPOLL_CTL_MOD:EPOLL_CTL_ADD;
   struct epoll_event epv;
   memset(&epv,0,sizeof(epv));
   epv.events=events;
   epv.data.ptr=ev;

   ev->events=events;

   if(epoll_ctl(efd,op,ev->fd,&epv)<0){
        printf("event add failed efd=%d [fd=%d],op=%d,events=%d,errno=%d[%s]\n",
            efd,ev->fd,op,events,errno,strerror(errno));
        ev->status=0;
        return;
   }
   ev->status=1;
   printf("event add OK fd=[%d],op=%d,events=%d\n",ev->fd,op,events);
}

static void eventdel(int efd,struct myevent_s *ev){//需要红黑树句柄efd,牌子ev
    //将牌子从epoll树上摘下来。
    //我用epoll_ctl需要句柄，需要被摘的fd,所以要ev。
    //摘下来之后status置0,这样下回add才能加上去。
    struct epoll_event epv;
    if(ev->status==0)
        return;
    memset(&epv,0,sizeof(epv));

    epv.data.ptr=ev;
    ev->status=0;

    epoll_ctl(efd,EPOLL_CTL_DEL,ev->fd,&epv);
    // DEL 只表示不再关心这个 fd 的事件，不等于 close。
    // status 前面已置 0，下次 eventadd 会重新走 ADD 挂上去。

}

static void acceptconn(int lfd,int events,void *arg){
    //创建cfd，失败，非阻塞下EAGAIN是没人来了，continue不是错误。
    struct sockaddr_in cin;
    memset(&cin,0,sizeof(cin));
    socklen_t len=sizeof(cin);
    //用void消除参数警告
    (void)events;
    (void)lfd;
    (void)arg;
    int i;
    
    int cfd=accept(lfd,(struct sockaddr *)&cin,&len);
    if(cfd==-1){
        if(errno!=EAGAIN&&errno!=EINTR)
            perror("accept");
        return;
    }

    //在数组中找一个空位置给cfd
    for(i=0;i<MAX_EVENTS;i++){
        if(g_events[i].status==0)
            break;
    }
    if(i==MAX_EVENTS){
        fprintf(stderr,"max connection limit reached,reject cfd=%d\n",cfd);
        close(cfd);
        return;
    }

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET,&cin.sin_addr,ip,sizeof(ip));
    printf("[accept] cfd=%d from %s:%d,slot=%d\n",cfd,ip,ntohs(cin.sin_port),i);

    //set设置recvdata回调，add上树
    eventset(&g_events[i],cfd,recvdata,&g_events[i]);
    eventadd(g_efd,EPOLLIN,&g_events[i]);
    
}

static void recvdata(int fd,int events,void *arg){
    struct myevent_s *ev=(struct myevent_s *)arg;
    //fprintf(stderr,"[1] 进函数 fd=%d events=%d\n",fd,events);
    (void)events;      //形参 events = 内核告诉我"发生了什么"，只读，不要往 eventadd 里塞

    //读一次，最多留一个字节给 '\0'
    int len=(int)recv(fd,ev->buf,sizeof(ev->buf)-1,0);
    //fprintf(stderr,"[2] recv 返回 len=%d errno=%d\n",len,errno);
    ev->buf[len>0?len:0]='\0';

    if(len>0){
        //fprintf(stderr,"[3] 走len>0 分支\n");
        ev->len=len;
        ev->last_active=(long)time(NULL);
        printf("[recv] fd=%d,len=%d,data=%s",fd,len,ev->buf);
        //切到"可写"：摘旧牌 -> 换回调 -> 挂 EPOLLOUT
        eventdel(g_efd,ev);
        eventset(ev,fd,senddata,ev);
        eventadd(g_efd,EPOLLOUT,ev);
    }
    else if(len==0){
        printf("[close] fd=%d pos=%d peer closed\n",fd,(int)(ev-g_events));
        eventdel(g_efd,ev);
        close(fd);
    }
    else{
        if(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)
            return;                       //读干净了/被信号打断，状态不变，牌还在树上
        int e=errno;                       //close 会改写 errno，先存
        printf("[recv error] fd=%d errno=%d(%s)\n",fd,e,strerror(e));
        eventdel(g_efd,ev);
        close(fd);
    }
}

static void senddata(int fd,int events,void *arg){
    struct myevent_s *ev=(struct myevent_s *)arg;
    (void)events;

    int n=(int)send(fd,ev->buf,(size_t)ev->len,MSG_NOSIGNAL);

    if(n>0 && n<ev->len){
        //没发完：剩下的挪到 buf 开头，牌子还是 EPOLLOUT，下次接着发
        memmove(ev->buf,ev->buf+n,(size_t)(ev->len-n));
        ev->len-=n;
        printf("[send] fd=%d 部分发送 n=%d 剩余=%d\n",fd,n,ev->len);
        return;
    }
    if(n==ev->len){
        //发完了，回到接收态
        ev->len=0;
        eventdel(g_efd,ev);
        eventset(ev,fd,recvdata,ev);
        eventadd(g_efd,EPOLLIN,ev);
        return;
    }
    if(n<0 && (errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))
        return;                           //内核发送缓冲区满了，等下次可写
    int e=errno;
    printf("[send error] fd=%d errno=%d(%s)\n",fd,e,strerror(e));
    eventdel(g_efd,ev);
    close(fd);
}

static void initlistensocket(int efd,unsigned short port){
    //socket->setsocketopt->memset->填地址-》bind->listen
    int lfd=socket(AF_INET,SOCK_STREAM,0);
    if(lfd<0){
        perror("socket");
        exit(1);
    }
    int opt=1;
    
    setsockopt(lfd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));

    struct sockaddr_in sin;
    sin.sin_family=AF_INET;
    sin.sin_port=htons(port);
    sin.sin_addr.s_addr=htonl(INADDR_ANY);

    if(bind(lfd,(struct sockaddr *)&sin,sizeof(sin))<0){
        perror("bind");
        close(lfd);
        exit(1);
    }
    if(listen(lfd,128)<0){
        perror("listen");
        close(lfd);
        exit(1);
    }

    int flags=fcntl(lfd,F_GETFL,0);
    fcntl(lfd,F_SETFL,flags|O_NONBLOCK);

    eventset(&g_events[MAX_EVENTS],lfd,acceptconn,&g_events[MAX_EVENTS]);
    eventadd(efd,EPOLLIN,&g_events[MAX_EVENTS]);

    printf("[init] listen fd=%d on port %u,slot=%d\n",lfd,(unsigned)port,MAX_EVENTS);
    //设置非阻塞，任何fd都不能阻塞，g_events[MAX_EVENTS]给lfd
    //eventset（acceptconn），add（epollin）
    //打印端口
}

int main(int argc,char *argv[]){
    unsigned short port=SERV_PORT;
    if(argc==2)
        port=(unsigned short)atoi(argv[1]);
    
    g_efd=epoll_create(MAX_EVENTS+1);
    if(g_efd<=0){
        perror("epoll_create");
        return 1;
    }

    initlistensocket(g_efd,port);
    printf("reactor server running,port=%d,max_events=%d\n",(unsigned)port,MAX_EVENTS);
    printf("how to test: ./client 127.0.0.1 %u\n",(unsigned)port);

    struct epoll_event events[MAX_EVENTS+1];

    while(1) {
        int nfd=epoll_wait(g_efd,events,MAX_EVENTS+1,1000);
        if(nfd<0){
            if(errno==EINTR)
                continue;
            perror("epoll_wait");
            break;
        }
        for(int i=0;i<nfd;i++){
            struct myevent_s *ev=(struct myevent_s *)events[i].data.ptr;
            if((events[i].events&EPOLLIN)&&(ev->events&EPOLLIN))
                ev->call_back(ev->fd,events[i].events,ev->arg);
            if((events[i].events&EPOLLOUT)&&(ev->events&EPOLLOUT))
                ev->call_back(ev->fd,events[i].events,ev->arg);
            if((events[i].events&(EPOLLERR|EPOLLHUP))&&(ev->events&EPOLLIN))
                ev->call_back(ev->fd,events[i].events,ev->arg);
        }
    }
    
    long now=time(NULL);
    for(int i=0;i<MAX_EVENTS;i++){
        if(g_events[i].status==0)
            continue;
        if(now-g_events[i].last_active>IDLE_TIMEOUT){
            printf("[timeout] close idle fd=%d,slot=%d\n",g_events[i].fd,i);
            eventdel(g_efd,&g_events[i]);
            close(g_events[i].fd);
        }
    }
    close(g_efd);
    return 0;
    // epoll_create -> initlistensocket -> 把 lfd 挂上
    // while(1)：epoll_wait -> 遍历就绪事件 -> 判断事件位 -> 回调
    // 事件判断必须用 &，不能用 ==：同一个 fd 可能同时可读可写，
    // 组合之后 events[i].events 是多个标志位或起来的值，用 == 会漏掉。
   
}