#include "config.h"

Config::Config(){
    PORT = 10000;

    LOGWrite = 0;

    TRIGMode;

    LISTENTrigmode = 0;

    CONNTrigmode = 0;

    OPT_LINGER = 0;

    sql_num = 8;

    thread_num = 8;
    
    close_log = 0;
}

void Config::parse_arg(int argc,char*argv[]){
    int opt;
    const char *str = "p:l:m:o:s:t:c:";
    while((opt = getopt(argc,argv,str)) != -1){
        switch(opt){
            case 'p':
            {
                PORT = atoi(optarg);
                break;
            }
            case 'l':
            {
                LOGWrite = atoi(optarg);
                break;
            case 'm':
            {
                TRIGMode = atoi(optarg);
                break;
            }
            case 'o':
            {
                OPT_LINGER = atoi(optarg);
                break;
            }
           case 's':
            {
                sql_num = atoi(optarg);
                break;
            }
           case 't':
            {
                thread_num = atoi(optarg);
                break;
            }
           case 'c':
            {
                close_log = atoi(optarg);
                break;
            }
            default:
                break;
            }
        }
    }
}