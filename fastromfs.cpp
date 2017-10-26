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
#include <CRC32.h>
#else
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#endif

#include "fastromfs.h"



#ifndef min
	#define min(a,b) (((a)>(b))?(b):(a))
#endif
#ifndef max
	#define max(a,b) (((a)>(b))?(a):(b))
#endif


bool Filesystem::exists(const char *name)
{
	if (!fsIsMounted) return false;

	if (FindFileEntryByName(name)>=0) return true;
	return false;
}

bool Filesystem::rename(const char *old, const char *newName)
{
	if (!fsIsMounted) return false;
	int idx = FindFileEntryByName(old);
	int newIdx = FindFileEntryByName(newName);
	if ((idx>=0) && (newIdx==-1)) {
		SetFileEntryName(idx, newName);
		return true;
	}
	return false;
}


void Filesystem::GetFileEntryName(int idx, char *dest)
{
	memcpy(dest, fs.fileEntry[idx].name, NAMELEN);
}

int Filesystem::GetFileEntryLen(int idx)
{
	return fs.fileEntry[idx].len;
}

int Filesystem::GetFileEntryFAT(int idx)
{
	return fs.fileEntry[idx].fat;
}

void Filesystem::SetFileEntryName(int idx, const char *src)
{
	strncpy(fs.fileEntry[idx].name, src, NAMELEN);
	fsIsDirty = true;
}

void Filesystem::SetFileEntryLen(int idx, int len)
{
	fs.fileEntry[idx].len = len;
	fsIsDirty = true;
}


void Filesystem::SetFileEntryFAT(int idx, int fat)
{
	fs.fileEntry[idx].fat = fat;
	fsIsDirty = true;
}



Dir *Filesystem::opendir()
{
	if (!fsIsMounted) return NULL;
	struct dirent *de = (struct dirent *)malloc(sizeof(dirent));
	if (!de) return NULL; // OOM
	de->off = -1;
	return (void*)de;
}

struct dirent *Filesystem::readdir(Dir *dir)
{
	if (!fsIsMounted) return NULL;
	struct dirent *de = reinterpret_cast<struct dirent *>(dir);
	de->off++;
	while (de->off < FILEENTRIES) {
		char name[NAMELEN];
		GetFileEntryName(de->off, name);
		if (name[0]) {
			strncpy(de->name, name, sizeof(name));
			de->name[sizeof(de->name)-1] = 0;
			de->len = GetFileEntryLen(de->off);
			return de;
		}
		de->off++;
	}
	return NULL;
}

int Filesystem::closedir(Dir *dir)
{
	if (!fsIsMounted) return false;
	if (!dir) return -1;
	free(dir);
	return 0;
}

#ifdef ARDUINO
void crc32(const void *data, size_t n_bytes, uint32_t* crc)
{
  *crc = 0;
  CRC32 crc32;
  crc32.update(data, n_bytes);
  *crc = crc32.finalize();
}
#else
uint32_t crc32_for_byte(uint32_t r) {
	for (int j = 0; j < 8; ++j) {
		r = (r & 1? 0: (uint32_t)0xEDB88320L) ^ r >> 1;
	}
	return r ^ (uint32_t)0xFF000000L;
}

void crc32(const void *data, size_t n_bytes, uint32_t* crc) {
	static uint32_t table[0x100];
	if (!*table) {
		for (size_t i = 0; i < 0x100; ++i) {
			table[i] = crc32_for_byte(i);
		}
	}
	for (size_t i = 0; i < n_bytes; ++i) {
		*crc = table[(uint8_t)*crc ^ ((uint8_t*)data)[i]] ^ *crc >> 8;
	}
}
#endif


Filesystem::Filesystem()
{
#ifndef ARDUINO
  for (int i=0; i<FATENTRIES; i++) flashErased[i] = false;
#endif
	fsIsDirty = false;
  fsIsMounted = false;
}

Filesystem::~Filesystem()
{
	if (fsIsMounted) umount();
}


