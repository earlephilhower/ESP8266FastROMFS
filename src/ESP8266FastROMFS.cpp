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

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#endif

#include <ESP8266FastROMFS.h>

#ifndef DEBUGFASTROMFS
  #define DEBUGFASTROMFS 0
#endif

#ifdef ARDUINO
  #define DEBUG_FASTROMFS if (DEBUGFASTROMFS) Serial.printf
#else
  #define DEBUG_FASTROMFS if (DEBUGFASTROMFS) printf
#endif


#ifndef min
#define min(a,b) (((a)>(b))?(b):(a))
#endif
#ifndef max
#define max(a,b) (((a)>(b))?(a):(b))
#endif


bool FastROMFilesystem::exists(const char *name)
{
  if (!fsIsMounted) return false;

  if (FindFileEntryByName(name) >= 0) return true;
  return false;
}

bool FastROMFilesystem::rename(const char *old, const char *newName)
{
  if (!fsIsMounted) return false;
  int idx = FindFileEntryByName(old);
  int newIdx = FindFileEntryByName(newName);
  if ((idx >= 0) && (newIdx == -1)) {
    SetFileEntryName(idx, newName);
    return true;
  }
  return false;
}


void FastROMFilesystem::GetFileEntryName(int idx, char *dest)
{
  memcpy(dest, fs.fileEntry[idx].name, NAMELEN);
}

int FastROMFilesystem::GetFileEntryLen(int idx)
{
  return fs.fileEntry[idx].len;
}

int FastROMFilesystem::GetFileEntryFAT(int idx)
{
  return fs.fileEntry[idx].fat;
}

void FastROMFilesystem::SetFileEntryName(int idx, const char *src)
{
  strncpy(fs.fileEntry[idx].name, src, NAMELEN);
  fsIsDirty = true;
}

void FastROMFilesystem::SetFileEntryLen(int idx, int len)
{
  fs.fileEntry[idx].len = len;
  fsIsDirty = true;
}


void FastROMFilesystem::SetFileEntryFAT(int idx, int fat)
{
  fs.fileEntry[idx].fat = fat;
  fsIsDirty = true;
}

#ifndef ARDUINO
void FastROMFilesystem::DumpToFile(FILE *f)
{
  if (fsIsMounted) return; // Can't dump a mounted FS!
  for (int i=0; i < fs.sectors; i++)
    fwrite(flash[i], SECTORSIZE, 1, f);
}

void FastROMFilesystem::LoadFromFile(FILE *f)
{
  if (fsIsMounted) return;
  for (size_t i=0; i<MAXFATENTRIES; i++)
    fread(flash[i], SECTORSIZE, 1, f);
}
#endif


FastROMFSDir *FastROMFilesystem::opendir()
{
  if (!fsIsMounted) return NULL;
  struct FastROMFSDirent *de = (struct FastROMFSDirent *)malloc(sizeof(struct FastROMFSDirent));
  if (!de) return NULL; // OOM
  de->off = -1;
  return (void*)de;
}

struct FastROMFSDirent *FastROMFilesystem::readdir(FastROMFSDir *dir)
{
  if (!fsIsMounted) return NULL;
  struct FastROMFSDirent *de = reinterpret_cast<struct FastROMFSDirent *>(dir);
  de->off++;
  while (de->off < FILEENTRIES) {
    char name[NAMELEN];
    GetFileEntryName(de->off, name);
    if (name[0]) {
      strncpy(de->name, name, sizeof(name));
      de->name[sizeof(de->name) - 1] = 0;
      de->len = GetFileEntryLen(de->off);
      return de;
    }
    de->off++;
  }
  return NULL;
}

int FastROMFilesystem::closedir(FastROMFSDir *dir)
{
  if (!fsIsMounted) return false;
  if (!dir) return -1;
  free(dir);
  return 0;
}

