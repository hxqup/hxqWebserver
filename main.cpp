#include"config/config.h"

int main(int argc,char* argv[]){
    WebServer server;

    string user = "root";
    string passwd = "test";
    string databaseName = "yourdb";

    Config config;
    config.parse_arg(argc,argv);

    server.init(config.PORT,user,passwd,databaseName,config.LOGWrite,config.OPT_LINGER,
                config.sql_num,config.thread_num,config.close_log);

    server.log_write();

    server.sql_pool();

    server.thread_pool();

    server.trig_mode();

    server.eventListen();

    server.eventLoop();

    return 0;
}