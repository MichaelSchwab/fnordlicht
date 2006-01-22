/*
 vim:fdm=marker ts=4 et ai
 */

/*
 *         fnordlicht firmware next generation
 *
 *    for additional information please
 *    see http://koeln.ccc.de/prozesse/running/fnordlicht
 *
 * (c) by Alexander Neumann <alexander@bumpern.de>
 *     Lars Noschinski <lars@public.noschinski.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * For more information on the GPL, please go to:
 * http://www.gnu.org/copyleft/gpl.html
 */



/* define the cpu speed when using the fuse bits set by the
 * makefile (and documented in fuses.txt), using an external
 * crystal oscillator at 16MHz */
#ifndef F_CPU
#define F_CPU 16000000UL
#endif

/* debug defines */
#ifndef DEBUG
#define DEBUG 0
#endif

/* includes */
#include <avr/io.h>
#include <stdint.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>

/* macros for extracting low and high byte */
#define LOW(x) (uint8_t)(0x00ff & x)
#define HIGH(x) (uint8_t)((0xff00 & x) >> 8)

/* define uart baud rate (19200) and mode (8N1) */
#define UART_BAUDRATE 19200
#define UART_UCSRC _BV(URSEL) | _BV(UCSZ0) | _BV(UCSZ1)
#define UART_UBRR (F_CPU/(UART_BAUDRATE * 16L)-1)

#define PWM_CHANNELS 3

/* possible pwm interrupts in a pwm cycle */
#define PWM_MAX_TIMESLOTS (PWM_CHANNELS+1)

#define FIFO_SIZE 32


/* structs */

/* contains all the data for one color channel */
struct Channel_t { /* {{{ */
    union {
        /* for adding fade-speed to brightness, and save the remainder */
        uint16_t brightness_and_remainder;

        /* for accessing brightness directly */
        struct {
            uint8_t brightness;
            uint8_t remainder;
        };
    };

    /* desired brightness for this channel */
    uint8_t target_brightness;

    /* fade speed, the msb is added directly to brightness,
     * the lsb is added to the remainder until an overflow happens */
    uint16_t speed;

    /* flags for this channel, eg channel target reached */
    uint8_t flags;

    /* output mask for switching on the leds for this channel */
    uint8_t mask;
};

/* }}} */

/* encapsulates all pwm data including timeslot and output mask array */
struct Timeslots_t { /* {{{ */
    struct {
        uint8_t mask;
        uint16_t top;
    } slots[PWM_MAX_TIMESLOTS];

    uint8_t index;  /* current timeslot intex in the 'slots' array */
    uint8_t count;  /* number of entries in slots */
    uint8_t next_bitmask; /* next output bitmask, or signal for start or middle of pwm cycle */
    uint8_t initial_bitmask; /* output mask set at beginning */
};

/* }}} */

/* global flag(=bit) structure */
struct Flags_t { /* {{{ */
    uint8_t new_cycle:1;    /* set by pwm interrupt after burst, signals the beginning of a new pwm cycle to the main loop. */
    uint8_t last_pulse:1;   /* set by pwm interrupt after last interrupt in the current cycle, signals the main loop to rebuild the pwm timslot table */
};

/* }}} */


