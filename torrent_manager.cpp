/*
 * BIT-TORRENT PROJECT - ECE1747H (PARALLEL PROGRAMMING)
 * ZOHAIB ALAM 	(997093318)
 * HATIF SATTAR (997063387)
 */

/*
 * ACKNOWLEDGEMENT:
 * 1- To get the IP address of Linux machine, obtained reference from:
 *    http://man7.org/linux/man-pages/man3/getifaddrs.3.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <sys/types.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <sstream>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/time.h>

#include <linux/if_link.h>

#include "thread_pool.h"
#include "status_manager.h"
#include "file_manager.h"
#include "torrent_manager.h"

#define DEBUG 1
#ifdef DEBUG
    #define DEBUG_PRINT printf
#else
    #define DEBUG_PRINT
#endif

#define STATIC_LOAD_BALANCING false

inline int32_t gettimeofdayMs()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    return (tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

static bool logsUpdate(int idx, char* str, char* input)
{
    int count = 0;
    char* line;
    char tmp[512];
    char* t2 = &tmp[0];

    line = strtok (input, "\n");

    while (line != NULL){
        if (count == idx){
            strcpy(t2, str);
            t2 += strlen(str);
        }
        else{
            strcpy(t2, line);
            t2 += strlen(line);
        }
        *t2 = '\n';
        t2++;

        line = strtok (NULL, "\n");
        count++;
    }

    if (idx > count) {
        printf ("[logsUpdate] ERROR index %d greater than entries"
                " in string %d\n", idx, count);
        return false;
    }
    strcpy (input, tmp);

    return true;
}

TorrentManager::TorrentManager() : mNumPeers(0), mExitApp(false)
{
    //constructor

    // intializing objects (first run)
    mThPool = new ThreadPool();
    mFileMan = new FileManager();
    mStatMan = new StatusManager();

    // init accept thread info
    mAccThreadInfo.status = false;
    pthread_mutex_init(&mAccThreadInfo.mutex, NULL);

    // init connect thread info
    for (int i=0; i < DOWNLOAD_LIMIT; ++i) {
        mConnThreadInfo[i].status = false;
        pthread_mutex_init(&mConnThreadInfo[i].mutex, NULL);
    }

    // get the peers list cache.
    updatePeersInfo();

    // initializing main mutex
    pthread_mutex_init( &mMutex, NULL);
}

TorrentManager::~TorrentManager()
{
    //destructor

    // Free peers list
    mPeerList.clear();

    if (mThPool)
        delete mThPool;
    if (mFileMan)
        delete mFileMan;
    if (mStatMan)
        delete mStatMan;

    // init accept thread info
    pthread_mutex_destroy(&mAccThreadInfo.mutex);

    // init connect thread info
    for (int i=0; i < DOWNLOAD_LIMIT; ++i)
        pthread_mutex_destroy(&mConnThreadInfo[i].mutex);

    // destroying main mutex
    pthread_mutex_destroy(&mMutex);
}

vector<string> split(string str, char delimiter) {
  vector<string> internal;
  stringstream ss(str); // Turn the string into a stream.
  string tok;

  while(getline(ss, tok, delimiter)) {
    internal.push_back(tok);
  }
  return internal;
}

bool TorrentManager::updatePeersInfo() {
    // Get the self IP.
    getIPAddress_v2();
    printf("\nSelf IP address:%s, Default Port:%d\n\n", mIP.c_str(), DEF_PORT);

    // reading the peerfile 
    ifstream file(LOCAL_PEERS_INFO);
    string str;

    // read the num of peers
    if (getline(file, str))
        mNumPeers = atoi(str.c_str());

    // allocate based on the numPeers
    mPeerList.clear();

    int i = 0;
    while (getline(file, str)) {
        if (i == mNumPeers) {
            printf("ERROR: More peer-info then numPeers\n");
            return false;
        }

        vector<string> peer = split(str, ',');

        if (mIP.compare(peer[0]) == 0) {
            // Don't include this peer in the list
            mNumPeers--;
        } else {
            mPeerList.emplace_back(peer[0], atoi(peer[1].c_str()));
            printf("IP:%s, ", mPeerList[i].ip.c_str());
            printf("Port:%d\n", mPeerList[i].port);
            ++i;
        }
    }
    printf("Num of peers:%d\n\n", mNumPeers);

    file.close();
    return true;
}

bool TorrentManager::run()
{
    int numFilesDown = 0; // num of files being downloaded
    int acceptSocket = 0;

    bool _static = STATIC_LOAD_BALANCING;

    ThreadParams threadParams = { mThPool, mFileMan, mStatMan,
            &mExitApp, &mMutex, NULL, NULL, &mNumPeers, &mPeerList,
            &mAccThreadInfo, &acceptSocket, _static};

    // Start accept thread
    if(pthread_create(&mAccThreadInfo.thread, NULL, &acceptConn, &threadParams)) {

        printf("ERROR: Accept thread creation failed\n");
        return false;
    }

    // Start reading terminal for user input
    printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
    printf("Reading user input from terminal\n\n");
    char input[20];
    while (true) {

        printf("Enter command: (Type 'h' for help)\n");
        fgets(input, 20, stdin);

        if (input[0] == 'h') {
            printf("\n********************************\n");
            printf("*********Commands List**********\n");
            printf("********************************\n");
            printf("1- Download file: 'd' <filename>\n");
            printf("   e.g: d samplefile.txt\n");
            printf("2- Get Files list: 'g'\n");
            printf("3- Quit torrent application: 'q'\n");
            printf("4- Print files status: 's'\n");
            printf("********************************\n\n");
        }
        else if (input[0] == 'd') {

            char file_name[FILE_PATH_SIZE];
            strncpy(file_name, input+2, 18);
            file_name[strlen(file_name)-1] = '\0';

            if (strlen(file_name) == 0) {
                printf("In valid argument - No file name provided\n");
                continue;
            }
            else if (mFileMan->fileExists(file_name) != -1) {
                // file exists - no need to download
                printf("File:%s already exists!!\n", file_name);
                continue;
            }
            else {

                pthread_mutex_lock( &mMutex );
                    if (numFilesDown == DOWNLOAD_LIMIT) {
                        pthread_mutex_unlock( &mMutex );
                        printf("ERROR: Reached downloading limit: 4 files\n");
                        continue;
                    }
                pthread_mutex_unlock( &mMutex );

                int idx = -1;
                // find out which idx is free.
                for (int i = 0; i < DOWNLOAD_LIMIT; ++i) {
                    pthread_mutex_lock( &mConnThreadInfo[i].mutex );
                        if (mConnThreadInfo[i].status == false) {
                            mConnThreadInfo[idx].status = true;
                            pthread_mutex_unlock( &mConnThreadInfo[i].mutex );
                            idx = i;
                            break;
                        }
                    pthread_mutex_unlock( &mMutex );
                }

                printf("Downloading file: %s\n", file_name);
                threadParams.fileName = &file_name[0]; 
                threadParams.threadStack = &numFilesDown;
                threadParams.threadInfo = &mConnThreadInfo[idx];


                if(pthread_create(&mConnThreadInfo[idx].thread, NULL,
                            &connectPeers, &threadParams)) {
                    printf("ERROR: Connect thread creation failed\n");
                    mConnThreadInfo[idx].status = false;
                    continue;
                }
                pthread_mutex_lock( &mMutex );
                    numFilesDown++;
                pthread_mutex_unlock( &mMutex );
            }
        }
        else if (input[0] == 's') {
            mFileMan->printFilesList();
            continue;
        }
        else if (input[0] == 'q') {
            pthread_mutex_lock( &mMutex );
                mExitApp = true;
            pthread_mutex_unlock( &mMutex );

            pthread_cancel( mAccThreadInfo.thread );

            // Closing the server socket for accept thread
            if (close(acceptSocket))
                printf("... Closed socket for Accept thread\n");

            pthread_join( mAccThreadInfo.thread, NULL );
            for (int i=0 ; i < numFilesDown; ++i)
                pthread_join( mConnThreadInfo[i].thread, NULL );

            printf("... Exiting Torrent application\n");
            break;
        }
    }

    return true;
}

#define MAX_CLIENT_SOCKETS 10
void* TorrentManager::acceptConn(void* arg)
{
    ThreadParams* params = (ThreadParams*)arg;
    ThreadPool* threadPool = (ThreadPool*) params->T_Pool;
    FileManager* fileMan = (FileManager*) params->F_Man;
    bool* exit = params->app_exit;
    pthread_mutex_t* exit_mutex = params->tParams_mutex;
    MainThreadData* threadInfo = params->threadInfo;

    int _cliSocket[MAX_CLIENT_SOCKETS];
    bool _cliBusy[MAX_CLIENT_SOCKETS];
    struct sockaddr_in cli_addr[MAX_CLIENT_SOCKETS];
    socklen_t clientLen;
    int _numClients = 0;
    int srvSocket = -1;
    struct sockaddr_in srv_addr;
    //char* _statLogs = &threadInfo->statLogs[0];
    char* _statLogs = (char*) calloc(512, sizeof(char));
    bool _exitThread = false;
    bool _static = params->staticLoad;

    char msgBuffer[20];

    char file_name[FILE_PATH_SIZE];
    int _fileIds[MAX_CLIENT_SOCKETS];

    pthread_t* _threads[MAX_CLIENT_SOCKETS];
    int _threadIds[MAX_CLIENT_SOCKETS];
    int _tmpBusy = false;
    char* tmpStat = _statLogs;

    // at start-up, none of the clients are busy
    memset(_cliBusy, 0x0, sizeof(_cliBusy));
    for (int i=0; i < MAX_CLIENT_SOCKETS; ++i)
        _cliSocket[i] = -1;

    // TODO: need to check whether need srvsocket for each
    // Set up the socket for accept connections
    while((srvSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0);
    bzero( (char*) &srv_addr, sizeof(srv_addr) );
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = INADDR_ANY;
    srv_addr.sin_port = htons(DEF_PORT);
    printf("Opened socket for Accept thread: %d\n", srvSocket);

    // socket passing for close later
    *params->accSocket = srvSocket;

    // Bind server socket
    while(bind(srvSocket, (struct sockaddr*)&srv_addr, sizeof(srv_addr)) < 0 );

    // Stage for listening connections.
    listen(srvSocket, 5);
    clientLen = sizeof(cli_addr[0]);

    for (int i=0; i < MAX_CLIENT_SOCKETS; ++i) {
        sprintf(tmpStat, "ON:false, thread:-1, fileId:-1, chunk-sent:0\n");
        tmpStat += strlen(tmpStat);
    }

    while (true) {
        pthread_mutex_lock(exit_mutex);
           _tmpBusy = _cliBusy[_numClients];
        pthread_mutex_unlock(exit_mutex);

        if (_tmpBusy)
            continue;

        pthread_mutex_lock(exit_mutex);
            _cliSocket[_numClients] = -1;
            _cliBusy[_numClients] = true;
        pthread_mutex_unlock(exit_mutex);

        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
            printf("%s: Waiting for new connection\n", __func__);
            while ((_cliSocket[_numClients] = accept(srvSocket,
                                                     (struct sockaddr *) &cli_addr[_numClients],
                                                     &clientLen)) < 0);
            printf("%s: Connection accepted from: %d\n", __func__, _cliSocket[_numClients]);
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

        // which file does client need?
        while (read(_cliSocket[_numClients], msgBuffer, 19) < 0);

        sprintf(file_name, "%s", msgBuffer);
        printf("%s: file requested:%s\n", __func__, file_name);

        if ((_fileIds[_numClients] = fileMan->fileExists(file_name)) == -1) {
            sprintf(msgBuffer, "N");
            while ( write(_cliSocket[_numClients], msgBuffer, 19) < 0 );
            printf("%s: File does not exist.\n", __func__);
            _cliBusy[_numClients] = false;
            continue;
        }

        printf("%s: File exists - starting sending...\n", __func__);
        sprintf(msgBuffer, "Y");
        while( write(_cliSocket[_numClients], msgBuffer, 19) < 0 );

        while( read(_cliSocket[_numClients], msgBuffer, 19) < 0);
        if (msgBuffer[0] == 'I') {

            int _fileSize = fileMan->getFileSize(_fileIds[_numClients]);
            sprintf(msgBuffer, "%d", _fileSize);
            printf("%s: fileSize is :%d\n", __func__, _fileSize);
            while( write(_cliSocket[_numClients], msgBuffer, 19) < 0 );

            // getting start signal
            while( read(_cliSocket[_numClients], msgBuffer, 19) < 0);
        }

        // Obtain a connect thread
        if ((_threadIds[_numClients] =
                    threadPool->getConnectThread(_threads[_numClients])) == -1) {

            // TODO: Fail entire data transfer when one thread fails
            printf("%s: ERROR: Not enough resources for accept"
                    " thread\n", __func__);
            goto error;
        }

        LowLevelThreadParams threadParams = { threadPool, fileMan, _numClients,
            _cliSocket[_numClients], _threadIds[_numClients],
            _fileIds[_numClients], _statLogs, 0, 0, &_cliBusy[_numClients], exit_mutex };


        if (_static) {
            if (pthread_create(_threads[_numClients], NULL, &sendData, &threadParams)) {

                printf("ERROR: Receive thread:%d creation failed\n",
                        _threadIds[_numClients]);
                goto error;
            }
        } else {
            printf("Creating thread for sending chunks\n");
            if (pthread_create(_threads[_numClients], NULL, &sendData_v2, &threadParams)) {

                printf("ERROR: Receive thread:%d creation failed\n",
                        _threadIds[_numClients]);
                goto error;
            }
        }

        // check whether to exit?
        pthread_mutex_lock( exit_mutex );
            _exitThread = *exit;
        pthread_mutex_unlock( exit_mutex );

        if (_exitThread) {
            pthread_mutex_lock( &threadInfo->mutex );
                threadInfo->status = false;
            pthread_mutex_unlock( &threadInfo->mutex );

            printf("... Exiting accepting thread\n");
            break;
        }

        _numClients++;
        if (_numClients == MAX_CLIENT_SOCKETS)
            _numClients = 0;
    }

error:
    if (_statLogs) free(_statLogs);
    pthread_exit(0);
}

void* TorrentManager::sendData_v2(void* arg)
{
    LowLevelThreadParams* params = (LowLevelThreadParams*)arg;
    FileManager* _fileMan = (FileManager*) params->F_Man;
    ThreadPool* _threadPool = (ThreadPool*) params->T_Pool;
    int _socket = params->socket;
    int _threadId = params->threadId;
    int _fileId = params->fileId;
    //char* _statLogs = params->statLogs;
    //int _threadIdx = params->idx;

    char _buffer[CHUNK_SIZE];
    //char tmpStat[128];
    char msgBuffer[20];
    int _size = 0;
    int _start = 0;
    int _idx = -1;

    int _chunksTransfered = 0;
    // obtain the picture of file locally..
    int _totSize = _fileMan->getFileSize(_fileId);
    int _totChunks = (_totSize / CHUNK_SIZE) + (((_totSize % CHUNK_SIZE) == 0) ? 0 : 1);

    while (1) {
        while (read(_socket, msgBuffer, 19) < 0);

        //printf("test: %s\n", msgBuffer);
        if (msgBuffer[0] == 'e') {
            //exit thread
            break;
        }

        // else retrieve idx
        _idx = atoi(msgBuffer);
        //DEBUG_PRINT("%s: Idx:%d.. totSize:%d\n", __func__, _idx, _totSize);

        // calculate size for that idx..
        _start = _idx * CHUNK_SIZE;
        _size = (_idx == _totChunks-1) ? _totSize - _start : CHUNK_SIZE;

        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

        // read chunk from file
        _fileMan->fileRead(_fileId, _start, _size, _buffer);

        // reading data from the peer
        while( write(_socket, _buffer, _size) < 0);

        /*
        sprintf(tmpStat, "ON:true, thread:%d, fileId:%d, chunk-sent:%d\n",
                _threadId, _fileId, _idx+1);
        pthread_mutex_lock(params->mutex);
            logsUpdate(_threadIdx, tmpStat, _statLogs);
        pthread_mutex_unlock(params->mutex);
        */

        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

        DEBUG_PRINT("%s: Chunk-Sent:%d, start:%d, size:%d\n", __func__, _idx,
                _start, _size);

        _chunksTransfered++;
    }

    pthread_mutex_lock(params->mutex);
        *params->busy = false;
        //sprintf(tmpStat, "ON:false, thread:-1, fileId:-1, chunk-sent:0\n");
        //logsUpdate(_idx, tmpStat, _statLogs);
    pthread_mutex_unlock(params->mutex);

    printf("%s: Thread:%d exiting, chunks-transfered:%d\n", __func__, _threadId,
            _chunksTransfered);

    close(_socket);
    _threadPool->freeThread(_threadId, false);
    pthread_exit(0);
}

