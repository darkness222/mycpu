#include "Bus.h"
#include "../devices/Device.h"
#include <algorithm>

namespace mycpu {

Bus::Bus() {}

void Bus::reset() {
    for (auto& device : devices_) {
        device->reset();
    }
}

void Bus::connectDevice(uint32 base_address, std::shared_ptr<Device> device) {
    devices_.push_back(device);
}

uint32 Bus::read(Address addr, uint8 size) {
    if (addr < constants::MMIO_BASE) {
        return 0;
    }
    return mmio_read(addr, size);
}

void Bus::write(Address addr, uint32 value, uint8 size) {
    if (addr < constants::MMIO_BASE) {
        return;
    }
    mmio_write(addr, value, size);
}

void Bus::tick() {
    for (auto& device : devices_) {
        device->tick();
    }
}

uint32 Bus::mmio_read(Address addr, uint8 size) {
    for (auto& device : devices_) {
        if (device->handlesAddress(addr)) {
            return device->read(addr, size);
        }
    }
    return 0;
}

void Bus::mmio_write(Address addr, uint32 value, uint8 size) {
    for (auto& device : devices_) {
        if (device->handlesAddress(addr)) {
            device->write(addr, value, size);
            return;
        }
    }
}

} // namespace mycpu
