// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~status_manager.h~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

/*
 * BIT-TORRENT PROJECT - ECE1747H (PARALLEL PROGRAMMING)
 * ZOHAIB ALAM 	(997093318)
 * HATIF SATTAR (997063387)
 */

#ifndef STATUS_MANAGER_H
#define STATUS_MANAGER_H

#include <pthread.h>

#define MAX_FILE_OPS	50
enum {
	NOT_USED=0,
	IN_PROGRESS,
    FINISHED,
};

/*
 *	This struct maintains the status of thread operation, i.e. receiving
 *  or sending X num of chunks of a file.
 */
struct FileOpStatus {
	int status;			/* one of the statuses from above enum */
	int fileId;
	bool receiver;		/* whether receiving or sending file */
	int chunksFinished;	/* # of chunks completed*/
	int chunksNeeded; 	/* # of chunks need to be sent or accepted */
	int totalChunks; 	/* total # of chunks in a file */
	int startChunkNum;	/* start chunk# for this file operation */
	int threadId;
	pthread_mutex_t mutex;
};

class StatusManager {
private:
	int mNumFileOps;
	pthread_mutex_t mutex;
	FileOpStatus filesStatus[MAX_FILE_OPS];

public:
	StatusManager();
	~StatusManager();

    /*
     * 1- look for the first idx whose 'status' == NOT_USED
     * 2- update all the struct members.
     * 3- set status=IN_PROGRESS
     * 4- return array idx
     */
	int getStatusId(int fileId, int threadId, int totChunks, int startChunk,
            bool receiving, int chunksReq);

    /*
     * 1- Goto the idx=statusId
     * 2- Pass by reference the required info
     * 
     */
    bool getStatus(int index, int &chunksFinished, int &startChunk,
            bool &finished);

    /*
     * 1- Update the info for idx=statusId
     * 2- if finishTransfer is true, then set status=FINISHED
     */
    bool updateStatus(int index, int chunksFinished, bool finishTransfer);

    /*
     * 1- set status= NOT_USED 
     */
    bool freeStatus(int index);
};

#endif