void* TorrentManager::sendData(void* arg)
{
    LowLevelThreadParams* params = (LowLevelThreadParams*)arg;
    FileManager* _fileMan = (FileManager*) params->F_Man;
    ThreadPool* _threadPool = (ThreadPool*) params->T_Pool;
    int _socket = params->socket;
    int _threadId = params->threadId;
    int _fileId = params->fileId;
    char* _statLogs = params->statLogs;
    int _idx = params->idx;

    char _buffer[CHUNK_SIZE];
    char tmpStat[128];
    int _tmpStart = 0;
    int _tmpSize = CHUNK_SIZE;
    char msgBuffer[20];
    int _chunksNeeded = 0;
    int _size = 0;
    int _start = 0;

    // reading the chunks needed info
    while( read(_socket, msgBuffer, 19) < 0 );
    _start = atoi(msgBuffer);
    while( read(_socket, msgBuffer, 19) < 0 );
    _size = atoi(msgBuffer);

    _tmpStart = _start;
    _chunksNeeded = (_size / CHUNK_SIZE) +
                        (((_size % CHUNK_SIZE) == 0) ? 0 : 1);
    printf("%s: chunks needed:%d\n", __func__, _chunksNeeded);

    for (int i=0; i < _chunksNeeded; ++i) {

        _tmpSize = (i == (_chunksNeeded-1)) ? (_size - _tmpStart + _start) :
            CHUNK_SIZE;
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

        // read chunk from file
        _fileMan->fileRead(_fileId, _tmpStart, _tmpSize, _buffer);

        // reading data from the peer
        while( write(_socket, _buffer, _tmpSize) < 0 );
        while( read(_socket, msgBuffer, 19) < 0 );

        sprintf(tmpStat, "ON:true, thread:%d, fileId:%d, chunk-sent:%d\n",
                _threadId, _fileId, i+1);

        pthread_mutex_lock(params->mutex);
            logsUpdate(_idx, tmpStat, _statLogs);
        pthread_mutex_unlock(params->mutex);

        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

        DEBUG_PRINT("%s: Chunk-Sent:%d, start:%d, size:%d\n", __func__, i,
                _tmpStart, _tmpSize);
        _tmpStart += _tmpSize;
    }

    pthread_mutex_lock(params->mutex);
        *params->busy = false;
        sprintf(tmpStat, "ON:false, thread:-1, fileId:-1, chunk-sent:0\n");
        logsUpdate(_idx, tmpStat, _statLogs);
    pthread_mutex_unlock(params->mutex);

    printf("%s: Thread:%d exiting, chunks-transfered:%d\n", __func__, _threadId,
            _chunksNeeded);

    close(_socket);
    _threadPool->freeThread(_threadId, false);
    pthread_exit(0);
}

