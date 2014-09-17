/**************************************************************************
 *         Program pro rizeni automatiky zahradniho jezirka               *
 **************************************************************************
 *    Verze: 01.00                                                        *
 *    Autor: PPx                                                          *
 *    Datum: 06/2014                                                      *
 * Procesor: ATTiny85                                                     *
 *                                                                        *
 * Zapojeni:  PB0 <- vystup komparatoru stavu prutoku vody z cerpadla     *
 *            PB1 -> stavova dioda (testovani)                            *
 *            PB2 <- vystup komparatoru venkovniho svetla                 *
 *            PB3 -> ovladani rele spinajiciho cerpadlo                   *
 *            PB4 -> ovladani rele spinajiciho LED osvetleni              *
 *                                                                        *
 *   Funkce: Program behem dne v casovem intervalu danym hodnotami kon-   *
 *           stant STOP_TIME a RUN_TIME odstavuje a spina cerpadlo. Pri   *
 *           setmeni je pri rozbehu cerpadla rosviceno LED osvetleni.     *
 *           Toto osvetleni sviti cca 1 hod. }dano konstantou             *
 *           LED_TIME_COUNT. Po uplynuti teto dobz je procesor preveden   *
 *           rezimu spnaku. Z nej je aktivovan rozednenim.                *
 *                                                                        *
 **************************************************************************
 *                         Popis zmen verzi                               *
 *------------------------------------------------------------------------*
 *                                                                        *
 **************************************************************************/

#define F_CPU     1000000L	//MCU frequency 1MHz

#include <inttypes.h>		//short forms for various integer types
#include <avr/io.h>			// Stanrd IO
#include <avr/interrupt.h>  //file to be included if using interrupts
#include <util/delay.h>  	//file to be included if delays
#include <avr/sleep.h>		//file to be included if using sleeping

#include "bitoperations.h"

// MCU frequency 1MHz
#define XTAL		1000000L  

// Nadefinujeme ovladane piny
#define SEN_SVT           2
#define SEN_CERP          0
#define SEN_BAT           1
#define OVL_CERP          4
#define OVL_SVT           3

// Definice casovych konstant programu
#define STOP_TIME		15*60//15*60
#define RUN_TIME		10*60//10*60
#define LATENT_TIME		1
#define PROTECT_TIME	20
// Definice citacich konstant
#define CHANGING_COUNT	10
#define LED_TIME_COUNT	13735//13735


// Definice logickych hodnot false/true
#define _FALSE_		0
#define _TRUE_		1

// Promenne pro praci se signalem od svetla
volatile int svetlo_stav;			// aktualni stav svetelneho senzoru
	// 1 ... svetlo, 0 ... tma
volatile int svetlo_senz_last;		// posledni hodnota nactena hodnota senzoru
volatile int svetlo_senz_change;	// priznak zmeny stavu senzoru
volatile int svetlo_senz_count;		// citac zmeny stavu senzory

// Promenne pro praci se sginalem prutoku vody hadici
volatile int voda_stav;				// aktualmi stav senzoru prutoku
	// 1 ... tece, 0 ... netece
volatile int voda_senz_last;		// posledni nactena hodnota senzoru
volatile int voda_senz_change;		// priznak zmeny stavu senzoru
volatile int voda_senz_changing;	// citac poctu zmen
volatile int voda_senz_count;		// citac zmeny stavu senzoru

// obecne promenne systemu
volatile int sleeping;				// priznak rezimu procesoru ve spanku
volatile int cerpadlo_running;		// priznak pozadavku na beh cerpadla
volatile int running_time;			// doba behu cerpadla
volatile int LED_running;			// priznak rosvicenych LED
volatile int HI_timer_count;		// vyzsi byte citace

// testovaci smazat
volatile int tmp;

// Uvitaci testovaci funkce - trojite probliknuti
void Welcome(void) {

  SETBIT(PORTB,SEN_BAT);
  _delay_ms(100);
  CLEARBIT(PORTB,SEN_BAT);
  _delay_ms(50);
  SETBIT(PORTB,SEN_BAT);
  _delay_ms(100);
  CLEARBIT(PORTB,SEN_BAT);
  _delay_ms(50);
  SETBIT(PORTB,SEN_BAT);
  _delay_ms(100);
  CLEARBIT(PORTB,SEN_BAT);

}

