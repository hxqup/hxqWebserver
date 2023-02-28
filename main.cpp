#include"webserver.h"

int main(){
    WebServer server;

    server.init();

    server.log_write();

    server.thread_pool();

    server.trig_mode();

    server.eventListen();

    server.eventLoop();

    return 0;
}