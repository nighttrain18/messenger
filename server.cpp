#include "server.h"

void server::start(){
    int MasterSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    masterSocket_ = MasterSocket;

    struct sockaddr_in SockAddr;
    SockAddr.sin_family = AF_INET;
    SockAddr.sin_port = htons(portNumber_);
    SockAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    bind(MasterSocket, (struct sockaddr*)(&SockAddr), sizeof(SockAddr));
    setNonblock(MasterSocket);
    listen(MasterSocket, SOMAXCONN);

    int EPoll = epoll_create1(0);

    struct epoll_event Event;
    Event.data.fd = MasterSocket;
    Event.events = EPOLLIN;
    epoll_ctl(EPoll, EPOLL_CTL_ADD, MasterSocket, &Event);

    unique_lock<mutex> buffer_lock(mutexOnMessageBufferForAnalyze_, defer_lock);
    thread t(&server::algoStart, this);
    t.detach();

    while(true){
        struct epoll_event Events[maxEvents_];
        int N = epoll_wait(EPoll, Events, maxEvents_, -1);

        for(int i = 0; i < N; i++){
            if(Events[i].data.fd == MasterSocket){
                sockaddr_in client_addr;
                socklen_t client_addr_size = sizeof(struct sockaddr_in);
                int SlaveSocket = accept(MasterSocket, (struct sockaddr*)(&client_addr), &client_addr_size);
                setNonblock(SlaveSocket);
                slaveSockets_[SlaveSocket] = client_addr.sin_addr.s_addr;
                struct epoll_event Event;
                Event.data.fd = SlaveSocket;
                Event.events = EPOLLIN;
                epoll_ctl(EPoll, EPOLL_CTL_ADD, SlaveSocket, &Event);
                cout << "New client connect..." << endl;
            }
            else{
                char buffer[1024];
                time_t td;
                int RecvSize = recv(Events[i].data.fd, buffer, 1024, MSG_NOSIGNAL);
                td = time(NULL);
                if(RecvSize == 0 && errno != EAGAIN){
                    shutdown(Events[i].data.fd, SHUT_RDWR);
                    slaveSockets_.erase(Events[i].data.fd);
                    epoll_ctl(EPoll, EPOLL_CTL_DEL, Events[i].data.fd, (epoll_event*)EPOLLIN);
                    close(Events[i].data.fd);
                    cout << "Client disconnect..." << endl;
                }
                if(RecvSize > 0){
                    for(auto it = slaveSockets_.begin(); it != slaveSockets_.end(); it++)
                        if(it->first != Events[i].data.fd)
                            send(it->first, buffer, RecvSize, MSG_NOSIGNAL);
                    buffer_lock.lock();          
                    messageBufferForAnalyze_.push(message_info(RecvSize, buffer, slaveSockets_[Events[i].data.fd], string(ctime(&td))));
                    buffer_lock.unlock();
                }
            }
        }
    }
}

void server::algoStart(){
    unique_lock<mutex> bufferForAnalyzeLock(mutexOnMessageBufferForAnalyze_, defer_lock);
    unique_lock<mutex> bufferForDatabaseLock(mutexOnMessageBufferForDatabase_, defer_lock);
    while(true){
        bufferForAnalyzeLock.lock();
        if(messageBufferForAnalyze_.empty()){
            bufferForAnalyzeLock.unlock();
            this_thread::sleep_for(chrono::microseconds(100));
            continue;
        }
        message_info msgi = move(messageBufferForAnalyze_.front());
        messageBufferForAnalyze_.pop();
        bufferForAnalyzeLock.unlock();
        if(handler_.findWords(msgi)){
            bufferForDatabaseLock.lock();
            messageBufferForDatabase_.push(msgi);
            if(messageBufferForDatabase_.size() == 5){
                bufferForDatabaseLock.unlock();
                thread t(&server::writeDataInDatabase, this);
                t.detach();
            }
            else
                bufferForDatabaseLock.unlock();
        }
    }
}

int server::setNonblock(int fd){
    int flags;
#if defined (O_NONBLOCK)
    if(-1 == (flags = fcntl(fd, F_GETFL, 0)))
        flags = 0;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
    flags = 1;
    return ioctl(fd, FIOBIO, &flags);
#endif
}

inline int my_find(const vector<string>& v, const string& str){
    int counter = 0;
    for(; counter < v.size(); counter++)
        if(v[counter] == str){
            counter++;
            return counter;
           }
}