int GetRequiredThreadsNum(int file_size, int chunk_size) {
    return ((file_size/chunk_size) + 1);
}

void* TorrentManager::connectPeers(void* arg)
{
    ThreadParams* params = (ThreadParams*)arg;
    ThreadPool* threadPool = (ThreadPool*) params->T_Pool;
    FileManager* fileMan = (FileManager*) params->F_Man;
    pthread_mutex_t* mutex = params->tParams_mutex;
    char* file_name = params->fileName;
    vector<PeerInfo> _peersList = *(params->peerlist);
    MainThreadData* mainThreadInfo = params->threadInfo;
    bool _static = params->staticLoad;
    int err = -1;

    struct hostent* server;
    string _ip;
    int _port;
    int _nPeers;
    //char* _statLogs = &mainThreadInfo->statLogs[0];
    char* _statLogs = (char*) calloc(512, sizeof(char));
    char* tmpStat = _statLogs;
    int _fileTotChunks = 0;
    int _fileSize = 0;
    int _totChunks = 0;
    char msgBuffer[20];

    pthread_mutex_lock(mutex);
        _nPeers = *(params->numPeers);
    pthread_mutex_unlock(mutex);

    struct sockaddr_in cli_addr[_nPeers];
    int srvSocket[_nPeers];

    int _chunksPer = 0;
    int _startIdx = 0;
    int _size = 0;

    pthread_t** _threads = NULL;
    int* _threadIds = NULL;
    int _fileId = -1;

    // profiler variables
    int _timeStart = gettimeofdayMs();
    int _timeEnd = 0;

    // "last processed chunk" -- chunk status queue
    int _lpChunk = 0;
    pthread_mutex_t _chunkQueueMutex;

    int threads_needed;

    int _nPeersAvail = 0; // peers contributing in download
    // loop through the peers - see if any is available.
    for (int i = 0; i < _nPeers; ++i) {
        while((srvSocket[_nPeersAvail] = socket(AF_INET, SOCK_STREAM, 0)) < 0);

        pthread_mutex_lock(mutex);
            // getting the IPAddress and PortNo of the ith peer.
            _ip = _peersList[i].ip;
            _port = _peersList[i].port;
        pthread_mutex_unlock(mutex);

        server = gethostbyname(_ip.c_str());
        bzero((char*) &cli_addr[i], sizeof(cli_addr[i]));
        cli_addr[i].sin_family = AF_INET;
        bcopy((char*) server->h_addr, (char*) &cli_addr[i].sin_addr.s_addr, server->h_length);
        cli_addr[i].sin_port = htons(_port);

        // try to connect to peer 'i'
        int tries = 0;
        do {
            if (connect(srvSocket[_nPeersAvail], (struct sockaddr*) &cli_addr[i],
                        sizeof(cli_addr[i])) >= 0) {
                printf("%s: Connected to: %s\n", __func__, _ip.c_str());
                break;
            }
           ++tries;
        } while (tries < 10);

        if (tries == 10) {
            printf("%s: peer(%s) is not active [skipping]\n", __func__, _ip.c_str());
            continue;
        }

        // check if the peer has the file
        sprintf(msgBuffer, "%s", file_name);
        while (write(srvSocket[_nPeersAvail], msgBuffer, 19) < 0);
        while (read(srvSocket[_nPeersAvail], msgBuffer, 19) < 0);

        if (msgBuffer[0] == 'N') { // response "NO"
            printf("Peer:%s does not have the file\n", _ip.c_str());
            continue;
        }

        // get the total chunks of file from the first peer
        if (_fileTotChunks == 0) {

            msgBuffer[0] = 'I'; // Req file Size in bytes
            while (write(srvSocket[_nPeersAvail], msgBuffer, 19 ) < 0);
            while (read(srvSocket[_nPeersAvail], msgBuffer, 19) < 0);

            _fileSize = atoi(msgBuffer);
            _fileTotChunks = (_fileSize / CHUNK_SIZE) + (((_fileSize % CHUNK_SIZE) == 0) ? 0 : 1);
            printf("%s: fileSize:%d and totchunks:%d\n", __func__,
                    _fileSize, _fileTotChunks);
        }

        msgBuffer[0] = 'S'; // Start sending
        while (write(srvSocket[_nPeersAvail], msgBuffer, 19 ) < 0);

        // update status logs
        sprintf(tmpStat, "thread:-1, chunk-start-idx:-1, total-chunks:-1, "
                "chunk-received:0..........\n");
        tmpStat += strlen(tmpStat);
        _nPeersAvail++;
    }

    if (_nPeersAvail == 0) {
        printf("%s: Poor you- no one has this file :P\n", __func__);
        goto error;
    }

    if (_static) {
        // static load balancing
        _chunksPer = _fileTotChunks/_nPeersAvail;
        _startIdx = 0;
        _size = 0;
    } else {
        // add dynamic load balancing for performance.
        _lpChunk = 0;
        pthread_mutex_init(&_chunkQueueMutex, NULL);
    }

    // start thread for each peer.
    _threads = (pthread_t**) malloc(_nPeersAvail);
    _threadIds = (int*) malloc(_nPeersAvail*sizeof(int));
    _fileId = -1;

    threads_needed = GetRequiredThreadsNum(_fileSize, CHUNK_SIZE);
    for (int i=0; i < _nPeersAvail; ++i) {
        if (threads_needed-- == 0)
            break;

        // Obtain a connect thread 
        if ((_threadIds[i]= threadPool->getAcceptThread(_threads[i])) == -1) {

            // TODO: Fail entire data transfer when one thread fails
            printf("%s: ERROR: Not enough resources for connect"
                    " thread\n", __func__);
            goto error;
        }

        // TODO: Incorporate Status Manager

        // Get corresponding fileInfo
        if (_fileId == -1) {
            if ((_fileId = fileMan->addFileToCache(file_name, _fileSize)) == -1) {

                // TODO: Fail entire data transfer when one thread fails
                printf("%s: ERROR: Cannot add file to cache", __func__);
                goto error;
            }
        }

        if (_static) {
            // Spawn the thread for file transfer
            _startIdx = (CHUNK_SIZE) * (_chunksPer) * (i);
            _size = (i == (_nPeersAvail-1)) ? (_fileSize - _startIdx) :
                (_chunksPer * CHUNK_SIZE);

            printf("%s: thread:%d, chunksPer:%d, startidx:%d, size:%d\n", __func__,
                    i, _chunksPer, _startIdx, _size);
            LowLevelThreadParams threadParams = { threadPool, fileMan, i, srvSocket[i],
                    _threadIds[i], _fileId, _statLogs, _size, _startIdx, NULL, mutex };

            if (pthread_create(_threads[i], NULL, &receiveData, &threadParams)) {
                printf("ERROR: Receive thread:%d creation failed\n", _threadIds[i]);
                goto error;
            }
        } else { // dynamic
            LowLevelThreadParams_v2 threadParams = { threadPool, fileMan, i, srvSocket[i],
                    _threadIds[i], _fileId, _statLogs, NULL, mutex, &_lpChunk,
                    &_chunkQueueMutex, _fileSize };

            if (pthread_create(_threads[i], NULL, &receiveData_v2, &threadParams)) {
                printf("ERROR: Receive thread:%d creation failed\n", _threadIds[i]);
                goto error;
            }
        }
        printf("%s: thread:%p created for chunk (%d - %d) tranfer\n",
                __func__, _threads[i], _startIdx, _size);
    }

    threads_needed = GetRequiredThreadsNum(_fileSize, CHUNK_SIZE);
    for (int i=0 ; i < min(_nPeersAvail,threads_needed); ++i) {
        DEBUG_PRINT("%s: waiting for thread:%d to finish\n", __func__, i);
        pthread_join(*_threads[i], NULL);
        threadPool->freeThread(_threadIds[i], true);
        DEBUG_PRINT("%s: thread:%d released\n", __func__, i);
    }

    _timeEnd = gettimeofdayMs();
    _totChunks = (_fileSize / CHUNK_SIZE) + (((_fileSize % CHUNK_SIZE) == 0) ? 0 : 1);

    printf("\n\n*******END OF FILE DATA TRANSFER STATUS *********\n");
    printf("%s: File transfered successfully [time: %dms]\n", __func__,
          (_timeEnd - _timeStart));
    printf("%s: File name:%s .. Size:%d .. Chunks:%d\n", __func__,
            file_name, _fileSize, _totChunks);
    printf("%s: File Transfer Status:\nNumber of Peers:%d\n%s", __func__,
            _nPeersAvail, _statLogs);
    printf("**************************************************\n\n\n");

    if (!fileMan->addFileToDisk(_fileId)) {
        printf("%s: ERROR: Failed to add file:%d into disc\n", __func__, _fileId);
    }

error:
    if (_threads != NULL) free(_threads);
    if (_threadIds != NULL) free(_threadIds);
    if (_statLogs != NULL) free(_statLogs);

    pthread_mutex_destroy(&_chunkQueueMutex);

    pthread_mutex_lock( &mainThreadInfo->mutex );
        mainThreadInfo->status = false;
    pthread_mutex_unlock( &mainThreadInfo->mutex );

    // Closing the server socket for accept thread
    for (int i=0; i < _nPeersAvail; ++i) {
        err = close(srvSocket[i]);
        if (!err)
            printf("... Closed socket for receive thread:%d\n", i);
    }
    pthread_exit(0);
}