void FastROMFilesystem::CRC32(const void *data, size_t n_bytes, uint32_t* crc)
{
  static const uint32_t table[256] PROGMEM = {
    0xd202ef8d, 0xa505df1b, 0x3c0c8ea1, 0x4b0bbe37, 0xd56f2b94, 0xa2681b02, 0x3b614ab8, 0x4c667a2e,
    0xdcd967bf, 0xabde5729, 0x32d70693, 0x45d03605, 0xdbb4a3a6, 0xacb39330, 0x35bac28a, 0x42bdf21c,
    0xcfb5ffe9, 0xb8b2cf7f, 0x21bb9ec5, 0x56bcae53, 0xc8d83bf0, 0xbfdf0b66, 0x26d65adc, 0x51d16a4a,
    0xc16e77db, 0xb669474d, 0x2f6016f7, 0x58672661, 0xc603b3c2, 0xb1048354, 0x280dd2ee, 0x5f0ae278,
    0xe96ccf45, 0x9e6bffd3, 0x0762ae69, 0x70659eff, 0xee010b5c, 0x99063bca, 0x000f6a70, 0x77085ae6,
    0xe7b74777, 0x90b077e1, 0x09b9265b, 0x7ebe16cd, 0xe0da836e, 0x97ddb3f8, 0x0ed4e242, 0x79d3d2d4,
    0xf4dbdf21, 0x83dcefb7, 0x1ad5be0d, 0x6dd28e9b, 0xf3b61b38, 0x84b12bae, 0x1db87a14, 0x6abf4a82,
    0xfa005713, 0x8d076785, 0x140e363f, 0x630906a9, 0xfd6d930a, 0x8a6aa39c, 0x1363f226, 0x6464c2b0,
    0xa4deae1d, 0xd3d99e8b, 0x4ad0cf31, 0x3dd7ffa7, 0xa3b36a04, 0xd4b45a92, 0x4dbd0b28, 0x3aba3bbe,
    0xaa05262f, 0xdd0216b9, 0x440b4703, 0x330c7795, 0xad68e236, 0xda6fd2a0, 0x4366831a, 0x3461b38c,
    0xb969be79, 0xce6e8eef, 0x5767df55, 0x2060efc3, 0xbe047a60, 0xc9034af6, 0x500a1b4c, 0x270d2bda,
    0xb7b2364b, 0xc0b506dd, 0x59bc5767, 0x2ebb67f1, 0xb0dff252, 0xc7d8c2c4, 0x5ed1937e, 0x29d6a3e8,
    0x9fb08ed5, 0xe8b7be43, 0x71beeff9, 0x06b9df6f, 0x98dd4acc, 0xefda7a5a, 0x76d32be0, 0x01d41b76,
    0x916b06e7, 0xe66c3671, 0x7f6567cb, 0x0862575d, 0x9606c2fe, 0xe101f268, 0x7808a3d2, 0x0f0f9344,
    0x82079eb1, 0xf500ae27, 0x6c09ff9d, 0x1b0ecf0b, 0x856a5aa8, 0xf26d6a3e, 0x6b643b84, 0x1c630b12,
    0x8cdc1683, 0xfbdb2615, 0x62d277af, 0x15d54739, 0x8bb1d29a, 0xfcb6e20c, 0x65bfb3b6, 0x12b88320,
    0x3fba6cad, 0x48bd5c3b, 0xd1b40d81, 0xa6b33d17, 0x38d7a8b4, 0x4fd09822, 0xd6d9c998, 0xa1def90e,
    0x3161e49f, 0x4666d409, 0xdf6f85b3, 0xa868b525, 0x360c2086, 0x410b1010, 0xd80241aa, 0xaf05713c,
    0x220d7cc9, 0x550a4c5f, 0xcc031de5, 0xbb042d73, 0x2560b8d0, 0x52678846, 0xcb6ed9fc, 0xbc69e96a,
    0x2cd6f4fb, 0x5bd1c46d, 0xc2d895d7, 0xb5dfa541, 0x2bbb30e2, 0x5cbc0074, 0xc5b551ce, 0xb2b26158,
    0x04d44c65, 0x73d37cf3, 0xeada2d49, 0x9ddd1ddf, 0x03b9887c, 0x74beb8ea, 0xedb7e950, 0x9ab0d9c6,
    0x0a0fc457, 0x7d08f4c1, 0xe401a57b, 0x930695ed, 0x0d62004e, 0x7a6530d8, 0xe36c6162, 0x946b51f4,
    0x19635c01, 0x6e646c97, 0xf76d3d2d, 0x806a0dbb, 0x1e0e9818, 0x6909a88e, 0xf000f934, 0x8707c9a2,
    0x17b8d433, 0x60bfe4a5, 0xf9b6b51f, 0x8eb18589, 0x10d5102a, 0x67d220bc, 0xfedb7106, 0x89dc4190,
    0x49662d3d, 0x3e611dab, 0xa7684c11, 0xd06f7c87, 0x4e0be924, 0x390cd9b2, 0xa0058808, 0xd702b89e,
    0x47bda50f, 0x30ba9599, 0xa9b3c423, 0xdeb4f4b5, 0x40d06116, 0x37d75180, 0xaede003a, 0xd9d930ac,
    0x54d13d59, 0x23d60dcf, 0xbadf5c75, 0xcdd86ce3, 0x53bcf940, 0x24bbc9d6, 0xbdb2986c, 0xcab5a8fa,
    0x5a0ab56b, 0x2d0d85fd, 0xb404d447, 0xc303e4d1, 0x5d677172, 0x2a6041e4, 0xb369105e, 0xc46e20c8,
    0x72080df5, 0x050f3d63, 0x9c066cd9, 0xeb015c4f, 0x7565c9ec, 0x0262f97a, 0x9b6ba8c0, 0xec6c9856,
    0x7cd385c7, 0x0bd4b551, 0x92dde4eb, 0xe5dad47d, 0x7bbe41de, 0x0cb97148, 0x95b020f2, 0xe2b71064,
    0x6fbf1d91, 0x18b82d07, 0x81b17cbd, 0xf6b64c2b, 0x68d2d988, 0x1fd5e91e, 0x86dcb8a4, 0xf1db8832,
    0x616495a3, 0x1663a535, 0x8f6af48f, 0xf86dc419, 0x660951ba, 0x110e612c, 0x88073096, 0xff000000
  };

  for (size_t i = 0; i < n_bytes; ++i) {
    *crc = table[(uint8_t) * crc ^ ((uint8_t*)data)[i]] ^ *crc >> 8;
  }
}


