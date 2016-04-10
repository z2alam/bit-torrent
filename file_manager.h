// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~file_manager.h~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

/*
 * BIT-TORRENT PROJECT - ECE1747H (PARALLEL PROGRAMMING)
 * ZOHAIB ALAM 	(997093318)
 * HATIF SATTAR (997063387)
 */

#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

#include <string.h>
#include <fstream>
#include <pthread.h>
#include <vector>

#define FILE_PATH_SIZE 	50
#define FILES_INFO_PATH "files_list.txt"
#define CHUNK_SIZE      1447 // 65536
#define PACKET_SIZE     1447 // 22 packets/chunk

using namespace std;

struct FileInfo {
	int fileId;
    string fileName;
	bool complete;			/* full file available */
    FILE* fp;
	int totChunksPerFile;
	int totChunksExisting;	/* how much chunks available if not full? */
    int file_size;
	pthread_mutex_t fMutex;

    FileInfo() : fileId(-1), fileName(""), complete(false), fp(NULL), totChunksPerFile(0),
                    totChunksExisting(0), file_size(0) { }
};

class FileManager {
private:
	int mNumFiles;
	vector<FileInfo> mFileInfo;
	pthread_mutex_t fileMutex;

private:
	/*
	 *	Cache all the content in FILES_INFO_PATH into local mFileInfo array.
	 *	The format of files_list.txt is as follows:
	 *			<TOTAL_NUM_OF_FILES>
	 *			<FILE_ID> <FILE_NAME>
	 *			<FILE_ID> <FILE_NAME>
	 *					:
	 *			<FILE_ID> <FILE_NAME>
	 * 	Sample content in files_list.txt
	 *			2
	 *			1 flower_image.png
	 *			2 readme.txt
	 *
	 * 	Note: "fileId" is matched with the array index (for convenience) as a policy
	 *
	 * 	Use-case:
	 *		This function will be first used in the constructor to cache the file
     *		"files_list.txt"
	 */
	bool cacheFilesList();

public:
	FileManager();
	~FileManager();

	/*
	 * Checks if the file=fileId exists,
	 * If yes then returns true, and pass the FileInfo ptr as the parameter.
	 * Else return false
	 */
	bool fileExists(int fileId);

	/*
	 * Checks if the file name=fileName exists,
	 * If yes then returns true, and pass the FileInfo ptr as the parameter.
	 * Else return false
	 */
	int fileExists(string fileName);

    void UpdateFilesListDoc(int id, string name);

	/*
	 * A new file is successfully downloaded.
	 * 1- Add the file in the local FILES_INFO_PATH,
	 * 2- Get a new cache from the FILES_INFO_PATH
	 */
    bool addFileToDisk(int idx);

    //Add file to cache when Peer is starting to receive the file
	int addFileToCache(string file_name, int file_size);

    //print all the files for the peer following info:
    //id, name, complete
    void printFilesList();

    /*
     * all should be wrapped in corresponding mutex
     * open file if mFileInfo[idx].fp == NULL
     * write to file from buf
     * 
     * For e.g;
     * fseek(fp, start, SEEK_SET); // seek to 'start'
     * fwrite(buf, sizeof(char), size, fp);
     * fseek(fp, 0, SEEK_SET); // seek back to beginning
     */
    bool fileWrite(int idx, int start, int size, void* buf);

    /*
     * all should be wrapped in corresponding mutex
     * open file if mFileInfo[idx].fp == NULL
     * read from file to buf
     * 
     * For e.g;
     * fseek(fp, start, SEEK_SET); // seek to 'start'
     * fread(buf, sizeof(char), size, fp); // read
     * fseek(fp, 0, SEEK_SET); // seek back to beginning
     */
    bool fileRead(int idx, int start, int size, void* buf);

    /*
     * mFileInfo[idx].totChunksExisting++;
     */
    int updateChunks(int idx);

    int getFileSize(int fileId);

    // Remove local file from (disk, cache, files_list.txt)
    int removeLocalFile(char* fileName);

    // Remove local file from files_list.txt, given id
    int removeFilefromFilesList(int id);

};

#endif
