#include <assert.h>
#include <stdint.h>
#include <stdio.h>

enum { EVENT_INIT, EVENT_DELAY, EVENT_COMMAND, EVENT_DATA, EVENT_BAUD };
typedef struct { uint32_t kind, value, first; } event_t;
static event_t events[512];
static uint32_t event_count;

static void record(uint32_t kind, uint32_t value, uint32_t first)
{
    assert(event_count < sizeof events / sizeof events[0]);
    events[event_count++] = (event_t){kind, value, first};
}
static void fm1_lcd_hw_init(void) { record(EVENT_INIT, 0u, 0u); }
static void fm1_delay_ms(uint32_t ms) { record(EVENT_DELAY, ms, 0u); }
static void fm1_lcd_wait(void) { }
static void fm1_lcd_deselect(void) { }
static void fm1_lcd_send_cmd(uint8_t command)
{ record(EVENT_COMMAND, command, 0u); }
static void fm1_lcd_send_data(const void *data, uint32_t length)
{ record(EVENT_DATA, length, ((const uint8_t *)data)[0]); }
static void fm1_lcd_baud(uint32_t baud) { record(EVENT_BAUD, baud, 0u); }

#include "../firmware/src/lcd.c"

int main(void)
{
    static const event_t prefix[] = {
        {EVENT_INIT, 0u, 0u}, {EVENT_DELAY, 80u, 0u},
        {EVENT_COMMAND, 0x01u, 0u}, {EVENT_DELAY, 150u, 0u},
        {EVENT_COMMAND, 0x11u, 0u}, {EVENT_DELAY, 120u, 0u},
        {EVENT_COMMAND, 0x3au, 0u}, {EVENT_DATA, 1u, 0x55u},
        {EVENT_COMMAND, 0x36u, 0u}, {EVENT_DATA, 1u, 0u},
        {EVENT_COMMAND, 0x21u, 0u}, {EVENT_COMMAND, 0x13u, 0u},
        {EVENT_BAUD, 4u, 0u}
    };
    uint32_t i, delays = 0u;
    lcd_init();
    assert(event_count > sizeof prefix / sizeof prefix[0]);
    for (i = 0u; i < sizeof prefix / sizeof prefix[0]; ++i) {
        assert(events[i].kind == prefix[i].kind);
        assert(events[i].value == prefix[i].value);
        assert(events[i].first == prefix[i].first);
    }
    for (i = 0u; i < event_count; ++i)
        if (events[i].kind == EVENT_DELAY) ++delays;
    assert(delays == 3u);
    assert(events[event_count - 1u].kind == EVENT_COMMAND &&
           events[event_count - 1u].value == 0x29u);
    puts("LCD startup command order and 80/150/120 ms waits: passed");
    return 0;
}
