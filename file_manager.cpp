/*
 * BIT-TORRENT PROJECT - ECE1747H (PARALLEL PROGRAMMING)
 * ZOHAIB ALAM 	(997093318)
 * HATIF SATTAR (997063387)
 */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <iostream>
#include <sstream>
#include <assert.h>
#include "file_manager.h"
#include "torrent_manager.h"

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

bool FileManager::cacheFilesList()
{
    bool success = false;

    // Check if files_list.txt exists. Read info file
    ifstream file (FILES_INFO_PATH);

    if (!file.good()) {
        //printf ("[FileManager] file %s doesnt exist - Num of Files "
        //        "= 0.\nCreating file... \n", FILES_INFO_PATH);
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

            vector<string> fileinfo = TorrentManager::split(str, ',');
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
            if (fileinfo.size() != 2) {
              mFileInfo[i].totChunksExisting = stoi(fileinfo[2]);
              mFileInfo[i].totChunksPerFile = stoi(fileinfo[3]);
              mFileInfo[i].file_size = stoi(fileinfo[4]);
            }

            if (mFileInfo[i].totChunksPerFile == mFileInfo[i].totChunksExisting) {
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
            } else {
              while (!mFileInfo[i].next_idx_stack.empty())
                  mFileInfo[i].next_idx_stack.pop();
              mFileInfo[i].next_idx_stack.push(mFileInfo[i].totChunksExisting);
            }

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

    // printf("[FileManager::%s] File Id %d does NOT exist\n",__func__, fileId);
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

void FileManager::UpdateFilesListDoc(FileInfo &info, int min_idx) {
  ifstream ifs;
  ifs.open(FILES_INFO_PATH);
  string line;
  string data;
  int numFiles = 0;
  bool file_exists = false;

  if (ifs.is_open()) {
    getline(ifs, line);
    numFiles = atoi(line.c_str());

    for (int i = 0; i < numFiles; ++i) {
      getline(ifs, line);

      int parsed_id = stoi(line);
      if (parsed_id == info.fileId) {
        file_exists = true;
        data += to_string(info.fileId) + "," + info.fileName + "," +
          to_string(min_idx) + "," + to_string(info.totChunksPerFile) + "," +
          to_string(info.file_size) + "\n";
      } else {
        data += line + "\n";
      }
    }

    if (!file_exists)
      data += to_string(info.fileId) + "," + info.fileName + "," +
        to_string(min_idx) + "," + to_string(info.totChunksPerFile) + "," +
        to_string(info.file_size) + "\n";

    ifs.close();
  }
  else {
    printf("[FileManager::%s] Error opening file %s\n",__func__ ,FILES_INFO_PATH);
  }

  ofstream ofs;
  ofs.open(FILES_INFO_PATH);
  if (ofs.is_open()) {
    ofs << numFiles + ((file_exists) ? 0 : 1) << "\n";
    ofs << data;
    ofs.close();
  }
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
    else {
        printf("[FileManager::%s] Error opening file %s\n",__func__ ,FILES_INFO_PATH);
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
    FileInfo &file_info = mFileInfo[idx];

    //set complete to true
    pthread_mutex_lock( &file_info.fMutex );
        file_info.complete = true;
    pthread_mutex_unlock( &file_info.fMutex );

    pthread_mutex_lock( &fileMutex );
    UpdateFilesListDoc(file_info, file_info.totChunksPerFile);
    pthread_mutex_unlock( &fileMutex );

    return true;
}

int FileManager::getMinimumUnfinishedChunk(unordered_set<int> &inprocess_chunks)
{
  int chunk_id = 0;

  if (inprocess_chunks.empty())
    return chunk_id;

  chunk_id = INT_MAX;
  for (const auto& id : inprocess_chunks) {
    chunk_id = min(id, chunk_id);
  }
  return chunk_id;
}

bool FileManager::addFileToCache(FileInfo &file_info)
{
    pthread_mutex_lock( &fileMutex );
    cout << "[FileManager] Num files = " << mNumFiles << endl;

    file_info.fileId = mNumFiles;
    mFileInfo.emplace_back(file_info);

    FILE* fp = fopen(file_info.fileName.c_str(), "w");
    fclose(fp);

    mNumFiles++;
    pthread_mutex_unlock(&fileMutex);

    pthread_mutex_lock( &fileMutex );
    UpdateFilesListDoc(file_info, 0);
    pthread_mutex_unlock( &fileMutex );

    return true;
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

int FileManager::getNextIdx(int file_id) {
    if (mFileInfo[file_id].next_idx_stack.empty())
        return -1;

    int next = mFileInfo[file_id].next_idx_stack.top();
    mFileInfo[file_id].next_idx_stack.pop();

    if (mFileInfo[file_id].next_idx_stack.empty() && (next+1 < mFileInfo[file_id].totChunksPerFile))
        mFileInfo[file_id].next_idx_stack.push(next+1);

    return next;
}

void FileManager::markChunkDone(int file_id, int idx) {
    mFileInfo[file_id].inprocess_chunks_idx.erase(idx);
}

void FileManager::markChunkInProcess(int file_id, int idx) {
    mFileInfo[file_id].inprocess_chunks_idx.insert(idx);
}

// updates and returns next..
int FileManager::updateChunks(int file_id, int chunk_idx_completed)
{
    int local_mNumFiles;

    pthread_mutex_lock( &fileMutex );
    local_mNumFiles = mNumFiles;
    pthread_mutex_unlock( &fileMutex );

    if (file_id >= local_mNumFiles) {
        printf ("[FileManager::%s] ERROR index %d is invalid. "
                "Total Num. of files = %d",__func__, file_id, local_mNumFiles);
        return -1;
    } else {
        pthread_mutex_lock( &mFileInfo[file_id].fMutex );

        int nextIdx = getNextIdx(file_id);
        markChunkDone(file_id, chunk_idx_completed);
        markChunkInProcess(file_id, nextIdx);
        mFileInfo[file_id].totChunksExisting++;

        // on every 10th chunk, as a policy, update the files_list doc
        // with the file status
        if ((mFileInfo[file_id].totChunksExisting % 1000) == 0) {
          int min_idx = getMinimumUnfinishedChunk(mFileInfo[file_id].inprocess_chunks_idx);
          UpdateFilesListDoc(mFileInfo[file_id], min_idx);
        }

        pthread_mutex_unlock( &mFileInfo[file_id].fMutex );
        return nextIdx;
    }
}

void FileManager::markChunkFailure(int file_id, int idx) {
    pthread_mutex_lock(&mFileInfo[file_id].fMutex);

    mFileInfo[file_id].next_idx_stack.push(idx);
    markChunkDone(file_id, idx);

    pthread_mutex_unlock(&mFileInfo[file_id].fMutex);
}

void FileManager::printFilesList()
{
    pthread_mutex_lock( &fileMutex );
    if (mNumFiles == 0) {
        printf("[FileManager] There are no existing files for the Peer!\n\n");
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

bool FileManager::isFileDownloaded(int file_id) {
  if (file_id == -1)
    return false;

  // mutual exclusive read
  pthread_mutex_lock(&mFileInfo[file_id].fMutex);
  int chunks_downloaded = mFileInfo[file_id].totChunksExisting;
  int total_chunks = mFileInfo[file_id].totChunksPerFile;
  pthread_mutex_unlock(&mFileInfo[file_id].fMutex);

  if (chunks_downloaded < total_chunks)
    return false;

  return true;
}

int FileManager::removeLocalFile(string fileName)
{
    int result = -1;
    unsigned int file_idx = -1;
    unsigned int file_id = -1;
    int local_mNumFiles;
    bool fileExists = false;

    pthread_mutex_lock( &fileMutex );
        local_mNumFiles = mNumFiles;
    pthread_mutex_unlock( &fileMutex );

    int i = 0;

    //Find the index of the file in the Cache: mFileInfo
    while (i < local_mNumFiles) {

        pthread_mutex_lock( &mFileInfo[i].fMutex );
        if (fileName.compare(mFileInfo[i].fileName) == 0) {
            pthread_mutex_unlock( &mFileInfo[i].fMutex );

            printf("[FileManager] File %s exists!\n", fileName.c_str());
            fileExists = true;
            file_idx = i;
            file_id = mFileInfo[i].fileId;
            break;
        }
        pthread_mutex_unlock( &mFileInfo[i].fMutex );
        i++;
    }

    //If File does not exit - exit with error
    if (fileExists == false) {
        result = -2;
    }
    else {
        //---------------
        //Remove the file
        //---------------
        pthread_mutex_lock( &fileMutex );

        //remove from cache
        if (mFileInfo[file_idx].fp != NULL) {
            fclose(mFileInfo[file_idx].fp);
        }

        if (mNumFiles > 0)
            mNumFiles--;

        if (mFileInfo.size() > file_idx) {
            mFileInfo.erase(mFileInfo.begin() + file_idx);
        }

        //remove file from files_list - since only 1 file, can remove everything
        int success = removeFilefromFilesList(file_id);
        if (success != 0) {
            return success;
        }

        //Remove from the local dir
        string syscall = "\\rm -f " + fileName;
        printf("[FileManager] Making System call:\n $ %s \n\n", syscall.c_str());
        system (syscall.c_str());

        pthread_mutex_unlock( &fileMutex );
        result = 0;
    }
    return result;
}

int FileManager::removeFilefromFilesList(int id) {
    int result = -1;
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
            int parse_id = stoi(line);
            if (parse_id != id) {
                data += line + "\n";
            }
        }

        ifs.close();
        result = 0;
    }
    else {
        printf("[FileManager::%s] Error opening file %s\n",__func__ ,FILES_INFO_PATH);
    }

    ofstream ofs;
    ofs.open(FILES_INFO_PATH);
    if (ofs.is_open()) {
        ofs << numFiles-1 << "\n";
        ofs << data;
        ofs.close();
    }

    return result;
}

int FileManager::chunksRemaining(int file_id) {
  pthread_mutex_lock(&mFileInfo[file_id].fMutex);
  int chunks_left = mFileInfo[file_id].totChunksPerFile - mFileInfo[file_id].totChunksExisting;
  pthread_mutex_unlock(&mFileInfo[file_id].fMutex);

  return chunks_left;
}

vector<string> FileManager::getIncompleteFileNames() {
  vector<string> out;
  for (FileInfo info: mFileInfo) {
    if (info.complete == false)
      out.emplace_back(info.fileName);
  }
  return out;
}

FileInfo FileManager::getFileInfoById(int file_id) {
    FileInfo info = FileInfo();

    if (file_id >= 0)
        info = mFileInfo[file_id];

    return info;
}
