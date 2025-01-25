#include <fcntl.h>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fstream>

#include "block.h"

// Disk size set to 32MB
#define DISK_SIZE 32 * 1024 * 1024

class Disk {
public:
    Disk() : diskfile(-1) {}

    // Creates a file which is your new emulated disk
    void dev_init(const std::string& diskfile_path) {
        if (diskfile >= 0) {
            return;
        }

        diskfile = open(diskfile_path.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
        if (diskfile < 0) {
            perror("disk_open failed");
            exit(EXIT_FAILURE);
        }

        ftruncate(diskfile, DISK_SIZE);
    }

    // Function to open the disk file
    int dev_open(const std::string& diskfile_path) {
        if (diskfile >= 0) {
            return 0;
        }

        diskfile = open(diskfile_path.c_str(), O_RDWR, S_IRUSR | S_IWUSR);
        if (diskfile < 0) {
            perror("disk_open failed");
            return -1;
        }
        return 0;
    }

    void dev_close() {
        if (diskfile >= 0) {
            close(diskfile);
        }
    }

    // Read a block from the disk
    int bio_read(int block_num, void *buf) {
        int retstat = pread(diskfile, buf, BLOCK_SIZE, block_num * BLOCK_SIZE);
        if (retstat <= 0) {
            std::memset(buf, 0, BLOCK_SIZE);
            if (retstat < 0) {
                perror("block_read failed");
            }
        }
        return retstat;
    }

    // Write a block to the disk
    int bio_write(int block_num, const void *buf) {
        int retstat = pwrite(diskfile, buf, BLOCK_SIZE, block_num * BLOCK_SIZE);
        if (retstat < 0) {
            perror("block_write failed");
        }
        return retstat;
    }

private:
    int diskfile;
};

int main() {
    Disk disk;

    // Initialize the disk
    disk.dev_init("diskfile.img");

    // Example block to write and read
    char write_buf[BLOCK_SIZE] = "Hello, disk!";
    disk.bio_write(0, write_buf);

    char read_buf[BLOCK_SIZE] = {};
    disk.bio_read(0, read_buf);

    std::cout << "Read from disk: " << read_buf << std::endl;

    disk.dev_close();
    return 0;
}