// Inicializace HW systemu pri RESETU
int HWsetup(void)
{
  // Zakazeme preruseni
  cli();

  // Nastavime rezim portu
  //    SEN_SVT  - Input
  //    SEN_CERP - Input
  //    SEN_BAT  - Input
  CLEARBIT(DDRB,SEN_SVT);
  CLEARBIT(DDRB,SEN_CERP);
  //CLEARBIT(DDRB,SEN_BAT);
  //    OVL_CERP - Output
  //    OVL_SVT  - Output
  SETBIT(DDRB,OVL_CERP);
  SETBIT(DDRB,OVL_SVT);
  SETBIT(DDRB,SEN_BAT);

  // Nastaveni Pull-Up rezistoru na vstupnich portech
  SETBIT(PORTB,SEN_SVT);
  SETBIT(PORTB,SEN_CERP);
  //SETBIT(PORTB,SEN_BAT);

  //nastavime preruseni od PCINT
  SETBIT(GIMSK,PCIE);
  SETBIT(PCMSK,SEN_CERP);		// povolime preruseni PCINT od SEN_VODA
  SETBIT(PCMSK,SEN_SVT);		// povolime preruseni PCINT od SEN_SVT

  
  return(0);
}

// Inicializace stavu mikroprocesoru
int setup(void)
{

  // Inicializace promennych
  // - Cerpadlo
  voda_stav = BITVAL(PORTB,SEN_CERP);
  voda_senz_last = BITVAL(PORTB,SEN_CERP);
  voda_senz_change = _TRUE_;
  voda_senz_changing = 0;
  voda_senz_count = 0;
  cerpadlo_running = _FALSE_;
  // - Osvetleni
  svetlo_stav = BITVAL(PORTB,SEN_SVT);
  svetlo_senz_last = svetlo_stav; //BITVAL(PORTB,SEN_SVT);
  svetlo_senz_change = _TRUE_;
  svetlo_senz_count = 0;
  LED_running = _FALSE_;
  // - Systemove
  sleeping = _FALSE_;

  return(0);
}

// Inicializace programu - nastaveni stavu programu


ISR(TIMER0_OVF_vect)    // handler for overflow TIMER0
{

	HI_timer_count++;
	if (HI_timer_count > LED_TIME_COUNT) {
		sleeping = _TRUE_;
	}

	if (tmp) {
	  SETBIT(PORTB,SEN_BAT);
	  tmp = _FALSE_;
	} else {
	  CLEARBIT(PORTB,SEN_BAT);
	  tmp = _TRUE_;
	}
	

}

// Obsluha preruseni pri zmene signalu od svetelneho senzoru - probuzeni systemu
ISR(INT0_vect)
{

	// pouzite pouze pro probuzeni MCU z rezimu spanku

}

// Obsluha preruseni pri zmene hodnoty jednoho z vstupnich pinu
ISR(PCINT0_vect)
{

  /* Pouze pro ladeni */
 
	if (BITVAL(PINB,SEN_CERP)) {
	  SETBIT(PORTB,SEN_BAT);
	} else {
	  CLEARBIT(PORTB,SEN_BAT);
	} 
	//*/

  if (BITVAL(PINB,SEN_SVT) != svetlo_senz_last) {
  // doslo ke zmene na pinu SEN_SVT
	svetlo_senz_change = _TRUE_;
	svetlo_senz_count = 0;
	svetlo_senz_last = BITVAL(PINB,SEN_SVT);

  }

  if (BITVAL(PINB,SEN_CERP) != voda_senz_last) {
  // doslo ke zmene na pinu SEN_VODA

	voda_senz_change = _TRUE_;
	voda_senz_count = 0;
	voda_senz_last = BITVAL(PINB,SEN_CERP);
  }

}

// funkce zajistujici rozbeh cerpadla
void CerpadloStart(void) {

	running_time = 0;			// vynulujeme citac doby behu cerpadla
	cerpadlo_running = _TRUE_;		// nastavime priznak behu cerpadla

	SETBIT(PORTB,OVL_CERP);		        // rozbehneme cerpadlo

}

// funkce zajistujici zastaveni cerpadla
void CerpadloStop(void) {

	CLEARBIT(PORTB,OVL_CERP);		// zastavime cerpadlo
}

// funkce zajistujici rosviceni LED osvetleni
void LEDStart() {

	// nastaveni casovace 0 na 1 hodinu
	HI_timer_count = 0;
	
	// rosvitime LED
	SETBIT(PORTB,OVL_SVT);
	LED_running = _TRUE_;
	
	// spustime casovac
	TCCR0A = 0;
	TCCR0B = 0;
	SETBIT(TCCR0B,CS02);
	SETBIT(TCCR0B,CS00);		// nastavime delicku 1/1024
	SETBIT(TIMSK,TOIE0);	// povolime preruseni casovace 0
}