void* TorrentManager::receiveData_v2(void* arg)
{
    LowLevelThreadParams_v2* params = (LowLevelThreadParams_v2*)arg;
    FileManager* _fileMan = (FileManager*) params->F_Man;
    //ThreadPool* _threadPool = (ThreadPool*) params->T_Pool;
    int _socket = params->socket;
    int _threadId = params->threadId;
    int _fileId = params->fileId;
    char* _statLogs = params->statLogs;
    int _totSize = params->fileSize;
    int _threadIdx = params->idx;
    int _totChunks = (_totSize / CHUNK_SIZE) + (((_totSize % CHUNK_SIZE) == 0) ? 0 : 1);
    int _idx = 0;

    // for jumping to latestProcessQueue
    //int _latestProcessIdx = -1;
    int _chunkSize = 0;
    int _chunkStart = 0;
    //bool _exit = false;

    char _buffer[CHUNK_SIZE];
    char _tmpStat[128];
    char msgBuffer[20];

    int _chunksReceived = 0;

    _idx = _fileMan->updateChunks(_fileId);
    sprintf(msgBuffer, "%d", _idx);
    while( write(_socket, msgBuffer, 19) < 0);

    // run the thread until chunk queue is empty
    while (1) {

        // calculating the chunk size of ith Chunk
        _chunkStart = _idx * CHUNK_SIZE;
        _chunkSize = (_idx == _totChunks-1) ? _totSize - _chunkStart : CHUNK_SIZE;
        //DEBUG_PRINT("%s: Idx:%d.. totChunks:%d.. totSize:%d.. chunkStart:%d"
        //        ".. chunkSize:%d\n", __func__, _idx, _totChunks, _totSize,
        //        _chunkStart, _chunkSize);

        while (read(_socket, _buffer, _chunkSize) < 0);
        // write chunk to file
        _fileMan->fileWrite(_fileId, _chunkStart, _chunkSize, _buffer);

        _idx = _fileMan->updateChunks(_fileId);
        // notify other client to exit
        if (_idx >= _totChunks) {
            sprintf(msgBuffer, "e");
            while( write(_socket, msgBuffer, 19) < 0);
            break;
        } else {
            sprintf(msgBuffer, "%d", _idx);
            while( write(_socket, msgBuffer, 19) < 0);
        }

        DEBUG_PRINT("%s: Chunk-Received:%d, start:%d, size:%d\n", __func__, _idx,
                _chunkStart, _chunkSize);
        _chunksReceived++;
    }

    printf("%s: Thread:%d exiting, chunks-received:%d\n", __func__, _threadId,
                        _chunksReceived);

    sprintf(_tmpStat, "****Thread:%d, fileId:%d, chunk-sent:%d",
                    _threadId, _fileId, _chunksReceived);
    pthread_mutex_lock(params->mutex);
        logsUpdate(_threadIdx, _tmpStat, _statLogs);
    pthread_mutex_unlock(params->mutex);

    close(_socket);
    pthread_exit(0);
}

