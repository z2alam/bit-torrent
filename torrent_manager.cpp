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

  // get the peers list cache.
  updatePeersInfo();

  // init connect thread info
  for (int i=0; i < DOWNLOAD_LIMIT; ++i) {
    mConnThreadInfo.emplace_back(mNumPeers, mThPool, mFileMan, &mPeerList);
  }

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

vector<string> TorrentManager::split(string str, char delimiter) {
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

void TorrentManager::printHelper() {
  printf("\n********************************\n");
  printf("*********Commands List**********\n");
  printf("********************************\n");
  printf("1- Download file: 'd' <filename>\n");
  printf("   e.g: d samplefile.txt\n");
  printf("2- Get Files list: 'g'\n");
  printf("3- Quit torrent application: 'q'\n");
  printf("4- Print files status: 's'\n");
  printf("5- Remove local file: r <filename>\n");
  printf("********************************\n\n");
}

void TorrentManager::startDownload(string file_name) {
  int idx = -1;
  // find out which idx is free.
  for (int i = 0; i < DOWNLOAD_LIMIT; ++i) {
    pthread_mutex_lock(&mConnThreadInfo[i].mutex);
    bool status = mConnThreadInfo[i].status;
    pthread_mutex_unlock(&mConnThreadInfo[i].mutex);

    if (status == false) {
      idx = i;
      break;
    }
  }

  if (idx == -1) {
    printf("ERROR: Reached downloading limit: 4 files\n");
    return;
  }

  printf("Downloading file: %s\n", file_name.c_str());
  mConnThreadInfo[idx].file_name = &file_name[0];

  if (pthread_create(&mConnThreadInfo[idx].thread, NULL, &downloadFileThread,
                     &mConnThreadInfo[idx])) {
    printf("ERROR: Connect thread creation failed\n");
    mConnThreadInfo[idx].status = false;
    return;
  }
  mConnThreadInfo[idx].status = true;
}

bool TorrentManager::run()
{
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

  // look up all the files in the file_manager, and trigger
  // startDownload for each incomplete file
  vector<string> incomplete_files = mFileMan->getIncompleteFileNames();
  for (string file : incomplete_files) {
    startDownload(file);
  }

  printHelper();

  // Start reading terminal for user input
  printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
  printf("Reading user input from terminal\n\n");
  string input;
  while (true) {
    printf("Enter command: (Type 'h' for help)\n");
    getline(std::cin >> std::ws, input);

    if (input[0] == 'h') {
      printHelper();
    }
    else if (input[0] == 'd') {
      string file_name = input.substr(2);

      if (file_name.size() == 0) {
        printf("In valid argument - No file name provided\n");
        continue;
      }
      else if (mFileMan->fileExists(file_name) != -1) {
        // file exists - no need to download
        printf("File:%s already exists!!\n", file_name.c_str());
        continue;
      }
      else {
        startDownload(file_name);
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

      for (int i=0 ; i < DOWNLOAD_LIMIT; ++i) {
        pthread_mutex_lock(&mConnThreadInfo[i].mutex);
        bool status = mConnThreadInfo[i].status;
        pthread_mutex_unlock(&mConnThreadInfo[i].mutex);

        if (status == true) { // active thread
          pthread_cancel(mConnThreadInfo[i].thread);
          pthread_join(mConnThreadInfo[i].thread, NULL);
          closeAllSockets(mConnThreadInfo[i].server_sockets);
        }
      }

      printf("... Exiting Torrent application\n");
      break;
    }
    else if (input[0] == 'r') {
        string file_name = input.substr(2);
        printf("Removing File:%s ...\n", file_name.c_str());

        if (file_name.size() == 0) {
          printf("In valid argument - No file name provided\n");
          continue;
        }
        else {
          int result = mFileMan->removeLocalFile(file_name);

          if (result == 0){
            printf("Successfully removed File:%s.\n", file_name.c_str());
          }
          else if (result == -1){
            printf("ERROR - Failed to remove the File:%s.\n", file_name.c_str());
          }
          else if (result == -2){
            printf("ERROR - File:%s does not exist\n", file_name.c_str());
          }
        }
      }
    sleep(1);
  }
  return true;
}

#define TIMEOUT 20
bool TimedReadFromSocket(int socket, char* buffer, int size, int count) {
  while (count--) {
    if (read(socket, buffer, size) > 0)
      return true;
  }
  return false;
}

bool TimedWriteFromSocket(int socket, char* buffer, int size, int count) {
  while (count--) {
    if (write(socket, buffer, size) > 0)
      return true;
  }
  return false;
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
    if (!TimedReadFromSocket(_cliSocket[_numClients], msgBuffer, 19, 2)) {
      _cliBusy[_numClients] = false;
      continue;
    }

    sprintf(file_name, "%s", msgBuffer);
    printf("%s: file requested:%s\n", __func__, file_name);

    // if file doesn't exist, then notify the requestor with a "Y" (yes) or "N" (no)
    if ((_fileIds[_numClients] = fileMan->fileExists(file_name)) == -1) {
      sprintf(msgBuffer, "N");
      TimedWriteFromSocket(_cliSocket[_numClients], msgBuffer, 19, 2);
      printf("%s: File does not exist.\n", __func__);
      _cliBusy[_numClients] = false;
      continue;
    }

    printf("%s: File exists - starting sending...\n", __func__);

    // write msg and then read back from socket
    sprintf(msgBuffer, "Y");
    if ((TimedWriteFromSocket(_cliSocket[_numClients], msgBuffer, 19, 2) == false) ||
        (TimedReadFromSocket(_cliSocket[_numClients], msgBuffer, 19, 2) == false)) {
      _cliBusy[_numClients] = false;
      continue;
    }

    if (msgBuffer[0] == 'I') {
      int _fileSize = fileMan->getFileSize(_fileIds[_numClients]);
      sprintf(msgBuffer, "%d", _fileSize);
      printf("%s: fileSize is :%d\n", __func__, _fileSize);

      // notify the requestor the file_size, and get a go ahead from it
      // to start sending file.
      if ((TimedWriteFromSocket(_cliSocket[_numClients], msgBuffer, 19, 2) == false) ||
          (TimedReadFromSocket(_cliSocket[_numClients], msgBuffer, 19, 2) == false)) {
        _cliBusy[_numClients] = false;
        continue;
      }
    }

    if (msgBuffer[0] == 'E') { // This peer is not needed by receiver
      _cliBusy[_numClients] = false;
      continue;
    }

    // Obtain a connect thread
    if ((_threadIds[_numClients] = threadPool->getConnectThread(_threads[_numClients])) == -1) {
      // TODO: Fail entire data transfer when one thread fails
      printf("%s: ERROR: Not enough resources for accept"
             " thread\n", __func__);
      goto error;
    }

    LowLevelThreadParams threadParams = { threadPool, fileMan, _numClients,
                                          _cliSocket[_numClients], _threadIds[_numClients],
                                          _fileIds[_numClients], _statLogs, 0, 0,
                                          &_cliBusy[_numClients], exit_mutex };

    printf("Creating thread for sending chunks\n");
    if (pthread_create(_threads[_numClients], NULL, &sendData_v2, &threadParams)) {
      printf("ERROR: Receive thread:%d creation failed\n",
              _threadIds[_numClients]);
      goto error;
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

  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

  while (1) {
    if (!TimedReadFromSocket(_socket, &msgBuffer[0], 19, 10)) {
        break;
    }

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

    // read chunk from file
    _fileMan->fileRead(_fileId, _start, _size, _buffer);

    // reading data from the peer
    if (!TimedWriteFromSocket(_socket, &_buffer[0], _size, 10)) {
        break;
    }

    /*
      sprintf(tmpStat, "ON:true, thread:%d, fileId:%d, chunk-sent:%d\n",
      _threadId, _fileId, _idx+1);
      pthread_mutex_lock(params->mutex);
      logsUpdate(_threadIdx, tmpStat, _statLogs);
      pthread_mutex_unlock(params->mutex);
    */

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

vector<int> TorrentManager::getContributingPeersList(vector<PeerInfo> &peers_list,
                                                     unordered_set<int> &contributing_peers,
                                                     vector<sockaddr_in> &client_addrs,
                                                     vector<int> *server_sockets,
                                                     string file_name,
                                                     pthread_mutex_t *mutex)
{
  char msg_buffer[20];
  vector<int> new_peers;

  for (uint32_t i = 0; i < peers_list.size(); ++i) {
    // check if this peer is already contributing.
    pthread_mutex_lock(mutex);
    bool peer_in_use = (contributing_peers.find(i) != contributing_peers.end());
    pthread_mutex_unlock(mutex);

    if (peer_in_use)
      continue;

    int server_socket;
    while ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0);

    hostent *server = gethostbyname(peers_list[i].ip.c_str());
    bzero((char*)&client_addrs[i], sizeof(client_addrs[i]));
    client_addrs[i].sin_family = AF_INET;
    bcopy((char*)server->h_addr, (char*)&client_addrs[i].sin_addr.s_addr, server->h_length);
    client_addrs[i].sin_port = htons(peers_list[i].port);

    int tries = 0;
    while (tries++ < 10) {
      if (connect(server_socket, (struct sockaddr*) &client_addrs[i],
                  sizeof(client_addrs[i])) >= 0) {
        printf("%s: Connected to: %s\n", __func__, peers_list[i].ip.c_str());
        break;
      }
    }

    if (tries >= 10) {
      close(server_socket);
      continue;
    }

    sprintf(msg_buffer, "%s", file_name.c_str());
    if (!TimedWriteFromSocket(server_socket, &msg_buffer[0], 19, 2)) {
      close(server_socket);
      continue;
    }

    if (!TimedReadFromSocket(server_socket, &msg_buffer[0], 19, 2) ||
        msg_buffer[0] == 'N') { // timeout or file doesn't exist
      close(server_socket);
      continue;
    }

    new_peers.emplace_back(i);

    // update server socket for the peer, and mark the peer as actively
    // contributing
    pthread_mutex_lock(mutex);
    (*server_sockets)[i] = server_socket;
    contributing_peers.insert(i);
    pthread_mutex_unlock(mutex);
  }
  return new_peers;
}

FileInfo TorrentManager::getFileInfoFromPeer(vector<int> &active_peers, string file_name,
                                             vector<sockaddr_in> &client_addrs,
                                             vector<int> &server_sockets,
                                             unordered_set<int> &contributing_peers,
                                             pthread_mutex_t *mutex)
{
  FileInfo file_info;
  char msg_buffer[20];

  for (int i: active_peers) {
    msg_buffer[0] = 'I';

    if ((TimedWriteFromSocket(server_sockets[i], &msg_buffer[0], 19, 2) == false) ||
        (TimedReadFromSocket(server_sockets[i], &msg_buffer[0], 19, 2) == false)) {
        // On timeout, close corresponding sockets, and erase the peer
        // from the contributing peers list
      downloadThreadCleanResources(i, server_sockets, mutex, contributing_peers);
      continue;
    }

    file_info.fileName = file_name;
    file_info.file_size = atoi(msg_buffer);
    file_info.totChunksPerFile = (file_info.file_size / CHUNK_SIZE) +
      (((file_info.file_size % CHUNK_SIZE) == 0) ? 0 : 1);

    printf("%s: fileSize:%d and totchunks:%d\n", __func__, file_info.file_size,
           file_info.totChunksPerFile);
    break;
  }
  return file_info;
}

void TorrentManager::downloadThreadCleanResources(int idx, vector<int> &server_sockets,
                                                  pthread_mutex_t *mutex,
                                                  unordered_set<int> &peers)
{
  pthread_mutex_lock(mutex);

  close(server_sockets[idx]);
  server_sockets[idx] = -1;
  peers.erase(idx);

  pthread_mutex_unlock(mutex);
}

void TorrentManager::createThreadsToReceiveFile(vector<int> &active_peers,
                                                FileInfo file_info,
                                                vector<sockaddr_in> &client_addrs,
                                                unordered_set<int> &contributing_peers,
                                                DownloadThreadData *params,
                                                vector<int> &receiver_thread_ids,
                                                vector<ThreadParams_L2> &rec_thread_params)
{
  ThreadPool *thread_pool = (ThreadPool*) params->thread_pool;
  FileManager *file_manager = (FileManager*) params->file_manager;
  vector<int> &server_sockets = params->server_sockets;
  pthread_mutex_t *params_mutex = &params->mutex;
  vector<pthread_t*> &threads = params->threads;
  char msg_buffer[20];

  // introduced to handle the case when chunks_needed < available_peers
  // then no need to spawn extra threads.
  int threads_needed = file_manager->chunksRemaining(file_info.fileId);

  for (int i: active_peers) {
    // check if this peer is active.
    pthread_mutex_lock(params_mutex);
    bool peer_not_active = (contributing_peers.find(i) == contributing_peers.end());
    pthread_mutex_unlock(params_mutex);

    if (peer_not_active)
      continue;

    // indicate the peer to exit if it's no longer needed
    if (threads_needed-- <= 0) {
      msg_buffer[0] = 'E';
      TimedWriteFromSocket(server_sockets[i], &msg_buffer[0], 19, 2);
      downloadThreadCleanResources(i, server_sockets, params_mutex, contributing_peers);
    } else {
      msg_buffer[0] = 'S'; // indicate peer to start sending chunks

      if (!TimedWriteFromSocket(server_sockets[i], &msg_buffer[0], 19, 2)) {
        downloadThreadCleanResources(i, server_sockets, params_mutex, contributing_peers);
        continue;
      }

      // Obtain a connect thread
      if ((receiver_thread_ids[i] = thread_pool->getAcceptThread(threads[i])) == -1) {
        printf("%s: ERROR: Not enough resources for download thread\n", __func__);
        downloadThreadCleanResources(i, server_sockets, params_mutex, contributing_peers);
        continue;
      }

      // setup params for a receive thread.
      rec_thread_params[i] = { params, receiver_thread_ids[i], i, server_sockets[i],
                               file_info };

      if (pthread_create(threads[i], NULL, &receiveData_v2, &rec_thread_params[i])) {
        printf("ERROR: Receive thread:%d creation failed\n", receiver_thread_ids[i]);
        downloadThreadCleanResources(i, server_sockets, params_mutex, contributing_peers);
        continue;
      }

      printf("%s: thread:%p created for peer:%d\n", __func__, threads[i], i);
    }
  }
}

void* TorrentManager::downloadFileThread(void* args)
{
  DownloadThreadData* params = (DownloadThreadData*) args;
  ThreadPool* thread_pool = (ThreadPool*) params->thread_pool;
  FileManager* file_man = (FileManager*) params->file_manager;

  pthread_mutex_t* params_mutex = &params->mutex;
  string file_name = string(params->file_name);
  vector<PeerInfo> peers_list = *(params->peers_list);

  int num_peers = peers_list.size();
  vector<sockaddr_in> client_addrs(num_peers);
  vector<int> *server_sockets = &params->server_sockets;
  vector<ThreadParams_L2> receiver_thread_params(num_peers);

  int download_start_time = gettimeofdayMs();
  int download_end_time = 0;

  unordered_set<int> contributing_peers;
  int wait_time_sec = 0;
  vector<int> receiver_thread_ids(num_peers);
  int file_id = file_man->fileExists(file_name);
  FileInfo file_info = file_man->getFileInfoById(file_id);

  //printf("total_chunks_existing:%d, next_idx:%d\n",
  //        file_man->mFileInfo[file_id].totChunksExisting,
  //        file_man->mFileInfo[file_id].next_idx_stack.top());
  printf("%s: Created thread for file transfer: %s\n", __func__, file_name.c_str());

  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
  while (!file_man->isFileDownloaded(file_info.fileId)) {
    // Get a list of indices of peers from peers_list vector that agree
    // in downloading of the file
    vector<int> new_peers = getContributingPeersList(peers_list, contributing_peers,
                                                     client_addrs, server_sockets,
                                                     file_name, params_mutex);

    // wait for some time, and retry connecting more peers
    if (new_peers.size() == 0) {
      if (file_info.fileId == -1) {
        printf("%s: Poor you - no one has this file. But we will attempt to look for\n"
               "some time before giving up on the download!\n", __func__);
        file_info.fileId = -2;
      }

      sleep(++wait_time_sec);
      if (wait_time_sec == 20) {
        printf("%s: Exiting thread. File download: %s is incomplete\n", __func__,
               file_name.c_str());
        break;
      }
      continue;
    }

    // if there is no file info, then get it from one of the peers
    if (file_info.fileId < 0) {
      file_info = getFileInfoFromPeer(new_peers, file_name, client_addrs, *server_sockets,
                                      contributing_peers, params_mutex);

      // add file to cache to indicate start of download.
      if (file_info.file_size != 0) {
        if (!file_man->addFileToCache(file_info)) {
          printf("%s: Failed to add file to the cache before download\n", __func__);
          break;
        }
      } else { // if size is zero, then it means all peers dropped
        continue;
      }
    }

    // Create a thread for each new peer to contribute in file download
    createThreadsToReceiveFile(new_peers, file_info, client_addrs,
                               contributing_peers, params, receiver_thread_ids,
                               receiver_thread_params);

    sleep(++wait_time_sec);
    if (wait_time_sec == 20)
      break;
  }
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

  // At this point, the file download is complete, and threads can exit.
  // So we can add a barrier here.
  pthread_mutex_lock(params_mutex);
  vector<pthread_t*> &receiver_threads = params->threads;
  pthread_mutex_unlock(params_mutex);

  for (int i = 0; i < num_peers; ++i) {
    if (receiver_threads[i] != NULL) {
      printf("%s: waiting for thread:%d to finish\n", __func__, i);
      pthread_join(*receiver_threads[i], NULL);
      thread_pool->freeThread(receiver_thread_ids[i], true);
      printf("%s: thread:%d released\n", __func__, i);
    }
  }

  int total_chunks = (file_info.file_size / CHUNK_SIZE) +
    (((file_info.file_size % CHUNK_SIZE) == 0) ? 0 : 1);

  if (!file_man->addFileToDisk(file_info.fileId)) {
    printf("%s: ERROR: Failed to add file:%d into disc\n", __func__,
           file_info.fileId);
  }

  download_end_time = gettimeofdayMs();
  printf("\n\n*******END OF FILE DATA TRANSFER STATUS *********\n");
  printf("%s: File transfered successfully [time: %dms]\n", __func__,
         (download_end_time - download_start_time));
  printf("%s: File name:%s .. Size:%d .. Chunks:%d\n", __func__,
         file_name.c_str(), file_info.file_size, total_chunks);
  printf("**************************************************\n\n\n");

  // close all sockets
  pthread_mutex_lock(params_mutex);
  closeAllSockets(*server_sockets);
  params->status = false;
  pthread_mutex_unlock(params_mutex);

  pthread_exit(0);
}

void TorrentManager::closeAllSockets(vector<int> &server_sockets) {
    for (uint32_t i = 0; i < server_sockets.size(); ++i) {
        close(server_sockets[i]);
        server_sockets[i] = -1;
    }
}

void* TorrentManager::receiveData_v2(void* arg)
{
  ThreadParams_L2 *params = (ThreadParams_L2*) arg;
  FileManager* _fileMan = (FileManager*) params->params_L1->file_manager;
  int _socket = params->socket;
  int _threadId = params->thread_id;
  int _fileId = params->file_info.fileId;
  int _totSize = params->file_info.file_size;
  int _totChunks = (_totSize / CHUNK_SIZE) + (((_totSize % CHUNK_SIZE) == 0) ? 0 : 1);
  int _idx = 0;

  // for jumping to latestProcessQueue
  int _chunkSize = 0;
  int _chunkStart = 0;

  char _buffer[CHUNK_SIZE];
  char msgBuffer[20];
  int _chunksReceived = 0;

  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

  _idx = _fileMan->updateChunks(_fileId, -1);
  sprintf(msgBuffer, "%d", _idx);
  if (!TimedWriteFromSocket(_socket, &msgBuffer[0], 19, 10)) {
    _fileMan->markChunkFailure(_fileId, _idx);
  } else {
    // run the thread until chunk queue is empty
    while (1) {
        // calculating the chunk size of ith Chunk
        _chunkStart = _idx * CHUNK_SIZE;
        _chunkSize = (_idx == _totChunks-1) ? _totSize - _chunkStart : CHUNK_SIZE;
        //DEBUG_PRINT("%s: Idx:%d.. totChunks:%d.. totSize:%d.. chunkStart:%d"
        //        ".. chunkSize:%d\n", __func__, _idx, _totChunks, _totSize,
        //        _chunkStart, _chunkSize);

        if (!TimedReadFromSocket(_socket, &_buffer[0], _chunkSize, 10)) {
          _fileMan->markChunkFailure(_fileId, _idx);
          break;
        }

        // write chunk to file
        _fileMan->fileWrite(_fileId, _chunkStart, _chunkSize, _buffer);

        _idx = _fileMan->updateChunks(_fileId, _idx);

        // notify other client to exit
        if (_idx == -1) {
          sprintf(msgBuffer, "e");
          TimedWriteFromSocket(_socket, &msgBuffer[0], 19, 10);
          break;
        } else {
          sprintf(msgBuffer, "%d", _idx);
          if (!TimedWriteFromSocket(_socket, &msgBuffer[0], 19, 10)) {
            _fileMan->markChunkFailure(_fileId, _idx);
            break;
          }
        }

        DEBUG_PRINT("%s: Chunk-Received:%d, start:%d, size:%d\n", __func__, _idx,
                    _chunkStart, _chunkSize);
        _chunksReceived++;
    }
  }

  printf("%s: Thread:%d exiting, chunks-received:%d\n", __func__, _threadId,
         _chunksReceived);

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

