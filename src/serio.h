#ifndef SERIO_H_INCLUDED
#define SERIO_H_INCLUDED

void sendint(long value);
void sendbuf(char* text);
void snewline();
void printbits(unsigned int val,char nbits);
void sendbyte(char data);

#endif // SERIO_H_INCLUDED