/* timer top values for 256 brightness levels (stored in flash) {{{ */
const uint16_t timeslot_table[] PROGMEM = {               \
      2,     8,    18,    31,    49,    71,    96,   126, \
    159,   197,   238,   283,   333,   386,   443,   504, \
    569,   638,   711,   787,   868,   953,  1041,  1134, \
   1230,  1331,  1435,  1543,  1655,  1772,  1892,  2016, \
   2144,  2276,  2411,  2551,  2695,  2842,  2994,  3150, \
   3309,  3472,  3640,  3811,  3986,  4165,  4348,  4535, \
   4726,  4921,  5120,  5323,  5529,  5740,  5955,  6173, \
   6396,  6622,  6852,  7087,  7325,  7567,  7813,  8063, \
   8317,  8575,  8836,  9102,  9372,  9646,  9923, 10205, \
  10490, 10779, 11073, 11370, 11671, 11976, 12285, 12598, \
  12915, 13236, 13561, 13890, 14222, 14559, 14899, 15244, \
  15592, 15945, 16301, 16661, 17025, 17393, 17765, 18141, \
  18521, 18905, 19293, 19685, 20080, 20480, 20884, 21291, \
  21702, 22118, 22537, 22960, 23387, 23819, 24254, 24693, \
  25135, 25582, 26033, 26488, 26946, 27409, 27876, 28346, \
  28820, 29299, 29781, 30267, 30757, 31251, 31750, 32251, \
  32757, 33267, 33781, 34299, 34820, 35346, 35875, 36409, \
  36946, 37488, 38033, 38582, 39135, 39692, 40253, 40818, \
  41387, 41960, 42537, 43117, 43702, 44291, 44883, 45480, \
  46080, 46684, 47293, 47905, 48521, 49141, 49765, 50393, \
  51025, 51661, 52300, 52944, 53592, 54243, 54899, 55558, \
  56222, 56889, 57560, 58235, 58914, 59598, 60285, 60975, \
  61670, 62369, 63072, 63779,   489,  1204,  1922,  2645, \
   3371,  4101,  4836,  5574,  6316,  7062,  7812,  8566, \
   9324, 10085, 10851, 11621, 12394, 13172, 13954, 14739, \
  15528, 16322, 17119, 17920, 18725, 19534, 20347, 21164, \
  21985, 22810, 23638, 24471, 25308, 26148, 26993, 27841, \
  28693, 29550, 30410, 31274, 32142, 33014, 33890, 34770, \
  35654, 36542, 37433, 38329, 39229, 40132, 41040, 41951, \
  42866, 43786, 44709, 45636, 46567, 47502, 48441, 49384, \
  50331, 51282, 52236, 53195, 54158, 55124, 56095, 57069, \
  58047, 59030, 60016, 61006, 62000, 62998 };

/* }}} */

/* global variables */
volatile struct Flags_t flags = {0, 0};

struct Timeslots_t pwm;       /* pwm timeslots (the top values and masks for the timer1 interrupt) */
struct Channel_t channels[3]; /* current channel records */

/* prototypes */
static inline void init_uart(void);
static inline void init_output(void);
static inline void init_timer1(void);
static inline void init_pwm(void);
void update_pwm_timeslots(void);
inline void do_fading(void);


/** init the hardware uart */
void init_uart(void) { /* {{{ */
    /* set baud rate */
    UBRRH = (uint8_t)(UART_UBRR >> 8);  /* high byte */
    UBRRL = (uint8_t)UART_UBRR;         /* low byte */

    /* set mode */
    UCSRC = UART_UCSRC;

    /* enable transmitter, receiver and receiver complete interrupt */
    UCSRB = _BV(TXEN) | _BV(RXEN) | _BV(RXCIE);

    /* send boot message */
    UDR = 'B';

    /* wait until boot message has been sent over the wire */
    while (!(UCSRA & (1<<UDRE)));
}
/* }}} */

/** init output channels */
void init_output(void) { /* {{{ */
    /* set all channels high -> leds off */
    PORTB = _BV(PB0) | _BV(PB1) | _BV(PB2);
    /* configure PB0-PB2 as outputs */
    DDRB = _BV(PB0) | _BV(PB1) | _BV(PB2);
}

/* }}} */

/** init timer 1 */
void init_timer1(void) { /* {{{ */
    /* no prescaler, CTC mode */
    TCCR1B = _BV(CS10) | _BV(WGM12);
    //TCCR1B = _BV(CS12) | _BV(CS10) | _BV(WGM12);
    TCCR1A = 0;

    /* enable timer1 overflow (=output compare 1a)
     * and output compare 1b interrupt */
    TIMSK |= _BV(OCIE1A) | _BV(OCIE1B);

    /* set TOP for CTC mode */
    OCR1A = 64000;

    /* load initial delay, trigger an overflow */
    OCR1B = 65000;
}

/* }}} */

/** init pwm */
void init_pwm(void) { /* {{{ */
    uint8_t i;

    for (i=0; i<3; i++) {
        channels[i].brightness = 0;
        channels[i].target_brightness = 0;
        channels[i].speed = 0x0100;
        channels[i].flags = 0;
        channels[i].remainder = 0;
        channels[i].mask = _BV(i);
    }

    //channels[0].brightness = 10;
    //channels[1].brightness = 12;
    //channels[2].brightness = 11;
    channels[0].brightness = 8;
    channels[1].brightness = 14;
    channels[2].brightness = 15;
    channels[0].target_brightness = channels[0].brightness;
    channels[1].target_brightness = channels[1].brightness;
    channels[2].target_brightness = channels[2].brightness;

    update_pwm_timeslots();
}

