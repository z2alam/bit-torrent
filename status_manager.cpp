/*
 * BIT-TORRENT PROJECT - ECE1747H (PARALLEL PROGRAMMING)
 * ZOHAIB ALAM 	(997093318)
 * HATIF SATTAR (997063387)
 */

#include "status_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <sys/types.h>
#include <assert.h>

StatusManager::StatusManager()
{
  //constructor
  for (int i = 0; i < MAX_FILE_OPS; ++i){
    filesStatus[i].status = NOT_USED;
    pthread_mutex_init(&filesStatus[i].mutex, NULL);
  }
}

StatusManager::~StatusManager()
{
  //destructor
}

int StatusManager::getStatusId(int fileId, int threadId,
        int totChunks, int startChunk, bool receiving, int chunksReq)
{
    for (int i = 0; i < MAX_FILE_OPS; i++){

        pthread_mutex_lock (&filesStatus[i].mutex);
        if( filesStatus[i].status == NOT_USED) {
            filesStatus[i].status = IN_PROGRESS;
            filesStatus[i].fileId = fileId;
            filesStatus[i].threadId = threadId;
            filesStatus[i].totalChunks = totChunks;
            filesStatus[i].startChunkNum = startChunk;
            filesStatus[i].chunksFinished = 0;
            filesStatus[i].chunksNeeded = chunksReq;
            pthread_mutex_unlock (&filesStatus[i].mutex);
            return i;
        }
        pthread_mutex_unlock (&filesStatus[i].mutex);

    }
    return -1;
}

bool StatusManager::getStatus(int index, int &chunksFinished,
        int &startChunk, bool &finished)
{
  if (index >= MAX_FILE_OPS){
      printf("[StatusManager] ERROR - array index %d is larger"
              " than MAX_FILE_OPS %d \n",index,MAX_FILE_OPS);
      return false;
  }
  else {
      pthread_mutex_lock (&filesStatus[index].mutex);

      chunksFinished = filesStatus[index].chunksFinished;
      startChunk = filesStatus[index].startChunkNum;
      finished = (filesStatus[index].status == FINISHED);

      pthread_mutex_unlock (&filesStatus[index].mutex);
      return true;
  }
}

bool StatusManager::updateStatus(int index, int chunksFinished,
        bool finishTransfer)
{
  if (index >= MAX_FILE_OPS){
      printf("[StatusManager] ERROR - array index %d is larger"
              " than MAX_FILE_OPS %d \n",index,MAX_FILE_OPS);
      return false;
  }
  else {
      pthread_mutex_lock (&filesStatus[index].mutex);

      filesStatus[index].chunksFinished = chunksFinished;
      if (finishTransfer){
          filesStatus[index].status = FINISHED;
      }
      else{
          filesStatus[index].status = IN_PROGRESS;
      }

      pthread_mutex_unlock (&filesStatus[index].mutex);

      return true;
  }
}

bool StatusManager::freeStatus(int index)
{
  if (index >= MAX_FILE_OPS){
      printf("[StatusManager] ERROR - array index %d is larger" 
              " than MAX_FILE_OPS %d \n",index,MAX_FILE_OPS);
      return false;
  }
  else {
      pthread_mutex_lock (&filesStatus[index].mutex);
      filesStatus[index].status = NOT_USED;
      pthread_mutex_unlock (&filesStatus[index].mutex);
      return true;
  }
}