// funkce zajistujici zhasnuti LED osvetleni
void LEDStop() {

	// zhasneme LED
	CLEARBIT(PORTB,OVL_SVT);
	LED_running = _FALSE_;
	
	// zakazeme preruseni casovace 0
	CLEARBIT(TIMSK,TOIE0);
}

// funkce diagnostikujici stavy vstupu
void PINTest(void) {

  /*  	if (svetlo_stav) {
	  SETBIT(PORTB,SEN_BAT);
	} else {
	  CLEARBIT(PORTB,SEN_BAT);
	} 
	//*/

	if (voda_senz_change) {
	// senzor SEN_VODA zmenil hodnotu

		voda_senz_count++;		// citac stability hodnoty SEN_VODA
	}

	if (svetlo_senz_change) {
	// senzor SEN_SVT zmenil hodnotu

		svetlo_senz_count++;	// citac stability hodnoty SEN_SVT
	}

	if (svetlo_senz_count > LATENT_TIME) {
	// hodnota senzoru SEN_SVT stabilni

		svetlo_stav = svetlo_senz_last;		// zmenime hodnotu dle mereni
		svetlo_senz_change = _FALSE_;		// hodnota se nemeni
		svetlo_senz_count = 0;				// vynulujeme citac stability
	}

	if (voda_senz_count > LATENT_TIME) {
	 // hodnota senzoru SEN_VODA stabilni

		voda_stav = voda_senz_last;			// zmenime hodnotu dle mereni
		voda_senz_change = _FALSE_;			// hodnota se nemeni
		voda_senz_count = 0;				// vynulujeme citac stability
		voda_senz_changing = 0;				// vynulujeme citac kmitani

	}

	if (voda_senz_changing > LATENT_TIME) {
	// senzor SEN_VODA kmita

		voda_stav = _FALSE_;				// nastavime stav jako NETECE
//	CLEARBIT(PCMSK,SEN_CERP);			// deaktivuj preruseni od SEN_VODA
		voda_senz_change = _FALSE_;			// hodnotta se nemeni
		voda_senz_count = 0;				// vynulujeme citac stability
		voda_senz_changing = 0;				// vynulujeme citac kmitani
	}
}

// hlavni program
int main(void)
{

  int i;	// citac

  // inicializace hardware - nastaveni portu apod.  
  HWsetup();

  // inicializace pocatecniho stavu systemu
  setup();

  // povolime preruseni
  sei();

  // vlastni telo programu
  for(;;)  // nekonecna smycka
  {
    Welcome();


	while (!sleeping) {
	// opakujeme dokud system neuspime

		for (i = 0; i < STOP_TIME; i++) {
		// rezim cerpadla - odpocinek
			PINTest();		// obsluha vstupu
			_delay_ms(1000);	// cekame 1 sekundu

		}

		// Zjistime zda nerosvitit LED osvetleni
      		if (!LED_running) {
		  if (!svetlo_stav) {
				LEDStart();		// rosvitime LED osvetleni
		  }
      		}
		
		// pustime cerpadlo
		CerpadloStart();

		while (cerpadlo_running) {
		//  rezim cerpadla - cerpame

			// zpracujeme vstupy
			PINTest();

			if (running_time > RUN_TIME) {
			// vyprsel cas cerpani

				cerpadlo_running = 0;	// nastavime priznak na false
			}

			if (running_time > PROTECT_TIME) {
			// cerpadlo se ji rozbehlo

			  if (voda_stav) {
			    if (voda_senz_change) {
			      cerpadlo_running = 0;
			    }
			  } else{
			    cerpadlo_running = 0;
			  }
			}

			_delay_ms(1000);

			running_time++;
		}

		CerpadloStop();

	}

	// zhasneme osvetleni
	LEDStop();

	// Ladeni
	CLEARBIT(PORTB,SEN_BAT);

	// uspime MCU
	while (sleeping) {
		
		// nastavime preruseni INT0 od nabezne hrany
		SETBIT(MCUCR,CS01);
		SETBIT(MCUCR,CS00);
		SETBIT(GIMSK,INT0);
		
		// nastavime rezim spnaku
		set_sleep_mode(SLEEP_MODE_IDLE);
		
		// uspime MCU
		sleep_mode();

		SETBIT(PORTB,SEN_BAT);	
		// MCU probuzen od svetelneho senzoru
		for(i = 0; i < PROTECT_TIME; i++) {		// pockame zda neslo o zablesk
			_delay_ms(1000);
		}
		CLEARBIT(PORTB,SEN_BAT);
		
		if (BITVAL(PINB,SEN_SVT)) {
			sleeping = _FALSE_;
		}
	
	}

  }

  return(0);


}
