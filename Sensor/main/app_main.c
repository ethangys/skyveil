#include <string.h>
#include <inttypes.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <esp_rmaker_standard_types.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_standard_devices.h>
#include <esp_rmaker_schedule.h>
#include <esp_rmaker_scenes.h>
#include <esp_rmaker_console.h>
#include <esp_rmaker_ota.h>
#include <esp_rmaker_common_events.h>
#include <esp_rmaker_core.h>

#include <app_network.h>
#include <app_insights.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

// Assign sensor input GPIO
#define SENSOR_PIN 4
// Define sleep time for deep sleep in microseconds
#define SLEEP_TIME 10 * 1000000

static const char *TAG = "app_main";

// Initialise device and parameters
esp_rmaker_device_t *rain_sensor;
esp_rmaker_param_t *water_param;
//Initialise rain state to persist across deep sleep
RTC_DATA_ATTR bool was_raining;

void app_driver_init(void)
{
    // Configure GPIO as input
    gpio_reset_pin(SENSOR_PIN);
    gpio_set_direction(SENSOR_PIN, GPIO_MODE_INPUT);
}

// Event handler for Rainmaker events
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == RMAKER_EVENT)
    {
        switch (event_id)
        {
        case RMAKER_EVENT_INIT_DONE:
            ESP_LOGI(TAG, "RainMaker Initialised.");
            break;
        case RMAKER_EVENT_CLAIM_STARTED:
            ESP_LOGI(TAG, "RainMaker Claim Started.");
            break;
        case RMAKER_EVENT_CLAIM_SUCCESSFUL:
            ESP_LOGI(TAG, "RainMaker Claim Successful.");
            break;
        case RMAKER_EVENT_CLAIM_FAILED:
            ESP_LOGI(TAG, "RainMaker Claim Failed.");
            break;
        case RMAKER_EVENT_LOCAL_CTRL_STARTED:
            ESP_LOGI(TAG, "Local Control Started.");
            break;
        case RMAKER_EVENT_LOCAL_CTRL_STOPPED:
            ESP_LOGI(TAG, "Local Control Stopped.");
            break;
        default:
            ESP_LOGW(TAG, "Unhandled RainMaker Event: %" PRIi32, event_id);
        }
    }
    else if (event_base == RMAKER_COMMON_EVENT)
    {
        switch (event_id)
        {
        case RMAKER_EVENT_REBOOT:
            ESP_LOGI(TAG, "Rebooting in %d seconds.", *((uint8_t *)event_data));
            break;
        case RMAKER_EVENT_WIFI_RESET:
            ESP_LOGI(TAG, "Wi-Fi credentials reset.");
            break;
        case RMAKER_EVENT_FACTORY_RESET:
            ESP_LOGI(TAG, "Node reset to factory defaults.");
            break;
        case RMAKER_MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Connected.");
            break;
        case RMAKER_MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT Disconnected.");
            break;
        case RMAKER_MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT Published. Msg id: %d.", *((int *)event_data));
            break;
        default:
            ESP_LOGW(TAG, "Unhandled RainMaker Common Event: %" PRIi32, event_id);
        }
    }
    else if (event_base == APP_NETWORK_EVENT)
    {
        switch (event_id)
        {
        case APP_NETWORK_EVENT_QR_DISPLAY:
            ESP_LOGI(TAG, "Provisioning QR : %s", (char *)event_data);
            break;
        case APP_NETWORK_EVENT_PROV_TIMEOUT:
            ESP_LOGI(TAG, "Provisioning Timed Out. Please reboot.");
            break;
        case APP_NETWORK_EVENT_PROV_RESTART:
            ESP_LOGI(TAG, "Provisioning has restarted due to failures.");
            break;
        default:
            ESP_LOGW(TAG, "Unhandled App Wi-Fi Event: %" PRIi32, event_id);
            break;
        }
    }
    else if (event_base == RMAKER_OTA_EVENT)
    {
        switch (event_id)
        {
        case RMAKER_OTA_EVENT_STARTING:
            ESP_LOGI(TAG, "Starting OTA.");
            break;
        case RMAKER_OTA_EVENT_IN_PROGRESS:
            ESP_LOGI(TAG, "OTA is in progress.");
            break;
        case RMAKER_OTA_EVENT_SUCCESSFUL:
            ESP_LOGI(TAG, "OTA successful.");
            break;
        case RMAKER_OTA_EVENT_FAILED:
            ESP_LOGI(TAG, "OTA Failed.");
            break;
        case RMAKER_OTA_EVENT_REJECTED:
            ESP_LOGI(TAG, "OTA Rejected.");
            break;
        case RMAKER_OTA_EVENT_DELAYED:
            ESP_LOGI(TAG, "OTA Delayed.");
            break;
        case RMAKER_OTA_EVENT_REQ_FOR_REBOOT:
            ESP_LOGI(TAG, "Firmware image downloaded. Please reboot your device to apply the upgrade.");
            break;
        default:
            ESP_LOGW(TAG, "Unhandled OTA Event: %" PRIi32, event_id);
            break;
        }
    }
    else
    {
        ESP_LOGW(TAG, "Invalid event received!");
    }
}

