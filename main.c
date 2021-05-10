#include <stdbool.h>
#include <stdint.h>
#include "nrf_delay.h"
#include "bsp.h"
#include "nrf_atfifo.h"
#include "app_button.h" 
#include "app_timer.h"
#include "app_error.h"
#include "nrf_drv_clock.h"
#include "led_softblink.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#define BUTTON_DEBOUNCE_DELAY       50
#define TIMEOUT_DELAY               APP_TIMER_TICKS(10000)

/* Function pointer primitive */
typedef void (*state_func_t)(void);

struct _state
{
    	uint8_t id;
    	state_func_t Enter;
   	state_func_t Do;
    	state_func_t Exit;
    	uint32_t delay_ms;
};
typedef struct _state state_t;

enum _event 
{
    	b1_evt = 0,
    	b2_evt = 1,
    	b3_evt = 2,
    	timeout_evt = 3,
    	no_evt = 4
};
typedef enum _event event_t;

/* Define FIFO */
NRF_ATFIFO_DEF(event_fifo, event_t, 10);

/* Define Timer */
APP_TIMER_DEF(s3_timeout_timer);

static const uint8_t leds_cw_pattern[] = {0, 1, 3, 2};
static const uint8_t leds_ccw_pattern[] = {0, 2, 3, 1};

static const led_sb_init_params_t led_sb_init_param = {
	.active_high = LED_SB_INIT_PARAMS_ACTIVE_HIGH,
	.duty_cycle_max = LED_SB_INIT_PARAMS_DUTY_CYCLE_MAX,
	.duty_cycle_min = LED_SB_INIT_PARAMS_DUTY_CYCLE_MIN,
	.duty_cycle_step = 1,
	.off_time_ticks = APP_TIMER_TICKS(5000),
	.on_time_ticks = APP_TIMER_TICKS(5000),
	.leds_pin_bm = LED_SB_INIT_PARAMS_LEDS_PIN_BM(LEDS_MASK),
	.p_leds_port = LED_SB_INIT_PARAMS_LEDS_PORT
};

static void button_handler(uint8_t pin_num, uint8_t btn_action)
{
	event_t evt;

    	if (btn_action == APP_BUTTON_PUSH) {
        	switch(pin_num) {
            		case BUTTON_1:
                		evt = b1_evt;
                	break;

            		case BUTTON_2:
                		evt = b2_evt;
                	break;
            		
			case BUTTON_3:
                		evt = b3_evt;
                	break;
        	}

		nrf_atfifo_alloc_put(event_fifo, &evt, sizeof(event_t), NULL);
	}
}

static const app_button_cfg_t p_buttons[] = {
	{BUTTON_1, APP_BUTTON_ACTIVE_LOW, NRF_GPIO_PIN_PULLUP, button_handler},
    	{BUTTON_2, APP_BUTTON_ACTIVE_LOW, NRF_GPIO_PIN_PULLUP, button_handler},
    	{BUTTON_3, APP_BUTTON_ACTIVE_LOW, NRF_GPIO_PIN_PULLUP, button_handler}
};

static void timeout_handler(void * p_context)
{
	event_t evt = timeout_evt;

	nrf_atfifo_alloc_put(event_fifo, &evt, sizeof(event_t), NULL);
}

void init_board(void)
{
    	/* Initialize the low frequency clock used by APP_TIMER */
    	uint32_t err_code;

	err_code = nrf_drv_clock_init();
    	APP_ERROR_CHECK(err_code);

    	nrf_drv_clock_lfclk_request(NULL);

    	APP_ERROR_CHECK(NRF_LOG_INIT(NULL));
    	NRF_LOG_DEFAULT_BACKENDS_INIT();
    	NRF_LOG_INFO("Logging initialized.");

    	/* Initialize the event fifo */
	NRF_ATFIFO_INIT(event_fifo);

    	/* Initialize the timer module */ 
    	app_timer_init();
	app_timer_create(&s3_timeout_timer, APP_TIMER_MODE_SINGLE_SHOT, timeout_handler);

    	/* Initialize the LEDs */
    	bsp_board_init(BSP_INIT_LEDS);

    	/* Setup button interrupt handler */
	app_button_init(p_buttons, ARRAY_SIZE(p_buttons), BUTTON_DEBOUNCE_DELAY);
    	app_button_enable();
}

event_t get_event(void)
{
	event_t evt = no_evt;

	nrf_atfifo_get_free(event_fifo, &evt, sizeof(event_t), NULL);

    	return evt;
}

void do_state_0(void)
{
	static uint8_t offset;

	bsp_board_leds_off();

	bsp_board_led_on(leds_cw_pattern[offset++]);

	offset %= 4;
}

void do_state_1(void)
{
	static bool turn_on = true;

	turn_on ? bsp_board_leds_on() : bsp_board_leds_off();
	
	turn_on = !turn_on;
}

void do_state_2(void)
{
	static uint8_t offset;

	bsp_board_leds_off();

	bsp_board_led_on(leds_ccw_pattern[offset++]);

	offset %= 4;
}

void do_state_3(void)
{
	return;
}

void start_state_3(void)
{
	app_timer_start(s3_timeout_timer, TIMEOUT_DELAY, NULL);

	/* Initialize fading LED driver */ 
	led_softblink_init(&led_sb_init_param);

	led_softblink_start(LEDS_MASK);
}

void exit_state_3(void)
{
	led_softblink_stop();
}

const state_t state0 = {
    	0,
    	bsp_board_leds_off,
    	do_state_0,
    	bsp_board_leds_off,
    	200
};

const state_t state1 = {
    	1,
    	bsp_board_leds_off,
    	do_state_1,
    	bsp_board_leds_off,
    	200
};

const state_t state2 = {
    	2,
    	bsp_board_leds_off,
    	do_state_2,
    	bsp_board_leds_off,
    	100
};

const state_t state3 = {
    	3,
	start_state_3,
    	do_state_3,
    	exit_state_3,
    	200
};

const state_t state_table[4][5] = {
	{state2, state1, state3, state0, state0},
	{state0, state2, state3, state1, state1},
	{state1, state0, state3, state2, state2},
	{state3, state3, state3, state0, state3}
};

int main(void)
{
    	state_t current_state = state0;
    	event_t evt;

	NRF_LOG_INFO("In main");

	init_board();

    	while (true) {
        	current_state.Enter();

        	while (true) {
            		current_state.Do();
            		nrf_delay_ms(current_state.delay_ms);
            		evt = get_event();
			if (evt != no_evt)
				break;
            		NRF_LOG_FLUSH();
        	}
        	current_state.Exit();
		current_state = state_table[current_state.id][evt];
    	}
}
