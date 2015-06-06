//AVR Libraries
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>

//UCR Developed Libraries
#include "ucr/bit.h"

// EEPROM Macros
#define read_eeprom_word(address) eeprom_read_word ((const uint16_t*)address)
#define write_eeprom_word(address,value) eeprom_write_word ((uint16_t*)address,(uint16_t)value)
#define update_eeprom_word(address,value) eeprom_update_word ((uint16_t*)address,(uint16_t)value)
#define write_eeprom_array(address,value_p,length) eeprom_write_block ((const void *)value_p, (void *)address, length)
// EEPROM Array
int EEMEM beat_EEPROM[400];

//--------Find GCD function --------------------------------------------------
unsigned long int findGCD(unsigned long int a, unsigned long int b) {
    unsigned long int c;
    while(1){
        c = a%b;

        if(c==0) {
          return b;
        }

        a = b;
        b = c;
    }
    return 0;
}
//--------End find GCD function ----------------------------------------------

//--------Timer Setup---------------------------------------------------------
// TimerISR() sets this to 1. C programmer should clear to 0.
volatile unsigned char TimerFlag = 0;
// Internal variables for mapping AVR's ISR to our cleaner TimerISR model.
unsigned long _avr_timer_M = 1; // Start count from here, down to 0. Default 1 ms.
unsigned long _avr_timer_cntcurr = 0; // Current internal count of 1ms ticks
void TimerOn() {
	// AVR timer/counter controller register TCCR1
	// bit3 = 0: CTC mode (clear timer on compare)
	// bit2bit1bit0=011: pre-scaler /64
	// 00001011: 0x0B
	// SO, 8 MHz clock or 8,000,000 /64 = 125,000 ticks/s
	// Thus, TCNT1 register will count at 125,000 ticks/s
	TCCR1B = 0x0B;
	// AVR output compare register OCR1A.
	// Timer interrupt will be generated when TCNT1==OCR1A
	// We want a 1 ms tick. 0.001 s * 125,000 ticks/s = 125
	// So when TCNT1 register equals 125,
	// 1 ms has passed. Thus, we compare to 125.
	OCR1A = 125;
	// AVR timer interrupt mask register
	// bit1: OCIE1A -- enables compare match interrupt
	TIMSK1 = 0x02;
	//Initialize avr counter
	TCNT1=0;
	// TimerISR will be called every _avr_timer_cntcurr milliseconds
	_avr_timer_cntcurr = _avr_timer_M;
	//Enable global interrupts: 0x80: 1000000
	SREG |= 0x80;
}
void TimerOff() {
	// bit3bit1bit0=000: timer off
	TCCR1B = 0x00;
}
void TimerISR() {
	TimerFlag = 1;
}
// In our approach, the C programmer does not touch this ISR, but rather TimerISR()
ISR(TIMER1_COMPA_vect) {
	// CPU automatically calls when TCNT1 == OCR1
	// (every 1 ms per TimerOn settings)
	// Count down to 0 rather than up to TOP (results in a more efficient comparison)
	_avr_timer_cntcurr--;
	if (_avr_timer_cntcurr == 0) {
		// Call the ISR that the user uses
		TimerISR();
		_avr_timer_cntcurr = _avr_timer_M;
	}
}
// Set TimerISR() to tick every M ms
void TimerSet(unsigned long M) {
	_avr_timer_M = M;
	_avr_timer_cntcurr = _avr_timer_M;
}
//--------End Timer Setup-----------------------------------------------------

//--------PWM Setup-----------------------------------------------------------
// 0.954 hz is lowest frequency possible with this function,
// based on settings in PWM_on()
// Passing in 0 as the frequency will stop the speaker from generating sound
void set_PWM(double frequency) {
	static double current_frequency; // Keeps track of the currently set frequency
  // Will only update the registers when the frequency changes, otherwise allows
  // music to play uninterrupted.
	if (frequency != current_frequency) {
		if (!frequency) { TCCR3B &= 0x08; } //stops timer/counter
		else { TCCR3B |= 0x03; } // resumes/continues timer/counter

		// prevents OCR3A from overflowing, using prescaler 64
		// 0.954 is smallest frequency that will not result in overflow
		if (frequency < 0.954) { OCR3A = 0xFFFF; }

		// prevents OCR3A from underflowing, using prescaler 64					// 31250 is largest frequency that will not result in underflow
		else if (frequency > 31250) { OCR3A = 0x0000; }

		// set OCR3A based on desired frequency
		else { OCR3A = (short)(8000000 / (128 * frequency)) - 1; }

		TCNT3 = 0; // resets counter
		current_frequency = frequency; // Updates the current frequency
	}
}