void Filesystem::DumpFS()
{
	printf("%-32s - %-5s - %-5s\n", "name", "len", "fat");
	for (int i=0; i<FILEENTRIES; i++) {
		if (fs.fileEntry[i].name[0]) printf("%32s - %5d - %5d\n", fs.fileEntry[i].name, fs.fileEntry[i].len, fs.fileEntry[i].fat);
	}
//	for (int i=0; i<FATENTRIES; i++) printf("%s%5d:%-5d ", 0==(i%8)?"\n":"", i, GetFAT(i));
	printf("\n\n");
}

void Filesystem::DumpSector(int sector)
{
#ifdef ARDUINO
  (void)sector;
#else
  printf("Sector: %d", sector);
	for (int i=0; i<SECTORSIZE; i++) printf("%s%02x ", (i%32)==0?"\n":"", flash[sector][i]);
	printf("\n");
#endif
}

int Filesystem::available()
{
	if (!fsIsMounted) return false;
	int avail = 0;
	for (int i=0; i<FATENTRIES; i++) {
		if (GetFAT(i)==0) avail += SECTORSIZE;
	}
	return avail;
}

int Filesystem::fsize(const char *name)
{
	if (!fsIsMounted) return false;
	int idx = FindFileEntryByName(name);
	if (idx < 0) return -1;
	return GetFileEntryLen(idx);
}

bool Filesystem::ValidateFAT()
{
	if (fs.magic != FSMAGIC) return false;
	uint32_t savedCRC = fs.crc;
	uint32_t calcCRC = 0;
	fs.crc = 0;
	crc32((void*)&fs, sizeof(fs), &calcCRC);
	if (savedCRC != calcCRC) return false; // Something baaaad here!
	return true;
}