#ifdef ARDUINO
extern "C" uint32_t _SPIFFS_start;
extern "C" uint32_t _SPIFFS_end;
#endif

FastROMFilesystem::FastROMFilesystem()
{
#ifdef ARDUINO
  baseAddr = ((uint32_t) (&_SPIFFS_start) - 0x40200000); // Magic constants taken from SPIFF_API.cpp
  baseSector = baseAddr / SECTORSIZE;
  totalSectors = ((uint32_t)&_SPIFFS_end - (uint32_t)&_SPIFFS_start) / SECTORSIZE;

  DEBUG_FASTROMFS("baseAddr=%08lx, baseSector=%ld, totalSectors=%ld\n", (long)baseAddr, (long)baseSector, (long)totalSectors);

  lastFlashSector = -1; // Invalidate the 1-word cache
#else
  for (int i = 0; i < MAXFATENTRIES; i++) flashErased[i] = false;
  totalSectors = MAXFATENTRIES;
#endif
  fsIsDirty = false;
  fsIsMounted = false;
}

FastROMFilesystem::FastROMFilesystem(int sectors)
{
#ifdef ARDUINO
  baseAddr = ((uint32_t) (&_SPIFFS_start) - 0x40200000); // Magic constants taken from SPIFF_API.cpp
  baseSector = baseAddr / SECTORSIZE;
  totalSectors = sectors;

  DEBUG_FASTROMFS("baseAddr=%08lx, baseSector=%ld, totalSectors=%ld\n", (long)baseAddr, (long)baseSector, (long)totalSectors);

  lastFlashSector = -1; // Invalidate the 1-word cache
#else
  for (int i = 0; i < MAXFATENTRIES; i++) flashErased[i] = false;
  totalSectors = sectors;
#endif
  fsIsDirty = false;
  fsIsMounted = false;
}


FastROMFilesystem::~FastROMFilesystem()
{
  if (fsIsMounted) umount();
}


void FastROMFilesystem::DumpFS()
{
  DEBUG_FASTROMFS("fs.epoch = %ld; fs.sectors = %ld\n", (long)fs.epoch, (long)fs.sectors);
  
  DEBUG_FASTROMFS("%-32s - %-5s - %-5s\n", "name", "len", "fat");
  for (int i = 0; i < FILEENTRIES; i++) {
    if (fs.fileEntry[i].name[0]) {
      char nm[NAMELEN+1];
      memcpy(nm, fs.fileEntry[i].name, NAMELEN);
      nm[NAMELEN] = 0;
      DEBUG_FASTROMFS("%32s - %5d - %5d\n", nm, fs.fileEntry[i].len, fs.fileEntry[i].fat);
    }
  }
  for (int i = 0; i < fs.sectors; i++) {
    DEBUG_FASTROMFS("%s%5d:%-5d ", 0 == (i % 8) ? "\n" : "", i, GetFAT(i));
  }
  DEBUG_FASTROMFS("\n\n");
}

void FastROMFilesystem::DumpSector(int sector)
{
  DEBUG_FASTROMFS("Sector: %d", sector);
#ifdef ARDUINO
  uint8_t *buff = new uint8_t[SECTORSIZE];
  ReadSector(sector, buff);
  for (int i = 0; i < SECTORSIZE; i++)
    DEBUG_FASTROMFS("%s%02x ", (i % 32) == 0 ? "\n" : "", buff[i]);
  delete[] buff;
#else
  for (int i = 0; i < SECTORSIZE; i++)
    DEBUG_FASTROMFS("%s%02x ", (i % 32) == 0 ? "\n" : "", flash[sector][i]);
#endif
  DEBUG_FASTROMFS("\n");
}

int FastROMFilesystem::available()
{
  if (!fsIsMounted) return false;
  int avail = 0;
  for (int i = 0; i < fs.sectors; i++) {
    if (GetFAT(i) == 0) avail += SECTORSIZE;
  }
  return avail;
}

int FastROMFilesystem::fsize(const char *name)
{
  if (!fsIsMounted) return false;
  int idx = FindFileEntryByName(name);
  if (idx < 0) return -1;
  return GetFileEntryLen(idx);
}

bool FastROMFilesystem::ValidateFAT()
{
  if (fs.magic != FSMAGIC) return false;
  uint32_t savedCRC = fs.crc;
  uint32_t calcCRC = 0;
  fs.crc = 0;
  CRC32((void*)&fs, sizeof(fs), &calcCRC);
  if (savedCRC != calcCRC) return false; // Something baaaad here!
  return true;
}

int FastROMFilesystem::FindOldestFAT()
{
  int oldIdx = 0;
  int64_t oldEpoch = 1LL << 62;
  for (int i = 0; i < FATCOPIES; i++) {
    uint64_t space[2]; // hold magic and epoch only
    ReadPartialSector(i, 0, space, sizeof(space));
    if (space[0] != FSMAGIC) {
      // This one is bad, so let's overwrite it!
      return i;
    }
    if ((int64_t)space[1] < oldEpoch) {
      oldIdx = i;
      oldEpoch = (int64_t)space[1];
    }
  }
  return oldIdx;
}

