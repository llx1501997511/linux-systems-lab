#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define SERV_PORT 8000
#define MAXLINE 8192
#define OPEN_MAX 1024

int main(int argc,char *argv[])
{
    unsigned short port=SERV_PORT;
    if(argc==2)
        port=(unsigned short)atoi(argv[1]);
    
    int lfd=socket(AF_INET,SOCK_STREAM,0);
    if(lfd<0){perror("socket");return 1;}

    int opt=1;
    setsockopt(lfd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));

    struct sockaddr_in serv;
    memset(&serv,0,sizeof(serv));
    serv.sin_family=AF_INET;
    serv.sin_addr.s_addr=htonl(INADDR_ANY);
    serv.sin_port=htons(port);

    if(bind(lfd,(struct sockaddr *)&serv,sizeof(serv))<0){
        perror("bind");
        close(lfd);
        return 1;
    }

    if(listen(lfd,128)<0){perror("listen");close(lfd);return 1;}

    int efd=epoll_create(OPEN_MAX);
    if(efd<0){
        perror("epoll_create");
        close(lfd);
        return 1;
    }

    struct epoll_event tep,ep[OPEN_MAX];

    memset(&tep,0,sizeof(tep));
    tep.events=EPOLLIN;
    tep.data.fd=lfd;

    if(epoll_ctl(efd,EPOLL_CTL_ADD,lfd,&tep)<0){
        perror("epoll_ctl add lfd");
        close(lfd);
        close(efd);
        return 1;
    }

    printf("epoll server listening on %u ...\n", (unsigned)port);
    printf("how to test: ./client 127.0.0.1 %u\n", (unsigned)port);

    while(1){
        int n=epoll_wait(efd,ep,OPEN_MAX,-1);
        if(n<0){
            if(errno==EINTR)
                continue;
            perror("epoll_wait");
            break;
        }

        for(int i=0;i<n;i++){
            if(!(ep[i].events&EPOLLIN))
                continue;
            if(ep[i].data.fd==lfd){
                struct sockaddr_in cli;
                socklen_t len=sizeof(cli);
                int cfd=accept(lfd,(struct sockaddr *)&cli,&len);

                if(cfd<0){
                    if(errno!=EAGAIN&&errno!=EINTR)
                        perror("accept");
                    continue;
                }
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET,&cli.sin_addr,ip,sizeof(ip));
                printf("[accept] cfd=%d from %s:%d\n", cfd, ip, ntohs(cli.sin_port));
                
                memset(&tep,0,sizeof(tep));
                tep.events=EPOLLIN;
                tep.data.fd=cfd;
                if(epoll_ctl(efd,EPOLL_CTL_ADD,cfd,&tep)<0){
                    perror("epoll_ctl add cfd");
                    close(cfd);
                }
            }else{
                    char buf[MAXLINE];
                    int fd=ep[i].data.fd;

                    ssize_t r=read(fd,buf,sizeof(buf));
                    if(r<=0){
                        if(r<0&&errno==EINTR)
                            continue;
                        
                        epoll_ctl(efd,EPOLL_CTL_DEL,fd,NULL);
                        close(fd);
                        printf("[close] fd=%d (r=%ld)\n", fd, (long)r);
                    }else{
                        for(ssize_t k=0;k<r;k++)
                            buf[k]=(char)toupper((unsigned char)buf[k]);
                        
                        if(write(fd,buf,(size_t)r)!=r)
                            perror("write");
                    }
                }
        }
    }
    close(efd);
    close(lfd);
    return 0;

}