void PWM_on() {
	TCCR3A = (1 << COM3A0);
		// COM3A0: Toggle PB6 on compare match between counter and OCR3A
	TCCR3B = (1 << WGM32) | (1 << CS31) | (1 << CS30);
		// WGM32: When counter (TCNT3) matches OCR3A, reset counter
		// CS31 & CS30: Set a prescaler of 64
	set_PWM(0);
}

void PWM_off() {
	TCCR3A = 0x00;
	TCCR3B = 0x00;
}
//--------End PWM Setup-------------------------------------------------------

//--------Task scheduler data structure---------------------------------------
// Struct for Tasks represent a running process in our simple real-time operating system.
typedef struct _task {
    /*Tasks should have members that include: state, period,
        a measurement of elapsed time, and a function pointer.*/
    signed char state; //Task's current state
    unsigned long int period; //Task period
    unsigned long int elapsedTime; //Time elapsed since last task tick
    int (*TickFct)(int); //Task tick function
} task;

//--------End Task scheduler data structure-----------------------------------

//--------Pin Defines---------------------------------------------------------
//PORTB
#define speaker 6
//PORTD
#define newNotePin 0x20
#define recordPin 0x10
#define playPin 0x08
#define drum1Pin 0x04
#define drum2Pin 0x02
#define drum3Pin 0x01
#define noButton 0x00
//--------End Pin Defines-----------------------------------------------------

//--------Vars----------------------------------------------------------------
//DEBUG Vars
char Buttons_DEBUG = 0;
char Play_DEBUG = 0;
char Record_DEBUG = 0;
//Sounds Vars
int noSound = 0;
int drum1Sound = 1;
int drum2Sound = 2;
int drum3Sound = 3;
//Available Notes
double note[] = {0, 293.66, 349.23, 493.88};
//beat_RAM Beat
int beat_RAM[400] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
//int beat_RAM[400] = {0,0,0,0,0,1,1,1,1,1,2,2,2,2,2,1,1,1,1,1,2,2,2,2,2,3,3,3,3,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,2,2,2,2,2,1,1,1,1,1,2,2,2,2,2,3,3,3,3,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,2,2,2,2,0,0,0,0,0,1,1,1,1,1,2,2,2,2,2,1,1,1,1,1,2,2,2,2,2,3,3,3,3,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,2,2,2,2,2,1,1,1,1,1,2,2,2,2,2,3,3,3,3,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,3,3,3,3,0,0,0,0,0,1,1,1,1,1,2,2,2,2,2,1,1,1,1,1,2,2,2,2,2,3,3,3,3,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,2,2,2,2,2,1,1,1,1,1,2,2,2,2,2,3,3,3,3,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,2,2,2,2,0,0,0,0,0,1,1,1,1,1,2,2,2,2,2,1,1,1,1,1,2,2,2,2,2,3,3,3,3,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,2,2,2,2,2,1,1,1,1,1,2,2,2,2,2,3,3,3,3,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,3,3,3,3};
unsigned int beat_Size = 400;
//Flags
char play_Flag = 0;
char record_Flag = 0;
char newNote_Flag = 0;
//Current note
int currentNote = 0;
//Number of LEDs
char num_LED = 8;
//--------End Vars------------------------------------------------------------

//--------Helper Function-----------------------------------------------------
void synch_EEPROM_RAM() {
  int i;
  for(i = 0; i < beat_Size; i++) {
    if (beat_RAM[i] == -1) {
      beat_RAM[i] = read_eeprom_word(&beat_EEPROM[i]);
    } else {
      //update_eeprom_word(&beat_EEPROM[i], beat_RAM[i]);
      write_eeprom_word(&beat_EEPROM[i], beat_RAM[i]);
    }
  }
}
//--------End Helper Function-------------------------------------------------

