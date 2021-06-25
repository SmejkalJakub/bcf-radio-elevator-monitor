#include <application.h>

#define BATTERY_UPDATE_INTERVAL   (60 * 60 * 1000)

#define UPDATE_NORMAL_INTERVAL             (10 * 1000)
#define BAROMETER_UPDATE_NORMAL_INTERVAL   (100)

#define BAROMETER_TAG_PUB_NO_CHANGE_INTEVAL (15 * 60 * 1000)
#define BAROMETER_TAG_PUB_VALUE_CHANGE 3.0f

#define ACCELEROMETER_UPDATE_NORMAL_INTERVAL (100)

#define RADIO_DELAY 1500

struct {
    event_param_t temperature;
    event_param_t humidity;
    event_param_t illuminance;
    event_param_t pressure;

} params;

twr_lis2dh12_t lis2dh12;
twr_lis2dh12_result_g_t result;

// LED instance
twr_led_t led;

// Thermometer instance
twr_tmp112_t tmp112;

// Button instance
twr_button_t button;
uint16_t button_event_count = 0;

float magnitude;

bool measuring = true;

void button_event_handler(twr_button_t *self, twr_button_event_t event, void *event_param)
{
    (void) self;
    (void) event_param;

    if (event == TWR_BUTTON_EVENT_PRESS)
    {
        if(measuring)
        {
            twr_led_set_mode(&led, TWR_LED_MODE_OFF);
            measuring = false;
        }
        else
        {
            twr_led_set_mode(&led, TWR_LED_MODE_ON);
            measuring = true;
        }
    }
}

void battery_event_handler(twr_module_battery_event_t event, void *event_param)
{
    (void) event_param;

    float voltage;

    if (event == TWR_MODULE_BATTERY_EVENT_UPDATE)
    {
        if (twr_module_battery_get_voltage(&voltage))
        {
            twr_radio_pub_battery(&voltage);
        }
    }
}


void climate_module_event_handler(twr_module_climate_event_t event, void *event_param)
{
    (void) event_param;

    if(!measuring)
    {
        float value;
        if (event == TWR_MODULE_CLIMATE_EVENT_UPDATE_BAROMETER)
        {
            if (twr_module_climate_get_pressure_pascal(&value))
            {
                if ((fabs(value - params.pressure.value) >= BAROMETER_TAG_PUB_VALUE_CHANGE) || (params.pressure.next_pub < twr_scheduler_get_spin_tick()))
                {
                    float meter;

                    if (!twr_module_climate_get_altitude_meter(&meter))
                    {
                        return;
                    }

                    twr_radio_pub_barometer(TWR_RADIO_PUB_CHANNEL_R1_I2C0_ADDRESS_DEFAULT, &value, &meter);
                    params.pressure.value = value;
                    params.pressure.next_pub = twr_scheduler_get_spin_tick() + BAROMETER_TAG_PUB_NO_CHANGE_INTEVAL;
                }
            }
        }
    }
}

// This function dispatches accelerometer events
void lis2dh12_event_handler(twr_lis2dh12_t *self, twr_lis2dh12_event_t event, void *event_param)
{
    if(measuring)
    {
        // Update event?
        if (event == TWR_LIS2DH12_EVENT_UPDATE)
        {
            // Successfully read accelerometer vectors?
            if (twr_lis2dh12_get_result_g(self, &result))
            {
                magnitude = pow(result.x_axis, 2) + pow(result.y_axis, 2) + pow(result.z_axis, 2);
                magnitude = sqrt(magnitude);
            }

            if(magnitude > 4)
            {
                static twr_tick_t radio_delay = 0;
                if (twr_tick_get() >= radio_delay)
                {
                    twr_led_pulse(&led, 100);
                    float value;
                    float meter;

                    if (twr_module_climate_get_pressure_pascal(&value))
                    {
                        if (!twr_module_climate_get_altitude_meter(&meter))
                        {
                            return;
                        }
                    }
                    twr_radio_pub_barometer(TWR_RADIO_PUB_CHANNEL_R1_I2C0_ADDRESS_DEFAULT, &value, &meter);
                }
            }
        }
    }
}

void application_init(void)
{
    // Initialize LED
    twr_led_init(&led, TWR_GPIO_LED, false, false);
    twr_led_set_mode(&led, TWR_LED_MODE_ON);

    // Initialize radio
    twr_radio_init(TWR_RADIO_MODE_NODE_SLEEPING);

    twr_lis2dh12_init(&lis2dh12, TWR_I2C_I2C0, 0x19);
    twr_lis2dh12_set_event_handler(&lis2dh12, lis2dh12_event_handler, NULL);
    twr_lis2dh12_set_resolution(&lis2dh12, TWR_LIS2DH12_RESOLUTION_8BIT);
    twr_lis2dh12_set_scale(&lis2dh12, TWR_LIS2DH12_SCALE_16G);
    twr_lis2dh12_set_update_interval(&lis2dh12, ACCELEROMETER_UPDATE_NORMAL_INTERVAL);

    // Initialize button
    twr_button_init(&button, TWR_GPIO_BUTTON, TWR_GPIO_PULL_DOWN, false);
    twr_button_set_event_handler(&button, button_event_handler, &button_event_count);

    // Initialize battery
    twr_module_battery_init();
    twr_module_battery_set_event_handler(battery_event_handler, NULL);
    twr_module_battery_set_update_interval(BATTERY_UPDATE_INTERVAL);

    // Initialize climate module
    twr_module_climate_init();
    twr_module_climate_set_event_handler(climate_module_event_handler, NULL);
    twr_module_climate_set_update_interval_barometer(BAROMETER_UPDATE_NORMAL_INTERVAL);
    twr_module_climate_measure_barometer();

    twr_radio_pairing_request("elevator-monitor", VERSION);
}
