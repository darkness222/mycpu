#pragma once

#include "../include/Types.h"
#include "../memory/Memory.h"
#include <memory>
#include <string>
#include <vector>

namespace mycpu {

class Bus;
class CsrFile;
class TrapHandler;
class RegisterFile;

class CpuCore {
public:
    virtual ~CpuCore() = default;

    virtual void reset() = 0;
    virtual void step() = 0;
    virtual void run(uint64 cycles) = 0;
    virtual void loadProgram(const std::vector<uint32>& program, uint32 start_address = 0) = 0;
    virtual bool loadElf(const std::vector<uint8>& elf_data) = 0;
    virtual bool loadBinary(const std::vector<uint32>& program, uint32 start_address = 0x00000000) = 0;

    virtual void setMemory(std::shared_ptr<Memory> memory) = 0;
    virtual void setBus(std::shared_ptr<Bus> bus) = 0;

    virtual SimulationMode getSimulationMode() const = 0;
    virtual std::string getCoreName() const = 0;
    virtual bool supportsTrueOverlapPipeline() const = 0;

    virtual CsrFile& getCsr() = 0;
    virtual TrapHandler& getTrapHandler() = 0;
    virtual uint32 getPC() const = 0;
    virtual CpuState getState() const = 0;
    virtual PipelineStage getCurrentStage() const = 0;
    virtual const RegisterFile& getRegisterFile() const = 0;
    virtual const CpuStats& getStats() const = 0;
    virtual const HazardSignals& getHazardSignals() const = 0;
    virtual const PeripheralState& getPeripherals() const = 0;
    virtual const PipelineRegisters& getPipelineRegisters() const = 0;
    virtual const std::vector<std::string>& getTrace() const = 0;
    virtual bool didTestFinish() const = 0;
    virtual bool didTestPass() const = 0;
    virtual uint32 getTestCode() const = 0;
    virtual uint32 getTohostAddress() const = 0;

    virtual SimulatorState getSimulatorState() const = 0;
    virtual std::string toJson() const = 0;
};

} // namespace mycpu
