#ifndef __MMU_H_
#define __MMU_H_

#include <cstdint>
#include <string>
#include <vector>

enum DataType : uint8_t {FreeSpace, Char, Short, Int, Float, Long, Double};

typedef struct Variable {
    std::string name;
    DataType type;
    uint32_t virtual_address;
    uint32_t size;
} Variable;

typedef struct Process {
    uint32_t pid;
    std::vector<Variable*> variables;
} Process;

class Mmu {
private:
    uint32_t _next_pid;
    uint32_t _max_size;
    std::vector<Process*> _processes;

public:
    Mmu(int memory_size);
    ~Mmu();

    uint32_t createProcess();
    void addVariableToProcess(uint32_t pid, std::string var_name, DataType type, uint32_t size, uint32_t address);
    void print();

    // Helper methods used by main.cpp for allocation, deallocation, and error checks.
    bool processExists(uint32_t pid) const;
    bool variableExists(uint32_t pid, const std::string& var_name) const;
    Process* getProcess(uint32_t pid) const;
    Variable* getVariable(uint32_t pid, const std::string& var_name) const;
    void removeVariableFromProcess(uint32_t pid, const std::string& var_name);
    void removeProcess(uint32_t pid);
    std::vector<uint32_t> getPids() const;
};

#endif // __MMU_H_
