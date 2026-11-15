#pragma once
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>

#include "esp_camera.h"
#include "esp_err.h"

/**
 * The XIAO ESP32-S3 Sense's camera (OV2640 or OV3660), over espressif/esp32-camera.
 *
 * JPEG frames in PSRAM, two buffers so a capture always gets the latest frame.
 * There's only one camera, and esp32-camera is a singleton, so this is too:
 * construct one, and start() it once.
 */
class Camera {
public:
    /** One captured frame. Returns its buffer to the driver when it goes. Move-only. */
    class Frame {
    public:
        Frame(Frame&& other) noexcept : fb_(std::exchange(other.fb_, nullptr)) {}
        Frame& operator=(Frame&& other) noexcept {
            if (this != &other) {
                release();
                fb_ = std::exchange(other.fb_, nullptr);
            }
            return *this;
        }
        Frame(const Frame&) = delete;
        Frame& operator=(const Frame&) = delete;
        ~Frame() { release(); }

        [[nodiscard]] std::span<const uint8_t> jpeg() const { return {fb_->buf, fb_->len}; }
        [[nodiscard]] std::size_t width() const { return fb_->width; }
        [[nodiscard]] std::size_t height() const { return fb_->height; }

    private:
        friend Camera;
        explicit Frame(camera_fb_t* fb) : fb_(fb) {}
        void release() {
            if (fb_ != nullptr) esp_camera_fb_return(fb_);
            fb_ = nullptr;
        }
        camera_fb_t* fb_;
    };

    struct Config {
        framesize_t frame_size = FRAMESIZE_VGA;
        int jpeg_quality = 12;  ///< 0-63, lower is better and bigger
    };

    Camera() = default;
    Camera(const Camera&) = delete;
    Camera& operator=(const Camera&) = delete;

    /** Power the sensor up, find it and start capturing. Fails, rather than aborting, without one. */
    [[nodiscard]] esp_err_t start(const Config& cfg);

    /** The newest frame. Only one may be held at a time per spare buffer. */
    [[nodiscard]] std::expected<Frame, esp_err_t> capture();

    /** The sensor's model, such as "OV2640", once started. */
    [[nodiscard]] const char* sensor_name() const;

private:
    bool started_ = false;
};
