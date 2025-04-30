/**************************************************************************
* Company: Learning the Art of Electronics
* Engineer: David Abrams
*
* Create Date:   2021-02-25
* Module Name:   main.c (Micro 4 - DAC out with Timer/Counter Interrupt)
* Project Name:  25L_TC_Skeleton.c
* Target Device:  Sparkfun SAMD21 Micro
* Description: Set up DAC for output with CPU clock set to 48MHz PLL clock.
* Use timer ISR to output synthetic sine wave
* Revision: 2021-04-20 clean up for lab; add PlaySong() function
*           2022-05-26 rename SetTC4Div()
***************************************************************************/
#include "samd21.h"     // add to use CMSIS
#include "ClockSysInit48M.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

/* 10-bit, 128 element sampled sine wave array generated in MATLAB */
uint16_t sine[] = {
512, 537, 562, 587, 612, 637, 661, 685, 709, 732, 754, 776, 798, 818, 838,
857, 875, 893, 909, 925, 939, 952, 965, 976, 986, 995, 1002, 1009, 1014,
1018, 1021, 1023, 1023, 1022, 1020, 1016, 1012, 1006, 999, 990, 981, 970,
959, 946, 932, 917, 901, 884, 866, 848, 828, 808, 787, 765, 743, 720, 697,
673, 649, 624, 600, 575, 549, 524, 499, 474, 448, 423, 399, 374, 350, 326,
303, 280, 258, 236, 215, 195, 175, 157, 139, 122, 106, 91, 77, 64, 53, 42,
33, 24, 17, 11, 7, 3, 1, 0, 0, 2, 5, 9, 14, 21, 28, 37, 47, 58, 71, 84, 98,
114, 130, 148, 166, 185, 205, 225, 247, 269, 291, 314, 338, 362, 386, 411,
436, 461, 486, 511
};

#define SAMPLES sizeof(sine) / sizeof(sine[0])  // number of samples in sine array above

// data for song demonstration

// note definitions (48MHz/128 = 375,000)
#define G6_NOTE ((375000/1568)-1)  // G6
#define F6_NOTE ((375000/1397)-1)  // F6
#define E6_NOTE ((375000/1318)-1)  // E6
#define D6_NOTE ((375000/1175)-1)  // D6
#define C6_NOTE ((375000/1040)-1)  // C6
#define B5_NOTE ((375000/988)-1)   // B5
#define A5_NOTE ((375000/880)-1)   // A5
#define G5_NOTE ((375000/783)-1)   // G5
#define F5_NOTE ((375000/698)-1)   // F5
#define E5_NOTE ((375000/659)-1)   // E5
#define D5_NOTE ((375000/587)-1)   // D5
#define C5_NOTE ((375000/523)-1)   // C5
#define B4_NOTE ((375000/494)-1)   // B4
#define QUIET   0xffff              // 5Hz - effectively silent note

// note duration definitions
#define BRK  50             // time between notes (mSec)
#define QTN  350            // time for quarter note (mSec)
#define QTNH (QTN * 1.5)    // quarter note held
#define HFN  (QTN * 2)      // half note is twice as long as quarter note
#define HFNH (QTN * 3)      // half note held
#define WHN  (QTN * 4)      // whole note
#define ETN  (QTN / 2)      // eighth note
#define ETNH (ETN * 1.5)    // eighth note held

// data for Mary Had a Little Lamb
const uint16_t mary_notes[] = {E5_NOTE, D5_NOTE, C5_NOTE, D5_NOTE, E5_NOTE,
                               E5_NOTE, E5_NOTE, D5_NOTE, D5_NOTE, D5_NOTE,
                               E5_NOTE, G5_NOTE, G5_NOTE, E5_NOTE, D5_NOTE,
                               C5_NOTE, D5_NOTE, E5_NOTE, E5_NOTE, E5_NOTE,
                               D5_NOTE, D5_NOTE, E5_NOTE, D5_NOTE, C5_NOTE, QUIET};

const uint32_t mary_htime[] = {QTN, QTN, QTN, QTN, QTN, QTN, HFN, QTN, QTN, HFN,
                               QTN, QTN, HFN, QTN, QTN, QTN, QTN, QTN, QTN, HFN,
                               QTN, QTN, QTN, QTN, WHN, 1000};

#define MARY_NUM_NOTES sizeof(mary_notes) / sizeof(mary_notes[0])

struct Song {
    const uint16_t* notes;
    const uint32_t* htime;
    const uint32_t num_notes;
    const char name[16];     // only room for 8 characters on 2 x 16 LCD display
    const int tempo;         // 0 = normal; 1 = 50% slower; 2 = half speed
};

struct Song Mary = {.notes = &mary_notes, .htime = &mary_htime,
                    .num_notes = MARY_NUM_NOTES, .name = "Mary", .tempo = 0};

/*********************************************************************
 *             Set up System Timer and Delay function
 *********************************************************************/
