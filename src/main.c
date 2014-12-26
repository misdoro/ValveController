#include "msp430g2553.h"
#include "signal.h"
#include "printf.c"

#define     LED1                  BIT0
#define     LED_DIR               P1DIR
#define     LED_OUT               P1OUT

#define  MAXON	3000
#define  TXBUF_SIZE 64//Must be a power of 2
#define TXBUF_MASK TXBUF_SIZE-1

unsigned long i,ii;
volatile char flag = 0;
volatile long ontime;
unsigned char readbyte='0';
volatile unsigned char txpos=0;
volatile unsigned char txlen=0;
volatile char txbuf[TXBUF_SIZE];
volatile int pos=0;

//Voltage data
long Vtrig_avg[8];
unsigned int Vtrig_pos=0;
long Vtrig_avg_sum;
long Vin_avg[8];
unsigned int Vin_pos=0;
long Vin_avg_sum;


void sendint(long value);
void sendbyte(char data);
void sendbuf(char* text);
void snewline();
void printbits(unsigned int val,char nbits);
void printPress(long mVolts);
void ADCStart();

void ConfigureAdc(void);

void ConfigurePeripherial(void);

int main(void){
	WDTCTL = WDTPW + WDTHOLD;
 	BCSCTL1 = CALBC1_8MHZ;
	DCOCTL = CALDCO_8MHZ;
	
	P1OUT |= BIT6;
	ConfigureAdc();
	
	ConfigurePeripherial();
	
	eint();
	
	ontime=1000;
	while(1){
		//Led blink loop
		//P1OUT &= ~BIT6;//Turn off green led
		
		if (ontime<MAXON){
			ontime++;
		}else{
			ontime=MAXON;
		}
		for (i=0;i<ontime;i++){
			P1OUT |= BIT0;//Turn on red led
			
		}
		
		for (i=ontime;i<MAXON;i++){
			P1OUT &= ~BIT0;//Turn off red led
		}
		ADCStart();
	}
	
	
}

void ADCStart(void){
	ADC10CTL0 |= ENC + ADC10SC;               // Sampling and conversion start
}

void ConfigureAdc(void)
{
	/* Configure ADC Temp Sensor Channel */
	ADC10CTL1 = INCH_5 + ADC10DIV_3;         // Temp Sensor ADC10CLK/4
	ADC10CTL0 = SREF_3 + ADC10SHT_3  + ADC10ON + ADC10IE;
	//SREF3=buff_ext_vref+&vref-=GND
	//ADC10sht_3 = longest retention time
	//REFON=0 for external reference
	//ADC10ON to enable ADC
	//ADC10IE to enable interrupts
	ADC10AE0 |= BIT5+BIT7;//A5 and A7 enabled as analog inputs
	ADCStart();
}



void ConfigurePeripherial(void){
	P1OUT &= ~(BIT0+BIT6);
	P1DIR =0;//ALL P1 as inputs
	
	P1DIR |= (BIT0+BIT6);//LED pins on P1 as outputs
	
	P1REN=0;
	
	P2DIR=0;
	P2DIR|=(BIT0+BIT1+BIT2+BIT3);//P2.0...P2.3 as OUT
	
	P2REN=0;
	P2REN|=(BIT4+BIT5);//Enable pull R on pin4&pin5
	P2OUT=0;
	P2OUT|=(BIT4+BIT5);//Pull up switch inputs
	
	P2SEL=0;
	P2SEL2=0;
	
	
	P1SEL = BIT1 + BIT2;//Enable UART on port 1
	P1SEL2 = BIT1 + BIT2;//Enable UART on port 1
	
	UCA0CTL1 |= UCSSEL_2;
	
	//8mhz-9600	
	UCA0BR0 = 0x41;
	UCA0BR1 = 0x03;	
	UCA0MCTL = 0x92;
	
	UCA0CTL1 &= ~UCSWRST;
	
	IE2 |= UCA0RXIE+UCA0TXIE;                                 // Enable USCI_A0 RX interrupt
};

