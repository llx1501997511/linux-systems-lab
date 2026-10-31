#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define BUFSIZE 4096

static ssize_t writen(int fd,const char *buf,size_t n){
	size_t left=n;
    const char *p=buf;
    while(left>0){
        ssize_t w=write(fd,p,left);
        if(w<0){
            if(errno==EINTR)   
                continue;
            return -1;
        }
        if(w==0)   
            return -1;
        left-=(size_t)w;
        p+=w;

    }
    return (ssize_t)n;
}

int main(int argc,char *argv[]){
    if(argc!=3){
        fprintf(stderr,"usage:%s <ip> <port>\n",argv[0]);
        return 1;
    }
    int cfd=socket(AF_INET,SOCK_STREAM,0);
    if(cfd<0){
        perror("socket");
        return 1;
    }

    struct sockaddr_in serv;
    memset(&serv,0,sizeof(serv));
    serv.sin_family=AF_INET;
    serv.sin_port=htons((uint16_t)atoi(argv[2]));
    if(inet_pton(AF_INET,argv[1],&serv.sin_addr)!=1){
        fprintf(stderr,"bad ip: %s\n",argv[1]);
        close(cfd);
        return 1;
    }

    if(connect(cfd,(struct sockaddr *)&serv,sizeof(serv))<0){
        perror("connect");
        close(cfd);
        return  1;
    }
    printf("[connected]%s：%s （输入内容回车发送，Ctrl+D 退出）\n",argv[1],argv[2]);

    char buf[BUFSIZ];
    while (1)
    {
        ssize_t n=read(STDIN_FILENO,buf,sizeof(buf));
        if(n==0)    break;
        if(n<0){
            if(errno==EINTR)    continue;
            perror("read stdin");
            break;
        }

        if((writen(cfd,buf,(size_t)n))<0){
            perror("write");
            break;
        }

        n=read(cfd,buf,sizeof(buf));
        if(n==0){printf("\n[server closed]\n");break;}
        if(n<0){
            if(errno==EINTR)
                continue;
            perror("read socket");
            break;
        }

        if((writen(STDOUT_FILENO,buf,(size_t)n))<0)
            break;
        /* code */
    }
    close(cfd);
    return 0;
    
}