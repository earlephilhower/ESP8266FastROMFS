#ifdef ARDUINO
#include <Arduino.h>
#else
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#endif
#include <ESP8266FastROMFS.h>

#ifdef ARDUINO
#define DEBUG_FASTROMFS Serial.printf
void RunFSTest()
#else
#define DEBUG_FASTROMFS printf
int main(int argc, char **argv)
#endif
{
  int len;
  char buff[1001];
#ifndef ARDUINO
  srand(time(NULL));
#endif
  FastROMFilesystem *fs = new FastROMFilesystem;
  fs->mkfs();
  DEBUG_FASTROMFS("mount ret = %d\n", fs->mount());
  DEBUG_FASTROMFS("Bytes Free: %d\n", fs->available());
  FastROMFile *f = fs->open("test.bin", "w");
  for (int i = 0; i < 200; i++) {
    f->write("0123456789", 10);
    f->write("abcdefghij", 10);
  }
  for (int i = 0; i < 200; i++) {
    f->write("0123456789", 10);
    f->write("abcdefghij", 10);
  }
  f->seek(12, SEEK_SET);
  f->write("Earle Is At 12", 14);
  f->close();
  fs->DumpFS();

  f = fs->open("test.bin", "r");
  f->seek(2);
  len = f->read(buff + 1, 64);
  buff[len + 1] = 0;
  DEBUG_FASTROMFS("buff@2='%s'\n", buff + 1);
  f->close();
  //	exit(1);

  f = fs->open("test.bin", "r");
  do {
    len = f->read(buff, 1000);
    buff[1000] = 0;
    DEBUG_FASTROMFS("buff='%s'\n", buff);
  } while (len);
  f->seek(-998, SEEK_END);
  len = f->read(buff, 1000);
  buff[1000] = 0;
  DEBUG_FASTROMFS("buffx='%s'\n", buff);
  f->close();

  f = fs->open("test.bin", "r+");
  f->seek(4080, SEEK_SET);
  f->write("I Am Spanning A 4K Block!", 25);
  f->seek(4070);
  f->read(buff, 1000);
  buff[1000] = 0;
  DEBUG_FASTROMFS("buffx='%s'\n", buff);
  f->close();

  f = fs->open("newfile.txt", "w");
  f->write("Four score and seven years ago our forefathers......", 50);
  f->close();

  f = fs->open("test.bin", "r+");
  f->read(buff, 50);
  buff[50] = 0;
  DEBUG_FASTROMFS("buffx='%s'\n", buff);
  f->close();

  f = fs->open("newfile.txt", "r+");
  f->read(buff, 50);
  buff[50] = 0;
  DEBUG_FASTROMFS("buffx='%s'\n", buff);
  f->close();

  DEBUG_FASTROMFS("Bytes Free: %d\n", fs->available());

  fs->DumpFS();

  DEBUG_FASTROMFS("newfile.txt: %d bytes\n", fs->fsize("newfile.txt"));
  DEBUG_FASTROMFS("test.bin: %d bytes\n", fs->fsize("test.bin"));



  fs->umount();

  DEBUG_FASTROMFS("UNMOUNT/REMOUNT...\n");
  fs->mount();
  FastROMFSDir *d = fs->opendir();
  do {
    struct FastROMFSDirent *de = fs->readdir(d);
    if (!de) break;
    DEBUG_FASTROMFS("File: '%s', len=%d\n", de->name, de->len);
  } while (1);
  fs->closedir(d);


  fs->rename("newfile.txt", "gettysburg.txt");
  d = fs->opendir();
  do {
    struct FastROMFSDirent *de = fs->readdir(d);
    if (!de) break;
    DEBUG_FASTROMFS("File: '%s', len=%d\n", de->name, de->len);
  } while (1);
  fs->closedir(d);

  f = fs->open("gettysburg.txt", "a+");
  f->read(buff, 30);
  buff[30] = 0;
  DEBUG_FASTROMFS("buff='%s', tell=%d\n", buff, f->tell());
  f->write("I forget the rest", 17);
  DEBUG_FASTROMFS("appended read = '");
  while (int l = f->read(buff, 30)) {
    buff[l] = 0;
    DEBUG_FASTROMFS("%s", buff);
  }
  DEBUG_FASTROMFS("'\n");
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
    if (c == 0) zeros++;
    else break;
  } while (1);
  DEBUG_FASTROMFS("I found %d zeros before the text: '", zeros);
  do {
    DEBUG_FASTROMFS("%c", c);
    f->read(&c, 1);
    if (c == 0) break;
  } while (1);
  DEBUG_FASTROMFS("'\n");
  f->close();

  f = fs->open("gettysburg.txt", "r");
  int flencalc = 0;
  while (!f->eof()) {
    f->read(&c, 1);
    flencalc++;
  }
  DEBUG_FASTROMFS("LEN=%d, calcLEN=%d\n", f->size(), flencalc);
  f->close();

  f = fs->open("bytebybyte.bin", "w+");
  c = 'a';
  for (int i = 0; i < 4096 * 2; i++)
    f->write(&c, 1);
  f->seek(0, SEEK_SET);
  zeros = 0;
  do {
    int x = f->read(&c, 1);
    if (x == 1) zeros++;
    else break;
  } while (1);
  DEBUG_FASTROMFS("I read %d bytes\n", zeros);

  f->close();

#ifndef ARDUINO
  f = fs->open("test.bin", "rb");
  int sz = f->size();
  srand(123); // Repeatable test
  for (int i = 0; i < 10000; i++) {
    int off = rand() % sz;
    int len = rand() % 100;
    f->seek(off);
    f->read(buff, len);
    if (i % 100 == 0) DEBUG_FASTROMFS("++++Loop %d\n", i);
  }
  f->close();
#endif

  f = fs->open("gettysburg.txt", "r");
  DEBUG_FASTROMFS("fgetc test: '");
  while (1) {
    int c = f->fgetc();
    if (c < 0) break;
    DEBUG_FASTROMFS("%c", (char)c);
  }
  DEBUG_FASTROMFS("'\n");
  f->close();


  fs->umount();


  delete fs;

}

#ifdef ARDUINO
void setup()
{
  Serial.begin(115200);
  RunFSTest();
}

void loop()
{
  /* nop */
}
#endif