// Scan the FS and return index of latest epoch
int FastROMFilesystem::FindNewestFAT()
{
  int newIdx = -1;
  int64_t newEpoch = 0;
  for (int i = 0; i < FATCOPIES; i++) {
    uint64_t space[2]; // hold magic and epoch
    ReadPartialSector(i, 0, space, sizeof(space));
    if (space[0] != FSMAGIC) continue; // Ignore invalid ones
    if ((int64_t)space[1] > newEpoch) {
      newIdx = i;
      newEpoch = (int64_t)space[1];
    }
  }
  return newIdx;
}

int FastROMFilesystem::FindFileEntryByName(const char *name)
{
  if (!name) return -1;

  for (int i = 0; i < FILEENTRIES; i++) {
    if (!strncmp(fs.fileEntry[i].name, name, sizeof(fs.fileEntry[i].name))) return i;
  }
  return -1;
}

int FastROMFilesystem::FindFreeFileEntry()
{
  for (int i = 0; i < FILEENTRIES; i++) {
    if (fs.fileEntry[i].name[0] == 0) return i;
  }
  return -1; // No space
}

int FastROMFilesystem::CreateNewFileEntry(const char *name)
{
  int idx = FindFreeFileEntry();
  int sec = FindFreeSector();
  if ((idx < 0) || (sec < 0)) return -1;
  strncpy(fs.fileEntry[idx].name, name, sizeof(fs.fileEntry[idx].name));
  fs.fileEntry[idx].fat = sec;
  fs.fileEntry[idx].len = 0;
  fsIsDirty = true;
  SetFAT(sec, FATEOF);
  if (!FlushFAT()) return -1;
  return idx;
}

bool FastROMFilesystem::unlink(const char *name)
{
  if (!fsIsMounted) return false;
  DEBUG_FASTROMFS("unlink('%s')\n", name);
  int idx = FindFileEntryByName(name);
  if (idx < 0) return false;
  int sec = fs.fileEntry[idx].fat;
  while (GetFAT(sec) != FATEOF) {
    int nextSec = GetFAT(sec);
    SetFAT(sec, 0);
    sec = nextSec;
  }
  SetFAT(sec, 0);
  fs.fileEntry[idx].name[0] = 0;
  fs.fileEntry[idx].len = 0;
  fs.fileEntry[idx].fat = 0;
  return FlushFAT();
}

int FastROMFilesystem::GetFAT(int idx)
{
  if ((idx < 0) || (idx >= fs.sectors)) return -1;

  int bo = (idx / 2) * 3;
  int ret;
  if (idx & 1) {
    ret = fs.fat[bo + 1] & 0x0f;
    ret <<= 8;
    ret |= fs.fat[bo + 2];
  } else {
    ret = fs.fat[bo + 1] & 0xf0;
    ret <<= 4;
    ret |= fs.fat[bo];
  }
  return ret;
}

void FastROMFilesystem::SetFAT(int idx, int val)
{
  if ((idx < 0) || (idx >= fs.sectors)) return;

  int bo = (idx / 2) * 3;
  if (idx & 1) {
    fs.fat[bo + 1] &= ~0x0f;
    fs.fat[bo + 1] |= (val >> 8) & 0x0f;
    fs.fat[bo + 2] = val & 0xff;
  } else {
    fs.fat[bo + 1] &= ~0xf0;
    fs.fat[bo + 1] |= (val >> 4) & 0xf0;
    fs.fat[bo] = val & 0xff;
  }

  fsIsDirty = true;
}

int FastROMFilesystem::FindFreeSector()
{
  int a = rand() % fs.sectors;
  for (int i = 0; (i < fs.sectors) && (GetFAT(a) != 0);  i++, a = (a + 1) % fs.sectors) {
    /*empty*/
  }
  if (GetFAT(a) != 0) return -1;
  return a;
}


bool FastROMFilesystem::EraseSector(int sector)
{
  if ((sector < 0) || (sector >= fs.sectors)) return false;

  DEBUG_FASTROMFS("EraseSector(%d)\n", sector);
#ifdef ARDUINO
  // If we're messing with this sector, invalidate any cached data corresponding to it
  if (sector == lastFlashSector) lastFlashSector = -1;

  return ESP.flashEraseSector(baseSector + sector);
#else
  memset(flash[sector], 0, SECTORSIZE);
  flashErased[sector] = true;
  return true;
#endif
}

bool FastROMFilesystem::WriteSector(int sector, const void *data)
{
  DEBUG_FASTROMFS("WriteSector(%d, data)\n", sector);

  if ((sector < 0) || (sector >= fs.sectors) || !data) return false;
  if ((const uintptr_t)data % 4) return false; // Need to have 32-bit aligned inputs!

#ifdef ARDUINO
  // If we're messing with this sector, invalidate any cached data corresponding to it
  if (sector == lastFlashSector) lastFlashSector = -1;

  return ESP.flashWrite(baseAddr + sector * FLASH_SECTOR_SIZE, (uint32_t*)data, FLASH_SECTOR_SIZE);
#else
  if (!flashErased[sector]) {
    DEBUG_FASTROMFS("!!!ERROR, sector not erased!!!\n");
    return false;
  }
  memcpy(flash[sector], data, SECTORSIZE);
  flashErased[sector] = false;
  return true;
#endif
}


