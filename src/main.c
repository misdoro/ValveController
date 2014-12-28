#include "msp430g2553.h"
#include "signal.h"
#include "printf.h"
#include "serio.h"

#undef      DEBUG
#define   DEBUG  true
#define     EASTER true

//Pin assignments
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

//User flash address, blocks start from 0x1040; 0x1080;0x10C0
#define     SWTHRESH        (unsigned int*) 0x1082//Software-defined threshold

//Buffer sizes
#define  TXBUF_SIZE 64//Must be a power of 2
#define  TXBUF_MASK TXBUF_SIZE-1


volatile char flag = 0;
#ifdef DEBUG
volatile long adcticks;
volatile long ta0ticks;
volatile unsigned int ta1ticks;
#endif // DEBUG
unsigned char readbyte='0';
volatile unsigned char txpos=0;
volatile unsigned char txlen=0;
volatile char txbuf[TXBUF_SIZE];
volatile int pos=0;
volatile int FlashWriteTimer;

//Voltage data
unsigned int Vtrig_arr_vals[8];
unsigned int Vtrig_pos=0;
unsigned int Vtrig_avg_sum;
unsigned int Vin_arr_vals[8];
unsigned int Vin_pos=0;
unsigned int Vin_avg_sum;

void ConfigureAdc(void);
void ConfigurePeripherial(void);
void ConfigureTimer1(void);
void ConfigureTimer2(void);
void StartTimer2(void);
void sendbyte(char data);

void printVoltData(unsigned int mVolts,char* prefix);
void printPress(long mVolts);
void ADCStart();

int main(void){
	WDTCTL = WDTPW + WDTHOLD;
 	BCSCTL1 = CALBC1_8MHZ;
	DCOCTL = CALDCO_8MHZ;

#ifdef DEBUG
	adcticks=0;
	ta0ticks=0;
	ta1ticks=0;
#endif // DEBUG
	ConfigureAdc();

	ConfigurePeripherial();

	ConfigureTimer1();
	ConfigureTimer2();

	eint();

    //Go to sleep, everything works on timers and interrupts from now on.
    //Plus, ADC has much less noise in this mode.
    _BIS_SR(CPUOFF + GIE);
    while(1){
    P1OUT^=LEDR;//Red will bilnk faster if we will have sleep problems
    };

}

void FlashErase(unsigned int *addr){
    dint();
    while(BUSY & FCTL3);
    FCTL1=FWKEY+ERASE;
    FCTL2=FWKEY+FSSEL_3+FN3+FN0;
    FCTL3=FWKEY;

    *addr=0;

    while(BUSY & FCTL3);
    FCTL1=FWKEY;
    FCTL3=FWKEY+LOCK;
    eint();
}

void FlashWrite(unsigned int *addr,unsigned int data){
    dint();
    FCTL1=FWKEY+WRT;
    FCTL2=FWKEY+FSSEL_3+FN3+FN0;
    FCTL3=FWKEY;
    *addr=data;
    FCTL1=FWKEY;
    FCTL3=FWKEY+LOCK;
    eint();
}

void ADCStart(void){
	ADC10CTL0 |= ENC + ADC10SC;               // Sampling and conversion start
}

void ConfigureAdc(void)
{
	/* Configure ADC */
	ADC10CTL1 = ADC_IN_VTRIG + ADC10DIV_3;//Start with input channel 5
	ADC10CTL0 = SREF_3 + ADC10SHT_3  + ADC10ON + ADC10IE;
	/**SREF3=buff_ext_vref+&vref-=GND
	*ADC10sht_3 = longest retention time
	*REFON=0 for external reference
	*ADC10ON to enable ADC
	*ADC10IE to enable interrupts*/
	ADC10AE0 |= BIT5+BIT7;//A5 and A7 enabled as analog inputs
}

