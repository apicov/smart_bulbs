#include <BulbNode.hpp>

// Seeed XIAO ESP32-S3 Sense: a bulb driving a 12 V strip.
extern "C" void app_main() {
    BulbNode::init_nvs();

    // Lives for the whole run, past the return from app_main. Built here rather
    // than at namespace scope so it's constructed after the scheduler starts.
    static BulbNode node;
    node.run();
}
