#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <ESP8266FastROMFS.h>

void usage()
{
	printf("ERROR!  Usage:  copytofsimage --image outfile.bin --file filename.txt\n");
	exit(-1);
}

int main(int argc, char **argv)
{
	char *image = "fastromfs.bin";
	char *file = NULL;
	for (int i=1; i<argc; i++) {
		if (!strcmp(argv[i], "--image")) { image = argv[++i]; i++; }
		else if (!strcmp(argv[i], "--file")) { file = argv[++i]; i++; }
		else usage();
	}
	if (!file) usage();

	FastROMFilesystem *fs = new FastROMFilesystem();
	FILE *f = fopen(image, "rb");
	fs->LoadFromFile(f);
	fclose(f);

	fs->mount();
	FILE *fi = fopen(file, "wb");
	FastROMFile *fo = fs->open(file, "rb");
	int ch;
	while (EOF !=  (ch = fo->fgetc())) fputc(ch, fi);
	fclose(fi);
	fo->close();
	fs->umount();

	f = fopen(image, "wb");
	fs->DumpToFile(f);
	fclose(f);

	return 0;
}