bool FastROMFilesystem::ReadSector(int sector, void *data)
{
  if ((sector < 0) || (sector >= fs.sectors) || !data) return false;
  if ((const uintptr_t)data % 4) return false; // Need to have 32-bit aligned inputs!

#ifdef ARDUINO
  return ESP.flashRead(baseAddr + sector * FLASH_SECTOR_SIZE, (uint32_t*)data, FLASH_SECTOR_SIZE);
#else
  memcpy(data, flash[sector], SECTORSIZE);
  return true;
#endif
}

bool FastROMFilesystem::ReadPartialSector(int sector, int offset, void *data, int len)
{
  if ((sector < 0) || (sector >= fs.sectors) || !data || (len < 0) || (offset < 0) || (offset + len > SECTORSIZE)) return false;

  // Easy case, everything is aligned and we can just do it...
  if ( ((offset % 4) == 0) && ((len % 4) == 0) && (((const uintptr_t)data % 4) == 0) ) {
#ifdef ARDUINO
    ESP.flashRead(baseAddr + sector * FLASH_SECTOR_SIZE + offset, (uint32_t*)data, len);
#else
    memcpy(data, &flash[sector][offset], len);
#endif
    return true;
  }

  memset(data, 0, len); // Clear buffer just for debugging sanity

  // We're gonna get wordy here for sanity's sake.  This align and shifting is a brain twister.
  uint8_t *destStart = reinterpret_cast<uint8_t*>(data);
  uint8_t *destEnd = destStart + len;
  uint8_t *destStartAligned = (uint8_t*) ((uintptr_t)(destStart + 3) & (uintptr_t) ~3);
  uint8_t *destEndAligned = (uint8_t*) ((uintptr_t)(destEnd) & (uintptr_t) ~3);
  int destLen = destEnd - destStart;
  int destLenAligned = destEndAligned - destStartAligned;
  int srcStart = offset;
  int srcEnd = srcStart + len;
  int srcLen = len;
  int srcStartAligned = (srcStart) & ~3;
  int srcEndAligned = (srcEnd + 3) & ~3;
  int srcLenAligned = srcEndAligned - srcStartAligned;
  int shiftLeftBytes = 0;
  int bytesToShift = 0;
  if (destLenAligned > 0) {
    // Read the flash aligned into the ram aligned
#ifdef ARDUINO
    ESP.flashRead(baseAddr + sector * FLASH_SECTOR_SIZE + srcStartAligned, (uint32_t*)destStartAligned, destLenAligned);
#else
    memcpy(destStartAligned, &flash[sector][srcStartAligned], destLenAligned);
#endif
    // Move it to the beginning of the buffer
    shiftLeftBytes = (destStartAligned - destStart) /* account for ram shift */ + (srcStart - srcStartAligned); /* flash shift */
    bytesToShift = destLenAligned - (srcStart - srcStartAligned); // the alignment flash bytes are thrown away, all else kept
    memmove(destStart, destStart + shiftLeftBytes, bytesToShift); // memmove because these overlap
    // Adjust the pointers to indicate what we've completed so far, again for sanity's sake.
    destStart += bytesToShift; // We wrote this many bytes
    destLen -= bytesToShift;
    memset(destStart, 0, destLen); // Clear for easier debug
    srcStart += bytesToShift;
    srcStartAligned = (srcStart) & ~3;
    srcLen -= bytesToShift;
    srcLenAligned = srcEndAligned - srcStartAligned;
  }
  // Now let's do the same thing to a stack buffer and memcpy to the final bit of ram
  uint8_t buff[64 + 8]; // bounce buffer, need to account for shift of RAM and flash
  uint8_t *alignBuff = (uint8_t*)((uintptr_t)(buff + 3) & (uintptr_t) ~3); // 32bit aligned pointer into that buffer
  // Read remainder of flash to the alignment bounce buffer.
#ifdef ARDUINO
  // Check if we have cached this data (only valid if it fits in 1 32-bit word)
  if ( (lastFlashSector == sector) && (lastFlashSectorOffset == srcStartAligned) && (srcLenAligned == 4) ) {
      *(uint32_t*)alignBuff = lastFlashSectorData;
  } else {
    // Nope, read it out
    ESP.flashRead(baseAddr + sector * FLASH_SECTOR_SIZE + srcStartAligned, (uint32_t*)alignBuff, srcLenAligned);
  
    // Store the read out data for potential use by subsequent ReadPartials if it was a single 32-bit read
    if (srcLenAligned == 4) {
      lastFlashSector = sector;
      lastFlashSectorOffset = srcStartAligned;
      lastFlashSectorData = *(uint32_t*)alignBuff;
    }
  }
#else
  memcpy(alignBuff, &flash[sector][srcStartAligned], srcLenAligned);
#endif
  // Move it to destination buffer
  memcpy(destStart, alignBuff + (srcStart - srcStartAligned), srcLen);
  // Eh voila...easy peasy, lemon squeezy
#ifndef ARDUINO
  void *simpledata = (void*)malloc(len);
  memcpy(simpledata, &flash[sector][offset], len);
  if (memcmp(simpledata, data, len))
    DEBUG_FASTROMFS("ERROR!  Misaligned read data doesn't match correct\n");
  free(simpledata);
#endif
  return true;
}

