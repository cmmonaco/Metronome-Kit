/*
 * Metronome_v1.c
 *
 * Copyright (c) 2012 Chris Monaco
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *    - Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Created: 6/16/2012 5:29:18 PM
 *  Author: Christopher Monaco
 */ 

//Define the clock speed for the interrupt library.
//We're using the 1MHz internal oscillator
#define F_CPU 1000000UL

//Include AVR Libraries
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

//Define values to be used in programming
#define MAX_SPEED              250   //Max metronome speed in BPM
#define MIN_SPEED              30    //Min metronome speed in BPM

#define DISP_CATH_PORT         PORTD //The Display cathodes are connected to pins PD0-PD6
#define DISP_AN_PORT           PORTC //The Display anodes are connected to pins PC0-PC2
#define DIGIT1                 0     //PC0
#define DIGIT2                 1     //PC1
#define DIGIT3                 2     //PC2
#define OUTPUT_PORT            PORTB //The Buzzer is connected to pin PB1/OCA1, LED is connected to PB0

#define INPUT                  PINB  //Buttons are on PB2-PB4
#define LED_PIN                0     //PB0
#define DEBOUNCE_COUNT_MAX     240    //Number used for debouncing the buttons
#define TIMESIG_DISPLAY_LENGTH 500  //Number of loops during which to display the current time signature

//Enumerated Values for use in programming
enum {BUTTON_UP = 2, BUTTON_DOWN, BUTTON_TIMESIG, NUM_BUTTONS} buttons;
enum {FOUR_FOUR, TWO_FOUR, TWO_TWO, THREE_EIGHT, FIVE_EIGHT, SIX_EIGHT, NUM_TIMESIG} timeSigs;

//Global Variables Declarations
//State Variables
uint8_t speed = 60; //Metronome speed, initialized to 60 BPM
uint8_t current_display = 1; //The current display we are on, used for multiplexing the display
uint16_t button_counter[NUM_BUTTONS]; //Array containing the amount of time each button is held down for

//Tone Generations Variables
uint16_t toneFreq = 2000; //Frequency of the tone sent to the buzzer, in Hz
uint8_t buzzPeriod = 40; //The length of the tone, toneFreq * 20ms
uint16_t iCounterMax; //Used for sounding the tone, see the timer1 ISR
uint16_t iCounter = 0;
uint8_t buzzerflag = 0;

//Time Signature Count Tone Change Variables
uint8_t current_TimeSig = FOUR_FOUR;
uint8_t beatCounter = 0; //Counts the number of beats
uint8_t divisor = 4; //Determined based on time signature

//Function Prototypes
void setup(); //Setup I/O and timers
void setDigit(int digit, int value); //sets the given value to the display at a given digit
void displayTimeSig(); //Displays the current time signature to the display


