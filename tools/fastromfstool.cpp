#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <ESP8266FastROMFS.h>

void usage()
{
	printf("Usage:  fastromfstool [command] [options] ...\n");
	printf("        fastromfstool mkfs --image fastromfs.bin --sectors count --dir dir-to-upload\n");
	printf("        fastromfstool ls --image fastromfs.bin\n");
	printf("        fastromfstool cpto --file sourcefile.bin --image fastromfs.bin\n");
	printf("        fastromfstool cpfrom --file sourcefile.bin --image fastromfs.bin\n");
	exit(-1);
}

FastROMFilesystem *LoadMount(const char *image)
{
	FastROMFilesystem *fs = new FastROMFilesystem();
	FILE *f = fopen(image, "rb");
	if (!f) {
		printf("ERROR:  Unable to open %s\n", image);
		exit(-1);
	}
	fs->LoadFromFile(f);
	fclose(f);
	fs->mount();
	return fs;
}

int main(int argc, char **argv)
{
	const char *image = "fastromfs.bin";
	const char *dir = "data";
	const char *file = "file.txt";
	int sectors = MAXFATENTRIES;
	enum {MKFS, LS, CPTO, CPFROM} command;

	if (argc < 2) usage();

	if (!strcmp(argv[1], "mkfs")) command = MKFS;
	else if (!strcmp(argv[1], "ls")) command = LS;
	else if (!strcmp(argv[1], "cpto")) command = CPTO;
	else if (!strcmp(argv[1], "cpfrom")) command = CPFROM;
	else usage();

	for (int i=2; i<argc; i++) {
		if (!strcmp(argv[i], "--image")) { image = argv[++i]; }
		else if (!strcmp(argv[i], "--dir")) { dir = argv[++i]; }
		else if (!strcmp(argv[i], "--sectors")) { sectors = atol(argv[++i]); }
		else if (!strcmp(argv[i], "--file")) { file = argv[++i]; i++; }
		else { printf("ERROR:  Unknown option '%s'\n", argv[i]); usage(); }
	}

	switch (command) {
	case MKFS:
	{
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
			if (!fo) {
				printf("ERROR:  Can't create file '%s' in filesystem\n", de->d_name);
				return -1;
			}
			int ch;
			while (EOF !=  (ch = fgetc(fi))) {
				if (fo->fputc(ch) < 0) {
					printf("ERROR:  Out of space\n");
					return -1;
				}
			}
			fclose(fi);
			fo->close();
		}
		fs->umount();
		FILE *fo = fopen(image, "wb");
		if (!fo) {
			printf("ERROR:  Unable to open image file '%s' for writing\n", image);
			return -1;
		}
		fs->DumpToFile(fo);
		return 0;
	}
	case LS:
	{
		FastROMFilesystem *fs = LoadMount(image);

		FastROMFSDir *d = fs->opendir();
		if (!d) {
			printf("ERROR:  Unable to opendir()\n");
			return -1;
		}
		do {
			struct FastROMFSDirent *de = fs->readdir(d);
			if (!de) break;
			printf("File: '%s', len=%d\n", de->name, de->len);
		} while (1);
		fs->closedir(d);
		fs->umount();
		return 0;
	}
	case CPTO:
	{
		FastROMFilesystem *fs = LoadMount(image);

		FILE *fi = fopen(file, "rb");
		if (!fi) {
			printf("ERROR:  Unable to open file '%s' for reading\n", file);
			return -1;
		}
		FastROMFile *fo = fs->open(file, "wb");
		int ch;
		while (EOF !=  (ch = fgetc(fi))) fo->fputc(ch);
		fclose(fi);
		fo->close();
		fs->umount();

		FILE *f = fopen(image, "wb");
		fs->DumpToFile(f);
		fclose(f);
		return 0;
	}
	case CPFROM:
	{
		FastROMFilesystem *fs = LoadMount(image);

		FastROMFile *fo = fs->open(file, "rb");
		if (!fo) {
			printf("ERROR:  Can't open file '%s' in filesystem for reading\n", file);
			return -1;
		}
		FILE *fi = fopen(file, "wb");
		if (!fi) {
			printf("ERROR:  Can't open file '%s' in for writing\n", file);
			return -1;
		}
		int ch;
		while (EOF !=  (ch = fo->fgetc())) fputc(ch, fi);
		fclose(fi);
		fo->close();
		fs->umount();

		FILE *f = fopen(image, "wb");
		if (!fo) {
			printf("ERROR:  Unable to open image file '%s' for writing\n", image);
			return -1;
		}
		fs->DumpToFile(f);
		fclose(f);
		return 0;
	}

	default:
		break;
	}

	return -1;
}
