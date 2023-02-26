#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<stdio.h>
#include<unistd.h>
#include<errno.h>
#include<string.h>
#include<fcntl.h>
#include<stdlib.h>
#include<cassert>
#include<sys/epoll.h>
#include<iostream>
#include"locker/locker.h"
#include"threadpool/threadpool.h"
#include"http_conn/http_conn.h"

// #define MAX_FD 65536
// #define MAX_EVENT_NUMBER 10000

// extern int addfd(int epollfd,int fd,bool one_shot);
// extern int removefd(int epollfd,int fd);

// void addsig(int sig,void(handler)(int),bool restart = true){
//     struct sigaction sa;
//     memset(&sa,'\0',sizeof(sa));
//     sa.sa_handler = handler;
//     // if(restart){
//     //     sa.sa_flags |= SA_RESTART;
//     // }
//     sigfillset(&sa.sa_mask);
//     assert(sigaction(sig,&sa,NULL) != -1);
// }

// void show_error(int connfd,const char* info){
//     printf("%s",info);
//     send(connfd,info,strlen(info),0);
//     close(connfd);
// }

// int main(int argc,char** argv){
//     if(argc <= 1){
//         printf("usage:%s port_number\n",basename(argv[0]));
//         return 1;
//     }

//     int port = atoi(argv[1]);

//     // 忽略SIGPIPE信号
//     addsig(SIGPIPE,SIG_IGN);

//     threadpool<http_conn>* pool = NULL;
//     try{
//         pool = new threadpool<http_conn>;
//     }
//     catch(...){
//         return 1;
//     }

//     http_conn* users = new http_conn[MAX_FD];
//     assert(users);
//     int user_count = 0;

//     int listenfd = socket(PF_INET,SOCK_STREAM,0);
//     assert(listenfd >= 0);
//     struct linger tmp = {1,0};
//     // SOL_SOCKET是通用选项
//     setsockopt(listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));
//     std::cout<<"hhh"<<std::endl;
//     int ret = 0;
//     // sockaddr_in用于IPv4
//     struct sockaddr_in address;
//     bzero(&address,sizeof(address));
//     address.sin_family = AF_INET;
//     address.sin_addr.s_addr = INADDR_ANY;
//     address.sin_port = htons(port);

//     // 端口复用
//     int reuse = 1;
//     setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
//     ret = bind(listenfd,(struct sockaddr*)&address,sizeof(address));
//     assert(ret >= 0);

//     ret = listen(listenfd,5);
//     assert(ret >= 0);

//     epoll_event events[MAX_EVENT_NUMBER];
//     int epollfd = epoll_create(5);
//     assert(epollfd != -1);
//     addfd(epollfd,listenfd,false);
//     http_conn::m_epollfd = epollfd;

//     while(true){
//         int number = epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);
//         if((number < 0) && (errno != EINTR)){
//             printf("epoll failure\n");
//             break;
//         }

//         for(int i = 0;i < number;i++){
//             int sockfd = events[i].data.fd;
//             if(sockfd == listenfd){
//                 struct sockaddr_in client_address;
//                 socklen_t client_addrlength = sizeof(client_address);
//                 int connfd = accept(listenfd,(struct sockaddr*)&client_address,&client_addrlength);
//                 if(connfd < 0){
//                     printf("errno is:%d\n",errno);
//                     continue;
//                 }
//                 if(http_conn::m_user_count >= MAX_FD){
//                     show_error(connfd,"Internal server busy");
//                     continue;
//                 }
//                 users[connfd].init(connfd,client_address);
//             }
//             else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
//                 users[sockfd].close_conn();
//             }
//             else if(events[i].events & EPOLLIN){
//                 std::cout<<"hhhh"<<std::endl;
//                 if(users[sockfd].read()){
//                     pool->append(users + sockfd);
//                 }
//                 else{
//                     users[sockfd].close_conn();
//                 }
//             }
//             else if(events[i].events & EPOLLOUT){
//                 if(!users[sockfd].write()){
//                     users[sockfd].close_conn();
//                 }
//             }
//             else{

//             }
//         }
//     }
// }

#define MAX_FD 65536   // 最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000  // 监听的最大的事件数量

// 添加文件描述符
extern void addfd( int epollfd, int fd, bool one_shot );
extern void removefd( int epollfd, int fd );

void addsig(int sig, void( handler )(int)){
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = handler;
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

int main( int argc, char* argv[] ) {
    
    if( argc <= 1 ) {
        printf( "usage: %s port_number\n", basename(argv[0]));
        return 1;
    }

    int port = atoi( argv[1] );
    addsig( SIGPIPE, SIG_IGN );

    threadpool< http_conn >* pool = NULL;
    try {
        pool = new threadpool<http_conn>;
    } catch( ... ) {
        return 1;
    }

    http_conn* users = new http_conn[ MAX_FD ];

    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );

    int ret = 0;
    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons( port );

    // 端口复用
    int reuse = 1;
    setsockopt( listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    ret = listen( listenfd, 5 );

    // 创建epoll对象，和事件数组，添加
    epoll_event events[ MAX_EVENT_NUMBER ];
    int epollfd = epoll_create( 5 );
    // 添加到epoll对象中
    addfd( epollfd, listenfd, false );
    http_conn::m_epollfd = epollfd;

    while(true) {
        
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        
        if ( ( number < 0 ) && ( errno != EINTR ) ) {
            printf( "epoll failure\n" );
            break;
        }

        for ( int i = 0; i < number; i++ ) {
            
            int sockfd = events[i].data.fd;
            
            if( sockfd == listenfd ) {
                
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                
                if ( connfd < 0 ) {
                    printf( "errno is: %d\n", errno );
                    continue;
                } 

                if( http_conn::m_user_count >= MAX_FD ) {
                    close(connfd);
                    continue;
                }
                users[connfd].init( connfd, client_address);

            } else if( events[i].events & ( EPOLLRDHUP | EPOLLHUP | EPOLLERR ) ) {

                users[sockfd].close_conn();

            } else if(events[i].events & EPOLLIN) {

                if(users[sockfd].read()) {
                    pool->append(users + sockfd);
                } else {
                    users[sockfd].close_conn();
                }

            }  else if( events[i].events & EPOLLOUT ) {

                if( !users[sockfd].write() ) {
                    users[sockfd].close_conn();
                }

            }
        }
    }
    
    close( epollfd );
    close( listenfd );
    delete [] users;
    delete pool;
    return 0;
}