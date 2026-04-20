#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

#define BIT(x) (1UL << (x))
#define PIN(bank, pin) ( (((bank) - 'A') << 8) | (pin) )
#define PINBANK(pin) ( pin >> 8 )
#define PINNO(pin) (255 & pin)

struct gpio {
	volatile uint32_t MODER, OTYPER, OSPEEDR, PUPDR, IDR, ODR, BSRR, LCKR, AFRL, AFRH;
};
#define GPIO(bank) ((struct gpio*) (0x40020000 + 0x400 * (bank)))

struct rcc {
	volatile uint32_t CR, PLLCFGR, CFGR, CIR, AHB1_RSTR, AHB2_RSTR, RESERVED0[2], APB1RSTR, APB2RSTR, RESERVED1[2], AHB1ENR, AHB2ENR, RESERVED2[2], APB1_ENR,
			 APB2_ENR, RESERVED3[2], AHB1LPENR, AHB2LPENR, RESERVED4[2], APB1LPENR, APB2LPENR, RESERVED5[2], BDCR, CSR, RESERVED6[2], SSCGR, PLLI2SCFGR,
			 RESERVED7, DCKCFGR;
};
#define RCC ((struct rcc*) 0x40023800)

struct usart {
	uint32_t SR, DR, BRR, CR1, CR2, CR3, GTPR;
};
#define USART1 ((struct usart*) 0x40011000)
#define USART2 ((struct usart*) 0x40004400)
#define USART6 ((struct usart*) 0x40011400)

struct syst {
	uint32_t CSR, RVR, CVR, CALIB;
};
#define SYST ( (struct syst *) 0xE000E010 )
static inline void syst_init(uint32_t ticks) {
	if ( (ticks - 1) > 0xffffff ) return;
	SYST->CSR |= BIT(0) | BIT(1) | BIT(2);
	SYST->RVR = ticks - 1;
	SYST->CVR = 0;
}

volatile uint32_t s_ticks = 0;
void SysTick_Handler(void) {
	s_ticks++;
}

enum {GPIO_MODE_INPUT, GPIO_MODE_OUTPUT, GPIO_MODE_ALTFN, GPIO_MODE_ANLG};
void gpio_set_mode(uint16_t pin, uint8_t mode) {
	struct gpio* gpio = GPIO(PINBANK(pin));
	uint8_t pin_n = PINNO(pin);
	RCC->AHB1ENR |= BIT(PINBANK(pin)); // enable GPIO clock
	gpio->MODER &= ~(3UL << (pin_n * 2)); // reset target pin
	gpio->MODER |= (mode << (pin_n * 2)); // set target pin to mode
}

void gpio_set_af(uint16_t pin, uint8_t af_n) {
	struct gpio *gpio = GPIO(PINBANK(pin));
	uint8_t n = PINNO(pin);
	uint8_t shift = (n % 8) * 4;
	if (n > 7) {
		gpio->AFRH &= ~(15UL << shift);
		gpio->AFRH |= ((af_n & 15) << shift);
	}
	else {
		gpio->AFRL &= ~(15UL << shift);
		gpio->AFRL |= ((af_n & 15) << shift);
	}
}

void gpio_set_pull(uint16_t pin, bool dir) {
	struct gpio *gpio = GPIO(PINBANK(pin));
	uint8_t n = PINNO(pin);
	gpio->PUPDR &= ~(3UL << (n * 2));
	gpio->PUPDR |= (dir << (n * 2));
}

#define FREQ 16000000
void usart_init(struct usart *usart, unsigned long baud) {
	uint8_t af_n = 7;
	uint16_t rx = 0, tx = 0;
	if (usart == USART2) {
		RCC->APB1_ENR |= BIT(17);
		tx = PIN('A', 2), rx = PIN('A', 3);
	}

	gpio_set_mode(tx, GPIO_MODE_ALTFN);
	gpio_set_af(tx, af_n);
	gpio_set_mode(rx, GPIO_MODE_ALTFN);
	gpio_set_af(rx, af_n);
	gpio_set_pull(rx, 1);

	usart->CR1 = 0;
	usart->BRR = FREQ / baud;
	usart->CR1 = BIT(13) | BIT(3) | BIT(2);
}

static inline int usart_read_ready(struct usart *usart) {
	return usart->SR & BIT(5);
}

static inline uint8_t usart_read_byte(struct usart *usart) {
	return (uint8_t)usart->DR;
}

static inline void usart_write_byte(struct usart *usart, uint8_t byte) {
	usart->DR = byte;
	while ( !(usart->SR & BIT(7)) );
}