void get_reading(void)
{
    // Assign GPIO level to rain status
    bool raining = !(gpio_get_level(SENSOR_PIN));
    // Update rainmaker and send notification to device if rain status has changed
    if ((!was_raining && raining) || (was_raining && !raining))
    {
        esp_rmaker_param_update_and_notify(water_param, esp_rmaker_bool(raining));
    }
    else
    {
        esp_rmaker_param_update(water_param, esp_rmaker_bool(raining));
    }
    was_raining = raining;
    // Add delay before deep sleep to ensure rainmaker updates
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    // Disable previous sleep wakeup source
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    // If raining, set deep sleep to wake up on timer
    if (raining)
    {
        ESP_LOGI(TAG, "Rain detected, entering deep sleep for %d minutes", SLEEP_TIME / 60000000);
        esp_sleep_enable_timer_wakeup(SLEEP_TIME);
    }
    // If not raining, set deep sleep to wake up on GPIO low (rain)
    else
    {
        ESP_LOGI(TAG, "No rain detected, entering deep sleep until rain");
        esp_deep_sleep_enable_gpio_wakeup(1ULL << SENSOR_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
    }
    esp_deep_sleep_start();
}

void app_main()
{
    // Initialise app-specific hardware drivers and initial states
    esp_rmaker_console_init();
    app_driver_init();

    // Initialise NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Initialise WiFi
    app_network_init();

    // Register event handlers for Rainmaker events
    ESP_ERROR_CHECK(esp_event_handler_register(RMAKER_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(RMAKER_COMMON_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(APP_NETWORK_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(RMAKER_OTA_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

    // Initialise Rainmaker agent
    esp_rmaker_config_t rainmaker_cfg = {
        .enable_time_sync = false,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "Rain Sensor", "Other");
    if (!node)
    {
        ESP_LOGE(TAG, "Could not initialise node. Aborting!!!");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        abort();
    }

    // Create rain sensor device
    rain_sensor = esp_rmaker_device_create("Rain Sensor", ESP_RMAKER_DEVICE_OTHER, NULL);

    // Add name parameter to device
    esp_rmaker_device_add_param(rain_sensor, esp_rmaker_name_param_create(ESP_RMAKER_DEF_NAME_PARAM, "Rain Sensor"));

    // Add water alarm and UI parameters
    water_param = esp_rmaker_param_create("water-alarm", NULL, esp_rmaker_bool(false), PROP_FLAG_READ);
    esp_rmaker_device_add_param(rain_sensor, water_param);
    esp_rmaker_param_add_ui_type(water_param, ESP_RMAKER_UI_TOGGLE);

    // Add device to node
    esp_rmaker_node_add_device(node, rain_sensor);

    // Enable OTA
    esp_rmaker_ota_enable_default();

    // Enable insights
    app_insights_enable();

    // Start Rainmaker agent
    esp_rmaker_start();

    // Start wifi and provisionining if not provisioned yet
    err = app_network_start(POP_TYPE_RANDOM);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Could not start Wifi. Aborting!!!");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        abort();
    }
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "Previous rain state: %s", was_raining ? "Raining" : "Not Raining");
    get_reading();
}
