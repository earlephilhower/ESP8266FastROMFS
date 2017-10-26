/*
  ESP8266FastROMFS
  Filesystem for onboard flash focused on speed

  Copyright (C) 2017  Earle F. Philhower, III

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _ESP8266FASTROMFS_H
#define _ESP8266FASTROMFS_H

// Enable debugging set to 1
#define DEBUGFASTROMFS 1

// Constants that define filesystem structure
#define FSMAGIC 0xdead0beef0f00dll
#define SECTORSIZE 4096
#define FSSIZEMB 3
#define FATENTRIES (FSSIZEMB * 1024 * 1024 / 4096)
#define FATBYTES ((FATENTRIES * 12) / 8)
#define FILEENTRIES ((int)((SECTORSIZE - (sizeof(uint64_t) + sizeof(uint64_t) + FATBYTES + sizeof(uint32_t)))/sizeof(FileEntry)))
#define FATEOF 0xfff
#define FATCOPIES 8
#define NAMELEN 24



class Filesystem;
class File;

typedef void Dir; // Opaque for the masses

struct dirent {
  int off;
  char name[NAMELEN + 1]; // Ensure space for \0
  int len;
};


// Private structs
typedef struct {
  char name[NAMELEN]; // Not necessarialy 0-terminated, beware!
  int32_t fat; // Index to first FAT block. Only need 16 bits, but easiest to ensure alignment @32
  int32_t len; // Can be 0 if file just created with no writes
} FileEntry;

typedef union {
  uint8_t filler[SECTORSIZE]; // Ensure we take 1 full sector in RAM
  struct {
    uint64_t magic;
    int64_t epoch; // If you roll over this, well, you're amazing
    uint8_t fat[ FATBYTES ]; // 12-bit packed, use accessors to get in here!
    FileEntry fileEntry[ FILEENTRIES ];
    uint32_t crc; // CRC32 over the complete entry (replace with 0 before calc'ing)
  };
} FilesystemInFlash;




class Filesystem
{
    friend File;
  public:
    Filesystem();
    ~Filesystem();
    bool mkfs();
    bool mount();
    bool umount();
    File *open(const char *name, const char *mode);
    bool unlink(const char *name);
    bool exists(const char *name);
    bool rename(const char *src, const char *dest);
    int available();
    int fsize(const char *name);
    Dir *opendir(const char *ignored) {
      (void)ignored;
      return opendir();
    };
    Dir *opendir();
    struct dirent *readdir(Dir *dir);
    int closedir(Dir *dir);

    void DumpFS();
    void DumpSector(int sector);

  protected:
    int GetFAT(int idx);
    void SetFAT(int idx, int val);
    bool EraseSector(int sector);
    bool WriteSector(int sector, const void *data);
    bool ReadSector(int sector, void *data);
    bool ReadPartialSector(int sector, int offset, void *dest, int len);
    int FindFreeSector();
    int FindFreeFileEntry();
    int FindFileEntryByName(const char *name);
    int CreateNewFileEntry(const char *name);
    void GetFileEntryName(int idx, char *dest);
    int GetFileEntryLen(int idx);
    int GetFileEntryFAT(int idx);
    void SetFileEntryName(int idx, const char *src);
    void SetFileEntryLen(int idx, int len);
    void SetFileEntryFAT(int idx, int fat);
    bool FlushFAT();
    int FindOldestFAT();
    int FindNewestFAT();
    bool ValidateFAT();

  private:
    FilesystemInFlash fs;
    bool fsIsMounted;
    bool fsIsDirty;
#ifndef ARDUINO
    uint8_t flash[FATENTRIES][SECTORSIZE];
    bool flashErased[FATENTRIES];
#endif
};




class File
{
    friend Filesystem;

  public:
    ~File() {
      free(data);
    };

    int write(const void *out, int size);
    int read(void *data, int size);
    int seek(int off, int whence);
    int seek(int off) {
      return seek(off, SEEK_SET);
    }
    int close();
    int tell();
    int eof();
    int size();
    void name(char *buff, int buffLen);
    int fputc(int c);
    int fgetc();

  private:
    File(Filesystem *fs, int fileIdx, int readOffset, int writeOffset, bool read, bool write, bool append, bool eraseFirstSector);
    Filesystem *fs; // Where do I live?
    int fileIdx; // Which entry

    int32_t writePos; // = offset from 0 in file
    int32_t readPos; // = offset from 0 in file
    int32_t curWriteSector; // = current sector in buffer
    int32_t curWriteSectorOffset; // = offset of byte[0] of the current sector in the file
    int32_t curReadSector;
    int32_t curReadSectorOffset;
    uint8_t *data; // = sector data.  On update, read old sector into it.
    bool dataDirty; // = flag the data here is dirty

    bool modeAppend; // = flag
    bool modeRead; // = flag
    bool modeWrite; // = flag
};

#endif
