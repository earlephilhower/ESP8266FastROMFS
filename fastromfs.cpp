#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>


#define FSMAGIC 0xdead0beef0f00dll
#define SECTORSIZE 4096
#define FSSIZEMB 3
#define FATENTRIES (FSSIZEMB * 1024 * 1024 / 4096)
#define FATBYTES ((FATENTRIES * 12) / 8)
#define FILEENTRIES ((int)((SECTORSIZE - (sizeof(uint64_t) + sizeof(uint64_t) + FATBYTES + sizeof(uint32_t)))/sizeof(FileEntry)))
#define FATEOF 0xfff
#define FATCOPIES 8
#define NAMELEN 24

#ifndef min
	#define min(a,b) (((a)>(b))?(b):(a))
#endif
#ifndef max
	#define max(a,b) (((a)>(b))?(a):(b))
#endif

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

typedef void Dir; // Opaque for the masses

struct dirent {
	int off;
	char name[NAMELEN + 1]; // Ensure space for \0
	int len;
};

class File;

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
	int available();
	int fsize(const char *name);
	Dir *opendir(const char *ignored) { return opendir(); };
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
	FileEntry *GetFileEntry(int idx);
	bool FlushFAT();
	int FindOldestFAT();
	int FindNewestFAT();
	bool ValidateFAT();

private:
	FilesystemInFlash fs;
	uint8_t flash[FATENTRIES][SECTORSIZE];
	bool flashErased[FATENTRIES];
};


Dir *Filesystem::opendir()
{
	struct dirent *de = (struct dirent *)malloc(sizeof(dirent));
	if (!de) return NULL; // OOM
	de->off = -1;
	return (void*)de;
}

struct dirent *Filesystem::readdir(Dir *dir)
{
	struct dirent *de = reinterpret_cast<struct dirent *>(dir);
	de->off++;
	while (de->off < FILEENTRIES) {
		FileEntry *f = GetFileEntry(de->off);
		if (f->name[0]) {
			strncpy(de->name, f->name, sizeof(f->name));
			de->name[sizeof(de->name)-1] = 0;
			de->len = f->len;
			return de;
		}
		de->off++;
	}
	return NULL;
}

int Filesystem::closedir(Dir *dir)
{
	if (!dir) return -1;
	free(dir);
	return 0;
}

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



Filesystem::Filesystem()
{
	for (int i=0; i<FATENTRIES; i++) flashErased[i] = false;
}

Filesystem::~Filesystem()
{
}


void Filesystem::DumpFS()
{
	printf("%-32s - %-5s - %-5s\n", "name", "len", "fat");
	for (int i=0; i<FILEENTRIES; i++) printf("%32s - %5d - %5d\n", fs.fileEntry[i].name, fs.fileEntry[i].len, fs.fileEntry[i].fat);
	for (int i=0; i<FATENTRIES; i++) printf("%s%5d:%-5d ", 0==(i%8)?"\n":"", i, GetFAT(i));
	printf("\n\n");
}

void Filesystem::DumpSector(int sector)
{
	printf("Sector: %d", sector);
	for (int i=0; i<SECTORSIZE; i++) printf("%s%02x ", (i%32)==0?"\n":"", flash[sector][i]);
	printf("\n");
}

int Filesystem::available()
{
	int avail = 0;
	for (int i=0; i<FATENTRIES; i++) {
		if (GetFAT(i)==0) avail += SECTORSIZE;
	}
	return avail;
}