void* TorrentManager::receiveData(void* arg)
{
    LowLevelThreadParams* params = (LowLevelThreadParams*)arg;
    FileManager* _fileMan = (FileManager*) params->F_Man;
    //ThreadPool* _threadPool = (ThreadPool*) params->T_Pool;
    int _socket = params->socket;
    int _threadId = params->threadId;
    int _fileId = params->fileId;
    char* _statLogs = params->statLogs;
    int _size = params->size;
    int _start = params->start;
    int _idx = params->idx;

    int _chunksNeeded = (_size / CHUNK_SIZE) + (((_size % CHUNK_SIZE) == 0) ? 0 : 1);
    printf("%s: chunks needed:%d\n", __func__, _chunksNeeded);
    char _buffer[CHUNK_SIZE];
    char _tmpStat[128];
    int _tmpStart = _start;
    int _tmpSize = 0;
    char msgBuffer[20];
    //bool eof = false;

    // informing peer # of chunks required
    sprintf(msgBuffer, "%d", _start);
    while( write(_socket, msgBuffer, 19) < 0 );
    sprintf(msgBuffer, "%d", _size);
    while( write(_socket, msgBuffer, 19) < 0 );

    for (int i=0; i < _chunksNeeded; ++i) {

        _tmpSize = (i == (_chunksNeeded-1)) ? (_size - _tmpStart + _start) : CHUNK_SIZE;

        // reading data from the peer
        while( read(_socket, _buffer, _tmpSize) < 0 );

        // write chunk to file
        _fileMan->fileWrite(_fileId, _tmpStart, _tmpSize, _buffer);
        _fileMan->updateChunks(_fileId);

        // handshake..
        while( write(_socket, msgBuffer, 19) < 0 );

        DEBUG_PRINT("%s: Chunk-Received:%d, start:%d, size:%d\n", __func__, i,
                _tmpStart, _tmpSize);

        // update start and size
        _tmpStart += _tmpSize;
    }
    printf("%s: Thread:%d exiting, chunks-received:%d\n", __func__, _threadId,
                        _chunksNeeded);

    sprintf(_tmpStat, "****Thread:%d, fileId:%d, chunk-sent:%d",
                    _threadId, _fileId, _chunksNeeded);
    pthread_mutex_lock(params->mutex);
        logsUpdate(_idx, _tmpStat, _statLogs);
    pthread_mutex_unlock(params->mutex);

    close(_socket);
    pthread_exit(0);
}

bool TorrentManager::getIPAddress_v2()
{
    struct ifaddrs *ifaddr, *ifa;
    int family, s, n;
    char host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    /* Walk through linked list, maintaining head pointer so we
       can free list later */
    for (ifa = ifaddr, n = 0; ifa != NULL; ifa = ifa->ifa_next, n++) {
        if (ifa->ifa_addr == NULL)
            continue;

        family = ifa->ifa_addr->sa_family;

        /* Display interface name and family (including symbolic
           form of the latter for the common families) */

        if (family == AF_INET || family == AF_INET6) {
            s = getnameinfo(ifa->ifa_addr,
                    (family == AF_INET) ? sizeof(struct sockaddr_in) :
                    sizeof(struct sockaddr_in6),
                    host, NI_MAXHOST,
                    NULL, 0, NI_NUMERICHOST);
            if (s != 0) {
                printf("getnameinfo() failed: %s\n", gai_strerror(s));
                exit(EXIT_FAILURE);
            }

            // Store the IPAddress of the server
            if (strlen(host) >= 14) {
                mIP = string(host);
            }
        }
    }
    freeifaddrs(ifaddr);
    return true;
}

