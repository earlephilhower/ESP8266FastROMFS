#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <ESP8266FastROMFS.h>

void usage()
{
	printf("ERROR!  Usage:  makefsimage --sectors count --out outfile.bin --dir dir-to-upload/\n");
	exit(-1);
}

int main(int argc, char **argv)
{
	const char *out = "fastromfs.bin";
	const char *dir = "data";
	int sectors = MAXFATENTRIES;

	for (int i=1; i<argc; i++) {
		if (!strcmp(argv[i], "--out")) { out = argv[++i]; }
		else if (!strcmp(argv[i], "--dir")) { dir = argv[++i]; }
		else if (!strcmp(argv[i], "--sectors")) { sectors = atol(argv[++i]); }
		else { printf("Unknown option: %s\n", argv[i]); usage(); }
	}

	FastROMFilesystem *fs = new FastROMFilesystem(sectors);
	fs->mkfs();
	fs->mount();

	DIR *d = opendir(dir);
	struct dirent *de;
	while (NULL != (de = readdir(d))) {
		if (de->d_name[0] == '.') continue;
		char buff[128];
		sprintf(buff, "%s/%s", dir, de->d_name);
		printf("Adding %s...\n", buff);
		FILE *fi = fopen(buff, "rb");
		FastROMFile *fo = fs->open(de->d_name, "wb");
		int ch;
		while (EOF !=  (ch = fgetc(fi))) {
			if (fo->fputc(ch) < 0) {
				printf("ERROR: Out of space\n");
				return -1;
			}
		}
		fclose(fi);
		fo->close();
	}
	fs->umount();
	FILE *fo = fopen(out, "wb");
	fs->DumpToFile(fo);
	return 0;
}
