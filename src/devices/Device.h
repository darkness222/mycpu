#pragma once

#include "../include/Types.h"
#include "../include/Constants.h"
#include <string>

namespace mycpu {

class Device {
public:
    Device(uint32 base_address, uint32 size);
    virtual ~Device() = default;

    virtual void reset();
    virtual uint32 read(Address addr, uint8 size) = 0;
    virtual void write(Address addr, uint32 value, uint8 size) = 0;
    virtual void tick();

    bool handlesAddress(Address addr) const;
    uint32 getBaseAddress() const { return base_address_; }
    uint32 getSize() const { return size_; }

protected:
    uint32 base_address_;
    uint32 size_;
};

class UARTDevice : public Device {
public:
    UARTDevice();
    virtual ~UARTDevice() = default;

    void reset() override;
    uint32 read(Address addr, uint8 size) override;
    void write(Address addr, uint32 value, uint8 size) override;

    std::string getBuffer() const { return buffer_; }
    bool hasData() const { return has_data_; }

private:
    std::string buffer_;
    bool has_data_;
    uint32 status_;
};

class TimerDevice : public Device {
public:
    TimerDevice();
    virtual ~TimerDevice() = default;

    void reset() override;
    uint32 read(Address addr, uint8 size) override;
    void write(Address addr, uint32 value, uint8 size) override;
    void tick() override;

    uint32 getValue() const { return counter_; }
    bool hasInterrupt() const { return interrupt_pending_; }

private:
    uint32 counter_;
    uint32 compare_;
    bool interrupt_pending_;
};

} // namespace mycpu
