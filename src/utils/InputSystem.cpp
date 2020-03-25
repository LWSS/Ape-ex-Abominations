#include "InputSystem.h"

// https://github.com/LWSS/evdev-mirror

// This struct is the same at <linux/input.h>
struct input_value {
    uint16_t type;
    uint16_t code;
    int32_t value;
};

void InputSystem::InputSystem() {
    static int fd = open("/dev/input/evdev-mirror", O_RDONLY /*| O_NONBLOCK*/);
    struct input_value input;

    if (fd < 0) {
        Logger::Log("Error opening evdev-mirror! (%d), %d\n", fd, errno);
        return;
    }

    while (running) {
        // if zero bytes, keep goin...
        ssize_t n = read(fd, &input, sizeof(input_value));

        static ssize_t s = sizeof(input_value);

        if (n == -1 || n != s) {
            break;
        }

        if (input.type == EV_KEY && input.value >= 0 && input.value <= 2) {
            pressedKeys[input.code] = input.value > 0;
        }
        //Logger::Log("Key: %d - state: %d\n", input.code, input.value);
        //std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}