int Filesystem::FindOldestFAT()
{
	int oldIdx = 0;
	int64_t oldEpoch = 1LL<<62;
	for (int i=0; i<FATCOPIES; i++) {
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
int Filesystem::FindNewestFAT()
{
	int newIdx = 0;
	int64_t newEpoch = 0;
	for (int i=0; i<FATCOPIES; i++) {
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

int Filesystem::FindFileEntryByName(const char *name)
{
	if (!name) return -1;

	for (int i=0; i<FILEENTRIES; i++) {
		if (!strncmp(fs.fileEntry[i].name, name, sizeof(fs.fileEntry[i].name))) return i;
	}
	return -1;
}

int Filesystem::FindFreeFileEntry()
{
	for (int i=0; i<FILEENTRIES; i++) {
		if (fs.fileEntry[i].name[0] == 0) return i;
	}
	return -1; // No space
}

int Filesystem::CreateNewFileEntry(const char *name)
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

bool Filesystem::unlink(const char *name)
{
	if (!fsIsMounted) return false;
printf("unlink('%s')\n", name);
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

int Filesystem::GetFAT(int idx)
{
	if ((idx < 0) || (idx >= FATENTRIES)) return -1;

	int bo = (idx/2) * 3;
	int ret;
	if (idx & 1) {
		ret = fs.fat[bo+1] & 0x0f;
		ret <<= 8;
		ret |= fs.fat[bo+2];
	} else {
		ret = fs.fat[bo+1] & 0xf0;
		ret <<= 4;
		ret |= fs.fat[bo];
	}
	return ret;
}

void Filesystem::SetFAT(int idx, int val)
{
	if ((idx < 0) || (idx >= FATENTRIES)) return;

	int bo = (idx/2) * 3;
	if (idx & 1) {
		fs.fat[bo+1] &= ~0x0f;
		fs.fat[bo+1] |= (val>>8) & 0x0f;
		fs.fat[bo+2] = val & 0xff;
	} else {
		fs.fat[bo+1] &= ~0xf0;
		fs.fat[bo+1] |= (val>>4) & 0xf0;
		fs.fat[bo] = val & 0xff;
	}

	fsIsDirty = true;
}

int Filesystem::FindFreeSector()
{
	int a = rand() % FATENTRIES;
	for (int i=0; (i<FATENTRIES) && (GetFAT(a) != 0);  i++, a = (a+1)%FATENTRIES) { /*empty*/ }
	if (GetFAT(a) != 0) return -1;
	return a;
}

#ifdef ARDUINO
static uint32_t baseAddr = ((ESP.getSketchSize() + FLASH_SECTOR_SIZE - 1) & (~(FLASH_SECTOR_SIZE - 1)));
static uint32_t baseSector = baseAddr / FLASH_SECTOR_SIZE;
#endif

bool Filesystem::EraseSector(int sector) 
{
	if ((sector<0) || (sector >= FATENTRIES)) return false;

	printf("EraseSector(%d)\nA", sector);
 #ifdef ARDUINO
   return ESP.flashEraseSector(baseSector + sector);
#else
	memset(flash[sector], 0, SECTORSIZE);
	flashErased[sector] = true;
	return true;
#endif
}

bool Filesystem::WriteSector(int sector, const void *data)
{
	if ((sector<0) || (sector >= FATENTRIES) || !data) return false;
	if ((const uintptr_t)data % 4) return false; // Need to have 32-bit aligned inputs!

	printf("WriteSector(%d, data)\n", sector);
#ifdef ARDUINO
  return ESP.flashWrite(baseAddr + sector * FLASH_SECTOR_SIZE, (uint32_t*)data, FLASH_SECTOR_SIZE);
#else
	if (!flashErased[sector]) {
		printf("!!!ERROR, sector not erased!!!\n");
		return false;
	}
	memcpy(flash[sector], data, SECTORSIZE);
	flashErased[sector] = false;
	return true;
#endif
}


bool Filesystem::ReadSector(int sector, void *data)
{
	if ((sector<0) || (sector >=FATENTRIES) || !data) return false;
	if ((const uintptr_t)data % 4) return false; // Need to have 32-bit aligned inputs!

#ifdef ARDUINO
  return ESP.flashRead(baseAddr + sector * FLASH_SECTOR_SIZE, (uint32_t*)data, FLASH_SECTOR_SIZE);
#else
	memcpy(data, flash[sector], SECTORSIZE);
	return true;
#endif
}

bool Filesystem::ReadPartialSector(int sector, int offset, void *data, int len)
{
	if ((sector<0) || (sector >=FATENTRIES) || !data || (len < 0) || (offset < 0) || (offset+len > SECTORSIZE)) return false;

	// Easy case, everything is aligned and we can just do it...
	if ( ((offset % 4)==0) && ((len % 4)==0) && (((const uintptr_t)data % 4)==0) ) {
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
	uint8_t *destStartAligned = (uint8_t*) ((uintptr_t)(destStart+3) & (uintptr_t) ~3);
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
	uint8_t *alignBuff = (uint8_t*)((uintptr_t)(buff+3) & (uintptr_t) ~3); // 32bit aligned pointer into that buffer
	// Read remainder of flash to the alignment bounce buffer.
#ifdef ARDUINO
  ESP.flashRead(baseAddr + sector * FLASH_SECTOR_SIZE + srcStartAligned, (uint32_t*)alignBuff, srcLenAligned);
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
		printf("ERROR!\n");
#endif
	return true;
}

bool Filesystem::mkfs()
{
	if (fsIsMounted) return false;
	memset(&fs, 0, sizeof(fs));
	fs.magic = FSMAGIC;
	fs.epoch = 1;
	for (int i = 0; i<FATCOPIES; i++) {
		SetFAT(i, FATEOF);
	}
	for (int i=0; i<FATCOPIES; i++) {
		if (!EraseSector(i)) return false;
		if (!WriteSector(i, &fs)) return false;
	}
	fsIsMounted = true;
	fsIsDirty = true;
	FlushFAT();
	fsIsMounted = false;
	fsIsDirty = false;
	return true;
}

bool Filesystem::mount()
{
	if (fsIsMounted) return false;
	printf("mount()\n");
	int idx = FindNewestFAT();
	if (idx >= 0) {
		if (!ReadSector(idx, &fs)) return false;
		if (!ValidateFAT()) return false;
		fsIsDirty = false;
		fsIsMounted = true;
		return true;
	}
	return false;
}

bool Filesystem::umount()
{
	if (!fsIsMounted) return false;
printf("umount()\n");
	if (!FlushFAT()) return false;
	return true;
}

bool Filesystem::FlushFAT()
{
printf("FlushFAT(), ismounted=%d, isdirty=%d\n", !!fsIsMounted, !!fsIsDirty);
	if (!fsIsMounted || !fsIsDirty) return true; // Nothing to do here...

	fs.epoch++;
	fs.crc = 0;
	uint32_t calcCRC = 0;
	crc32((void*)&fs, sizeof(fs), &calcCRC);
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

File *Filesystem::open(const char *name, const char *mode)
{
	if (!fsIsMounted) return NULL;
	if (!name || !mode) return NULL;

printf("open('%s', '%s')\n", name, mode);
	if (!strcmp(mode, "r") || !strcmp(mode, "rb")) { //  Open text file for reading.  The stream is positioned at the beginning of the file.
		int fidx = FindFileEntryByName(name);
		if (fidx < 0) return NULL;
		return new File(this, fidx, 0, 0,  true, false, false);
	} else if (!strcmp(mode, "r+") || !strcmp(mode, "r+b")) { // Open for reading and writing.  The stream is positioned at the beginning of the file.
		int fidx = FindFileEntryByName(name);
		if (fidx < 0) return NULL;
		return new File(this, fidx, 0, 0,  true, true, false);
	} else if (!strcmp(mode, "w") || !strcmp(mode, "wb")) { // Truncate file to zero length or create text file for writing.  The stream is positioned at the beginning of the file.
		unlink(name); // ignore failure, may not exist
		int fidx = CreateNewFileEntry(name);
		return new File(this, fidx, 0, 0, false, true, false);
	} else if (!strcmp(mode, "w+") || !strcmp(mode, "w+b")) { // Open for reading and writing.  The file is created if it does not exist, otherwise it is truncated.  The stream is positioned at the beginning of the file.
		unlink(name); // ignore failure, may not exist
		int fidx = CreateNewFileEntry(name);
		return new File(this, fidx, 0, 0, true, true, false);
	} else if (!strcmp(mode, "a") || !strcmp(mode, "ab")) { // Open for appending (writing at end of file).  The file is created if it does not exist.  The stream is positioned at the end of the file.
		int fidx = FindFileEntryByName(name);
		if (fidx < 0) fidx = CreateNewFileEntry(name);
		return new File(this, fidx, 0, fs.fileEntry[fidx].len, false, true, true);
	} else if (!strcmp(mode, "a+") || !strcmp(mode, "a+b")) { // Open for reading and appending (writing at end of file).  The file is created if it does not exist.  The initial file position for reading is at the beginning of the file, but output is always appended to the end of the file.
		int fidx = FindFileEntryByName(name);
		if (fidx < 0) fidx = CreateNewFileEntry(name);
		return new File(this, fidx, 0, fs.fileEntry[fidx].len, true, true, true);
	}
	return NULL;
}

File::File(Filesystem *fs, int fileIdx, int readOffset, int writeOffset, bool read, bool write, bool append)
{
	this->fs = fs;
	this->modeRead = read;
	this->modeWrite = write;
	this->modeAppend = append;
	this->fileIdx = fileIdx;

	if (modeWrite||modeAppend) data = new uint8_t[SECTORSIZE];
        dataDirty = false;

        readPos = readOffset;
	writePos = writeOffset;

        curWriteSector = -1;
        curWriteSectorOffset = -SECTORSIZE;
        curReadSector = -1;
        curReadSectorOffset = -SECTORSIZE;

}

int File::fgetc()
{
	uint8_t c;
	if (0 == read(&c, 1)) return -1;
	return c;
}

int File::fputc(int c)
{
	uint8_t cc = (uint8_t)c;
	if (0 == write(&cc, 1)) return -1;
	return 0;
}

int File::size()
{
	return fs->GetFileEntryLen(fileIdx);
}

void File::name(char *buff, int len)
{
	char name[NAMELEN+1];
	fs->GetFileEntryName(fileIdx, name);
	name[NAMELEN] = 0;
	strncpy(buff, name, len);
	buff[len-1] = 0;
}


int File::tell()
{
	if (modeRead) return readPos;
	return writePos;
}

int File::eof()
{
	if (modeRead) return (readPos == fs->GetFileEntryLen(fileIdx)) ? true : false;
	return false;  //TODO...what does eof() on a writable only file mean?
}

int File::write(const void *out, int size)
{
	if (!size || !out || !modeWrite) return 0;
	int writtenBytes = 0;

	// Make sure we're writing somewhere within the current sector
	if (! ( (curWriteSectorOffset <= writePos) && ((curWriteSectorOffset+SECTORSIZE) > writePos) ) ) {
		if (dataDirty) {
			if (!fs->EraseSector(curWriteSector)) return 0;
			if (!fs->WriteSector(curWriteSector, data)) return 0;
		}
		// Traverse the FAT table, optionally extending the file
		curWriteSector = fs->GetFileEntryFAT(fileIdx);
		curWriteSectorOffset = 0;
		while (! ( (curWriteSectorOffset <= writePos) && ((curWriteSectorOffset+SECTORSIZE) > writePos) ) ) {
			if (fs->GetFAT(curWriteSector) == FATEOF) { // Need to extend
				int newSector = fs->FindFreeSector();
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
		} else { // New sector...
			memset(data, 0, SECTORSIZE);
		}
		fs->SetFileEntryLen(fileIdx, max(fs->GetFileEntryLen(fileIdx), curWriteSectorOffset));
	}

	// We're in the correct sector.  Start writing and extending/overwriting
	while (size) {
		int amountWritableInThisSector = min(size, SECTORSIZE - (writePos % SECTORSIZE));
		if (writePos >= curWriteSectorOffset+SECTORSIZE) amountWritableInThisSector = 0;
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
		out = reinterpret_cast<const char*>(out) + amountWritableInThisSector;
	}

	return writtenBytes;
}

int File::close()
{
printf("close()\n");
	if (!modeWrite && !modeAppend) return 0;
	if (!dataDirty) return 0;
	if (!fs->EraseSector(curWriteSector)) return -1;
	int ret = fs->WriteSector(curWriteSector, data) ? 0 : -1;
	delete this;
	return ret;
}


int File::read(void *in, int size)
{
	if (!modeRead || !in || !size) return 0;

	int readableBytesInFile = fs->GetFileEntryLen(fileIdx) - readPos;
	size = min(readableBytesInFile, size); // We can only read to the end of file...
	if (size <= 0) return 0;

	int readBytes = 0;

	// Make sure we're reading from somewhere in the current sector
	if (! ( (curReadSectorOffset <= readPos) && ((curReadSectorOffset+SECTORSIZE) > readPos) ) ) {
		// Traverse the FAT table, optionally extending the file
		curReadSector = fs->GetFileEntryFAT(fileIdx);
		curReadSectorOffset = 0;
		while (! ( (curReadSectorOffset <= readPos) && ((curReadSectorOffset+SECTORSIZE) > readPos) ) ) {
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
		if (readPos > curReadSectorOffset+SECTORSIZE) amountReadableInThisSector = 0;
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

int File::seek(int off, int whence)
{
	int absolutePos; // = offset we want to seek to from start of file
	switch(whence) {
		case SEEK_SET: absolutePos = off; break;
		case SEEK_CUR: absolutePos = readPos + off; break;
		case SEEK_END: absolutePos = fs->GetFileEntryLen(fileIdx) + off; break;
		default: return -1;
	}
	if (absolutePos < 0) return -1; // Can't seek before beginning of file
	if (modeAppend) {
		if (!modeRead) return -1; // seeks not allowed on append
	} else {
		readPos = absolutePos; // a+ => read can move, write always appends
		writePos = absolutePos; // a+ => read can move, write always appends
	}
	return 0;
}


#ifdef ARDUINO
void RunFSTest()
#else
int main(int argc, char **argv)
#endif
{
	int len;
	char buff[1001];
#ifndef ARDUINO
	srand(time(NULL));
#endif
	Filesystem *fs = new Filesystem;
	fs->mkfs();
	printf("mount ret = %d\n", fs->mount());
	printf("Bytes Free: %d\n", fs->available());
	File *f = fs->open("test.bin", "w");
	for (int i=0; i<200; i++) {
		f->write("0123456789", 10);
		f->write("abcdefghij", 10);
	}
	for (int i=0; i<200; i++) {
		f->write("0123456789", 10);
		f->write("abcdefghij", 10);
	}
	f->seek(12, SEEK_SET);
	f->write("Earle Is At 12", 14);
	f->close();
	fs->DumpFS();

	f = fs->open("test.bin", "r");
	f->seek(2);
	len = f->read(buff+1, 64);
	buff[len+1] = 0;
	printf("buff@2='%s'\n", buff+1);
	f->close();
//	exit(1);

	f = fs->open("test.bin", "r");
	do {
		len = f->read(buff, 1000);
		buff[1000] = 0;
		printf("buff='%s'\n", buff);
	} while (len);
	f->seek(-998, SEEK_END);
	len = f->read(buff, 1000);
	buff[1000] = 0;
	printf("buffx='%s'\n", buff);
	f->close();

	f = fs->open("test.bin", "r+");
	f->seek(4080, SEEK_SET);
	f->write("I Am Spanning A 4K Block!", 25);
	f->seek(4070);
	f->read(buff, 1000);
	buff[1000] = 0;
	printf("buffx='%s'\n", buff);
	f->close();

	f = fs->open("newfile.txt", "w");
	f->write("Four score and seven years ago our forefathers......", 50);
	f->close();

	f = fs->open("test.bin", "r+");
	f->read(buff, 50);
	buff[50]=0;
	printf("buffx='%s'\n", buff);
	f->close();

	f = fs->open("newfile.txt", "r+");
	f->read(buff, 50);
	buff[50]=0;
	printf("buffx='%s'\n", buff);
	f->close();

	printf("Bytes Free: %d\n", fs->available());

	fs->DumpFS();

	printf("newfile.txt: %d bytes\n", fs->fsize("newfile.txt"));
	printf("test.bin: %d bytes\n", fs->fsize("test.bin"));



	fs->umount();

	printf("UNMOUNT/REMOUNT...\n");
	fs->mount();
	Dir *d = fs->opendir();
	do {
		struct dirent *de = fs->readdir(d);
		if (!de) break;
		printf("File: '%s', len=%d\n", de->name, de->len);
	} while (1);
	fs->closedir(d);


	fs->rename("newfile.txt", "gettysburg.txt");
	d = fs->opendir();
	do {
		struct dirent *de = fs->readdir(d);
		if (!de) break;
		printf("File: '%s', len=%d\n", de->name, de->len);
	} while (1);
	fs->closedir(d);

	f = fs->open("gettysburg.txt", "a+");
	f->read(buff, 30);
	buff[30] = 0;
	printf("buff='%s', tell=%d\n", buff, f->tell());
	f->write("I forget the rest", 17);
	printf("appended read = '");
	while (int l=f->read(buff, 30)) {
		buff[l] = 0;
		printf("%s", buff);
	}
	printf("'\n");
	f->close();


	f = fs->open("expand.bin", "w");
	f->seek(5000, SEEK_SET);
	f->write("@10,000", 8);
	f->close();
	fs->DumpFS();
	f = fs->open("expand.bin", "rb");
	int zeros = 0;
	char c;
	do {
		f->read(&c, 1);
		if (c==0) zeros++;
		else break;
	} while(1);
	printf("I found %d zeros before the text: '", zeros);
	do {
		printf("%c", c);
		f->read(&c, 1);
		if (c==0) break;
	} while(1);
	printf("'\n");
	f->close();

	f = fs->open("gettysburg.txt", "r");
	int flencalc=0;
	while (!f->eof()) { f->read(&c, 1); flencalc++; }
	printf("LEN=%d, calcLEN=%d\n", f->size(), flencalc);
	f->close();

	f = fs->open("bytebybyte.bin", "w+");
	c = 'a';
	for (int i=0; i<4096*2; i++)
		f->write(&c, 1);
	f->seek(0, SEEK_SET);
	zeros = 0;
	do {
		int x = f->read(&c, 1);
		if (x==1) zeros++;
		else break;
	} while(1);
	printf("I read %d bytes\n", zeros);

	f->close();

#ifndef ARDUINO
	f = fs->open("test.bin", "rb");
	int sz = f->size();
	srand(123); // Repeatable test
	for (int i=0; i<10000; i++) {
		int off = rand() % sz;
		int len = rand() % 100;
		f->seek(off);
		f->read(buff, len);
		if (i%100 == 0) printf("++++Loop %d\n", i);
	}
	f->close();
#endif

	f = fs->open("gettysburg.txt", "r");
	printf("fgetc test: '");
	while (1) {
		int c = f->fgetc();
		if (c < 0) break;
		printf("%c", (char)c);
	}
	printf("'\n");
	f->close();


	fs->umount();


	delete fs;

}

