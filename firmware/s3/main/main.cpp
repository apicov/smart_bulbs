#include <chrono>
#include <string_view>

#include <esp_log.h>

#include <BulbNode.hpp>
#include <Camera.hpp>
#include <SensorHub.hpp>

// Seeed XIAO ESP32-S3 Sense: a bulb driving a 12 V strip, with I2C sensors and a camera.
namespace {
constexpr const char* TAG = "main";

constexpr gpio_num_t pin(int gpio) { return gpio < 0 ? GPIO_NUM_NC : static_cast<gpio_num_t>(gpio); }

// The VL53L1X moves here from 0x29, which the TSL2591 can't leave.
constexpr uint8_t VL53L1X_ADDRESS = 0x30;

/** Start the camera and prove it with one frame. Returns a status that lives forever. */
const char* start_camera(Camera& camera) {
    if (const esp_err_t err = camera.start({}); err != ESP_OK) return esp_err_to_name(err);
    const auto frame = camera.capture();
    if (!frame) return esp_err_to_name(frame.error());
    ESP_LOGI(TAG, "Camera test frame: %zux%zu, %zu bytes of JPEG", frame->width(), frame->height(),
             frame->jpeg().size());
    return "ok";
}
}  // namespace

extern "C" void app_main() {
    BulbNode::init_nvs();

    // These live for the whole run, past the return from app_main. Built here rather
    // than at namespace scope so they're constructed after the scheduler starts.
    static BulbNode node;
    node.run();

    // After the light, so a camera problem can't keep the lamp dark.
    static Camera camera;
    static SensorHub hub;
    hub.set_camera_status(start_camera(camera));
    hub.start({.pins = {.port = I2C_NUM_0,
                        .sda = pin(CONFIG_NODE_I2C_SDA_GPIO),
                        .scl = pin(CONFIG_NODE_I2C_SCL_GPIO)},
               .vl53l1x_xshut = pin(CONFIG_NODE_VL53L1X_XSHUT_GPIO),
               .vl53l1x_address = VL53L1X_ADDRESS,
               .period = std::chrono::seconds{CONFIG_NODE_SENSOR_PERIOD_S}},
              [](std::string_view json) {
                  if (node.connected()) node.publish("sensors", json, true);
              });
}
