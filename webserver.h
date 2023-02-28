# ifndef WEBSERVER_H
# define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "threadpool/threadpool.h"
#include "http_conn/http_conn.h"

const int MAX_FD = 65536;
const int MAX_EVENT_NUMBER = 10000;
const int TIMESLOT = 5;

class WebServer
{
public:
    WebServer();
    ~WebServer();

    void init(int port = 10000,int log_write = 1,int opt_linger = 0,int thread_num = 8,int m_trigmode = 3);
    void trig_mode();
    void log_write();
    void thread_pool();
    void eventListen();
    void eventLoop();
    void timer(int connfd,struct sockaddr_in client_address);
    void adjust_timer(util_timer *timer);
    void deal_timer(util_timer *timer,int sockfd);
    bool dealclientdata();
    bool dealwithsignal(bool& timeout,bool& stop_server);
    void dealwithread(int sockfd);
    void dealwithwrite(int sockfd);

public:
    int m_port;
    char* m_root;
    int m_log_write;
    int m_close_log;

    int m_pipefd[2];
    int m_epollfd;
    http_conn *users;

    // 线程池相关
    threadpool<http_conn> *m_pool;
    int m_thread_num;

    // epoll_event相关
    epoll_event events[MAX_EVENT_NUMBER];

    int m_listenfd;
    int m_OPT_LINGER;
    int m_TRIGMode;
    int m_LISTENTrigmode;
    int m_CONNTrigmode;

    // 定时器相关
    client_data *users_timer;
    Utils utils;
};

#endif