interrupt(USCIAB0RX_VECTOR) rx_interrupt(void){	
	if (IFG2&UCA0RXIFG){//check interrupt flag
		readbyte=UCA0RXBUF;
		if (readbyte=='+'){
			//Brighter red led
			P1OUT^=BIT6;//Toggle green LED
			ontime+=100;
			if (ontime>=3000)
				ontime=3000;
			sendbyte('+');
			snewline();
		}
		else if (readbyte=='-'){
			//Deemer red led
			P1OUT^=BIT6;//Toggle green led
			ontime-=100;
			if (ontime<=20)
				ontime=20;
			
			sendbyte('-');
			snewline();
		}else if (readbyte=='i'){
			//Print device name
			sendbuf("ValveControl01");
			snewline();
		}else if (readbyte=='p'){
			//Print port statuses
			sendbuf("P1:");
			printbits(P1IN,8);
			sendbuf("P2:");
			printbits(P2IN,6);
			sendbuf("End");
			snewline();
		}else if (readbyte=='q'){
			//Print port statuses
			sendbuf("P1:");
			snewline();
			sendbyte('I');
			printbits(P1IN,8);
			sendbyte('O');
			printbits(P1OUT,8);
			sendbyte('D');
			printbits(P1DIR,8);
			sendbyte('R');
			printbits(P1REN,8);
			sendbyte('S');
			printbits(P1SEL,8);
		}else if (readbyte=='w'){
			//Print port statuses
			sendbuf("P2:");
			snewline();
			sendbyte('I');
			printbits(P2IN,6);
			sendbyte('O');
			printbits(P2OUT,6);
			sendbyte('D');
			printbits(P2DIR,6);
			sendbyte('R');
			printbits(P2REN,6);
			sendbyte('S');
			printbits(P2SEL,6);
		}else if (readbyte>='0' && readbyte<='8'){
			P2OUT ^= BIT0<<(readbyte-'0');
		}else if (readbyte=='g'){
			sendbuf("int");
			sendint(ontime);
			snewline();
		}else if (readbyte=='a'){
			printbits(ADC10CTL1,16);
			print_f("%u",Vtrig_pos);
			snewline();
			print_f("%u",Vin_pos);
			snewline();
		}else if(readbyte=='v'){
			sendbuf("Vtrig: ");
			long volts = (Vtrig_avg_sum * 10000) / 8192;
			print_f("%l",volts);
			sendbuf("mV");
			snewline();
			
			sendbuf("Ptrig: ");
			printPress(volts);
			snewline();
			
			sendbuf("Vin: ");
			volts = (Vin_avg_sum * 10000) / 8192;
			print_f("%l",volts);
			sendbuf("mV");
			snewline();
			sendbuf("Pin: ");
			printPress(volts);
			snewline();
			snewline();
		}else{
			sendbuf("Unknown command");
			snewline();
		}
	}
}

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

void printPress(long mVolts){
	long mVdm=mVolts/1000;
	long pm=(mVolts-(mVdm)*1000+100);
	long pmd=pm/110;
	print_f("%l",pmd);
	sendbyte('.');
	print_f("%l",(pm-pmd*110)/11);
	sendbyte('E');
	print_f("%l",(mVdm)-11);
	sendbuf("Torr");
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

void sendbyte(char data){
	dint();//Disable interrupts
	if (txlen==0 && (IFG2&UCA0TXIFG)){
		UCA0TXBUF=data;
		IE2 &= ~UCA0TXIE;
	}else if (txlen<TXBUF_SIZE){
		volatile int nbpos=txpos+txlen;
		nbpos &=(TXBUF_MASK);
		txlen++;
		txbuf[nbpos]=data;
		IE2 |= UCA0TXIE;  
	}
	eint();//Enable interrupts
}

interrupt(USCIAB0TX_VECTOR) tx_interrupt(void){
	if (IFG2&UCA0TXIFG){
		if (txlen>0){
			UCA0TXBUF=txbuf[txpos];
			txpos++;
			txpos&=(TXBUF_MASK);
			txlen--;
		}else{
			IE2 &= ~UCA0TXIE;
		}
	}
}

interrupt (ADC10_VECTOR) ADC10_ISR(void)
{
	
	if(Vtrig_pos<8){
		Vtrig_avg[Vtrig_pos++]=ADC10MEM;
		if (Vtrig_pos==8){//Switch ADC to A7 after the last Vtrig measurement
			ADC10CTL0 &= ~ENC;
			ADC10CTL1 = INCH_7 + ADC10DIV_3;
		}
	}else if(Vin_pos<8){
		Vin_avg[Vin_pos++]=ADC10MEM;
		if (Vin_pos==8){//Switch ADC to A5 after the last Vin measurement
			ADC10CTL0 &= ~ENC;
			ADC10CTL1 = INCH_5 + ADC10DIV_3;
		}
	}
	if (Vin_pos>=8){
		Vtrig_pos=0;
		Vin_pos=0;
		Vtrig_avg_sum=0;
		Vin_avg_sum=0;
		for (i = 0; i < 8; i++){
			Vtrig_avg_sum+=Vtrig_avg[i];
			Vin_avg_sum+=Vin_avg[i];
		}
	}
}