// use 1mSec system clock for timing

volatile uint32_t msTicks = 0;

void SysTick_Handler(void)  {       // SysTick interrupts every millisecond
    msTicks++;
}

// Function to delay for n milliseconds
void Delay (uint32_t DelayMSec)  {
  uint32_t endTicks;

  endTicks = msTicks + DelayMSec;

  while (msTicks < endTicks)  {       // wait for DelayMSec SysTick interrupts
      __WFE ();                       // tell CPU ok to power-down until interrupt
  }
}

/*********************************************************************
 *              DAC Support Functions
 **********************************************************************/

void DAC_Init(void){
   // insert DAC_Init() function from previous lab  
}

/*********************************************************************
 *             Timer/Counter Support Functions
 **********************************************************************/

// Pin definitions from samd21g18a.h for reference
//   #define PIN_PB08E_TC4_WO0   40L        / PB08 (TC4:0 - Arduino pin A1 on mux E)
//   ***** you will need to bitwise AND this value with 0x1f to get valid pin number
//   #define PORT_PB08E_TC4_WO0  (1ul << 8)

// Pin multiplexer definitions in port.h
//   #define PORT_PMUX_PMUXE_E   (PORT_PMUX_PMUXE_E_Val << PORT_PMUX_PMUXE_Pos)

void TC4_Init(void){

    /***************** PORT I/O Initialization  *************/
    /* Set up Arduino Port A1 (PB_08) as output pin         */
    /* unnecessary except for debugging                     */
    /********************************************************/

    // Set pin PB08 as a TC4 WO0 output
    REG_PORT_DIRSET1 = PORT_PB08E_TC4_WO0;           // port group 1 (PB pins)

    // Configure the TC4 output pin with DRVSTR = 0, PULLEN = 0, INEN = 1 and PMUXEN = 1
    PORT->Group[1].PINCFG[(PIN_PB08E_TC4_WO0 & 0x1f)].reg = PORT_PINCFG_PMUXEN | PORT_PINCFG_INEN;

    // Enable pin multiplexer configuration E (even pin)
    PORT->Group[1].PMUX[(PIN_PB08E_TC4_WO0  & 0x1f) >> 1].reg = PORT_PMUX_PMUXE_E;

    /******************** POWER MANAGER Initialization  ******************/
    /* On reset power to Timer Counters are disabled, must be enabled    */
    /*********************************************************************/

    PM->APBCMASK.reg |= PM_APBCMASK_TC4;    // enable the TC4 bus clock 

    /******** GENERIC CLOCK CONTROLLER Initialization ********/
    /* Connect Clock 0 to TC4 and enable TC4 clock.          */
    /* Clock 0 is enabled and set to 48MHz                   */
    /*********************************************************/

    GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID_TC4_TC5 | GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK0;
    while(GCLK->STATUS.bit.SYNCBUSY);       // requires write synchronization   

    /****************** TC4 Initialization **********************/
    /*    Setup TC4 for 16-bit waveform generation operation    */
    /************************************************************/

    // Reset the TC before configuring it
    TC4->COUNT16.CTRLA.reg = TC_CTRLA_SWRST;
    while(TC4->COUNT16.CTRLA.bit.SWRST);        // wait for reset synchronization
    while(TC4->COUNT16.STATUS.bit.SYNCBUSY);    // wait for sync synchronization

    // configure for 16 bit mode, waveform generation, no prescaler and run in standby mode
    TC4->COUNT16.CTRLA.reg =
           TC_CTRLA_MODE_COUNT16 | TC_CTRLA_WAVEGEN_MFRQ | TC_CTRLA_PRESCALER_DIV1 | TC_CTRLA_ENABLE;
    while(TC4->COUNT16.STATUS.bit.SYNCBUSY);    // wait for enable bit synchronization

    // configure initial compare value to 239 (100Khz timer square wave)
    TC4->COUNT16.CC[0].reg = 239;
    while(TC4->COUNT16.STATUS.bit.SYNCBUSY);    // wait for sync synchronization

    /******************************************************************************
    *  Enable TC4 interrupt in NVIC and configure TC4 to interrupt on CC0 match
    ******************************************************************************/

    // Configure and enable the NVIC interrupt for TC4 w/ high priority
    NVIC_DisableIRQ(TC4_IRQn);
    NVIC_ClearPendingIRQ(TC4_IRQn);
    NVIC_SetPriority(TC4_IRQn, 0);  // 0 is highest priority; 3 lowest
    NVIC_EnableIRQ(TC4_IRQn);

    // Enable the TC4 interrupt request on the MC0 (match/compare 0) bit
    TC4->COUNT16.INTENSET.reg = TC_INTENSET_MC0;
}

/******* TC4 Interrupt Service Routine (not done) *******/
uint16_t hightime_calc(int freq, int duty) {
  return (1e6 / freq) * duty * 1e-2;
}
uint16_t lowtime_calc(int freq, int duty) {
  return (1e6 / freq) * (100 - duty) * 1e-2;
}
int hightime = 959;
int lowtime = 3839;