bool FastROMFilesystem::mkfs()
{
  if (fsIsMounted) return false;
  memset(&fs, 0, sizeof(fs));
  fs.magic = FSMAGIC;
  fs.epoch = 1;
  fs.sectors = totalSectors;
  for (int i = 0; i < FATCOPIES; i++) {
    SetFAT(i, FATEOF);
  }
  for (int i = 0; i < FATCOPIES; i++) {
    if (!EraseSector(i)) return false;
    if (!WriteSector(i, &fs)) return false;
  }
  // Fake Flush() out to ensure we're written...
  fsIsMounted = true;
  fsIsDirty = true;
  FlushFAT();
  fsIsMounted = false;
  fsIsDirty = false;
  return true;
}

bool FastROMFilesystem::mount()
{
  DEBUG_FASTROMFS("mount()\n");
  if (fsIsMounted) return false;
  fs.sectors = totalSectors; // We can potentially read up to this many sectors...
  int idx = FindNewestFAT();
  if (idx >= 0) {
    DEBUG_FASTROMFS("FAT is located at sector %d\n", idx);
    if (!ReadSector(idx, &fs)) return false;
    if (!ValidateFAT()) return false;
    fsIsDirty = false;
    fsIsMounted = true;
    return true;
  } else {
    DEBUG_FASTROMFS("ERROR!!! FAT NOT FOUND!\n");
    return false;
  }
}

bool FastROMFilesystem::umount()
{
  if (!fsIsMounted) return false;
  DEBUG_FASTROMFS("umount()\n");
  if (!FlushFAT()) return false;
  fsIsMounted = false;
  return true;
}

bool FastROMFilesystem::FlushFAT()
{
  DEBUG_FASTROMFS("FlushFAT(), ismounted=%d, isdirty=%d\n", !!fsIsMounted, !!fsIsDirty);
  if (!fsIsMounted || !fsIsDirty) return true; // Nothing to do here...

  fs.epoch++;
  fs.crc = 0;
  uint32_t calcCRC = 0;
  CRC32((void*)&fs, sizeof(fs), &calcCRC);
  fs.crc = calcCRC;
  int idx = FindOldestFAT();
  if (idx >= 0) {
    if (!EraseSector(idx)) return false;
    bool ret = WriteSector(idx, &fs);
    if (ret) fsIsDirty = false;
    return ret;
  }
  return false;
}

FastROMFile *FastROMFilesystem::open(const char *name, const char *mode)
{
  if (!fsIsMounted) return NULL;
  if (!name || !mode || !name[0] || !mode[0]) return NULL;

  DEBUG_FASTROMFS("open('%s', '%s')\n", name, mode);
  if (!strcmp(mode, "r") || !strcmp(mode, "rb")) { //  Open text file for reading.  The stream is positioned at the beginning of the file.
    int fidx = FindFileEntryByName(name);
    if (fidx < 0) return NULL;
    return new FastROMFile(this, fidx, 0, 0,  true, false, false, false);
  } else if (!strcmp(mode, "r+") || !strcmp(mode, "r+b")) { // Open for reading and writing.  The stream is positioned at the beginning of the file.
    int fidx = FindFileEntryByName(name);
    if (fidx < 0) return NULL;
    return new FastROMFile(this, fidx, 0, 0,  true, true, false, false);
  } else if (!strcmp(mode, "w") || !strcmp(mode, "wb")) { // Truncate file to zero length or create text file for writing.  The stream is positioned at the beginning of the file.
    unlink(name); // ignore failure, may not exist
    int fidx = CreateNewFileEntry(name);
    if (fidx < 0) return NULL; // No directory space left
    return new FastROMFile(this, fidx, 0, 0, false, true, false, true);
  } else if (!strcmp(mode, "w+") || !strcmp(mode, "w+b")) { // Open for reading and writing.  The file is created if it does not exist, otherwise it is truncated.  The stream is positioned at the beginning of the file.
    unlink(name); // ignore failure, may not exist
    int fidx = CreateNewFileEntry(name);
    if (fidx < 0) return NULL; // No directory space left
    return new FastROMFile(this, fidx, 0, 0, true, true, false, true);
  } else if (!strcmp(mode, "a") || !strcmp(mode, "ab")) { // Open for appending (writing at end of file).  The file is created if it does not exist.  The stream is positioned at the end of the file.
    int fidx = FindFileEntryByName(name);
    int sfidx = fidx;
    if (fidx < 0) fidx = CreateNewFileEntry(name);
    if (fidx < 0) return NULL; // No directory space left
    return new FastROMFile(this, fidx, 0, fs.fileEntry[fidx].len, false, true, true, sfidx < 0 ? true : false);
  } else if (!strcmp(mode, "a+") || !strcmp(mode, "a+b")) { // Open for reading and appending (writing at end of file).  The file is created if it does not exist.  The initial file position for reading is at the beginning of the file, but output is always appended to the end of the file.
    int fidx = FindFileEntryByName(name);
    int sfidx = fidx;
    if (fidx < 0) fidx = CreateNewFileEntry(name);
    if (fidx < 0) return NULL; // No directory space left
    return new FastROMFile(this, fidx, 0, fs.fileEntry[fidx].len, true, true, true, sfidx < 0 ? true : false);
  }
  return NULL;
}

