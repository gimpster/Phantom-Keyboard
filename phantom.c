/* USB Keyboard Firmware code for the Phantom Keybaord
 * http://geekhack.org/showwiki.php?title=Island:26742
 * Copyright (c) 2012 Fredrik Atmer, Bathroom Epiphanies Inc
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include "usb_keyboard.h"
#include "teensy.h"
#include "phantom.h"

/* pressed   keeps track of which keys that are pressed 
   release   keeps track of which keys that may be released
   queue     contains the keys that are sent in the HID packet 
   mod_keys  is the bit pattern corresponding to pressed modifier keys */
uint8_t pressed[NKEY];
volatile uint8_t release[NKEY];
uint8_t queue[7] = {255,255,255,255,255,255,255};
uint8_t mod_keys = 0;

void init(void);
void send(void);
void poll(void);
void key_press(uint8_t key);
void key_release(uint8_t key);
void update_leds(uint8_t keyboard_leds);
void setup_leds(void);
void setup_io_pins(void);
void setup_bounce_timer(void);
void toggle_leds(void);

/* Check for keys ready to be released, and 
   advance the release counter on all keys. */
ISR(TIMER0_COMPA_vect) {
  uint8_t key;
  for(key = 0; key < NKEY; key++) {
    if(release[key] == 0x01)
      key_release(key);
    release[key] >>= 1;
  }
}

int main(void) {
  uint8_t row, col, key;
  uint8_t current, change, mask, previous[NCOL];

  init();
  for(col = 0; col < NROW; col++)
    previous[col] = 0;

  for(;;) {
    // Set one column at a time to low output, 
    // register which rows are pulled down.
    for(col = 0, key = 0; col < NCOL; col++) {
      *col_port[col] &= ~col_bit[col];
      _delay_us(1);
      current = (~PINB & 0x3F);
      change = current ^ previous[col];
      if(change)
	for(row = 0, mask = 0x01; row < NROW; row++, key++, mask <<= 1)
	  if(change & mask)
	    if(current & mask)
	      if(!pressed[key])
		key_press(key);
	      else
		release[key] = 0;
	    else
	      release[key] = 0x80;
	  else;
      else
	key += NROW;
      previous[col] = current;
      *col_port[col] |= col_bit[col];
    }
    update_leds(keyboard_leds);
  }
}

void send(void) {
  //return;
  uint8_t i;
  for(i=0; i<6; i++)
    keyboard_keys[i] = queue[i]<255? layout[queue[i]]: 0;
  keyboard_modifier_keys = mod_keys;
  usb_keyboard_send();
}

/* */
void key_press(uint8_t key) {
  uint8_t i;
  pressed[key] = true;
  release[key] = 0x00;
  if(is_modifier[key])
    mod_keys |= layout[key];
  else {
    for(i = 5; i > 0; i--) queue[i] = queue[i-1];
    queue[0] = key;
  }
  send();
}

/* */
void key_release(uint8_t key) {
  uint8_t i;
  pressed[key] = false;
  release[key] = 0x00;
  if(is_modifier[key])
    mod_keys &= ~layout[key];
  else {
    for(i = 0; i < 6; i++) 
      if(queue[i]==key)
	break;
    for(; i < 6; i++)
      queue[i] = queue[i+1];
  }
  send();
}

/* Call initialization functions */
void init(void) {
  CLKPR = 0x80; CLKPR = 0;
  usb_init();
  setup_io_pins();
  setup_leds();
  setup_bounce_timer();
  while(!usb_configured());
  _delay_ms(1000);
  // Enable interrupts
  sei();
}

/* */
void update_leds(uint8_t keyboard_leds) {
    PORTB = (PORTB & 0x3F) | ((keyboard_leds << 5) & 0xC0);
    DDRB  = (DDRB  & 0x3F) | ((keyboard_leds << 5) & 0xC0);
}

/* PORTB is set as input with pull-up resistors
   PORTC,D,E,F are set to high output */
void setup_io_pins(void) {
  uint8_t row, col, key;
  for(row = 0; row < NROW; row++) {
    *row_ddr[row]  &= ~row_bit[row];
    *row_port[row] |=  row_bit[row];
  }
  for(col = 0; col < NCOL; col++) {
    *col_ddr[col]  |= col_bit[col];
    *col_port[col] |= col_bit[col];
  }
  for(key = 0; key < NKEY; key++) {
    pressed[key] = false;
    release[key] = 0x00;
  }
}

/* LEDs are on output compare pins OC1B OC1C
   This activates fast PWM mode on them.
   Prescaler 256 and 8-bit counter results in
   16000000/256/256 = 244 Hz blink frequency.
   LED_A: Caps Lock
   LED_B: Scroll Lock  */
/* Output on PWM pins are turned off when the timer 
   reaches the value in the output compare register,
   and are turned on when it reaches TOP (=256). */
void setup_leds(void) {
  TCCR1A |=      // Timer control register 1A
    (1<<WGM10) | // Fast PWM 8-bit
    (1<<COM1B1)| // Clear OC1B on match, set at TOP
    (1<<COM1C1); // Clear OC1C on match, set at TOP
  TCCR1B |=      // Timer control register 1B
    (1<<WGM12) | // Fast PWM 8-bit
    (1<<CS12);   // Prescaler 256
  OCR1B = 250;    // Output compare register 1B
  OCR1C = 250;    // Output compare register 1C
  // LEDs: LED_A -> PORTB6, LED_B -> PORTB7
  DDRB  &= 0x3F;
  PORTB &= 0x3F;
}

/* Timer0 is used to call the debounce algorithm
   at regular intervals via an interrupt.
   The interrupt is triggered when the timer reaches 
   the value in the output compare register. In CTC
   mode this also resets the timer. */
void setup_bounce_timer(void) {
  TCCR0A |=      // Timer control register 0A
    (1<<WGM01);  // Set CTC, clear timer on compare
  TCCR0B |=      // Timer control register 0A
//  (1<<CS00) |  // Prescaler 1024, frequency 15.6kHz
    (1<<CS02);   // Prescaler 256, frequency 62.5kHz
  TIMSK0 |=      // Timer interrupt mask register 0
    (1<<OCIE0A); // Enable timer interrupt on compare match with OCR0A
  OCR0A = 32;    // Output compare register 0A
}

/* Simple LED blinker for debugging use */
void toggle_leds(void) {DDRB  = PORTB = ~DDRB & 0b11000000;}
