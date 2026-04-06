#include "Bus.h"
#include "../devices/Device.h"
#include <algorithm>
#include <memory>

namespace mycpu {

Bus::Bus() {}

void Bus::reset() {
    for (auto& device : devices_) {
        device->reset();
    }
}

void Bus::connectDevice(uint32 base_address, std::shared_ptr<Device> device) {
    (void)base_address;
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

bool Bus::hasPendingTimerInterrupt() const {
    for (const auto& device : devices_) {
        auto timer = std::dynamic_pointer_cast<TimerDevice>(device);
        if (timer && timer->hasInterrupt()) {
            return true;
        }
    }
    return false;
}

void Bus::clearTimerInterrupt() {
    for (const auto& device : devices_) {
        auto timer = std::dynamic_pointer_cast<TimerDevice>(device);
        if (timer) {
            timer->clearInterrupt();
        }
    }
}

uint32 Bus::getTimerValue() const {
    for (const auto& device : devices_) {
        auto timer = std::dynamic_pointer_cast<TimerDevice>(device);
        if (timer) {
            return timer->getValue();
        }
    }
    return 0;
}

std::string Bus::getUartBuffer() const {
    for (const auto& device : devices_) {
        auto uart = std::dynamic_pointer_cast<UARTDevice>(device);
        if (uart) {
            return uart->getBuffer();
        }
    }
    return "";
}

bool Bus::hasPendingSoftwareInterrupt() const {
    for (const auto& device : devices_) {
        auto ic = std::dynamic_pointer_cast<InterruptControllerDevice>(device);
        if (ic && ic->hasSoftwareInterrupt()) {
            return true;
        }
    }
    return false;
}

bool Bus::hasPendingExternalInterrupt() const {
    for (const auto& device : devices_) {
        auto ic = std::dynamic_pointer_cast<InterruptControllerDevice>(device);
        if (ic && ic->hasExternalInterrupt()) {
            return true;
        }
    }
    return false;
}

uint32 Bus::getInterruptPendingBits() const {
    for (const auto& device : devices_) {
        auto ic = std::dynamic_pointer_cast<InterruptControllerDevice>(device);
        if (ic) {
            return ic->getPendingBits();
        }
    }
    return 0;
}

uint32 Bus::getInterruptEnabledBits() const {
    for (const auto& device : devices_) {
        auto ic = std::dynamic_pointer_cast<InterruptControllerDevice>(device);
        if (ic) {
            return ic->getEnabledBits();
        }
    }
    return 0;
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
