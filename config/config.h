#ifndef CONFIG_H
#define CONFIG_H

#include "../webserver.h"

using namespace std;

class Config
{
public:
    Config();
    ~Config(){};

    void parse_arg(int argc,char *argv[]);

    int PORT;

    int LOGWrite;

    int TRIGMode;

    int LISTENTrigmode;

    int CONNTrigmode;

    int OPT_LINGER;

    int sql_num;

    int thread_num;

    int close_log;
};

#endif