//--------User defined FSMs---------------------------------------------------
enum Buttons_States {Buttons_INIT, Buttons_IDLE, Buttons_RECORD, Buttons_NEW, Buttons_PLAY, Buttons_DRUM1, Buttons_DRUM2, Buttons_DRUM3};
int SMButtons(int state) {
  char portDTmp = ~PIND & 0x3F;

  //State machine transitions
  switch (state) {
    case Buttons_INIT:
      state = Buttons_IDLE;
      break;
    case Buttons_IDLE:
      if (Buttons_DEBUG) PORTA = 1;
      if (portDTmp == noButton)
        state = Buttons_IDLE;
      else if (portDTmp == drum3Pin)
        state = Buttons_DRUM3;
      else if (portDTmp == drum2Pin)
        state = Buttons_DRUM2;
      else if (portDTmp == drum1Pin)
        state = Buttons_DRUM1;
      else if (portDTmp == playPin)
        state = Buttons_PLAY;
      else if (portDTmp == recordPin)
        state = Buttons_RECORD;
      else if (portDTmp == newNotePin)
        state = Buttons_NEW;
      break;
    case Buttons_RECORD:
      if (Buttons_DEBUG) PORTA = 2;
      if (portDTmp == recordPin) {
        state = Buttons_RECORD;
      } else if (portDTmp == noButton) {
        record_Flag = (record_Flag == 0) ? 1 : 0;
        play_Flag = (record_Flag == 1) ? 0 : record_Flag;
        newNote_Flag = (record_Flag == 1) ? 0 : record_Flag;
        state = Buttons_IDLE;
      }
      break;
    case Buttons_PLAY:
      if (Buttons_DEBUG) PORTA = 3;
      if (portDTmp == playPin) {
        state = Buttons_PLAY;
      }
      else if (portDTmp == noButton) {
        play_Flag = (play_Flag == 0) ? 1 : 0;
        record_Flag = (play_Flag == 1) ? 0 : record_Flag;
        newNote_Flag = (play_Flag == 1) ? 0 : record_Flag;
        state = Buttons_IDLE;
      }
      break;
    case Buttons_DRUM1:
      if (Buttons_DEBUG) PORTA = 4;

      if (portDTmp == drum1Pin) {
        currentNote = drum1Sound;
        state = Buttons_DRUM1;
      } else if (portDTmp == noButton) {
        currentNote = noSound;
        state = Buttons_IDLE;
      }
      break;
    case Buttons_DRUM2:
      if (Buttons_DEBUG) PORTA = 5;

      if (portDTmp == drum2Pin) {
        currentNote = drum2Sound;
        state = Buttons_DRUM2;
      } else if (portDTmp == noButton) {
        currentNote = noSound;
        state = Buttons_IDLE;
      }
      break;
    case Buttons_DRUM3:
      if (Buttons_DEBUG) PORTA = 6;

      if (portDTmp == drum3Pin) {
        currentNote = drum3Sound;
        state = Buttons_DRUM3;
      } else if (portDTmp == noButton) {
        currentNote = noSound;
        state = Buttons_IDLE;
      }
      break;
    case Buttons_NEW:
      if (Buttons_DEBUG) PORTA = 7;

      if (portDTmp == newNotePin) {
        state = Buttons_DRUM3;
      } else if (portDTmp == noButton) {
        state = Buttons_IDLE;
      }
      break;
    default:
      state = Buttons_INIT;
      break;
  }

  //State machine actions
  switch (state) {
    case Buttons_INIT:
      break;
    case Buttons_IDLE:
      break;
    case Buttons_RECORD:
      break;
    case Buttons_PLAY:
      break;
    case Buttons_DRUM1:
      break;
    case Buttons_DRUM2:
      break;
    case Buttons_DRUM3:
      break;
    default:
      break;
  }

  if (play_Flag == 0) set_PWM(note[currentNote]);

  return state;
}

enum Play_States {Play_INIT, Play_IDLE, Play_LOOP};
int SMPlay(int state) {
  static int loop_Index = 0;
  char play_FlagTmp = play_Flag;

  //State machine transitions
  switch(state) {
    case Play_INIT:
      PORTA = 0x00;
      loop_Index = 0;
      state = Play_IDLE;
      break;
    case Play_IDLE:
      if (Play_DEBUG) PORTA = 1;
      if (play_FlagTmp == 0)
        state = Play_IDLE;
      else if (play_FlagTmp == 1)
        state = Play_LOOP;
      break;
    case Play_LOOP:
      if (Play_DEBUG) PORTA = loop_Index;

      //show loop progress
      if (loop_Index <= beat_Size/num_LED) PORTA = 0x01;
      else if (loop_Index <= beat_Size/num_LED*2) PORTA = 0x03;
      else if (loop_Index <= beat_Size/num_LED*3) PORTA = 0x07;
      else if (loop_Index <= beat_Size/num_LED*4) PORTA = 0x0F;
      else if (loop_Index <= beat_Size/num_LED*5) PORTA = 0x1F;
      else if (loop_Index <= beat_Size/num_LED*6) PORTA = 0x3F;
      else if (loop_Index <= beat_Size/num_LED*7) PORTA = 0x7F;
      else if (loop_Index <= beat_Size/num_LED*8) PORTA = 0xFF;

      set_PWM(note[beat_RAM[loop_Index]]);
      loop_Index = (loop_Index < beat_Size-1) ? loop_Index+1 : 0;

      if (play_FlagTmp == 1)
        state = Play_LOOP;
      else if (play_FlagTmp == 0) {
        set_PWM(0);
        state = Play_INIT;
      }
      break;
    default:
      state = Play_INIT;
      break;
  }

  //State machine actions
  switch(state) {
    case Play_INIT:
      break;
    case Play_IDLE:
      break;
    case Play_LOOP:
      break;
    default:
      break;
  }

  return state;
}

