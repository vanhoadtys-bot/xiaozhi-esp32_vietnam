#pragma once
#include <algorithm>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "mcp_server.h"

// Measured on the real tank: pads 40/39 (IN3/IN4) drive the LEFT track,
// pads 43/44 (IN1/IN2) drive the RIGHT track.
#define TANK_LEFT_IN1_GPIO   GPIO_NUM_40
#define TANK_LEFT_IN2_GPIO   GPIO_NUM_39
#define TANK_RIGHT_IN3_GPIO  GPIO_NUM_43
#define TANK_RIGHT_IN4_GPIO  GPIO_NUM_44

#define TANK_LEFT_REVERSED   true
#define TANK_RIGHT_REVERSED  false

#define TANK_DEFAULT_MS 1000
#define TANK_MAX_MS     5000
#define TANK_TAG        "Tank"

class TankController {
public:
    TankController() {
        gpio_config_t cfg = {};
        cfg.pin_bit_mask = (1ULL << TANK_LEFT_IN1_GPIO) | (1ULL << TANK_LEFT_IN2_GPIO) |
                           (1ULL << TANK_RIGHT_IN3_GPIO) | (1ULL << TANK_RIGHT_IN4_GPIO);
        cfg.mode = GPIO_MODE_OUTPUT;
        cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
        cfg.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&cfg));
        StopMotors();

        esp_timer_create_args_t args = {};
        args.callback = &TankController::OnTimeout;
        args.arg = this;
        args.name = "tank_stop";
        ESP_ERROR_CHECK(esp_timer_create(&args, &stop_timer_));

        RegisterTools();
        ESP_LOGI(TANK_TAG, "Tank controller ready");
    }

