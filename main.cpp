#include "server.h"

int main(){
    server_settings settings;
    settings.port_ = 12347;
    settings.howManyClients_ = 32;
    settings.databasePath_ = "/home/winner/";
    settings.databaseName_ = "message_db";
    settings.words_.push_back("house");
    settings.words_.push_back("friend");

    server my_messenger(settings);

    my_messenger.start();
}