enum Record_States {Record_INIT, Record_IDLE, Record_LOOP};
int SMRecord(int state) {
  static int loop_Index = 0;
  char record_FlagTmp = record_Flag;

  switch (state) {
    case Record_INIT:
      PORTA = 0x00;
      loop_Index = 0;
      synch_EEPROM_RAM();
      state = Record_IDLE;
      break;
    case Record_IDLE:
      if (Record_DEBUG) PORTA = 1;
      if (record_FlagTmp == 0)
        state = Record_IDLE;
      else if (record_FlagTmp == 1)
        state = Record_LOOP;
      break;
    case Record_LOOP:
      if (Record_DEBUG) PORTA = 2;

      //show loop progress
      if (loop_Index <= beat_Size/num_LED) PORTA = 0x01;
      else if (loop_Index <= beat_Size/num_LED*2) PORTA = 0x03;
      else if (loop_Index <= beat_Size/num_LED*3) PORTA = 0x07;
      else if (loop_Index <= beat_Size/num_LED*4) PORTA = 0x0F;
      else if (loop_Index <= beat_Size/num_LED*5) PORTA = 0x1F;
      else if (loop_Index <= beat_Size/num_LED*6) PORTA = 0x3F;
      else if (loop_Index <= beat_Size/num_LED*7) PORTA = 0x7F;
      else if (loop_Index <= beat_Size/num_LED*8) PORTA = 0xFF;

      beat_RAM[loop_Index] = currentNote;
      loop_Index = (loop_Index < beat_Size-1) ? loop_Index+1 : 0;

      if (record_FlagTmp == 1)
        state = Record_LOOP;
      else if (record_FlagTmp == 0) {
        state = Record_INIT;
      }
      break;
    default:
      state = Record_INIT;
      break;
  }
  return state;
}
// --------END User defined FSMs-----------------------------------------------


int main() {
  //PORT I/O Define
  DDRA = 0xFF; PORTA = 0x00; //LED Output
  DDRB = 0xFF; PORTB = 0x00; //PWM for speaker on PB6
  //DDRC = 0xFF; PORTA = 0x00; //LED Debug Output
  DDRD = 0x00; PORTD = 0xFF; //Button Inputs

  // period for the tasks
  unsigned long int SMButtons_calc = 1;
  unsigned long int SMPlay_calc = 5;
  unsigned long int SMRecord_calc = 5;

  //calculate gcd
  unsigned long int tmpGCD = SMButtons_calc;
  tmpGCD = findGCD(tmpGCD, SMPlay_calc);
  tmpGCD = findGCD(tmpGCD, SMRecord_calc);

  unsigned long int GCD = tmpGCD;

  //recalculate GCD periods for scheduler
  unsigned long int SMButtons_period = SMButtons_calc/GCD;
  unsigned long int SMPlay_period = SMPlay_calc/GCD;
  unsigned long int SMRecord_period = SMRecord_calc/GCD;

  //declare array of tasks
  static task SMButtons_task, SMPlay_task, SMRecord_task;
  task *tasks[] = { &SMButtons_task, &SMPlay_task, &SMRecord_task};
  const unsigned short numTasks = sizeof(tasks)/sizeof(task*);

  // Task 1
  SMButtons_task.state = -1;
  SMButtons_task.period = SMButtons_period;
  SMButtons_task.elapsedTime = SMButtons_period;
  SMButtons_task.TickFct = &SMButtons;

  // Task 2
  SMPlay_task.state = -1;
  SMPlay_task.period = SMPlay_period;
  SMPlay_task.elapsedTime = SMPlay_period;
  SMPlay_task.TickFct = &SMPlay;

  // Task 3
  SMRecord_task.state = -1;
  SMRecord_task.period = SMRecord_period;
  SMRecord_task.elapsedTime = SMRecord_period;
  SMRecord_task.TickFct = &SMRecord;

  // set the timer
  TimerSet(GCD);
  // turn timer on
  TimerOn();

  // setup PWM
  PWM_on();

  unsigned short i;
  while(1) {
    // scheduler code
    for ( i = 0; i < numTasks; i++ ) {
      // task is ready to tick
      if ( tasks[i]->elapsedTime == tasks[i]->period ) {
          // setting next state for task
          tasks[i]->state = tasks[i]->TickFct(tasks[i]->state);
          // reset the elapsed time for next tick.
          tasks[i]->elapsedTime = 0;
      }
      tasks[i]->elapsedTime += 1;
    }

    while(!TimerFlag);
    TimerFlag = 0;

  }

  return 0;
}
