#include <stdio.h>
#include <strings.h>
#include <ctype.h>

#include "mgos.h"
#include "mgos_ro_vars.h"
#include "mgos_app.h"
#include "mgos_system.h"
#include "mgos_timers.h"

#include <mgos_neopixel.h>
#include <frozen.h>

#include "mgos_mqtt.h"

struct pixel
{
    int r;
    int g;
    int b;
    int x; // blink/fade the led if set
};
static struct pixel *pixels;           // My array of pixels.
static struct mgos_neopixel *my_strip; // Low-level led strip.

static void turn_off_led(void *param)
{
    mgos_gpio_write(mgos_sys_config_get_pins_statusLed(), 0);
}

void blink_led()
{
    mgos_gpio_write(mgos_sys_config_get_pins_statusLed(), 1);
    mgos_set_timer(100, 0, turn_off_led, NULL);
}

void set_pixel(int number, char color, int value)
{
    switch (color)
    {
    case 'r':
        pixels[number].r = value;
        break;
    case 'g':
        pixels[number].g = value;
        break;
    case 'b':
        pixels[number].b = value;
        break;
    case 'x':
        pixels[number].x = value;
    default:
        LOG(LL_ERROR, ("Internal brain damage - unknown color '%c'", color));
    }
}

static void j_cb(void *callback_data,
                 const char *name, size_t name_len,
                 const char *path, const struct json_token *token)
{
    if (token->type == JSON_TYPE_NUMBER)
    {
        int pixelid = -1;
        char color;
        int value;
        // this might accept some whitespace here and there. Unix.
        int ret = sscanf(path, ".leds.%i.%c", &pixelid, &color);
        //color = tolower(color);
        // Only proceed if we got two values from sscanf and we're talking about a known color.
        if (ret == 2 && (color == 'r' || color == 'g' || color == 'b'))
        {
            value = atoi(token->ptr);
            // This is where the magix happens:
            LOG(LL_INFO, ("Setting LED %d (%c): %d", pixelid, color, value));
            set_pixel(pixelid, color, value);
        }
    }
}

static void decay()
{
    int factor = mgos_sys_config_get_leds_decay();
    for (int i = 0; i < mgos_sys_config_get_leds_number(); i++)
    {
        pixels[i].r = pixels[i].r * factor / 100;
        pixels[i].g = pixels[i].g * factor / 100;
        pixels[i].b = pixels[i].b * factor / 100;
    }
}

static void show()
{

    for (int i = 0; i < mgos_sys_config_get_leds_number(); i++)
    {
        LOG(LL_VERBOSE_DEBUG, ("Syncing pixel %d (%d:%d:%d)", i,
                               pixels[i].r, pixels[i].g, pixels[i].b));
        mgos_neopixel_set(my_strip, i,
                          pixels[i].r,
                          pixels[i].g,
                          pixels[i].b);
    }
    LOG(LL_VERBOSE_DEBUG, ("Showing new pixels (mgos_neopixel_show)"));
    mgos_neopixel_show(my_strip);
}

static void show_and_decay()
{
    show();
    decay();
}

/* 
   MQTT functions

*/

static void hsv_stream_handler(struct mg_connection *nc, const char *topic, int topic_len, const char *msg, int msg_len, void *ud)
{
    LOG(LL_INFO, ("HSV Hanlder got message topic '%.*s' message: '%.*s'", topic_len, topic, msg_len, msg));
    blink_led();
}

static void rgb_stream_handler(struct mg_connection *nc, const char *topic, int topic_len, const char *msg, int msg_len, void *ud)
{
    LOG(LL_INFO, ("RGB handler got message topic '%.*s' message: '%.*s'", topic_len, topic, msg_len, msg));
    blink_led();
    int ret = json_walk(msg, msg_len, j_cb, NULL);
    LOG(LL_INFO, ("Scanned delta and found %d entries", ret));
}

/* static void sub

    connection, fmt, ..

*/
static void setup_mqtt()
{
    char rgbtopic[] = "my/rgb-stream";
    char hsvtopic[] = "my/hvs-stream";

    mgos_mqtt_sub(rgbtopic, rgb_stream_handler, NULL);
    LOG(LL_INFO, ("Subscribed to '%s'", rgbtopic));
    mgos_mqtt_sub(hsvtopic, rgb_stream_handler, NULL);
    LOG(LL_INFO, ("Subscribed to '%s'", hsvtopic));
}

static void setup_debug()
{

    mgos_gpio_set_mode(0, MGOS_GPIO_MODE_INPUT);
    LOG(LL_INFO, ("Enabled show() on button: %d",
                  mgos_gpio_set_button_handler(0, MGOS_GPIO_PULL_UP, MGOS_GPIO_INT_EDGE_NEG, 50, show, NULL)));

    /*
    mgos_gpio_set_mode(0, MGOS_GPIO_MODE_INPUT);
    mgos_gpio_set_int_handler(0, MGOS_GPIO_INT_EDGE_NEG, show, NULL);
    mgos_gpio_enable_int(0).
*/
}

enum mgos_app_init_result mgos_app_init(void)
{
    blink_led();
    LOG(LL_INFO, ("My device ID is: %s", mgos_sys_config_get_device_id())); // Get config param
    LOG(LL_INFO, ("Initializing. Using pin %i for status.", mgos_sys_config_get_pins_statusLed()));
    mgos_gpio_set_mode(mgos_sys_config_get_pins_statusLed(), MGOS_GPIO_MODE_OUTPUT);
    LOG(LL_INFO, ("LED strip on PIN %i", mgos_sys_config_get_pins_ledStrip()));

    pixels = (struct pixel *)calloc(mgos_sys_config_get_leds_number(), sizeof(struct pixel)); // Allocate pixel array
    my_strip = mgos_neopixel_create(mgos_sys_config_get_pins_ledStrip(), mgos_sys_config_get_leds_number(), MGOS_NEOPIXEL_ORDER_GRB);

    setup_mqtt();
    setup_debug();
    mgos_set_timer(mgos_sys_config_get_leds_decaydelay(),
                   MGOS_TIMER_REPEAT, show_and_decay, NULL);

    LOG(LL_INFO, ("Init hopefully successful"));
    return MGOS_APP_INIT_SUCCESS;
}