/************************************************************************/
/* Main Entry Point Into Program                                        */
/************************************************************************/
int main()
{
	setup(); //call setup function to get everything ready
	
	while(1)
	{
		//The main program loop will handle input and debouncing of the buttons
		
		/*
		The for loop here increments a the counter variable corresponding to a
		  specific button every time the main loop is cycled and a button is depressed.
		  This way we can debounce a button by making sure it is reading a single value 
		  for a certain amount of time.
		 */
		for(int i = 2; i < NUM_BUTTONS; i++)
		{
			if((INPUT & (1<<i)) == 0)
			{
				button_counter[i]++;
			}
			else
			{
				button_counter[i] = 0;
			}
		}
		
		/*
		This if statement now checks the counter vairable of the corresponding button and
		if it equals the Debounce Count the buttons action will be performed.
		*/
		if(button_counter[BUTTON_UP] == DEBOUNCE_COUNT_MAX)
		{
			//While the button is depressed, disable the timer1 interrupt and increment
			//metronome speed, but not too fast - that's what the delay is for.
			while((INPUT & (1<<BUTTON_UP)) == 0)
			{
				TIMSK1 = 0;
				speed++;
				_delay_ms(50);
			}
				
			TCNT1 = 0; //Restart the timer at zero
			TIMSK1 = (1<<OCIE1A); //Re-enable the interrupt on timer1
		}
		else if(button_counter[BUTTON_DOWN] == DEBOUNCE_COUNT_MAX)
		{
			//Same as above except now we decrement speed
			while((INPUT & (1<<BUTTON_DOWN)) == 0)
			{
				TIMSK1 = 0;
				speed--;
				_delay_ms(50);
			}
			
			TCNT1 = 0;
			TIMSK1 = (1<<OCIE1A);
		}
		else if(button_counter[BUTTON_TIMESIG] == DEBOUNCE_COUNT_MAX)
		{
			/*
			First change the current_TimeSig variable by one. If incrementing it causes it
			to exceed the number of time signature options, set it back to zero, or four-four.
			*/
			if((current_TimeSig + 1) >= NUM_TIMESIG)
			{
				current_TimeSig = FOUR_FOUR;
			}
			else
			{
				current_TimeSig++;
			}
			
			/*
			Next, depending on the time signature chosen, update the divisor variable. This will
			be explained in more detail in the timer1 ISR below.
			*/
			switch(current_TimeSig)
			{
				case FOUR_FOUR: divisor = 4;
				break;
				case TWO_TWO: ;
				case TWO_FOUR: divisor = 2;
				break;
				case THREE_EIGHT: divisor = 3;
				break;
				case FIVE_EIGHT: divisor = 5;
				break;
				case SIX_EIGHT: divisor = 6;
			}				
				
			//Put the current time signature on the display		
			displayTimeSig();
		}
	}
	
	return 0;
}


/************************************************************************/
/* Setup()                                                              */
/* Handles all initial set up of registers and timers.                  */
/************************************************************************/
void setup()
{
	//Handle Pin Direction Settings
	DDRD = 0xFF; //DISP_CATH_PORT, set all pins as output
	DDRC = 0x07; //DISP_AN_PORT, set pins PC0, PC1, PC2 as output
	DDRB = 0x03; //I/O pins on port B, set pins 0 and 1 as output
	PORTB = 0x1C; //Set internal pull-ups on pins PB2, PB3, PB4
	
	//Initialize iCounterMax for tone generation
	iCounterMax = toneFreq*((float)60/(float)speed);
	
	//Initialize all button counters to 0
	for(int i = 0; i < NUM_BUTTONS; i++)
	{
		button_counter[i] = 0;
	}
	
	//Set-up Timers and Interrupts
	//Timer0 for display multiplexing
	TCCR0A = (1<<WGM01); //set timer with normal port operation and clear on compare match mode or CTC
	TCCR0B = (1<<CS01) | (1<<CS00); //use clock divided by 64
	OCR0A = 38; //timer cycles at approx. 400Hz, (1MHz/64/400Hz - 1) = 38
	TIMSK0 = (1<<OCIE0A); //Enable match interrupt on compare register A
	
	//Timer1 for Generation a Tone
	TCCR1A = (1<<WGM10) | (1<<WGM11); //Fast PWM mode, start with output disabled
	TCCR1B = (1<<WGM12) | (1<<WGM13) | (1<<CS11); //clock divided by 8
	OCR1A = (F_CPU/8)/toneFreq - 1; //This calculation will generate a PWM with the frequency given by toneFreq
	TIMSK1 = (1<<OCIE1A); //Enable match interrupt
	
	sei(); //Enable global interrupt
}


