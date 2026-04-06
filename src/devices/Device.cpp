#include "Device.h"
#include <stdexcept>

namespace mycpu
{

    Device::Device(uint32 base_address, uint32 size)
        : base_address_(base_address), size_(size) {}

    void Device::reset() {}

    void Device::tick() {}

    bool Device::handlesAddress(Address addr) const
    {
        return addr >= base_address_ && addr < (base_address_ + size_);
    }

    UARTDevice::UARTDevice()
        : Device(constants::UART_BASE, 0x10), has_data_(false), status_(0x01)
    {
        reset();
    }

    void UARTDevice::reset()
    {
        buffer_.clear();
        has_data_ = false;
        status_ = 0x01;
    }

    uint32 UARTDevice::read(Address addr, uint8 size)
    {
        (void)size;
        uint32 offset = addr - constants::UART_BASE;

        switch (offset)
        {
        case 0x00: // 数据寄存器
            if (has_data_ && !buffer_.empty())
            {
                char c = buffer_[0];
                buffer_.erase(buffer_.begin());
                if (buffer_.empty())
                {
                    has_data_ = false;
                }
                return static_cast<uint32>(c);
            }
            return 0;
        case 0x04: // 状态寄存器
            return status_ | (has_data_ ? 0x01 : 0x00);
        default:
            return 0;
        }
    }

    void UARTDevice::write(Address addr, uint32 value, uint8 size)
    {
        (void)size;
        uint32 offset = addr - constants::UART_BASE;

        switch (offset)
        {
        case 0x00: // 数据寄存器
        {
            char c = static_cast<char>(value & 0xFF);
            buffer_ += c;
            has_data_ = true;
        }
        break;
        case 0x04: // 控制寄存器
            break;
        default:
            break;
        }
    }

    TimerDevice::TimerDevice()
        : Device(constants::TIMER_BASE, 0x10), counter_(0), compare_(0), interrupt_pending_(false)
    {
        reset();
    }

    void TimerDevice::reset()
    {
        counter_ = 0;
        compare_ = 0;
        interrupt_pending_ = false;
    }

    uint32 TimerDevice::read(Address addr, uint8 size)
    {
        (void)size;
        uint32 offset = addr - constants::TIMER_BASE;

        switch (offset)
        {
        case 0x00: // 计时器当前值
            return counter_;
        case 0x04: // 比较值
            return compare_;
        case 0x08: // 控制寄存器
            return interrupt_pending_ ? 0x01 : 0x00;
        default:
            return 0;
        }
    }

    void TimerDevice::write(Address addr, uint32 value, uint8 size)
    {
        (void)size;
        uint32 offset = addr - constants::TIMER_BASE;

        switch (offset)
        {
        case 0x00: // 计时器当前值
            counter_ = value;
            break;
        case 0x04: // 比较值
            compare_ = value;
            break;
        case 0x08: // 控制寄存器
            if (value & 0x02)
            {
                interrupt_pending_ = false;
            }
            break;
        default:
            break;
        }
    }

    void TimerDevice::tick()
    {
        counter_++;
        if (counter_ >= compare_ && compare_ > 0)
        {
            interrupt_pending_ = true;
        }
    }

    InterruptControllerDevice::InterruptControllerDevice()
        : Device(constants::INTERRUPT_BASE, 0x10), pending_bits_(0), enabled_bits_((1u << 3) | (1u << 11))
    {
        reset();
    }

    void InterruptControllerDevice::reset()
    {
        pending_bits_ = 0;
        enabled_bits_ = (1u << 3) | (1u << 11);
    }

    uint32 InterruptControllerDevice::read(Address addr, uint8 size)
    {
        (void)size;
        uint32 offset = addr - constants::INTERRUPT_BASE;
        switch (offset)
        {
        case 0x00:
            return pending_bits_;
        case 0x04:
            return enabled_bits_;
        default:
            return 0;
        }
    }

    void InterruptControllerDevice::write(Address addr, uint32 value, uint8 size)
    {
        (void)size;
        uint32 offset = addr - constants::INTERRUPT_BASE;
        switch (offset)
        {
        case 0x04:
            enabled_bits_ = value & ((1u << 3) | (1u << 11));
            pending_bits_ &= enabled_bits_;
            break;
        case 0x08:
            pending_bits_ |= value & enabled_bits_ & ((1u << 3) | (1u << 11));
            break;
        case 0x0C:
            pending_bits_ &= ~(value & ((1u << 3) | (1u << 11)));
            break;
        default:
            break;
        }
    }

} // namespace mycpu
