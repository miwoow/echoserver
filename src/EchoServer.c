/*************************************************************************
  > File Name: EchoServer.c
  > Author: xudong
  > Mail: xudong@xianlai-inc.com
  > Created Time: 2018年10月30日 星期二 17时43分00秒
 ************************************************************************/


#include <sys/socket.h> 
#include <sys/epoll.h> 
#include <netinet/in.h> 
#include <arpa/inet.h> 
#include <fcntl.h> 
#include <unistd.h> 
#include <stdio.h> 
#include <errno.h> 
#include <strings.h>
#include <string.h>
#include <time.h>

#include <netdb.h>
 
#include <sys/types.h>
#include <stdlib.h>

#define MAX_EVENTS 10240

const char *KEY="Hello QPD.";

typedef struct _myevent_s 
{ 
    int fd; 
    void (*call_back)(int fd, int events, void *arg); 
    void *arg; 
    int status; // 1: in epoll wait list, 0 not in 
    char buff[128]; // recv data buffer 
    int len, s_offset; 
    long last_active; // last active time 
}myevent_s; 

int g_epollFd; 
myevent_s g_Events[MAX_EVENTS+1]; // g_Events[MAX_EVENTS] is used by listen fd 
void RecvData(int fd, int events, void *arg); 
void SendData(int fd, int events, void *arg); 

// set event 
void EventSet(myevent_s *ev, int fd, void (*call_back)(int, int, void*), void *arg) 
{ 
    ev->fd = fd; 
    ev->call_back = call_back; 
    ev->arg = arg; 
    ev->status = 0;
    bzero(ev->buff, sizeof(ev->buff));
    ev->s_offset = 0; 
    ev->len = 0;
    ev->last_active = time(NULL); 
} 

void EventAdd(int epollFd, int events, myevent_s *ev) 
{ 
    struct epoll_event epv = {0, {0}}; 
    int op; 
    epv.data.ptr = ev; 
    epv.events = events; 
    if(ev->status == 1){ 
        op = EPOLL_CTL_MOD; 
    } 
    else{ 
        op = EPOLL_CTL_ADD; 
        ev->status = 1; 
    } 
    if(epoll_ctl(epollFd, op, ev->fd, &epv) < 0)  {
        printf("Event Add failed[fd=%d], op=%d, evnets[%d]\n", ev->fd, op, events); 
        perror("Event add failed");
    }
} 

void EventDel(int epollFd, myevent_s *ev) 
{ 
    struct epoll_event epv = {0, {0}}; 
    if(ev->status != 1) return; 
    epv.data.ptr = ev; 
    ev->status = 0;
    epoll_ctl(epollFd, EPOLL_CTL_DEL, ev->fd, &epv); 
} 


void AcceptConn(int fd, int events, void *arg) 
{ 
    
    long now = time(NULL);
    struct sockaddr_in sin; 
    socklen_t len = sizeof(struct sockaddr_in); 
    int nfd, i; 
    // accept 
    if((nfd = accept(fd, (struct sockaddr*)&sin, &len)) == -1) 
    { 
        if(errno != EAGAIN && errno != EINTR) 
        { 
        }
        //printf("%s: accept, %d", __func__, errno); 
        return; 
    } 
    do 
    { 
        for(i = 0; i < MAX_EVENTS; i++) 
        { 
            if(g_Events[i].status == 0) 
            { 
                break; 
            } 
        } 
        if(i == MAX_EVENTS) 
        { 
            printf("%s:max connection limit[%d].", __func__, MAX_EVENTS); 
            break; 
        } 
        // set nonblocking
        int iret = 0;
        if((iret = fcntl(nfd, F_SETFL, O_NONBLOCK)) < 0)
        {
            printf("%s: fcntl nonblocking failed:%d", __func__, iret);
            break;
        }
        // add a read event for receive data 
        EventSet(&g_Events[i], nfd, RecvData, &g_Events[i]); 
        EventAdd(g_epollFd, EPOLLIN, &g_Events[i]); 
    }while(0); 
    printf("new conn[%s:%d][time:%ld], pos[%d]\n", inet_ntoa(sin.sin_addr), 
            ntohs(sin.sin_port), g_Events[i].last_active, i); 
    long now2 = time(NULL);
    if (now2-now>1) {
        printf("Accept spend 1 sec\n");
    }
} 

// receive data 
void RecvData(int fd, int events, void *arg) 
{ 
    long now = time(NULL);
    myevent_s *ev = (myevent_s*)arg; 
    int len; 

    len = recv(fd, ev->buff+ev->len, sizeof(ev->buff)-1-ev->len, 0); 
    //EventDel(g_epollFd, ev);
    if(len > 0)
    {
        ev->len += len;
        ev->buff[ev->len] = '\0'; 
        if (strlen(ev->buff) >= strlen(KEY)) {
            if (strncmp(ev->buff, KEY, strlen(KEY)) == 0) {
                long now2 = time(NULL);
                //printf("C[%d]:%s %ld\n", fd, ev->buff, now2-now); 
            } else {
                close(ev->fd);
                EventDel(g_epollFd, ev);
                return;
            }
        } else {
            close(ev->fd);
            EventDel(g_epollFd, ev);
            return;
        }
        // change to send event 
        ev->call_back = SendData; 
        ev->s_offset = 0; 
        ev->last_active = time(NULL); 

        EventAdd(g_epollFd, EPOLLOUT, ev); 
    } 
    else if(len == 0) 
    { 
        close(ev->fd); 
        EventDel(g_epollFd, ev);
        //printf("[fd=%d] pos[%ld], closed gracefully.\n", fd, ev-g_Events); 
    } 
    else 
    { 
        close(ev->fd); 
        EventDel(g_epollFd, ev);
        //printf("recv[fd=%d] error[%d]:%s\n", fd, errno, strerror(errno)); 
    } 
} 

