#include <string.h>
#include <inttypes.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#include <esp_log.h>
#include <esp_event.h>
#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_types.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_standard_devices.h>
#include <esp_rmaker_schedule.h>
#include <esp_rmaker_scenes.h>
#include <esp_rmaker_console.h>
#include <esp_rmaker_ota.h>
#include <esp_rmaker_common_events.h>

#include <app_network.h>
#include <app_insights.h>

#include "app_driver.h"

static const char *TAG = "app_main";
esp_rmaker_device_t *awning_device;

//Callback function that runs on a write to the device parameter in rainmaker
static esp_err_t write_cb(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param,
                          const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx)
{

    if (ctx)
    {
        ESP_LOGI(TAG, "Received write request via : %s", esp_rmaker_device_cb_src_to_str(ctx->src));
    }
    if (strcmp(esp_rmaker_param_get_name(param), ESP_RMAKER_DEF_POWER_NAME) == 0)
    {
        ESP_LOGI(TAG, "Received value = %s for %s - %s",
                 val.val.b ? "true" : "false", esp_rmaker_device_get_name(device),
                 esp_rmaker_param_get_name(param));
        app_driver_set_state(val.val.b);

        esp_rmaker_param_update(param, val);
    }
    return ESP_OK;
}
//Event handler for rainmaker events
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

void app_main()
{
    //Initialise app specific hardware drivers and initial states
    esp_rmaker_console_init();
    app_driver_init();
    app_driver_set_state(DEFAULT_STATE);

    //Initialise NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    //Initialise WiFi
    app_network_init();

    //Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(RMAKER_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(RMAKER_COMMON_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(APP_NETWORK_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(RMAKER_OTA_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

    //Initialise ESP Rainmaker agent
    esp_rmaker_config_t rainmaker_cfg = {
        .enable_time_sync = false,
    };
    esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "Awning", "Switch");
    if (!node)
    {
        ESP_LOGE(TAG, "Could not initialise node. Aborting!!!");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        abort();
    }

    //Create awning device using blinds device parameter
    awning_device = esp_rmaker_device_create("Awning", ESP_RMAKER_DEVICE_BLINDS_EXTERNAL, NULL);

    //Add write callback to awning device
    esp_rmaker_device_add_cb(awning_device, write_cb, NULL);

    //Add "Awning" name to device
    esp_rmaker_device_add_param(awning_device, esp_rmaker_name_param_create(ESP_RMAKER_DEF_NAME_PARAM, "Awning"));

    //Add power parameter with toggle switch UI type
    esp_rmaker_param_t *power_param = esp_rmaker_power_param_create(ESP_RMAKER_DEF_POWER_NAME, DEFAULT_STATE);
    esp_rmaker_device_add_param(awning_device, power_param);

    //Assign power parameter as primary parameter, allowing it to be used from home screen of mobile device
    esp_rmaker_device_assign_primary_param(awning_device, power_param);

    //Add device to node
    esp_rmaker_node_add_device(node, awning_device);

    //Enable OTA
    esp_rmaker_ota_enable_default();

    //Enable insights
    app_insights_enable();

    //Start rainmaker agent
    esp_rmaker_start();

    //Start WiFi and provisioning process if not provisioned
    err = app_network_start(POP_TYPE_RANDOM);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Could not start Wifi. Aborting!!!");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
        abort();
    }
}