/* }}} */

/** update pwm timeslot table */
void update_pwm_timeslots(void) { /* {{{ */
    uint8_t sorted[PWM_CHANNELS] = { 0, 1, 2 };
    uint8_t i, j;
    uint8_t mask = 0;
    uint8_t last_brightness = 0;

    /* sort channels according to the current brightness */
    for (i=0; i<PWM_CHANNELS; i++) {
        for (j=i+1; j<PWM_CHANNELS; j++) {
            if (channels[sorted[j]].brightness < channels[sorted[i]].brightness) {
                uint8_t temp;

                temp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = temp;
            }
        }
    }

    /* timeslot index */
    j = 0;

    /* calculate timeslots and masks */
    for (i=0; i < PWM_CHANNELS; i++) {

        /* check if a timeslot is needed */
        if (channels[sorted[i]].brightness > 0 && channels[sorted[i]].brightness < 255) {
            /* if the next timeslot will be after the middle of the pwm cycle, insert the middle interrupt */
            if (last_brightness < 181 && channels[sorted[i]].brightness >= 181) {
                /* middle interrupt: top 65k and mask 0xff */
                pwm.slots[j].top = 65000;
                pwm.slots[j].mask = 0xff;
                j++;
            }

            /* insert new timeslot if brightness is new */
            if (channels[sorted[i]].brightness > last_brightness) {

                /* remember mask and brightness for next timeslot */
                mask |= channels[sorted[i]].mask;
                last_brightness = channels[sorted[i]].brightness;

                /* allocate new timeslot */
                pwm.slots[j].top = pgm_read_word(&timeslot_table[channels[sorted[i]].brightness - 1 ]);
                pwm.slots[j].mask = mask;
                j++;
            } else {
            /* change mask of last-inserted timeslot */
                mask |= channels[sorted[i]].mask;
                pwm.slots[j-1].mask = mask;
            }
        }
    }

    /* if all interrupts happen before the middle interrupt, insert it here */
    if (last_brightness < 181) {
        /* middle interrupt: top 65k and mask 0xff */
        pwm.slots[j].top = 65000;
        pwm.slots[j].mask = 0xff;
        j++;
    }

    /* reset pwm structure */
    pwm.index = 0;
    pwm.count = j;

    /* next interrupt is the first in a cycle, so set the bitmask to 0 */
    pwm.next_bitmask = 0;

    /* calculate initial bitmask */
    pwm.initial_bitmask = 0xff;
    for (i=0; i < PWM_CHANNELS; i++)
        if (channels[i].brightness > 0)
            pwm.initial_bitmask &= ~channels[i].mask;
}

/* }}} */

/** fade any channels not already at their target brightness */
void do_fading(void) { /* {{{ */
    uint8_t i;
    uint16_t value;

    /* iterate over the channels */
    for (i=0; i<PWM_CHANNELS; i++) {

        /* increase brightness */
        if (channels[i].brightness < channels[i].target_brightness) {
            /* calculate new brightness value, high byte is brightness, low byte is remainder */
            value = (uint16_t)channels[i].remainder + (uint16_t)(channels[i].brightness << 8) + channels[i].speed;

            /* if new brightness is lower than before or brightness is higher than the target, just set the target brightness */
            if (HIGH(value) < channels[i].brightness || HIGH(value) > channels[i].target_brightness) {
                channels[i].brightness = channels[i].target_brightness;
            } else {
                /* set new brightness */
                channels[i].brightness = HIGH(value);

                /* save remainder */
                channels[i].remainder = LOW(value);
            }

        /* or decrease brightness */
        } else if (channels[i].brightness > channels[i].target_brightness) {
            /* calculate new brightness value, high byte is brightness, low byte is remainder */
            value = (uint16_t)channels[i].remainder + (uint16_t)(channels[i].brightness << 8) - channels[i].speed;

            /* if new brightness is higher than before or brightness is lower than the target, just set the target brightness */
            if (HIGH(value) > channels[i].brightness || HIGH(value) < channels[i].target_brightness) {
                channels[i].brightness = channels[i].target_brightness;
            } else {
                /* set new brightness */
                channels[i].brightness = HIGH(value);

                /* save remainder */
                channels[i].remainder = LOW(value);
            }
        }
    }
}

/* }}} */

