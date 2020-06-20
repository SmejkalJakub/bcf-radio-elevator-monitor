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

bc_lis2dh12_t lis2dh12;
bc_lis2dh12_result_g_t result;

// LED instance
bc_led_t led;

// Thermometer instance
bc_tmp112_t tmp112;

// Button instance
bc_button_t button;
uint16_t button_event_count = 0;

float magnitude;

bool measuring = true;

void button_event_handler(bc_button_t *self, bc_button_event_t event, void *event_param)
{
    (void) self;
    (void) event_param;

    if (event == BC_BUTTON_EVENT_PRESS)
    {
        if(measuring)
        {
            bc_led_set_mode(&led, BC_LED_MODE_OFF);
            measuring = false;
        }
        else
        {
            bc_led_set_mode(&led, BC_LED_MODE_ON);
            measuring = true;
        }
    }
}

void battery_event_handler(bc_module_battery_event_t event, void *event_param)
{
    (void) event_param;

    float voltage;

    if (event == BC_MODULE_BATTERY_EVENT_UPDATE)
    {
        if (bc_module_battery_get_voltage(&voltage))
        {
            bc_radio_pub_battery(&voltage);
        }
    }
}


void climate_module_event_handler(bc_module_climate_event_t event, void *event_param)
{
    (void) event_param;

    if(!measuring)
    {
        float value;
        if (event == BC_MODULE_CLIMATE_EVENT_UPDATE_BAROMETER)
        {
            if (bc_module_climate_get_pressure_pascal(&value))
            {
                if ((fabs(value - params.pressure.value) >= BAROMETER_TAG_PUB_VALUE_CHANGE) || (params.pressure.next_pub < bc_scheduler_get_spin_tick()))
                {
                    float meter;

                    if (!bc_module_climate_get_altitude_meter(&meter))
                    {
                        return;
                    }

                    bc_radio_pub_barometer(BC_RADIO_PUB_CHANNEL_R1_I2C0_ADDRESS_DEFAULT, &value, &meter);
                    params.pressure.value = value;
                    params.pressure.next_pub = bc_scheduler_get_spin_tick() + BAROMETER_TAG_PUB_NO_CHANGE_INTEVAL;
                }
            }
        }
    }
}

// This function dispatches accelerometer events
void lis2dh12_event_handler(bc_lis2dh12_t *self, bc_lis2dh12_event_t event, void *event_param)
{
    if(measuring)
    {
        // Update event?
        if (event == BC_LIS2DH12_EVENT_UPDATE)
        {
            // Successfully read accelerometer vectors?
            if (bc_lis2dh12_get_result_g(self, &result))
            {
                magnitude = pow(result.x_axis, 2) + pow(result.y_axis, 2) + pow(result.z_axis, 2);
                magnitude = sqrt(magnitude);
            }

            if(magnitude > 4)
            {
                static bc_tick_t radio_delay = 0;
                if (bc_tick_get() >= radio_delay)
                {
                    bc_led_pulse(&led, 100);
                    float value;
                    float meter;

                    if (bc_module_climate_get_pressure_pascal(&value))
                    {
                        if (!bc_module_climate_get_altitude_meter(&meter))
                        {
                            return;
                        }
                    }
                    bc_radio_pub_barometer(BC_RADIO_PUB_CHANNEL_R1_I2C0_ADDRESS_DEFAULT, &value, &meter);
                }
            }
        }
    }
}

void application_init(void)
{
    // Initialize LED
    bc_led_init(&led, BC_GPIO_LED, false, false);
    bc_led_set_mode(&led, BC_LED_MODE_ON);

    // Initialize radio
    bc_radio_init(BC_RADIO_MODE_NODE_SLEEPING);

    bc_lis2dh12_init(&lis2dh12, BC_I2C_I2C0, 0x19);
    bc_lis2dh12_set_event_handler(&lis2dh12, lis2dh12_event_handler, NULL);
    bc_lis2dh12_set_resolution(&lis2dh12, BC_LIS2DH12_RESOLUTION_8BIT);
    bc_lis2dh12_set_scale(&lis2dh12, BC_LIS2DH12_SCALE_16G);
    bc_lis2dh12_set_update_interval(&lis2dh12, ACCELEROMETER_UPDATE_NORMAL_INTERVAL);

    // Initialize button
    bc_button_init(&button, BC_GPIO_BUTTON, BC_GPIO_PULL_DOWN, false);
    bc_button_set_event_handler(&button, button_event_handler, &button_event_count);

    // Initialize battery
    bc_module_battery_init();
    bc_module_battery_set_event_handler(battery_event_handler, NULL);
    bc_module_battery_set_update_interval(BATTERY_UPDATE_INTERVAL);

    // Initialize climate module
    bc_module_climate_init();
    bc_module_climate_set_event_handler(climate_module_event_handler, NULL);
    bc_module_climate_set_update_interval_barometer(BAROMETER_UPDATE_NORMAL_INTERVAL);
    bc_module_climate_measure_barometer();

    bc_radio_pairing_request("elevator-monitor", VERSION);
}