void server::writeDataInDatabase(){
    QSqlQuery query(database_);
    static size_t msgcount = 1;
    unique_lock<mutex> bufferForDatabaseLock(mutexOnMessageBufferForDatabase_, defer_lock);
    bufferForDatabaseLock.lock();
    int size = messageBufferForDatabase_.size();
    for(int i = 0; i < size; i++){
        message_info msgi = messageBufferForDatabase_.front();
        query.exec("insert into message_list values(null, '" + QString::fromUtf8(msgi.message_, msgi.messageSize_) +"'," + QString::number(msgi.whoSend_) + ", '" + QString::fromStdString(msgi.whenSend_) + "')");
        for(auto it = msgi.whatFound_.begin(); it != msgi.whatFound_.end(); ++it){
            int pos = my_find(handler_.patterns_, *it);
            query.exec("insert into search_result values(null, " + QString::number(pos) + ", " + QString::number(msgcount) + ")");
        }
        messageBufferForDatabase_.pop();
        msgcount++;
    }
    bufferForDatabaseLock.unlock();
    cout << "Write in database " << endl;
}

/* ========== Ниже описываются функции работы алгоритма ========== */
void algo::addString(const string& s){
    int vertex_number = 0;                                                // начинаем идти с корневой вершины
    for(int i = 0; i < s.size(); i++){                                    // идем по всем символам в добавляемой строке
        char ch = s[i] - 'a';                                             // нормируем букву по размеру массива
        if(bohr_[vertex_number].nextNode_[ch] == -1){                      // если перехода из вершины по символу в строке еще нет
            bohr_.push_back(node(s[i], vertex_number));                    // то создаем ребенка с отцом = текущая вершина
            bohr_[vertex_number].nextNode_[ch] = bohr_.size() - 1;          // делаем переход из текущего корня в созданную вершину
            vertex_number = bohr_.size() - 1;                              // перемещаемся во вновь созданную вершину
            if(i == s.size() - 1){                                        // если строка закончилась
                bohr_[vertex_number].flag_ = true;                          // то ставит в текущий узел символ того, что в этом узлу заканчивается строка
                bohr_[vertex_number].patternsNum_ = patterns_.size();       // обозначаем, какая поисковая строка заканчивается в текущей вершине
                break;
            }
        }
        else{                                                             // если переход по текущему символу в добавляемой строке имеется
            vertex_number = bohr_[vertex_number].nextNode_[ch];            // то производим переход
        }
    }
    patterns_.push_back(s);                                                // после того, как добавили строку в автомат
}
void algo::initialize(){
    bohr_.push_back(node('@', 0));                                // добавляем узел корень
    bohr_[0].shuffLink_ = 0;                                      // ссылка на самого себя
}
int algo::getSuffLink(int _from){
   int parent_num = bohr_[_from].parentNum_;                                       // получаем номер узла родителя
   int parent_suff_link = bohr_[parent_num].shuffLink_;                            // получаем суффиксную ссылку из родительского узла
   //  теперь двигаемся из суффиксной ссылки родительского узла
   if(bohr_[parent_suff_link].nextNode_[bohr_[_from].symNode_ - 'a'] == -1)         // если перехода по символу нет
       return bohr_[parent_suff_link].shuffLink_;                                  // то возвращаем суффиксную ссылку
   else
       return bohr_[parent_suff_link].nextNode_[bohr_[_from].symNode_ - 'a'];       //
}
void algo::createDepends(){ //определяем суффиксные ссылки
    for(int i = 1; i < bohr_.size(); i++){ //
        if(bohr_[i].parentNum_ == 0){ //
            bohr_[i].shuffLink_ = 0; //
            continue;
        }
        bohr_[i].shuffLink_ = getSuffLink(i); //
        if(bohr_[bohr_[i].shuffLink_].flag_ == true) //
            bohr_[i].shuffFlink_ = bohr_[i].shuffLink_; //
    }
}
bool algo::findWords(message_info& msgi){
    bool exit_code = false;
    int vertex_num = 0; //
    for(int i = 0; i < msgi.messageSize_; i++){ //
        char ch = msgi.message_[i] - 'a';
        if(ch < 0 || ch > 26) //
            continue;
        if(bohr_[vertex_num].nextNode_[ch] == -1){ //
            if(bohr_[vertex_num].shuffLink_ == bohr_[vertex_num].shuffFlink_){ //
                string s = patterns_[bohr_[vertex_num].patternsNum_];
                msgi.whatFound_.insert(move(s));
                vertex_num = 0;
                exit_code = true;
                continue;
            }
            vertex_num = bohr_[vertex_num].shuffLink_; //
        }
        else
            vertex_num = bohr_[vertex_num].nextNode_[ch]; //
        if(bohr_[vertex_num].flag_){ //
            string s = patterns_[bohr_[vertex_num].patternsNum_];
            msgi.whatFound_.insert(move(s));
            exit_code = true;
            vertex_num = 0;
            continue;
        }
    }
    return exit_code;
}

