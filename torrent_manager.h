// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~torrent_manager.h~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

/*
 * BIT-TORRENT PROJECT - ECE1747H (PARALLEL PROGRAMMING)
 * ZOHAIB ALAM 	(997093318)
 * HATIF SATTAR (997063387)
 */

#ifndef TORRENT_MANAGER_H
#define TORRENT_MANAGER_H

#include <string>
#include <vector>
#include <pthread.h>
#include <unordered_set>

#include "file_manager.h"

#define LOCAL_PEERS_INFO 	"peers_info.txt"
#define MAX_IP_SIZE 30
#define DEF_PORT 3000
#define DOWNLOAD_LIMIT 4  // 4 files

using namespace std;

// Forward declarations
class ThreadPool;
class FileManager;
class StatusManager;
struct ThreadInfo;

struct PeerInfo {
  string ip;
  int port;

PeerInfo(string ip_, int port_) : ip(ip_), port(port_) {}
};

struct MainThreadData {
  bool status;
  char statLogs[512];
  pthread_t thread;
  pthread_mutex_t mutex;
};

struct DownloadThreadData {
  bool status;
  pthread_t thread;
  pthread_mutex_t mutex;
  ThreadPool *thread_pool;
  FileManager *file_manager;
  char *file_name;
  int num_peers;
  vector<pthread_t*> threads;
  vector<int> server_sockets;
  vector<PeerInfo> *peers_list;

  DownloadThreadData(int num_peers_, char* file_name_, ThreadPool *thread_pool_,
                     FileManager *file_manager_, vector<PeerInfo> *peers_list_) :
  num_peers(num_peers_), file_name(file_name_), thread_pool(thread_pool_),
    file_manager(file_manager_), peers_list(peers_list_), status(true) {
    threads = vector<pthread_t*>(num_peers);
    server_sockets = vector<int>(num_peers);
  }
};

struct ThreadParams {
  ThreadPool* T_Pool;
  FileManager* F_Man;
  StatusManager* S_Man;
  bool* app_exit; // input
  pthread_mutex_t* tParams_mutex;
  char* fileName; // for connect thread only
  int* threadStack; // do minus 1 to indicate thread exit
  int* numPeers;
  vector<PeerInfo> *peerlist;
  MainThreadData* threadInfo;
  int* accSocket;
  bool staticLoad;
};

// Level-2 thread parameters
struct ThreadParams_L2 {
  DownloadThreadData *params_L1; // level 1 params
  int thread_id;
  int peer_idx;
  int socket;
  FileInfo file_info;
};

struct LowLevelThreadParams {
  ThreadPool* T_Pool;
  FileManager* F_Man;
  int idx;
  int socket;
  int threadId;
  int fileId;
  char* statLogs;
  int size;
  int start;
  bool* busy;
  pthread_mutex_t* mutex;
};

class TorrentManager {
 private:
  string mIP; // self IP
  int mNumPeers;
  vector<PeerInfo> mPeerList;

  // main mutex
  bool mExitApp;
  pthread_mutex_t mMutex;

  // utility objects
  ThreadPool* mThPool;
  FileManager* mFileMan;
  StatusManager* mStatMan;

  // accept and connect thread
  MainThreadData mAccThreadInfo;
  DownloadThreadData mConnThreadInfo[DOWNLOAD_LIMIT];

 private:
  bool updatePeersInfo();
  bool getIPAddress_v2();

 public:
  TorrentManager();
  ~TorrentManager();

  /*
   *	Main Thread:
   *	1- Cache all Peers Info into local memory.
   *			The file_format of peers_info.txt is as follows:
   *				<NUM_PEERS>
   *				<IP_ADDRESS>,<PORTNO>
   *				<IP_ADDRESS>,<PORTNO>
   *						:
   *				<IP_ADDRESS>,<PORTNO>
   *			A sample file is:
   *					2
   *					129.97.56.11,10057
   *					129.97.56.11,10057
   *
   *
   *	2- Initialize ThreadPool class
   *	3- Initialize FileManager class
   *	4- Initialize StatusManager class
   *	5- Create an accept master THREAD for accepting connection
   *     (FOR FILE SENDING). This thread will be hooked to
   *     acceptConn function
   *	6- Infinite Loop for "Read Terminal". Exits only if user
   *     enters 'q' (QUIT) command.
   *		a-	If the user enters 'd'(download'), obtain file_name
   *		    from user.
   *			i) 	If the fileExists - break.
   *			ii)	Else create a connect THREAD for connecting all
   *			    the peers (FOR FILE RECEIVING). This thread will
   *			    be hooked to connectPeers.
   *			    The thread will exit in case of error OR when the
   *			    file is received.
   */
  bool run();

  /* Utility function; given a delim-separated string and delimeter,
   * return a vector of string with delim-separated values (strings).
   */
  static vector<string> split(string str, char delim);

 private:
  // Print Helper function with sample commands
  void printHelper();

  /*
   *	Thread-Loop.
   *	1- wait on accept soccket until any client requests a file.
   *	2- On accept , get the filename, and if exists then
   *	acknowledge back.
   *	3- Start one thread from ThreadPool to send the file chunks
   *	(sendData()).
   */
  static void* acceptConn(void* arg);

  /*
   *	1- Receive #ofChunksNeeded and startChunkIdx
   *	2- Loop until all the chunks are sent to the peer
   *	3- Close the socket on finish.
   */
  static void* sendData(void* threadInfo);
  static void* sendData_v2(void* threadInfo);

  /*
   *	Thread-Loop.
   *	1- The passed arg is the filename that the user wants to
   *	   download.
   *	2- Connect to each peer one-by-one, and store the
   *	   socket-id for those peers that has the file with the
   *	   given filename.
   *	3- Obtain total#OfChunks from one of the peers
   *	4- Perform static-load-balancing of chunks among peers
   *	5- Start a thread from ThreadPool for each of the peers
   *	   (receiveData()).
   *	6- Save the status in a log text.
   */
  static void* connectPeers(void* arg);

  static void* downloadFileThread(void* arg);

  /*
   *	1- Loop until all the chunks are received from the peer.
   *	2- Close the socket on finish.
   */
  static void* receiveData(void* threadInfo);
  static void* receiveData_v2(void* threadInfo);

  static vector<int> getContributingPeersList(vector<PeerInfo> &peers_list,
                                              unordered_set<int> &contributing_peers,
                                              vector<sockaddr_in> &client_addrs,
                                              vector<int> *server_sockets,
                                              pthread_mutex_t *mutex);

  static FileInfo getFileInfoFromPeer(vector<int> &active_peers, string file_name,
                                      vector<sockaddr_in> &client_addrs,
                                      vector<int> &server_sockets,
                                      unordered_set<int> &contributing_peers,
                                      pthread_mutex_t *mutex);

  static void createThreadsToReceiveFile(vector<int> &active_peers, FileInfo file_info,
                                         vector<sockaddr_in> &client_addrs,
                                         unordered_set<int> &contributing_peers,
                                         ThreadParams *params,
                                         vector<int> &receiver_thread_ids,
                                         vector<ThreadParams_L2> &rec_thread_params);

  static void closeAllSockets(vector<int> *server_sockets);

  static int getRequiredThreadsNum(FileInfo file_info);

  static void downloadThreadCleanResources(int idx, vector<int> &server_sockets,
                                           pthread_mutex_t *mutex,
                                           unordered_set<int> &peers);

  static void startDownload(string file_name);
};
#endif
