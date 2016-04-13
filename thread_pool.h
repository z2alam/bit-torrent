// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~thread_pool.h~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>

#define MAX_RECEIVER_THREADS	15
#define MAX_SENDER_THREADS	15
struct ThreadInfo {
  bool available;	/* availability of thread */
  int thread_id; //array index
  pthread_t thread;
  bool receiver;	/* whether a receiver or sender thread */
  pthread_mutex_t tMutex;
};

class ThreadPool {
  private:
    int mSenderNum, mReceiverNum;
    ThreadInfo mRecThreads[MAX_RECEIVER_THREADS];
    ThreadInfo mSenThreads[MAX_SENDER_THREADS];
    pthread_mutex_t mainMutex;

  public:
    ThreadPool();
    ~ThreadPool();

    /*
     * Goes through mRecThreads list, and check if any thread is available.
     * 		Mark the non-used thread as "not available"
     *		Mark the receiver_thread = true
     *		mReceiverNum++
     * 		Pass the address of the corresponding ThreadInfo as param
     * 		Return true
     * Otherwise Return false
     */
    int getAcceptThread(pthread_t* &thread);

    /*
     * Goes through mSenThreads list, and check if any thread is available.
     * 		Mark the non-used thread as "not available"
     *		Mark the receiver_thread = false
     *		mSenderNum++
     * 		Pass the address of the corresponding ThreadInfo as param
     * 		Return true
     * Otherwise Return false
     */
    int getConnectThread(pthread_t* &thread);

    /*
     * 	Uninitialize the ThreadInfo struct.
     * 	set available to true
     *	if thread->receiver ? mReceiverNum-- : mSenderNum--;
     */
    bool freeThread(int idx, bool receive);
};

#endif
