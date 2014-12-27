/**
*Serial output functions sugar implementation.
*/

#include "serio.h"
#include "printf.h"

void printbits(unsigned int portval,char numbit){
	unsigned int bit=1;
	unsigned int i;
	for (i=0;i<numbit;i++){
		if (portval&bit)
			sendbyte('1');
		else
			sendbyte('0');
		bit=bit<<1;
	}
	snewline();
}

void sendint(long value){
	print_f("%l",value);
}

void snewline(){
	sendbuf("\r\n");
}

void put_s(char *s) {
	sendbuf(s);
}

void put_c(unsigned char c){
	sendbyte(c);
}

void sendbuf(char* buf){
	while(*buf!='\0'){
		sendbyte(*buf++);
	}
}

