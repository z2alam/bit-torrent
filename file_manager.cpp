/*
 * BIT-TORRENT PROJECT - ECE1747H (PARALLEL PROGRAMMING)
 * ZOHAIB ALAM 	(997093318)
 * HATIF SATTAR (997063387)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <iostream>
#include <sstream>
#include <assert.h>
#include "file_manager.h"

using namespace std;

FileManager::FileManager()
{
    //constructor
    cacheFilesList();
}

FileManager::~FileManager()
{
    //destroy all mutex
    pthread_mutex_destroy (&fileMutex);

    for (int i = 0; i < mNumFiles; i++){
        pthread_mutex_destroy (&mFileInfo[i].fMutex);

        if (mFileInfo[i].fp != NULL) {
            fclose(mFileInfo[i].fp);
        }
    }
    mFileInfo.clear();
}

vector<string> splitdelim(string str, char delimiter) {
  vector<string> internal;
  stringstream ss(str); // Turn the string into a stream.
  string tok;

  while(getline(ss, tok, delimiter)) {
    internal.push_back(tok);
  }
  return internal;
}

bool FileManager::cacheFilesList()
{
    bool success = false;

    // Check if files_list.txt exists. Read info file
    ifstream file (FILES_INFO_PATH);

    if (!file.good()) {
        printf ("[FileManager] file %s doesnt exist - Num of Files "
                "= 0.\nCreating file... \n", FILES_INFO_PATH);
        ofstream outfile (FILES_INFO_PATH);
        mNumFiles = 0;
        success = true;
    } else {
        string str;

        if (getline (file, str)){
            mNumFiles = atoi(str.c_str());
            printf ("[FileManager] Num of files:%d\n", mNumFiles);
        }

        //Parse the rest of the file and store file names to mFileInfo
        int i = 0;
        while (getline(file, str)) {
            assert (i < mNumFiles);

            vector<string> fileinfo = splitdelim(str, ',');
            mFileInfo.emplace_back();

            //Initialize mutex
            pthread_mutex_init( &mFileInfo[i].fMutex, NULL);

            //Store file ID
            mFileInfo[i].fileId = stoi(fileinfo[0]);
            printf("[FileManager] File Id:%d,", mFileInfo[i].fileId);

            //Store file Name
            mFileInfo[i].fileName = fileinfo[1];
            printf(" Name:%s,", mFileInfo[i].fileName.c_str());

            //File is complete
            mFileInfo[i].complete = true;

            //Open file and determine # of chunks
            FILE *infile = fopen(mFileInfo[i].fileName.c_str(), "r");
            if (infile == NULL){
                printf ("\n\n[FileManager] ERROR Could not open file %s.\n\n",
                        mFileInfo[i].fileName.c_str());
                continue;
            }

            fseek(infile, 0, SEEK_END);
            int file_size = ftell(infile);
            printf(" Size:%d,", file_size);
            mFileInfo[i].file_size = file_size;
            mFileInfo[i].fp = NULL;

            mFileInfo[i].totChunksPerFile = (file_size / CHUNK_SIZE) + 
                (((file_size % CHUNK_SIZE) == 0) ? 0 : 1);
            printf(" Num. Chunks:%d\n", mFileInfo[i].totChunksPerFile);

            //Since file is complete total existing chunks equal total chunks per file
            mFileInfo[i].totChunksExisting = mFileInfo[i].totChunksPerFile;
            fclose(infile);

            i++;
        }
        file.close();
        success = true;
    }
    return success;
}

bool FileManager::fileExists(int fileId)
{
    int i = 0;
    int local_mNumFiles;

    pthread_mutex_lock( &fileMutex );
        local_mNumFiles = mNumFiles;
    pthread_mutex_unlock( &fileMutex );

    while (i < local_mNumFiles){
        pthread_mutex_lock( &mFileInfo[i].fMutex );
            if (fileId == mFileInfo[i].fileId){
                pthread_mutex_unlock( &mFileInfo[i].fMutex );

                printf("[FileManager] File Id %d exists!\n", fileId);
                return true;
            }
        pthread_mutex_unlock( &mFileInfo[i].fMutex );
        i++;
    }

    printf("[FileManager::%s] File Id %d does NOT exist\n",__func__, fileId);
    return false;
}

// overload
int FileManager::fileExists(string fileName)
{
    int i = 0;

    //check  strlen of both strings
    int local_mNumFiles;

    pthread_mutex_lock( &fileMutex );
        local_mNumFiles = mNumFiles;
    pthread_mutex_unlock( &fileMutex );

    while (i < local_mNumFiles) {

        pthread_mutex_lock( &mFileInfo[i].fMutex );
        if (fileName.compare(mFileInfo[i].fileName) == 0) {
          pthread_mutex_unlock( &mFileInfo[i].fMutex );

          printf("[FileManager] File %s exists!\n", fileName.c_str());
          return i;
        }
        pthread_mutex_unlock( &mFileInfo[i].fMutex );
        i++;
    }

    printf("[FileManager::%s] File %s does NOT exist\n",__func__ ,fileName.c_str());
    return -1;
} 

void FileManager::UpdateFilesListDoc(int id, string name) {
    ifstream ifs;
    ifs.open(FILES_INFO_PATH);
    string line;
    string data;
    int numFiles = 0;

    if (ifs.is_open()) {
        getline(ifs, line);
        numFiles = atoi(line.c_str());

        for (int i = 0; i < numFiles; ++i) {
            getline(ifs, line);
            data += line + "\n";
        }

        data += to_string(id) + "," + name;
        ifs.close();
    }

    ofstream ofs;
    ofs.open(FILES_INFO_PATH);
    if (ofs.is_open()) {
        ofs << numFiles+1 << "\n";
        ofs << data;
        ofs.close();
    }
}

bool FileManager::addFileToDisk(int idx)
{
    FileInfo* file = &mFileInfo[idx];

    //set complete to true
    pthread_mutex_lock( &file->fMutex );
        file->complete = true;
    pthread_mutex_unlock( &file->fMutex );

    pthread_mutex_lock( &fileMutex );
         UpdateFilesListDoc(file->fileId, file->fileName);
    pthread_mutex_unlock( &fileMutex );

    return true;
}


int FileManager::addFileToCache(string file_name, int file_size)
{
    int _nFiles = 0;

    pthread_mutex_lock( &fileMutex );
    cout << "[FileManager] Num files = " << mNumFiles << endl;

    mFileInfo.emplace_back();

    mFileInfo[mNumFiles].fileId = mNumFiles;
    mFileInfo[mNumFiles].fileName = file_name;
    mFileInfo[mNumFiles].complete = false;
    mFileInfo[mNumFiles].file_size = file_size;
    pthread_mutex_init( &mFileInfo[mNumFiles].fMutex, NULL);

    FILE* fp = fopen(file_name.c_str(), "w");
    fclose(fp);

    _nFiles = mNumFiles;
    mNumFiles++;
    pthread_mutex_unlock( &fileMutex );

    return _nFiles;
}

bool FileManager::fileWrite(int idx, int start, int size, void* buf) {

    int local_mNumFiles;

    pthread_mutex_lock( &fileMutex );
    local_mNumFiles = mNumFiles;
    pthread_mutex_unlock( &fileMutex );


    if (idx >= local_mNumFiles) {
        printf ("[FileManager::%s] ERROR index %d is invalid. "
                "Total Num. of files = %d",__func__, idx, local_mNumFiles);
        return false;
    }
    else{
        pthread_mutex_lock( &mFileInfo[idx].fMutex );

        //If file ptr is NULL open the file
        if (mFileInfo[idx].fp == NULL){
            mFileInfo[idx].fp = fopen(mFileInfo[idx].fileName.c_str(), "r+");
        }

        fseek(mFileInfo[idx].fp, start, SEEK_SET); // seek to 'start'
        fwrite(buf, sizeof(char), size, mFileInfo[idx].fp); // write
        fseek(mFileInfo[idx].fp, 0, SEEK_SET); // seek back to beginning

        pthread_mutex_unlock( &mFileInfo[idx].fMutex );
        return true;
    }
}

bool FileManager::fileRead(int idx, int start, int size, void* buf)
{
    int local_mNumFiles;

    pthread_mutex_lock( &fileMutex );
    local_mNumFiles = mNumFiles;
    pthread_mutex_unlock( &fileMutex );

    if (idx >= local_mNumFiles) {
        printf ("[FileManager::%s] ERROR index %d is invalid."
                "Total Num. of files = %d",__func__, idx, local_mNumFiles);
        return false;
    } else {
        pthread_mutex_lock( &mFileInfo[idx].fMutex );

        //If file ptr is NULL open the file
        if (mFileInfo[idx].fp == NULL){
            mFileInfo[idx].fp = fopen(mFileInfo[idx].fileName.c_str(), "r+");
        }

        fseek(mFileInfo[idx].fp, start, SEEK_SET); // seek to 'start'
        fread(buf, sizeof(char), size, mFileInfo[idx].fp); // read
        fseek(mFileInfo[idx].fp, 0, SEEK_SET); // seek back to beginning

        pthread_mutex_unlock( &mFileInfo[idx].fMutex );
        return true;
    }
}

// updates and returns next..
int FileManager::updateChunks(int idx)
{
    int local_mNumFiles;
    int nextIdx = -1;

    pthread_mutex_lock( &fileMutex );
    local_mNumFiles = mNumFiles;
    pthread_mutex_unlock( &fileMutex );

    if (idx >= local_mNumFiles) {
        printf ("[FileManager::%s] ERROR index %d is invalid. "
                "Total Num. of files = %d",__func__, idx, local_mNumFiles);
        return -1;
    } else {
        pthread_mutex_lock( &mFileInfo[idx].fMutex );
            nextIdx = mFileInfo[idx].totChunksExisting;
            mFileInfo[idx].totChunksExisting += 1;
        pthread_mutex_unlock( &mFileInfo[idx].fMutex );
        return nextIdx;
    }
}


void FileManager::printFilesList()
{
    pthread_mutex_lock( &fileMutex );
    if (mNumFiles == 0) {
        printf("[FileManager] There are no existing files for the Peer!\n");
    } else {
        printf("[FileManager] Printing list of existing files for the Peer:\n");
        for (int i = 0; i < mNumFiles; i++){
            printf("File Id = %d, Name = %s\n", mFileInfo[i].fileId,mFileInfo[i].fileName.c_str());
        }
        printf("\n\n");
    }
    pthread_mutex_unlock( &fileMutex );
}

int FileManager::getFileSize(int fileId)
{
    int local_mNumFiles;
    int _fileSize = 0;

    pthread_mutex_lock( &fileMutex );
        local_mNumFiles = mNumFiles;
    pthread_mutex_unlock( &fileMutex );

    if (fileId >= local_mNumFiles) {
        printf ("[FileManager::%s] ERROR index %d is invalid."
                "Total Num. of files = %d",__func__, fileId, local_mNumFiles);
        return -1;
    } else {
        pthread_mutex_lock( &mFileInfo[fileId].fMutex );
            _fileSize = mFileInfo[fileId].file_size;
        pthread_mutex_unlock( &mFileInfo[fileId].fMutex );
        return _fileSize;
    }
}

