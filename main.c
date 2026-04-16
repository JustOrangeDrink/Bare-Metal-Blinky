#include <inttypes.h>
#include <stdbool.h>

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
	gpio->MODER &= ~(3UL << (pin_n * 2)); // reset target pin
	gpio->MODER |= (mode << (pin_n * 2)); // set target pin to mode
}

void gpio_write(uint16_t pin, bool val) {
	struct gpio* gpio = GPIO(PINBANK(pin));
	uint16_t pin_n = PINNO(pin);
	gpio->BSRR = (BIT(pin_n) << (val ? 0 : 16));
}

void spin(volatile uint32_t count) {
	while (count--) (void) 0;
}

bool timer_expired(uint32_t *t, uint32_t prd, uint32_t now) {
	if (now + prd < *t) *t = 0;
	if (*t == 0) *t = now + prd;
	if (*t > now) return false; // "now" variable will increment as interrupts happen, so it is supossed to "overgrow" *t and ignore this condition
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
	syst_init(16000000 / 1000);

	uint16_t LED2 = PIN('A', 5);
	struct blink_state LED2_state = {0, 700, 700, 0, HEARTBEAT, 0};
	uint16_t BUTTON = PIN('C', 13); // user button

	RCC->AHB1ENR |= BIT(PINBANK(LED2)); // enable GPIOA clock
	RCC->AHB1ENR |= BIT(PINBANK(BUTTON)); // enable GPIOC clock
										  //
	gpio_set_mode(LED2, GPIO_MODE_OUTPUT);
	gpio_set_mode(BUTTON, GPIO_MODE_INPUT);

	bool prev = 0; // previous state of button pressing
	for (;;) {
		bool now = button_is_pressed(BUTTON);
		if (now && !prev) LED2_state.mode = (LED2_state.mode + 1) % 3; // if "now" is 1 and "prev" is 0 - this is first change in series of button events
		prev = now; // our current state is considered now previous
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