static inline void usart_write_buff(struct usart *usart, char *buff, size_t len) {
	while (len--) usart_write_byte(usart, (uint8_t)*buff++);
}

void gpio_write(uint16_t pin, bool val) {
	struct gpio* gpio = GPIO(PINBANK(pin));
	uint16_t pin_n = PINNO(pin);
	gpio->BSRR = (BIT(pin_n) << (val ? 0 : 16));
}

bool timer_expired(uint32_t *t, uint32_t prd, uint32_t now) {
	if (now + prd < *t) *t = 0;
	if (*t == 0) *t = now + prd;
	if (*t > now) return false; // "now" variable will increment as interrupts happen, so it is supossed to "overgrow" *t and ignore this condition at some point
	*t = (now - *t > prd) ? (now + prd) : (*t + prd);
	return true;
}

bool button_is_pressed(uint16_t button) {
	struct gpio *gpio = GPIO(PINBANK(button));
	uint8_t n = PINNO(button);
	if (!(gpio->IDR & BIT(n))) {
		return true;
	} else return false;
}

struct blink_state {
	uint32_t last;
	uint32_t default_period;
	uint32_t period;
	bool state;
	uint8_t mode;
	uint8_t beat_cycle;
};

enum {DEFAULT, HEARTBEAT, LONG_PULSE};
void blinker_task(uint16_t led, struct blink_state *led_state) {
	if (!((s_ticks - led_state->last) >= led_state->period)) return;
	led_state->last = s_ticks;

	switch (led_state->mode) {
	case DEFAULT:
		led_state->beat_cycle = 0; // clear heartbeat cycles so that when we go back to heartbeat it started from beginning
		led_state->state ^= 1;
		led_state->period = led_state->default_period; // if our period was changed by other modes we switch it back to default
		break;
	
	case HEARTBEAT: // dun-dun...dun-dun...dun-dun... mode
		if (led_state->beat_cycle == 0 || led_state->beat_cycle == 2) { // "beat" cycles
			led_state->state = 1;
			led_state->period = 200;
		}
		if (led_state->beat_cycle == 1) { // quick silence between beats
			led_state->state = 0;
			led_state->period = 100;
		}
		if (led_state->beat_cycle == 3) { // final silence
			led_state->state = 0;
			led_state->period = led_state->default_period;
		}
		led_state->beat_cycle = (led_state->beat_cycle + 1) % 4; // cycles through beat count, if equal to 4 - wrap to zero
		break;
	
	case LONG_PULSE:
		led_state->state ^= 1;
		if (led_state->state == 1) led_state->period = led_state->default_period * 2;
		else led_state->period = led_state->default_period / 2;
		break;
	}

	gpio_write(led, led_state->state); // turn led on/off
}

int main() {
	syst_init(FREQ / 1000);
	usart_init(USART2, 115200);

	uint16_t LED2 = PIN('A', 5);
	struct blink_state LED2_state = {0, 700, 700, 0, DEFAULT, 0};
	uint16_t BUTTON = PIN('C', 13); // user button

	gpio_set_mode(LED2, GPIO_MODE_OUTPUT);
	gpio_set_mode(BUTTON, GPIO_MODE_INPUT);

	bool prev = 0; // previous state of button pressing
	for (;;) {
		bool now = button_is_pressed(BUTTON);
		if (now && !prev) {
			// uncomment this to change button from USART write to LED mode change
			// LED2_state.mode = (LED2_state.mode + 1) % 3; // if "now" is 1 and "prev" is 0 - this is first change in series of button events
			usart_write_buff(USART2, "Hi! :3\r\n", 8);
		}
		prev = now; // our current state

		if (usart_read_ready(USART2)) {
			uint8_t byte = usart_read_byte(USART2);
			LED2_state.mode = byte - '0';
			usart_write_byte(USART2, byte); // echo of input
		}

		blinker_task(LED2, &LED2_state);
	}

	return 0;
}

__attribute__((naked, noreturn))
void _reset(void) {
	extern uint32_t _sdata, _edata, _sidata, _sbss, _ebss;

	for (uint32_t *dst = &_sbss; dst < &_ebss;) *dst++ = 0;
	for (uint32_t *dst = &_sdata, *src = &_sidata; dst < &_edata;) *dst++ = *src++;

	main();
	for(;;) (void) 0;
}

extern uint32_t _estack;

__attribute__((section(".vectors")))
uint32_t* vector_table[62+16] = {
	&_estack, (uint32_t*) _reset, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, (uint32_t*) SysTick_Handler
};
