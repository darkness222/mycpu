#pragma once

#include "../include/Types.h"
#include "../include/Constants.h"
#include <vector>
#include <memory>
#include <string>

namespace mycpu {

class Device;
class CPU;

class Bus {
public:
    Bus();
    ~Bus() = default;

    void reset();
    void connectDevice(uint32 base_address, std::shared_ptr<Device> device);

    uint32 read(Address addr, uint8 size);
    void write(Address addr, uint32 value, uint8 size);

    void tick();
    bool hasPendingTimerInterrupt() const;
    void clearTimerInterrupt();
    uint32 getTimerValue() const;
    std::string getUartBuffer() const;
    bool hasPendingSoftwareInterrupt() const;
    bool hasPendingExternalInterrupt() const;
    uint32 getInterruptPendingBits() const;
    uint32 getInterruptEnabledBits() const;

private:
    std::vector<std::shared_ptr<Device>> devices_;

    uint32 mmio_read(Address addr, uint8 size);
    void mmio_write(Address addr, uint32 value, uint8 size);
};

} // namespace mycpu