// send data 
void SendData(int fd, int events, void *arg) 
{ 
    myevent_s *ev = (myevent_s*)arg; 
    struct sockaddr_in sa;
    char *peeraddr;
    char *mpeer = (char *)malloc(128);
    int len; 

    if (!getpeername(fd, (struct sockaddr *)&sa, &len)) {
        memset(mpeer, 0, 128);
        peeraddr = inet_ntoa(sa.sin_addr);
        memcpy(mpeer, peeraddr, strlen(peeraddr));

        // send data 
        len = send(fd, mpeer + ev->s_offset, strlen(mpeer) - ev->s_offset, 0);
        if(len > 0) 
        {
            printf("send[fd=%d], [%d<->%d]%s\n", fd, len, strlen(mpeer), mpeer);
            ev->s_offset += len;
            if(ev->s_offset == strlen(mpeer))
            {
                // change to receive event
                close(ev->fd);
                EventDel(g_epollFd, ev); 
            }
        } 
        else 
        { 
            close(ev->fd); 
            EventDel(g_epollFd, ev); 
            printf("send[fd=%d] error[%d]\n", fd, errno); 
        } 
    } else {
        close(ev->fd); 
        EventDel(g_epollFd, ev); 
        printf("send[fd=%d] error[%d]\n", fd, errno); 
    }
    free(mpeer);
} 

int InitListenSocket(int epollFd, short port) 
{ 
    int listenFd = socket(AF_INET, SOCK_STREAM, 0); 
    int code;

    fcntl(listenFd, F_SETFL, O_NONBLOCK);
    EventSet(&g_Events[MAX_EVENTS], listenFd, AcceptConn, &g_Events[MAX_EVENTS]); 

    EventAdd(epollFd, EPOLLIN,  &g_Events[MAX_EVENTS]); 

    struct sockaddr_in sin; 
    bzero(&sin, sizeof(sin)); 

    sin.sin_family = AF_INET; 
    sin.sin_addr.s_addr = INADDR_ANY; 
    sin.sin_port = htons(port); 

    code = bind(listenFd, (struct sockaddr*)&sin, sizeof(sin));
    if (code != 0) {
        perror("ERROR bind failed");
        close(listenFd);
        return 1;
    }
    listen(listenFd, 1024); 
    return 0;
} 

int main(int argc, char *argv[]) 
{ 
    unsigned short port = 2333; // default port 
    int code;

    if(argc == 2){ 
        port = atoi(argv[1]); 
    } 

    g_epollFd = epoll_create(MAX_EVENTS); 

    if(g_epollFd <= 0) {
        printf("create epoll failed.%d\n", g_epollFd); 
    }

    code = InitListenSocket(g_epollFd, port); 
    if (code != 0) {
        return 1;
    }

    struct epoll_event events[MAX_EVENTS]; 
    int checkPos = 0; 
    while(1){ 
        long now = time(NULL); 
        for(int i = 0; i < 100; i++, checkPos++) // doesn't check listen fd 
        { 
            if(checkPos == MAX_EVENTS) checkPos = 0; // recycle 
            if(g_Events[checkPos].status != 1) continue; 
            long duration = now - g_Events[checkPos].last_active; 
            if(duration >= 60) // 60s timeout 
            { 
                close(g_Events[checkPos].fd); 
                //printf("[fd=%d] timeout[%ld--%ld].\n", g_Events[checkPos].fd, g_Events[checkPos].last_active, now); 
                EventDel(g_epollFd, &g_Events[checkPos]); 
            } 
        } 
        long now2 = time(NULL);
        if (now2-now > 1) {
            printf("close socket spend %ld\n", now2-now);
        }
        now = time(NULL);
        int fds = epoll_wait(g_epollFd, events, MAX_EVENTS, 1000); 
        now2 = time(NULL);
        if (now2-now > 1) {
            printf("epoll wait spend %ld\n", now2-now);
        }

        if(fds < 0){ 
            printf("epoll_wait error, exit\n"); 
            break; 
        } 
        now = time(NULL);
        for(int i = 0; i < fds; i++){ 
            myevent_s *ev = (myevent_s*)events[i].data.ptr; 
            if(events[i].events & EPOLLIN) // read event 
            { 
                ev->call_back(ev->fd, events[i].events, ev->arg); 
            } 
            if(events[i].events & EPOLLOUT) // write event 
            { 
                ev->call_back(ev->fd, events[i].events, ev->arg); 
            } 
        } 
        now2 = time(NULL);
        if (now2-now > 1) {
            printf("call back spend %ld\n", now2-now);
        }
    } 
    return 0; 
}
