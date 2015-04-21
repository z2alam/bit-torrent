
#include "thread_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <sys/types.h>
#include <assert.h>


ThreadPool::ThreadPool() : mSenderNum(0), mReceiverNum(0)
{
    //constructor

    //initialize mRecThreads and mSenThreads arrays
    for (int i = 0; i < MAX_RECEIVER_THREADS; i++){
        mRecThreads[i].available = true;
        mRecThreads[i].thread_id = i;
        mRecThreads[i].receiver = true;
        pthread_mutex_init (&mRecThreads[i].tMutex, NULL);
    }

    for (int i = 0; i < MAX_SENDER_THREADS; i++){
        mSenThreads[i].available = true;
        mSenThreads[i].thread_id = i;
        mSenThreads[i].receiver = false;
        pthread_mutex_init (&mSenThreads[i].tMutex, NULL);
    }
    pthread_mutex_init(&mainMutex, NULL);
}

ThreadPool::~ThreadPool()
{
    //destructor
    
    //Cancel threads in progress
    for (int i = 0; i < MAX_RECEIVER_THREADS; i++){
        if (mRecThreads[i].available == false){
            pthread_cancel (mRecThreads[i].thread);
            pthread_join (mRecThreads[i].thread, NULL);
        }
    }

    for (int i = 0; i < MAX_SENDER_THREADS; i++){
        if (mSenThreads[i].available == false){
            pthread_cancel (mSenThreads[i].thread);
            pthread_join (mSenThreads[i].thread, NULL);
        }
    }
    //destroy all mutex
    for (int i = 0; i < MAX_RECEIVER_THREADS; i++){
        pthread_mutex_destroy (&mRecThreads[i].tMutex);
    }

    for (int i = 0; i < MAX_SENDER_THREADS; i++){
        pthread_mutex_destroy (&mSenThreads[i].tMutex);
    }

    pthread_mutex_destroy (&mainMutex);
}

int ThreadPool::getAcceptThread(pthread_t* &thread)
{
    for (int i = 0; i < MAX_RECEIVER_THREADS; i++){
        pthread_mutex_lock( &mRecThreads[i].tMutex);
        if (mRecThreads[i].available == true){
            printf("[ThreadPool] Found available thread %d to receive data.\n",i);
            mRecThreads[i].available = false;
            mReceiverNum++;
            thread = &mRecThreads[i].thread;
            pthread_mutex_unlock( &mRecThreads[i].tMutex);
            return i;
        }
        pthread_mutex_unlock( &mRecThreads[i].tMutex);
    }
    return -1;
}

int ThreadPool::getConnectThread(pthread_t* &thread)
{
    for (int i = 0; i < MAX_SENDER_THREADS; i++){
        pthread_mutex_lock( &mSenThreads[i].tMutex);
        if (mSenThreads[i].available == true) {

            printf("[ThreadPool] Found available thread %d to send data.\n",i);
            mSenThreads[i].available = false;
            mSenderNum++;
            thread = &mSenThreads[i].thread;
            pthread_mutex_unlock( &mSenThreads[i].tMutex);
            return i;
        }
        pthread_mutex_unlock( &mSenThreads[i].tMutex);
    }
    return -1;
}

bool ThreadPool::freeThread(int idx, bool receiver)
{
    ThreadInfo* thread = (receiver == true) ? &mRecThreads[idx] :
        &mSenThreads[idx];

    pthread_mutex_lock( &thread->tMutex);
        thread->available = true;
    pthread_mutex_unlock( &thread->tMutex);

    pthread_mutex_lock( &mainMutex);
        (receiver) ? mReceiverNum-- : mSenderNum--; 
    pthread_mutex_unlock( &mainMutex);

    return true;
}