int Filesystem::fsize(const char *name)
{
	int idx = FindFileEntryByName(name);
	if (idx < 0) return -1;
	FileEntry *f = GetFileEntry(idx);
	return f->len;
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
	for (int i=0; i<FATENTRIES; i++) {
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
	for (int i=0; i<FATENTRIES; i++) {
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

FileEntry *Filesystem::GetFileEntry(int idx)
{
	if ((idx < 0) || (idx >=FILEENTRIES)) return NULL;

	return &fs.fileEntry[idx];
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
	SetFAT(sec, FATEOF);
	if (!FlushFAT()) return -1;
	return idx;
}

bool Filesystem::unlink(const char *name)
{
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
}

int Filesystem::FindFreeSector()
{
	int a = rand() % FATENTRIES;
	for (int i=0; (i<FATENTRIES) && (GetFAT(a) != 0);  i++, a = (a+1)%FATENTRIES) { /*empty*/ }
	if (GetFAT(a) != 0) return -1;
	return a;
}

bool Filesystem::EraseSector(int sector) 
{
	if ((sector<0) || (sector >=FATENTRIES)) return false;

	printf("EraseSector(%d)\n", sector);
	memset(flash[sector], 0, SECTORSIZE);
	flashErased[sector] = true;
	return true;
}

bool Filesystem::WriteSector(int sector, const void *data)
{
	if ((sector<0) || (sector >=FATENTRIES) || !data) return false;

	printf("WriteSector(%d, data)\n", sector);
	if (!flashErased[sector]) {
		printf("!!!ERROR, sector not erased!!!\n");
		return false;
	}
	memcpy(flash[sector], data, SECTORSIZE);
	flashErased[sector] = false;
	return true;
}


bool Filesystem::ReadSector(int sector, void *data)
{
	if ((sector<0) || (sector >=FATENTRIES) || !data) return false;

	memcpy(data, flash[sector], SECTORSIZE);
	return true;
}

bool Filesystem::ReadPartialSector(int sector, int offset, void *data, int len)
{
	if ((sector<0) || (sector >=FATENTRIES) || !data || (len < 0) || (offset < 0) || (offset+len > SECTORSIZE)) return false;

	memcpy(data, &flash[sector][offset], len);
	return true;
}

bool Filesystem::mkfs()
{
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
	return true;
}

bool Filesystem::mount()
{
	int idx = FindNewestFAT();
	if (idx >= 0) {
		if (!ReadSector(idx, &fs)) return false;
		if (!ValidateFAT()) return false;
		return true;
	}
	return false;
}

bool Filesystem::umount()
{
	if (!FlushFAT()) return false;
	return true;
}

bool Filesystem::FlushFAT()
{
	fs.epoch++;
	fs.crc = 0;
	uint32_t calcCRC = 0;
	crc32((void*)&fs, sizeof(fs), &calcCRC);
	fs.crc = calcCRC;
	int idx = FindOldestFAT();
	if (idx >= 0) {
		if (!EraseSector(idx)) return false;
		return WriteSector(idx, &fs);
	}
	return false;
}

class File
{
friend Filesystem;

public:
	~File() {};

	int write(const void *out, int size);
	int read(void *data, int size);
	int seek(int off, int whence);
	int seek(int off) { return seek(off, SEEK_SET); }
	int close();
	int tell();
	int eof();

private:
	File(Filesystem *fs, int fileIdx, int readOffset, int writeOffset, bool read, bool write, bool append);
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

File *Filesystem::open(const char *name, const char *mode)
{
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

int File::tell()
{
	if (modeRead) return readPos;
	return writePos;
}

int File::eof()
{
	if (modeRead) return (readPos == fs->GetFileEntry(fileIdx)->len) ? true : false;
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
		curWriteSector = fs->GetFileEntry(fileIdx)->fat;
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
		if (fs->GetFileEntry(fileIdx)->len > curWriteSectorOffset) { // Read in old data
			if (!fs->ReadSector(curWriteSector, data)) return 0;
		} else { // New sector...
			memset(data, 0, SECTORSIZE);
		}
		fs->GetFileEntry(fileIdx)->len = max(fs->GetFileEntry(fileIdx)->len, curWriteSectorOffset);
	}

	// We're in the correct sector.  Start writing and extending/overwriting
	while (size) {
		int amountWritableInThisSector = min(size, SECTORSIZE - (writePos % SECTORSIZE));
		if (writePos && (writePos %SECTORSIZE)==0)  amountWritableInThisSector = 0;
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
		fs->GetFileEntry(fileIdx)->len = max(fs->GetFileEntry(fileIdx)->len, writePos); // Potentially we just extended the file
		// Reduce bytes available to write, increment data pointer
		size -= amountWritableInThisSector;
		out = reinterpret_cast<const char*>(out) + amountWritableInThisSector;
	}

	return writtenBytes;
}

int File::close()
{
	if (!modeWrite && !modeAppend) return 0;
	if (!dataDirty) return 0;
	if (!fs->EraseSector(curWriteSector)) return -1;
	return fs->WriteSector(curWriteSector, data) ? 0 : -1;
}


int File::read(void *in, int size)
{
	if (!modeRead || !in || !size) return 0;

	int readableBytesInFile = fs->GetFileEntry(fileIdx)->len - readPos;
	size = min(readableBytesInFile, size); // We can only read to the end of file...
	if (size <= 0) return 0;

	int readBytes = 0;

	// Make sure we're reading from somewhere in the current sector
	if (! ( (curReadSectorOffset <= readPos) && ((curReadSectorOffset+SECTORSIZE) > readPos) ) ) {
		// Traverse the FAT table, optionally extending the file
		curReadSector = fs->GetFileEntry(fileIdx)->fat;
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
		if (readPos && (readPos %SECTORSIZE)==0)  amountReadableInThisSector = 0;
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
		case SEEK_END: absolutePos = fs->GetFileEntry(fileIdx)->len + off; break;
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



int main(int argc, char *argv[])
{
	srand(time(NULL));
	Filesystem *fs = new Filesystem;
	fs->mkfs();
	fs->mount();
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
	int len;
	char buff[1001];
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
	f->write("Four score and seven years ago our forefathers...", 50);
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


	Dir *d = fs->opendir();
	do {
		struct dirent *de = fs->readdir(d);
		if (!de) break;
		printf("File: '%s'\n", de->name);
	} while (1);
	fs->closedir(d);
	return 0;
}

