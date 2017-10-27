#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <ESP8266FastROMFS.h>

void usage()
{
	printf("ERROR!  Usage:  lsfsimage --image outfile.bin\n");
	exit(-1);
}

int main(int argc, char **argv)
{
	char *image = "fastromfs.bin";

	for (int i=1; i<argc; i++) {
		if (!strcmp(argv[i], "--image")) { image = argv[++i]; i++; }
		else usage();
	}

	FastROMFilesystem *fs = new FastROMFilesystem();
	FILE *f = fopen(image, "rb");
	fs->LoadFromFile(f);
	fclose(f);

	fs->mount();
	FastROMFSDir *d = fs->opendir();
	do {
		struct FastROMFSDirent *de = fs->readdir(d);
		if (!de) break;
		printf("File: '%s', len=%d\n", de->name, de->len);
	} while (1);
	fs->closedir(d);
	fs->umount();

	return 0;
}