/** prepare next timeslot */
static inline void prepare_next_timeslot(void) { /* {{{ */
    /* check if this is the last interrupt */
    if (pwm.index >= pwm.count) {
        /* select first timeslot and trigger timeslot rebuild */
        pwm.index = 0;
        flags.last_pulse = 1;
        OCR1B = 65000;
    } else {
        /* load new top and bitmask */
        OCR1B = pwm.slots[pwm.index].top;
        pwm.next_bitmask = pwm.slots[pwm.index].mask;

        /* select next timeslot */
        pwm.index++;
    }

    /* clear compare interrupts which might have in the meantime happened */
    //TIFR |= _BV(OCF1B);
}

/* }}} */

/** timer1 overflow (=output compare a) interrupt */
SIGNAL(SIG_OUTPUT_COMPARE1A) { /* {{{ */
    /* decide if this interrupt is the beginning of a pwm cycle */
    if (pwm.next_bitmask == 0) {
        /* output initial values */
        PORTB = pwm.initial_bitmask;

        /* signal new cycle to main procedure */
        flags.new_cycle = 1;

        /* if next timeslot would happen too fast or has already happened, just spinlock */
        while (TCNT1 + 500 > pwm.slots[pwm.index].top)
        {
            /* spin until timer interrupt is near enough */
            while (pwm.slots[pwm.index].top > TCNT1) {
                asm volatile ("nop");
            }

            /* output value */
            PORTB |= pwm.slots[pwm.index].mask;

            /* we can safely increment index here, since we are in the first timeslot and there
             * will always be at least one timeslot after this (middle) */
            pwm.index++;
        }

    }

    /* prepare the next timeslot */
    prepare_next_timeslot();
}
/* }}} */

/** timer1 output compare b interrupt */
SIGNAL(SIG_OUTPUT_COMPARE1B) { /* {{{ */
    /* normal interrupt, output pre-calculated bitmask */
    PORTB |= pwm.next_bitmask;

    /* and calculate the next timeslot */
    prepare_next_timeslot();
}
/* }}} */

/** uart receive interrupt */
SIGNAL(SIG_UART_RECV) { /* {{{ */
    uint8_t data = UDR;

    switch (data) {
        case '1':
            channels[0].target_brightness-=1;
            break;
        case '4':
            channels[0].target_brightness+=1;
            break;
        case '2':
            channels[1].target_brightness-=1;
            break;
        case '5':
            channels[1].target_brightness+=1;
            break;
        case '3':
            channels[2].target_brightness-=1;
            break;
        case '6':
            channels[2].target_brightness+=1;
            break;
        case '0':
            channels[0].target_brightness=0;
            channels[1].target_brightness=0;
            channels[2].target_brightness=0;
            break;
        case '=':
            channels[0].target_brightness=channels[0].brightness;
            channels[1].target_brightness=channels[1].brightness;
            channels[2].target_brightness=channels[2].brightness;
            break;
        case '>':
            channels[0].speed >>= 1;
            channels[1].speed >>= 1;
            channels[2].speed >>= 1;
            break;
        case '<':
            channels[0].speed <<= 1;
            channels[1].speed <<= 1;
            channels[2].speed <<= 1;
            break;
        case 's':
            UDR = HIGH(channels[0].speed);
            while (!(UCSRA & (1<<UDRE)));
            UDR = LOW(channels[0].speed);
            while (!(UCSRA & (1<<UDRE)));
            break;
        case 'b':
            UDR = channels[0].brightness;
            while (!(UCSRA & (1<<UDRE)));
            UDR = channels[1].brightness;
            while (!(UCSRA & (1<<UDRE)));
            UDR = channels[2].brightness;
            while (!(UCSRA & (1<<UDRE)));
            break;
    }
}
/* }}} */

/** main function
 */
int main(void) {
    init_output();
    init_uart();
    init_timer1();
    init_pwm();

    /* enable interrupts globally */
    sei();

    while (1) {
        if (flags.new_cycle) {
            flags.new_cycle = 0;

            do_fading();

            /*
            UDR = channels[0].brightness;
            while (!(UCSRA & (1<<UDRE)));
            UDR = channels[1].brightness;
            while (!(UCSRA & (1<<UDRE)));
            */

        }

        if (flags.last_pulse) {
            flags.last_pulse = 0;

            update_pwm_timeslots();

            UDR = pwm.count;
            while (!(UCSRA & (1<<UDRE)));
        }
    }
}


