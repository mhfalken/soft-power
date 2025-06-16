/*
 * Battery power control
 * 7-2024 Michael Hansen
 *
 * ATTINY13
 *   Device setup:
 *   - Internal 128kHz clock
 *   - CLKDIV8 not set
 *
 *  Programmer:
 *   AVRISP MKII - programming clock (ISP clock 16 kHz [<128kHz/4))
 *
 */ 

#include <stdint.h>
#include <xc.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>

#define BIT(x) (1 << (x))

#define GPO_PWR          1  // Active low (power-on)
#define GPI_BUTTON       0  // Low = pressed (pull-up)
#define GPI_CPU_CTRL     2  // High = power-on request from CPU
#define GPIO_CPU_BUTTON  3  // Active low, tristate high (repeat button state)

uint8_t pwrCtrlState;
#define PWR_STATE_off      0  // OFF
#define PWR_STATE_on       1  // ON
#define PWR_STATE_on_off   2  // Power request off


/* Wait approx. n * 1 ms*/
void WaitMs(uint16_t ms)
{
	uint8_t cnt;  // 8 bit
	uint16_t i;
	
	TCNT0 = 0;	// 16 clocks pr. 1 ms
	cnt = 16;   // 1 ms
	for (i=0; i<ms; i++) {
		if (cnt == 0) {
			while (TCNT0 > 200)  // Wait for wrap, 200 is just at high number
			;
		}
		else {
			while (TCNT0 < cnt)
			  ;  // wait 1 ms
		}
		cnt += 16;  // next ms, will wrap at 256 (8 bit)
	}
}


ISR(PCINT0_vect, ISR_BLOCK)
{
	 // PIN change interrupt (wake-up)
	 // Interrupt is auto cleared
} 

#define POLL_DELAY_MS   10
#define DELAY_1S_CNT    85  // 1000/POLL_DELAY_MS (tested value)

void PollState()
{
	static uint16_t btnTime=0;
	static uint16_t waitTimer=0;
	
	if ((PINB & BIT(GPI_BUTTON)) == 0) {
		// Button pressed
		btnTime++;
	}
	else
		btnTime = 0;
	waitTimer++;
	
	switch (pwrCtrlState) {
		case PWR_STATE_off:
			if (waitTimer > 10*DELAY_1S_CNT) {
				PORTB |= BIT(GPO_PWR);  // Power OFF
				pwrCtrlState = PWR_STATE_on_off;
			}
			if (btnTime > 1*DELAY_1S_CNT) {
				// Button -> Power ON
				PORTB &= ~BIT(GPO_PWR);  // Power ON
				waitTimer = 0;
			}
			if (PINB & BIT(GPI_CPU_CTRL)) {
				// CPU -> Power ON 
				PORTB &= ~BIT(GPO_PWR);  // Power ON
				pwrCtrlState = PWR_STATE_on;				
			}
		  break;
		case PWR_STATE_on:
			if ((PINB & BIT(GPI_CPU_CTRL)) == 0) {
				// CPU -> Power OFF
				PORTB |= BIT(GPO_PWR);  // Power OFF
				DDRB &= ~BIT(GPIO_CPU_BUTTON);  // Input/Tristate 'high - extern pull-up'
				waitTimer = 0;
				pwrCtrlState = PWR_STATE_on_off;
				break;
			}
			if (btnTime > 4*DELAY_1S_CNT) {
				// Button -> Power OFF
				PORTB |= BIT(GPO_PWR);  // Power OFF
				DDRB &= ~BIT(GPIO_CPU_BUTTON);  // Input/Tristate 'high - extern pull-up'
				waitTimer = 0;
				pwrCtrlState = PWR_STATE_on_off;
				break;
			}
			if (btnTime > 0) {
				DDRB |= BIT(GPIO_CPU_BUTTON);  // Output low
			}
			else {
				DDRB &= ~BIT(GPIO_CPU_BUTTON);  // Input/Tristate 'high - extern pull-up'
			}
			break;
		case PWR_STATE_on_off:
			if (waitTimer > 3*DELAY_1S_CNT) {
				// Wait for power OFF to settle
							
				// Setup pin change awake
				GIFR |= BIT(PCIF);   // Clear possible pending interrupt
				GIMSK |= BIT(PCIE);  // Pin change interrupt enable
				SREG |= BIT(7);      // Global interrupt enable
				PCMSK |= BIT(GPI_BUTTON) | BIT(GPI_CPU_CTRL);  // Enable pin change interrupt
				
				// Set to sleep
				MCUCR |= BIT(SE) | BIT(SM1);
				sleep_cpu();
				// Sleeping ... wait for input changes
				
				// Just for extra security reset the system.
				WDTCR |= BIT(WDE);  // Reset (using watchdog max 16ms) 
				WaitMs(20);  // Wait for reset
			}
			break;
	}
}


int main(void)
{
	// Initialize AVR

	MCUSR = 0;  // Disable watchdog 
	WDTCR |= BIT(WDCE) | BIT(WDE);  // Disable watchdog
	WDTCR = 0;  // Disable watchdog

  ACSR = BIT(ACD); // Disable Analog comparator (save power)
  PORTB |= BIT(GPO_PWR) | BIT(GPI_BUTTON);  // PWR off, Button pull-up
	DDRB |= BIT(GPO_PWR);  // PWR output

  /* Timer 0
	  Div 8
      128 kHz/8 -> 16 kHz (16 clocks pr. 1 ms)  */
  TCCR0B = 2;

	while(1)
	{
		PollState();
		WaitMs(POLL_DELAY_MS);
	}
}