FastROMFile::~FastROMFile()
{
  free(data);
}

FastROMFile::FastROMFile(FastROMFilesystem *fs, int fileIdx, int readOffset, int writeOffset, bool read, bool write, bool append, bool eraseFirstSector)
{
  this->fs = fs;
  this->modeRead = read;
  this->modeWrite = write;
  this->modeAppend = append;
  this->fileIdx = fileIdx;

  if (modeWrite || modeAppend) {
    data = (uint8_t*)malloc(SECTORSIZE);
    if (eraseFirstSector) {
      memset(data, 0, SECTORSIZE);
      fs->EraseSector(fs->GetFileEntryFAT(fileIdx));
      fs->WriteSector(fs->GetFileEntryFAT(fileIdx), data);
    }
  } else {
    data = NULL;
  }
  dataDirty = false;

  readPos = readOffset;
  writePos = writeOffset;

  curWriteSector = -1;
  curWriteSectorOffset = -SECTORSIZE;
  curReadSector = -1;
  curReadSectorOffset = -SECTORSIZE;

}

int FastROMFile::fgetc()
{
  uint8_t c;
  if (0 == read(&c, 1)) return -1;
  return c;
}

int FastROMFile::fputc(int c)
{
  uint8_t cc = (uint8_t)c;
  if (0 == write(&cc, 1)) return -1;
  return 0;
}

int FastROMFile::size()
{
  return fs->GetFileEntryLen(fileIdx);
}

void FastROMFile::name(char *buff, int len)
{
  char name[NAMELEN + 1];
  fs->GetFileEntryName(fileIdx, name);
  name[NAMELEN] = 0;
  strncpy(buff, name, len);
  buff[len - 1] = 0;
}


int FastROMFile::tell()
{
  if (modeRead) return readPos;
  return writePos;
}

int FastROMFile::eof()
{
  if (modeRead) return (readPos == fs->GetFileEntryLen(fileIdx)) ? true : false;
  return false;  //TODO...what does eof() on a writable only file mean?
}

size_t FastROMFile::write(const uint8_t *out, size_t size)
{
  if (!size || !out || !modeWrite) return 0;
  size_t writtenBytes = 0;

  // Make sure we're writing somewhere within the current sector
  if (! ( (curWriteSectorOffset <= writePos) && ((curWriteSectorOffset + SECTORSIZE) > writePos) ) ) {
    if (dataDirty) {
      if (!fs->EraseSector(curWriteSector)) return 0;
      if (!fs->WriteSector(curWriteSector, data)) return 0;
    }
    // Traverse the FAT table, optionally extending the file
    curWriteSector = fs->GetFileEntryFAT(fileIdx);
    curWriteSectorOffset = 0;
    int lastSector = -1; // Used to update file links
    while (! ( (curWriteSectorOffset <= writePos) && ((curWriteSectorOffset + SECTORSIZE) > writePos) ) ) {
      lastSector = curWriteSector;
      if (fs->GetFAT(curWriteSector) == FATEOF) { // Need to extend
        int newSector = fs->FindFreeSector();
        if (newSector < 0) return 0; // Out of space
        fs->SetFAT(curWriteSector, newSector);
        fs->SetFAT(newSector, FATEOF);
        curWriteSector = newSector;
        memset(data, 0, SECTORSIZE);
        if (!fs->EraseSector(curWriteSector)) return 0;
        if (!fs->WriteSector(curWriteSector, data)) return 0;
      } else {
        curWriteSector = fs->GetFAT(curWriteSector);
      }
      curWriteSectorOffset += SECTORSIZE;
    }
    if (fs->GetFileEntryLen(fileIdx) > curWriteSectorOffset) { // Read in old data
      if (!fs->ReadSector(curWriteSector, data)) return 0;
      // Try and allocate a new sector to write the updated data, update the FAT links
      int newSector = fs->FindFreeSector();
      if (newSector > 0) {
        if (lastSector==-1) {
          int nextSector = fs->GetFAT(fs->GetFileEntryFAT(fileIdx));
          fs->SetFileEntryFAT(fileIdx, newSector);
          fs->SetFAT(newSector, nextSector);
        } else {
          int nextSector = fs->GetFAT(curWriteSector);
          fs->SetFAT(lastSector, newSector);
          fs->SetFAT(newSector, nextSector);
        }
        dataDirty = true; // We definitely need to rewrite, no matter what happens later on
        fs->SetFAT(curWriteSector, 0); // Free original block
        curWriteSector = newSector;
      } else {
        // No space, just leave it where it is...
      }
    } else { // New sector...
      memset(data, 0, SECTORSIZE);
    }
    fs->SetFileEntryLen(fileIdx, max(fs->GetFileEntryLen(fileIdx), curWriteSectorOffset));
  }

  // We're in the correct sector.  Start writing and extending/overwriting
  while (size) {
    int amountWritableInThisSector = min((int)size, (int)(SECTORSIZE - (writePos % SECTORSIZE)));
    if (writePos >= curWriteSectorOffset + SECTORSIZE) amountWritableInThisSector = 0;
    if (amountWritableInThisSector == 0) {
      if (dataDirty) { // need to flush this sector
        if (!fs->EraseSector(curWriteSector)) return 0;
        if (!fs->WriteSector(curWriteSector, data)) return 0;
        dataDirty = false;
      }
      if (fs->GetFAT(curWriteSector) != FATEOF) { // Update - read in old data
        curWriteSector = fs->GetFAT(curWriteSector);
        if (!fs->ReadSector(curWriteSector, data)) return 0;
      } else { // Extend the file
        int newSector = fs->FindFreeSector();
        if (newSector < 0) return 0; // Out of space
        fs->SetFAT(curWriteSector, newSector);
        curWriteSector = newSector;
        fs->SetFAT(newSector, FATEOF);
      }
      curWriteSectorOffset = writePos;
      amountWritableInThisSector = min(size, SECTORSIZE);
    }
    // By now either have writable space in old or new sector
    memcpy(&data[writePos % SECTORSIZE], out, amountWritableInThisSector);
    dataDirty = true; // We need to flush this on close() or leaving the sector
    writePos += amountWritableInThisSector; // We wrote this little bit
    writtenBytes += amountWritableInThisSector;
    if (!modeAppend) readPos = writePos;
    fs->SetFileEntryLen(fileIdx, max(fs->GetFileEntryLen(fileIdx), writePos)); // Potentially we just extended the file
    // Reduce bytes available to write, increment data pointer
    size -= amountWritableInThisSector;
    out = reinterpret_cast<const uint8_t*>(out) + amountWritableInThisSector;
  }

  return writtenBytes;
}