private:
    struct Step { int l; int r; int ms; };   // +1 forward, -1 backward, 0 stop

    esp_timer_handle_t stop_timer_ = nullptr;
    volatile bool dancing_ = false;
    volatile bool cancel_dance_ = false;

    static void SetWheel(gpio_num_t a, gpio_num_t b, int dir) {
        gpio_set_level(a, dir > 0 ? 1 : 0);
        gpio_set_level(b, dir < 0 ? 1 : 0);
    }
    static void SetTracks(int left, int right) {
        SetWheel(TANK_LEFT_IN1_GPIO, TANK_LEFT_IN2_GPIO, TANK_LEFT_REVERSED ? -left : left);
        SetWheel(TANK_RIGHT_IN3_GPIO, TANK_RIGHT_IN4_GPIO, TANK_RIGHT_REVERSED ? -right : right);
    }
    static void StopMotors() {
        SetWheel(TANK_LEFT_IN1_GPIO, TANK_LEFT_IN2_GPIO, 0);
        SetWheel(TANK_RIGHT_IN3_GPIO, TANK_RIGHT_IN4_GPIO, 0);
    }
    static void OnTimeout(void*) { StopMotors(); }

    // Dance runs on its own task so the tool call returns at once.
    static void DanceTask(void* arg) {
        auto* self = static_cast<TankController*>(arg);
        // A short 0,0 pause between reversals protects the battery and driver.
        static const Step steps[] = {
            {1, -1, 500}, {0, 0, 100}, {-1, 1, 500}, {0, 0, 100},
            {1, -1, 500}, {0, 0, 100}, {-1, 1, 500}, {0, 0, 100},
            {1, 1, 600},  {0, 0, 100}, {-1, -1, 600}, {0, 0, 100},
            {1, -1, 1500}, {0, 0, 100},
        };
        bool cancelled = false;
        for (const auto& s : steps) {
            if (self->cancel_dance_) { cancelled = true; break; }
            SetTracks(s.l, s.r);
            int remaining = s.ms;
            while (remaining > 0 && !self->cancel_dance_) {
                int slice = remaining > 50 ? 50 : remaining;
                vTaskDelay(pdMS_TO_TICKS(slice));
                remaining -= slice;
            }
            if (self->cancel_dance_) { cancelled = true; break; }
        }
        if (!cancelled) {
            StopMotors();
        }
        self->dancing_ = false;
        vTaskDelete(nullptr);
    }

    void CancelDance() {
        if (dancing_) {
            cancel_dance_ = true;
            for (int i = 0; i < 10 && dancing_; i++) {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
    }

    void StartDance() {
        CancelDance();
        esp_timer_stop(stop_timer_);
        cancel_dance_ = false;
        dancing_ = true;
        if (xTaskCreate(DanceTask, "tank_dance", 3072, this, 5, nullptr) != pdPASS) {
            dancing_ = false;
            StopMotors();
            ESP_LOGE(TANK_TAG, "Could not start dance task");
        }
    }

    // left/right: +1 forward, -1 backward, 0 stop. Always auto-stops.
    void Drive(int left, int right, int ms) {
        CancelDance();
        ms = std::max(100, std::min(ms, TANK_MAX_MS));
        esp_timer_stop(stop_timer_);
        SetTracks(left, right);
        esp_timer_start_once(stop_timer_, (uint64_t)ms * 1000);
    }

    void TestPin(int index) {
        CancelDance();
        gpio_num_t pins[4] = {TANK_LEFT_IN1_GPIO, TANK_LEFT_IN2_GPIO,
                              TANK_RIGHT_IN3_GPIO, TANK_RIGHT_IN4_GPIO};
        index = std::max(1, std::min(index, 4));
        esp_timer_stop(stop_timer_);
        StopMotors();
        gpio_set_level(pins[index - 1], 1);
        esp_timer_start_once(stop_timer_, 400 * 1000);
    }

    void RegisterTools() {
        auto& mcp = McpServer::GetInstance();

        mcp.AddTool("self.tank.forward",
            "Drive the tank robot forward for a short time, then stop automatically.",
            PropertyList({Property("duration_ms", kPropertyTypeInteger, TANK_DEFAULT_MS, 100, TANK_MAX_MS)}),
            [this](const PropertyList& p) -> ReturnValue {
                Drive(1, 1, p["duration_ms"].value<int>());
                return true;
            });

        mcp.AddTool("self.tank.backward",
            "Drive the tank robot backward for a short time, then stop automatically.",
            PropertyList({Property("duration_ms", kPropertyTypeInteger, TANK_DEFAULT_MS, 100, TANK_MAX_MS)}),
            [this](const PropertyList& p) -> ReturnValue {
                Drive(-1, -1, p["duration_ms"].value<int>());
                return true;
            });

        mcp.AddTool("self.tank.turn_left",
            "Turn the tank robot left on the spot for a short time, then stop automatically.",
            PropertyList({Property("duration_ms", kPropertyTypeInteger, 500, 100, TANK_MAX_MS)}),
            [this](const PropertyList& p) -> ReturnValue {
                Drive(-1, 1, p["duration_ms"].value<int>());
                return true;
            });

        mcp.AddTool("self.tank.turn_right",
            "Turn the tank robot right on the spot for a short time, then stop automatically.",
            PropertyList({Property("duration_ms", kPropertyTypeInteger, 500, 100, TANK_MAX_MS)}),
            [this](const PropertyList& p) -> ReturnValue {
                Drive(1, -1, p["duration_ms"].value<int>());
                return true;
            });

        mcp.AddTool("self.tank.dance",
            "Make the tank robot do a complete dance (about 5 seconds of wiggles, back and forth, "
            "and a spin). Call this ONE tool when the user asks to dance; do not combine other "
            "move tools for dancing.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                StartDance();
                return true;
            });

        mcp.AddTool("self.tank.stop",
            "Stop the tank robot immediately, including during a dance.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                CancelDance();
                esp_timer_stop(stop_timer_);
                StopMotors();
                return true;
            });

        mcp.AddTool("self.tank.test_pin",
            "Wiring test only: pulse one motor driver input (1=IN1 ... 4=IN4) for 0.4 seconds.",
            PropertyList({Property("pin", kPropertyTypeInteger, 1, 1, 4)}),
            [this](const PropertyList& p) -> ReturnValue {
                TestPin(p["pin"].value<int>());
                return true;
            });
    }
};