/************************************************************************/
/* setDigit(int digit, int value)                                       */
/* Places a 0-9 value into a specific digit location of the display.    */
/************************************************************************/
void setDigit(int digit, int value)
{
	/*
	digit corresponds to the digits on the display. Digit 1 is the
	left most digit or the hundreds place, digit 2 is the center digit
	and digit three is the right most digit.
	*/
	
	//Here we set the pin of the correct anode to high
	switch(digit)
	{
		case DIGIT1: DISP_AN_PORT |= (1<<DIGIT1);
		break;
		case DIGIT2: DISP_AN_PORT |= (1<<DIGIT2);
		break;
		case DIGIT3: DISP_AN_PORT |= (1<<DIGIT3);
		break;
	}
	
	/*
	Since a common anode display is used, the anode must be made high(1)
	while the cathode is brought low(0). When the cathode and anode are both
	high there will be no potential across the LED and therefore no current
	flow. Below we are setting the pins corresponding to the proper digit to 
	low while the rest are left high so the the proper digit is displayed. Please
	see the datasheet for the display for a schematic view of how the display is
	wired.
	*/
	switch(value)
	{
		case 0: DISP_CATH_PORT = 0b01000000;
		break;
		case 1: DISP_CATH_PORT = 0b01111001;
		break;
		case 2: DISP_CATH_PORT = 0b00100100;
		break;
		case 3: DISP_CATH_PORT = 0b00110000;
		break;
		case 4: DISP_CATH_PORT = 0b00011001;
		break;
		case 5: DISP_CATH_PORT = 0b00010010;
		break;
		case 6: DISP_CATH_PORT = 0b00000010;
		break;
		case 7: DISP_CATH_PORT = 0b01111000;
		break;
		case 8: DISP_CATH_PORT = 0b00000000;
		break;
		case 9: DISP_CATH_PORT = 0b00010000;
	}
}


/************************************************************************/
/* displayTimeSig()                                                     */
/* Puts the time signature on the display for the amount of time        */
/* specified by TIMESIG_DISPLAY_LENGTH.                                 */
/************************************************************************/
void displayTimeSig()
{
	//Local Variable Declarations
	uint16_t i = 0; //loop control variable i
	int flag = 0; //flag to determine current display to be active
	
	//Disable both the display and the tone timer interrupts so we can
	//use the display and temporarily stop the metronome tone sounds
	TIMSK0 = 0;
	TIMSK1 = 0;
	
	//Ensure that all Anodes are turned off and the display is completely
	//disabled.
	DISP_AN_PORT &= ~((1<<DIGIT1) | (1<<DIGIT2) | (1<<DIGIT3));
	DISP_CATH_PORT = 0b11111111;
	
	/*
	This loop will run and display the chosen time signature for a length
	specified by TIMESIG_DISPLAY_LENGTH. While in the loop, the flag
	variable is updated to switch back and forth between the first and third
	digit of the display. This happens fast enough that your eyes do not
	notice the change. This form of multiplexing is explained in more detail
	in the Timer0 ISR below.
	*/
	while(i <= TIMESIG_DISPLAY_LENGTH)
	{
		if(flag == 0)
		{
			DISP_AN_PORT &= ~(1<<DIGIT3); //Disable third digit
			DISP_AN_PORT |= (1<<DIGIT1); //Enable the first digit
			flag = 1; //update flag
			
			//Set the correct digit corresponding to the first number in
			//the time signature
			if(current_TimeSig == FOUR_FOUR)
				DISP_CATH_PORT = 0b00011001;
			else if((current_TimeSig == TWO_TWO) || (current_TimeSig == TWO_FOUR))
				DISP_CATH_PORT = 0b00100100;
			else if(current_TimeSig == THREE_EIGHT)
				DISP_CATH_PORT = 0b00110000;
			else if(current_TimeSig == FIVE_EIGHT)
				DISP_CATH_PORT = 0b00010010;
			else if(current_TimeSig == SIX_EIGHT)
				DISP_CATH_PORT = 0b00000010;
				
		}
		else
		{
			DISP_AN_PORT &= ~(1<<DIGIT1);
			DISP_AN_PORT |= (1<<DIGIT3);
			flag = 0;
			
			if((current_TimeSig == FOUR_FOUR) || (current_TimeSig == TWO_FOUR))
				DISP_CATH_PORT = 0b00011001;
			else if(current_TimeSig == TWO_TWO)
				DISP_CATH_PORT = 0b00100100;
			else if((current_TimeSig == THREE_EIGHT) || (current_TimeSig == FIVE_EIGHT) || (current_TimeSig == SIX_EIGHT))
				DISP_CATH_PORT = 0b00000000;
				
		}
		
		_delay_us(1); //MAY NOT NEED TO BE HERE??????
		i++; //Update counter
	}
	
	//Re-enable the interrupts
	TIMSK0 = (1<<OCIE0A);
	TIMSK1 = (1<<OCIE1A);
}