void TC4_IRQHandler (void) {
  if(REG_PORT_IN1 & PORT_PB08E_TC4_WO0) { //if output pin is hight
	TC4->COUNT16.CC[0].reg = hightime; // count hightime CPU cycles
	while(TC4->COUNT16.STATUS.bit.SYNCBUSY);
  } else {
	TC4->COUNT16.CC[0].reg = lowtime; //otherwise count lowtime cycles
	while(TC4->COUNT16.STATUS.bit.SYNCBUSY);
  }
  TC4->COUNT16.INTFLAG.reg = TC_INTFLAG_MC0;	//clear the interrupt so that it will run again
}


/****** Set timer interrupt frequency (not done) ******/
// Argument is actual value for timer counter
// divider NOT the frequency desired

void SetTC4Div(uint16_t freq_divider) {
  freq_divider = 24E6 / freq_divider;
  TC4->COUNT16.CC[0].reg = freq_divider;
  while(TC4->COUNT16.STATUS.bit.SYNCBUSY);
}

/********************************************
 *            utility functions
 ********************************************/

/** enable pushbutton on Arduino Pin 0 as input with pullup **/
void PORT_Init(void) {
  REG_PORT_DIRCLR0 = PORT_PA11;
  // Configure the button input pin with DRVSTR = 0, PULLEN = 1, INEN = 1 and PMUXEN = 0
  PORT->Group[0].PINCFG[(PIN_PA11)].reg = PORT_PINCFG_PULLEN | PORT_PINCFG_INEN;
  REG_PORT_OUTSET0 = PORT_PA11;
}

/** function to return true if pushbutton pressed **/
bool ReadButton(void) {
  return (REG_PORT_IN0 & PORT_PA11) == 0;  // true if button pressed (active low)
}

/** function to play a song on DAC **/
void PlaySong(struct Song *song) {
        printf("Playing: %s\n", song->name);
        for (int i = 0; i < song->num_notes; i++) {
              SetTC4Div(song->notes[i]);
              Delay(song->htime[i]);
              if (song->tempo == 1) {
                  Delay(song->htime[i] >> 1);
              } else if (song->tempo == 2) {
                  Delay(song->htime[i]);
              }

              SetTC4Div(QUIET);    // 50mSec between notes
              Delay(BRK);
    }
}



/*******************************
 *            main()
 ******************************/
#define FSM_CLK 50

#define ORBIT         1
#define SURFACE       2
#define ORBIT_DUTY    20 
#define SURFACE_DUTY  10

#define  DN_INC       (SURFACE_DUTY-ORBIT_DUTY)*FSM_CLK/2e8   // increment for 5 sec descent
#define  UP_INC       (ORBIT_DUTY-SURFACE_DUTY)*FSM_CLK/4e8   // increment for 2.5 sec ascent


int main (void)
{
  // system initializations
  int freq = 100;
  int duty = 20;
  int state = ORBIT;
  hightime = hightime_calc(freq, duty);
  lowtime = lowtime_calc(freq, duty);
  // ClockSysInit48M();                    // set CPU clock to 48MHz
  SysTick_Config(SystemCoreClock/1000); // Configure SysTick for Delay() timer
  DAC_Init();                           // 1V full scale, 10-bit right justified
  TC4_Init();                           // initialize timer/counter 4 for DAC output timing
  PORT_Init();                          // enable pushbutton on Arduino pin 0  

  uint32_t endTicks;
  
  // main loop - DAC and Timer/Counter Interrupt tests.
  while(1) {
    endTicks = msTicks + FSM_CLK;  // calculate FSM cycle end time
    switch(state){
      case ORBIT:
        if(ReadButton()){
          state = SURFACE;
        } else if (duty <= ORBIT_DUTY) {
          duty = ORBIT_DUTY;
          hightime = hightime_calc(freq, duty);
          lowtime = lowtime_calc(freq, duty);
        } else {
          duty = duty + UP_INC;
          hightime = hightime_calc(freq, duty);
          lowtime = lowtime_calc(freq, duty);
        }
      break;

      case SURFACE:
        if(!ReadButton()){
          state = ORBIT;
        } else if (duty >= SURFACE_DUTY) {
          duty = SURFACE_DUTY;
          hightime = hightime_calc(freq, duty);
          lowtime = lowtime_calc(freq, duty);
        } else {
          duty = duty + DN_INC;
          hightime = hightime_calc(freq, duty);
          lowtime = lowtime_calc(freq, duty);
        }
      break;

      default:
        state = ORBIT;
        break;

    }

    while (msTicks < endTicks){       // wait for end of FSM clock period
         __WFE ();                      // tell CPU ok to power down till interrupt
      }
  }

  return 0;
}
/*************************** End of file ****************************/