void ConfigureTimer1(void){
    TA0CCTL0 = CCIE;                            // CCR0 interrupt enabled
    TA0CTL = TASSEL_2 + MC_1 + ID_3;            // SMCLK/8 (1MHz), upmode
    TA0CCR0 =  1000;                            // 1000 = 1kHz
}

void StartTimer2(void){
    TA1R=0;
    TA1CCR0 =  100;
    TA1CTL|= MC_1;
}

void ConfigureTimer2(void){
    TA1CCTL0 = CCIE;                            // CCR0 interrupt enabled
    TA1CTL = TASSEL_2 + MC_0 + ID_3;            // SMCLK/8 (1MHz), upmode
    TA1CCR0 =  100;                             // 100 = 10 kHz = 0.1 ms pulse width
}

void ConfigurePeripherial(void){//Configure ports and pins

	P1OUT   = BUTTON;
	P1DIR   = (LEDR+LEDG);//LED pins on P1 as outputs
	P1REN   = BUTTON;//Pull up pin3 for the button
	P1SEL   = BIT1 + BIT2;//Enable UART on port 1
	P1SEL2  = BIT1 + BIT2;//Enable UART on port 1
	P1IE    = BUTTON;
	P1IES   = BUTTON;

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

void CloseValve(void){
    P2OUT&=~(OUTRST+OUTCTL);
    P1OUT|=LEDR;
    ConfigureTimer2();
    StartTimer2();
}

interrupt(USCIAB0RX_VECTOR) rx_interrupt(void){
	if (IFG2&UCA0RXIFG){//check interrupt flag
		readbyte=UCA0RXBUF;//Clear interrupt by reading the byte
		if (txlen>0)return;//Discard command if we are still sending data
		if (readbyte=='i'){
			//Print device name
			sendbuf("ValveControl01");
		}else if (readbyte=='c'){
            //Close the valve - cut the output
            CloseValve();
            sendbuf("OK");
		}else if (readbyte=='o'){
            //Open the valve - enable output
            if (Vin_avg_sum<Vtrig_avg_sum && Vin_avg_sum<*SWTHRESH){
                P1OUT|=LEDG;
                P2OUT|=(OUTSET+OUTCTL);
                StartTimer2();
                sendbuf("OK");
            }else{
                P1OUT|=LEDR;
                sendbuf("PERR");
            }
		}else if (readbyte=='p'){
			//Get the valve state
            if (P2IN&SWOPEN && (P2IN&SWCLOSED)==0){//Zero at input means "activated"
                sendbuf("CLOSED");
            }else if((P2IN&SWOPEN)==0 && (P2IN&SWCLOSED)){
                sendbuf("OPEN");
            }else{
                sendbuf("UNDEF");
            }
        }else if(readbyte=='v'){
            //Print actual input voltage and corresponding pressure
            printVoltData(Vin_avg_sum,"in");
        }else if(readbyte=='s'){
            //Print software-defined threshold voltage and pressure
            printVoltData(*SWTHRESH,"st");
        }else if(readbyte=='t'){
            //Print hardware-defined threshold voltage and pressure
            printVoltData(Vtrig_avg_sum,"tr");
        #ifdef DEBUG
        }else if (readbyte=='e'){
            //Print timers:
            print_f("ADC %n",adcticks);
            snewline();
            print_f("T0 %n",ta0ticks);
            snewline();
            print_f("T1 %u",ta1ticks);
            snewline();
            print_f("SWT %x",*SWTHRESH);
        }else if (readbyte=='f'){
            //Flash erase
            FlashErase(SWTHRESH);
            sendbuf("Erased");
        }else if (readbyte=='g'){
            //Flash write
            FlashWrite(SWTHRESH,Vin_avg_sum);
            print_f("Written %u",Vin_avg_sum);
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
			print_f("in%u",Vin_avg_sum);
			snewline();
			print_f("st%u",*SWTHRESH);
			snewline();
			print_f("ht%u",Vtrig_avg_sum);
        #endif
#ifdef EASTER
		}else if(readbyte=='d'){
                        sendbuf("\\...,^..^");
                        snewline();
                        sendbuf(" /\\ /\\  `");
#endif
		}else{
			sendbuf("Unknown command");
		}
		snewline();
	}
}

void printVoltData(unsigned int mVolts, char* prefix){
    long volts = (mVolts * 10000L) / 8192;
	print_f("V%s: %lmV",prefix,volts);
    snewline();
    print_f("P%s: ",prefix);
    printPress(volts);
}

void printPress(long mVolts){
	long mVdm=mVolts/1000;
	long pm=(mVolts-(mVdm)*1000+100);
	long pmd=pm/110;
	print_f("%l.%lE%lTorr",pmd,(pm-pmd*110)/11,mVdm-11);
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

interrupt(PORT1_VECTOR) port1_interrupt(void){
    //Port1 interrupt
    if (P1IFG&BUTTON){
        //Disable further interrupts from a button
        P1IE &=~BUTTON;
        P1IFG&=~BUTTON;

        FlashErase(SWTHRESH);
        FlashWriteTimer=0;
        //Start timer
        TA1CTL=TASSEL_2 + MC_1 + ID_3;
        TA1R=0;
        TA1CCR0 =  10000;
    }
}

interrupt(TIMER1_A0_VECTOR) timer1_interrupt(void){
    //As timer1 is used to generate pulses, set outputs to default values:
    P2OUT|=OUTRST;
    P2OUT&=~OUTSET;
    P1OUT&=~(LEDG+LEDR);
    //Another use of timer1 is to wait for button release
    if ((TA1CCR0 == 10000) && ((P1IN&BUTTON)==0)){
        //If we waited long enough and the button is still pressed
        TA1CTL=TASSEL_2 + MC_1 + ID_3;
        TA1R=0;
        TA1CCR0 =  10000;
        FlashWriteTimer++;
        if (FlashWriteTimer>1000){
            P1OUT|=LEDG;
        }else{
            P1OUT|=LEDR;
        }
    }else{
        if (FlashWriteTimer>1000){
            FlashWrite(SWTHRESH,Vin_avg_sum);
            FlashWriteTimer=0;
        }
        //re-enable button interrupts
        P1IE|=BUTTON;
        //Disable and clear timer1
        TA1CTL=TASSEL_2 + MC_0 + ID_3;
        TA1R=0;
    }
    #ifdef DEBUG
    ta1ticks++;
    #endif // DEBUG
}

interrupt(TIMER0_A0_VECTOR) timer0_interrupt(void){
    //Timer0 is used to initiate ADC
    //P1OUT ^= LEDR;
    ADCStart();
    #ifdef DEBUG
    ta0ticks++;
    #endif // DEBUG
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
		Vtrig_arr_vals[Vtrig_pos++]=ADC10MEM;
		if (Vtrig_pos==8){//Select Vin channel after the last Vtrig measurement
			ADC10CTL0 &= ~ENC;
			ADC10CTL1 = ADC_IN_VIN + ADC10DIV_3;
		}
	}else if(Vin_pos<8){
		Vin_arr_vals[Vin_pos++]=ADC10MEM;
		if (Vin_pos==8){//Select VTRIG channel after the last VIN measurement
			ADC10CTL0 &= ~ENC;
			ADC10CTL1 = ADC_IN_VTRIG + ADC10DIV_3;
		}
	}
	if (Vin_pos>=8){
        //Process result of the measurement cycle
		Vtrig_pos=0;
		Vin_pos=0;
		Vtrig_avg_sum=0;
		Vin_avg_sum=0;
		for (i = 0; i < 8; i++){
			Vtrig_avg_sum+=Vtrig_arr_vals[i];
			Vin_avg_sum+=Vin_arr_vals[i];
		}
		//Once the result is ready, check software threshold
		if (Vin_avg_sum>=*SWTHRESH){
            CloseValve();
		}
	}
	#ifdef DEBUG
    adcticks++;
    #endif // DEBUG
}