int FastROMFile::close()
{
  DEBUG_FASTROMFS("close()\n");
  if (!modeWrite && !modeAppend) return 0;
  if (!dataDirty) return 0;
  if (!fs->EraseSector(curWriteSector)) return -1;
  int ret = fs->WriteSector(curWriteSector, data) ? 0 : -1;
  delete this;
  return ret;
}


int FastROMFile::sync()
{
  if (!modeWrite && !modeAppend) return 0;
  if (!dataDirty) return 0;
  if (!fs->EraseSector(curWriteSector)) return -1;
  if (!fs->WriteSector(curWriteSector, data)) return -1;
  dataDirty = false;
  return fs->FlushFAT();
}

int FastROMFile::read(void *in, int size)
{
  if (!modeRead || !in || !size) return 0;

  int readableBytesInFile = fs->GetFileEntryLen(fileIdx) - readPos;
  size = min(readableBytesInFile, size); // We can only read to the end of file...
  if (size <= 0) return 0;

  int readBytes = 0;

  // Make sure we're reading from somewhere in the current sector
  if (! ( (curReadSectorOffset <= readPos) && ((curReadSectorOffset + SECTORSIZE) > readPos) ) ) {
    // Traverse the FAT table, optionally extending the file
    curReadSector = fs->GetFileEntryFAT(fileIdx);
    curReadSectorOffset = 0;
    while (! ( (curReadSectorOffset <= readPos) && ((curReadSectorOffset + SECTORSIZE) > readPos) ) ) {
      if (fs->GetFAT(curReadSector) == FATEOF) { // Oops, reading past EOF!
        return 0; // EOF!...this path shouldn't happen...
      } else {
        curReadSector = fs->GetFAT(curReadSector);
      }
      curReadSectorOffset += SECTORSIZE;
    }
  }

  while (size) {
    int offsetIntoData = readPos % SECTORSIZE; //= pointer into data[]
    int amountReadableInThisSector = min(size, SECTORSIZE - (readPos % SECTORSIZE));
    if (readPos > curReadSectorOffset + SECTORSIZE) amountReadableInThisSector = 0;
    if (amountReadableInThisSector == 0) {
      if (curReadSector == FATEOF) { // end
        return readBytes; // Hit EOF...again, should not happen ever
      } else {
        curReadSector = fs->GetFAT(curReadSector);
      }
      curReadSectorOffset += SECTORSIZE;
      amountReadableInThisSector = min(size, SECTORSIZE);
    }
    if (curReadSector == curWriteSector) { // R-A-W, so forward the data
      memcpy(in, &data[offsetIntoData], amountReadableInThisSector);
    } else {
      if (!fs->ReadPartialSector(curReadSector, offsetIntoData, in, amountReadableInThisSector)) return 0;
    }
    readPos += amountReadableInThisSector;
    if (!modeAppend) writePos = readPos;
    size -= amountReadableInThisSector;
    readBytes += amountReadableInThisSector;
    in = reinterpret_cast<char*>(in) + amountReadableInThisSector;
  }
  return readBytes;
}

bool FastROMFile::seek(int off, int whence)
{
  int absolutePos; // = offset we want to seek to from start of file
  switch (whence) {
    case SEEK_SET: absolutePos = off; break;
    case SEEK_CUR: absolutePos = readPos + off; break;
    case SEEK_END: absolutePos = fs->GetFileEntryLen(fileIdx) + off; break;
    default: return false;
  }
  if (absolutePos < 0) return -1; // Can't seek before beginning of file
  if (modeAppend) {
    if (!modeRead) return -1; // seeks not allowed on append
  } else {
    readPos = absolutePos; // a+ => read can move, write always appends
    writePos = absolutePos; // a+ => read can move, write always appends
  }
  return true;
}
