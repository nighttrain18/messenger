#ifndef SERVER_H
#define SERVER_H


/*
*/
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <set>
#include <iostream>
#include <chrono>
#include <queue>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <map>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <iterator>
#include <time.h>
#include <future>
#include <chrono>


using namespace std;

struct message_info;
struct node;
struct algo{
    vector<node> bohr_;                       // коллекция для хранения вершин бора
    vector<string> patterns_;                 // коллекция для хранения поисковых строк
    void addString(const string& s);        // добавляем строку в бор
    void initialize();                       // инициализируем бор
    int getSuffLink(int _from);            // получаем суффиксную ссылку
    void createDepends();                   // создаём связи между состояниями при помощи суффиксных ссылок
    bool findWords(message_info& msgi);     // ищем ключевые слова в сообщении
};

struct message_info{                                                      // структура для хранения информации о сообщении
    message_info(int msgsize, char* buf, int sender, string time_date){
        messageSize_ = msgsize;
        for(int i = 0; i < msgsize; i++)
            message_[i] = buf[i];
        whoSend_ = sender;
        whenSend_ = time_date;
    }
    int messageSize_;         // размер сообщения
    char message_[1024];       // содержание сообщения
    int whoSend_;             // адрес отправителя сообщения
    set<string> whatFound_;   // какие ключевые слова были найдены в сообщении
    string whenSend_;         // когда было отправлено сообщение
};

struct server_settings{                 // структура для определения исходных параметров
    int port_;                           // на какой порт открываем сервер
    int howManyClients_;               // какое количество клиентов может держать сервер
    string databasePath_;               // путь к создаваемой базе данных
    string databaseName_;               // имя базы данных
    vector<string> words_;               // по каким словам будет происходить поиск
};

class server{                                          // наш сервер
private:
    int masterSocket_;
    int portNumber_;                                  // порт на который открываем сервер
    int maxEvents_;                                   // максимальное количество клиентов, которые держит сервер
    queue<message_info> messageBufferForDatabase_;         // буфер для найденных по ключевым словам сообщений
    mutex mutexOnMessageBufferForDatabase_;                                       // мьютекс для буфера для найденных по ключевым словам сообщений
    queue<message_info> messageBufferForAnalyze_;    // буфер для поступивших сообщений от клиентов
    mutex mutexOnMessageBufferForAnalyze_;                                    // мьютекс для буфера для поступивших сообщений от клиентов
    map<int, int> slaveSockets_;                        // словарь для хранения пар (открытый сокет для клиента, адрес клиента)
    algo handler_;                                      // объект класса алгоритма
    QSqlDatabase database_;                                   // база данных
private:
    void algoStart();                                 // функция запуска алгоритма
    int setNonblock(int fd);                          // функция перевода сокета в неблокирующий режим
    void writeDataInDatabase();                       // функция записи информации в базу данных
public:
    server(server_settings settings){
        portNumber_ = settings.port_;
        maxEvents_ = settings.howManyClients_;

        handler_.initialize();
        for(int i = 0; i < settings.words_.size(); i++)
            handler_.addString(settings.words_[i]);
        handler_.createDepends();

        database_ = QSqlDatabase::addDatabase("QSQLITE");
        string path = settings.databasePath_ + settings.databaseName_ + ".db";
        remove(path.c_str());                                                    // удаляем базу, если она есть
        database_.setDatabaseName(QString::fromStdString(path));
        if(!database_.open())
            cout << "Database not open" << endl;

        /* Создаём три таблицы в базе данных */
        QSqlQuery query(database_);
        query.exec("CREATE TABLE message_list(id_message INTEGER not null primary key autoincrement, message_text text not null, sender_ip integer not null, date_of_send text)");
        query.exec("CREATE TABLE words_list(id_word INTEGER not null primary key autoincrement, word text)");
        query.exec("CREATE TABLE search_result(id INTEGER not null primary key autoincrement, id_word int not null, id_message int not null, foreign key(id_word) references words_list(id_word), foreign key(id_message) references message_list(id_message))");
        for(int i = 0; i < settings.words_.size(); i++)
            query.exec("insert into words_list values(null, '" + QString::fromStdString(settings.words_[i]) + "')");

    }
    ~server(){
        shutdown(masterSocket_, SHUT_RDWR);
        close(masterSocket_);
        writeDataInDatabase();
    }
    void start();                       // функция запуска сервера
};

struct node{                                 // структура узла бора
    node(char _symb, int _parent_num){
        for(int i = 0; i < 26; i++)
            nextNode_[i] = -1;
        flag_ = false;
        patternsNum_ = -1;
        symNode_ = _symb;
        parentNum_ = _parent_num;
        shuffLink_ = -1;
        shuffFlink_ = -2;
    }
    int nextNode_[26];                       // массив переходов по символам. Показывает номер узла в который мы придем по символу из алфавита
    bool flag_;                               // индикатор того факта, заканчивается в узле искомое слово или нет
    char symNode_;                           // символ алфавита в узле
    int patternsNum_;                        // номер поисковой строки, которая заканчивается на этом узлу
    int shuffLink_;                          // суффиксная ссылка
    int shuffFlink_;                         // хорошая суффиксная ссылка
    int parentNum_;                          // номер верщины - родителя данного узла
};


#endif // SERVER_H
