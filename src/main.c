#include "msp430g2553.h"
#include "signal.h"
#include "printf.h"
#include "serio.h"

#define  DEBUG  true
#define  EASTER true

#define     LEDR            BIT0
#define     LEDG            BIT6
#define     BUTTON          BIT3
#define     ADC_IN_VTRIG    INCH_5
#define     ADC_IN_VIN      INCH_7
#define     SWOPEN          BIT4
#define     SWCLOSED        BIT5
#define     OUTCTL          BIT0
#define     OUTRST          BIT1
#define     OUTSET          BIT3

#define  MAXON	3000
#define  TXBUF_SIZE 64//Must be a power of 2
#define  TXBUF_MASK TXBUF_SIZE-1


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

void ConfigureAdc(void);
void ConfigurePeripherial(void);
void ConfigureTimerA(void);
void sendbyte(char data);

void printPress(long mVolts);
void ADCStart();

int main(void){
	WDTCTL = WDTPW + WDTHOLD;
 	BCSCTL1 = CALBC1_8MHZ;
	DCOCTL = CALDCO_8MHZ;

	P1OUT |= LEDG;
	ontime=0;
	ConfigureAdc();

	ConfigurePeripherial();

	ConfigureTimerA();

	eint();

    //Go to sleep, everything works on timers and interrupts from now on.
    //Plus, ADC has much less noise in this mode.
    _BIS_SR(CPUOFF + GIE);
    while(1){
    P1OUT^=LEDR;//Red will bilnk faster if we will have sleep problems
    };

}

void ADCStart(void){
	ADC10CTL0 |= ENC + ADC10SC;               // Sampling and conversion start
}

void ConfigureAdc(void)
{
	/* Configure ADC */
	ADC10CTL1 = ADC_IN_VTRIG + ADC10DIV_3;//Start with input channel 5
	ADC10CTL0 = SREF_3 + ADC10SHT_3  + ADC10ON + ADC10IE;
	//SREF3=buff_ext_vref+&vref-=GND
	//ADC10sht_3 = longest retention time
	//REFON=0 for external reference
	//ADC10ON to enable ADC
	//ADC10IE to enable interrupts
	ADC10AE0 |= BIT5+BIT7;//A5 and A7 enabled as analog inputs
}

void ConfigureTimerA(void){
    CCTL0 = CCIE;                             // CCR0 interrupt enabled
    TACTL = TASSEL_2 + MC_1 + ID_3;           // SMCLK/8, upmode
    CCR0 =  3000;
}

void ConfigurePeripherial(void){//Configure ports and pins

	P1OUT = BUTTON+LEDG;//BIT6 for green LED, bit3 for button
	P1DIR = (LEDR+LEDG);//LED pins on P1 as outputs
	P1REN = BUTTON;//Pull up pin3 for the button
	P1SEL = BIT1 + BIT2;//Enable UART on port 1
	P1SEL2 = BIT1 + BIT2;//Enable UART on port 1

	P2DIR=(OUTCTL+OUTRST+OUTSET);//P2.0, P2.1, P2.3 as OUT

	P2REN=(SWOPEN+SWCLOSED);//Enable pull R on pin4&pin5
	P2OUT=(SWOPEN+SWCLOSED);//Pull switch inputs to high
	P2OUT|=OUTRST;

	P2SEL=0;
	P2SEL2=0;

	UCA0CTL1 |= UCSSEL_2;

	//8mhz-9600
	UCA0BR0 = 0x41;
	UCA0BR1 = 0x03;
	UCA0MCTL = 0x92;

	UCA0CTL1 &= ~UCSWRST;

	IE2 |= UCA0RXIE+UCA0TXIE;// Enable USCI_A0 RX interrupt
};

interrupt(USCIAB0RX_VECTOR) rx_interrupt(void){
	if (IFG2&UCA0RXIFG){//check interrupt flag
		readbyte=UCA0RXBUF;//Clear interrupt by reading the byte
		if (txlen>0)return;//Discard command if we are still sending data
		if (readbyte=='i'){
			//Print device name
			sendbuf("ValveControl01");
			snewline();
		}else if (readbyte=='o'){
            print_f("Ontime: %l",ontime);
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
		}else if (readbyte=='a'){
			printbits(ADC10CTL1,16);
			print_f("%u",Vtrig_pos);
			snewline();
			print_f("%u",Vin_pos);
			snewline();
		}else if(readbyte=='v'){
			long volts = (Vtrig_avg_sum * 10000) / 8192;
			print_f("Vtr: %lmV",volts);
			snewline();

			sendbuf("Ptr: ");
			printPress(volts);
			snewline();

			volts = (Vin_avg_sum * 10000) / 8192;
			print_f("Vin: %lmV",volts);
			snewline();
			sendbuf("Pin: ");
			printPress(volts);
			snewline();
#ifdef EASTER
		}else if(readbyte=='d'){
			snewline();
                        sendbuf("\\...,^..^");
                        snewline();
                        sendbuf(" /\\ /\\  `");
                        snewline();
#endif
		}else{
			sendbuf("Unknown command");
			snewline();
		}
	}
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

void sendbyte(char data){
	dint();//Disable interrupts
	if (txlen==0 && (IFG2&UCA0TXIFG)){//Start sending directly if we have nothing in send buffer
		UCA0TXBUF=data;
		IE2 &= ~UCA0TXIE;
	}else if (txlen<TXBUF_SIZE){//Put byte in the send buffer, if there is some place left.
		volatile int nbpos=txpos+txlen;
		nbpos &=(TXBUF_MASK);
		txlen++;
		txbuf[nbpos]=data;
		IE2 |= UCA0TXIE;
	}
	eint();//Enable interrupts
}

interrupt(TIMER0_A0_VECTOR) timera_interrupt(void){
    P1OUT ^= LEDR+LEDG;
    ADCStart();
}

interrupt(USCIAB0TX_VECTOR) tx_interrupt(void){
	if (IFG2&UCA0TXIFG){
		if (txlen>0){//Send byte if we have something to send
			UCA0TXBUF=txbuf[txpos];
			txpos++;
			txpos&=(TXBUF_MASK);
			txlen--;
		}else{//Clear interrupt flag
			IE2 &= ~UCA0TXIE;
		}
	}
}

interrupt (ADC10_VECTOR) ADC10_ISR(void)
{
    int i;
	if(Vtrig_pos<8){
		Vtrig_avg[Vtrig_pos++]=ADC10MEM;
		if (Vtrig_pos==8){//Select Vin channel after the last Vtrig measurement
			ADC10CTL0 &= ~ENC;
			ADC10CTL1 = ADC_IN_VIN + ADC10DIV_3;
		}
	}else if(Vin_pos<8){
		Vin_avg[Vin_pos++]=ADC10MEM;
		if (Vin_pos==8){//Select VTRIG channel after the last VIN measurement
			ADC10CTL0 &= ~ENC;
			ADC10CTL1 = ADC_IN_VTRIG + ADC10DIV_3;
		}
	}
	if (Vin_pos>=8){//Process result of the measurement cycle
		Vtrig_pos=0;
		Vin_pos=0;
		Vtrig_avg_sum=0;
		Vin_avg_sum=0;
		for (i = 0; i < 8; i++){
			Vtrig_avg_sum+=Vtrig_avg[i];
			Vin_avg_sum+=Vin_avg[i];
		}
		ontime++;
	}
}