/************************************************************************/
/* ISR(TIMER0_COMPA_vect)                                               */
/* Interrupt service routine for Timer0 compare match. Handles display  */
/* multiplexing.                                                        */
/************************************************************************/
ISR(TIMER0_COMPA_vect)
{
	/*
	This ISR is what is used to multiplex each of the three digits of
	the display at rate of approximately 400Hz. This means that at any
	given moment in time, only one of the three digits is actually lit
	up. The displays are turned on and off too quickly for your eyes to
	perceive the changes and so it appears as though all three displays
	are on at the same time. Many technologies depend on this "trick"
	and it is a powerful method to understand. This is a very simple
	implementation but works very well for our purposes.
	
	So how to we do it here? A variable called current_display keeps
	track of which display to act on. The digit for that display is
	set and current_display is updated. And the cycle continues for
	all the digits.
	*/
	if(current_display == DIGIT1)
	{
		if(speed >= 100)
		{
			DISP_AN_PORT &= ~((1<<DIGIT2) | (1<<DIGIT3));
			setDigit(current_display, (speed - (speed % 100))/100);
		}
		current_display = DIGIT2;
	}
	else if(current_display == DIGIT2)
	{
		if(speed >= 10)
		{
			DISP_AN_PORT &= ~((1<<DIGIT1) | (1<<DIGIT3));
			setDigit(current_display, ((speed % 100) - ((speed % 100) % 10))/10);
		}
		current_display = DIGIT3;
	}
	else if(current_display == DIGIT3)
	{
		DISP_AN_PORT &= ~((1<<DIGIT1) | (1<<DIGIT2));
		setDigit(current_display, (speed % 100) % 10);
		current_display = DIGIT1;
	}
}

/************************************************************************/
/* ISR(TIMER1_COMPA_vect)                                               */
/* Used to turn on and off the PWM on pin PB1 so that tone only sounds  */
/* on beats. Also flashes an LED.                                       */
/************************************************************************/
ISR(TIMER1_COMPA_vect)
{
	/*
	This is where the actual operation of the metronome occurs! Back in
	setup() function we started Timer1 in fast PWM mode. This toggles
	pin PB1 at rate we also specified. In addition to toggling the pin,
	this interrupt is fired. If the PB1 toggling function is always 
	enabled we would hear a constant tone from the speaker. This is of
	no use for a metronome, so instead we only turn the toggling 
	functionality on for a short time at each beat so the we get a short
	"click" of the metronome.
	
	In addition, a beat counter variable keeps track of the number of 
	beats that have gone by and using modulo division changes the frequency
	of one beat to make a different beat for the first count of a measure
	based on the time signature that is selected. 
	
	Oh, and the LED is toggled as well. Examining the code we can see that
	the led is actually turned on and off during the period that the sound
	active. This is pulsing the LED so rapidly, however, that your eyes 
	cannot see the on and off and instead see a constant LED.
	*/
	iCounter++; //increment interrupt counter
	TCCR1A &= ~(1<<COM1A0); //disable PWM output on pin PB1
	
	if((iCounter == iCounterMax) || buzzerflag == 1)
	{			
		buzzerflag = 1; //set buzzer flag to on
		OUTPUT_PORT ^= (1<<LED_PIN); //toggle LED
		TCCR1A |= (1<<COM1A0); //enable PWM output on pin PB1
		
		//if the buzzer has been on for the specified length of time
		if(iCounter == (iCounterMax + buzzPeriod))
		{
			buzzerflag = 0; //set buzzer flag to off
			iCounter = 0; //reset increment counter
			OUTPUT_PORT &= ~(1<<LED_PIN); //Turn off LED
			
			//Check if we've reached the number of beats required to change frequencies
			if(beatCounter % divisor == 0) 
			{
				toneFreq = 1000;
				buzzPeriod = 20;
				beatCounter++;
			}
			else
			{
				toneFreq = 2000;
				buzzPeriod = 40;
				beatCounter++;
			}
			
			iCounterMax = toneFreq*((float)60/(float)speed); //recalculate interrupt counter max
			OCR1A = (F_CPU/8)/toneFreq; //modify compare value so timer1 fires at proper rate
		}		
		
	}
}