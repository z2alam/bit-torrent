/*
 * BIT-TORRENT PROJECT - ECE1747H (PARALLEL PROGRAMMING)
 * ZOHAIB ALAM 	(997093318)
 * HATIF SATTAR (997063387)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <sys/types.h>

#include "torrent_manager.h"
#include "file_manager.h"

using namespace std;

int main (int argc, char *argv[])
{
    TorrentManager* t_man = new TorrentManager();

    t_man->run();

    if (t_man != NULL)
        delete t_man;

    return 0;
}

