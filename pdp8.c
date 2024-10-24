#include <stdio.h>

#define MEMSIZE 32768  // 15-bit addresses-able memory

// Operation Codes
#define OP_AND 0
#define OP_TAD 1
#define OP_ISZ 2
#define OP_DCA 3
#define OP_JMS 4
#define OP_JMP 5
#define OP_IO 6
#define OP_MICRO 7

// I bit
#define DIRECT 0
#define INDIRECT 1

// Z bit
#define ZERO 0
#define CURRENT 1

unsigned short memory[MEMSIZE];
unsigned short AC = 0;  // accumulator; 12 bits
unsigned short PC = 0;  // program counter; 12 bits

// These registers are shifted by 12 bits to the left
unsigned short LK = 0;  // link bit; 1 bit
unsigned short IF = 0;  // instruction field register; 3 bits
unsigned short DF = 0;  // data field register; 3 bits

unsigned short address;  // memory address
unsigned short inst;     // instruction from memory
unsigned char indirect;  // I bit
unsigned char page;      // Z bit

unsigned char input;
int isDebug = 1;

unsigned short getAddrPageZero(unsigned short inst);
unsigned short getAddrPageCurrent(unsigned short inst);
unsigned short getIndirectAddress(unsigned short address);

int main() {
    FILE *interpreterFile;
    interpreterFile = fopen("focal.dump.bn.ascii", "r");

    char ch;
    while ((ch = fgetc(interpreterFile)) != EOF) {
        // load interpreter into memory
    }

    while (1) {
        // get input line from user
        // scanf("%s", input);

        // fetch instruction from memory
        address = PC | IF;
        inst = memory[address];

        // decode instruction
        indirect = (inst >> 8) & 01;
        page = (inst >> 7) & 01;

        // do operation
        switch (inst >> 9) {
            case OP_AND:
                // AND the operand into AC.
                if (page == CURRENT)
                    address = getAddrPageCurrent(inst);
                else
                    address = getAddrPageZero(inst);

                PC = (PC + 1) & 07777;
                if (indirect) address = getIndirectAddress(address);
                AC = AC & memory[address];
                break;

            case OP_TAD:
                // ADD the operand to AC.
                if (page == CURRENT)
                    address = getAddrPageCurrent(inst);
                else
                    address = getAddrPageZero(inst);

                PC = (PC + 1) & 07777;
                if (indirect) address = getIndirectAddress(address);

                AC = (AC | LK) + memory[address];
                LK = AC & 010000;
                AC = AC & 007777;
                break;

            case OP_ISZ:
                // Increment operand. If zero, SKIP next instruction.
                if (page == CURRENT)
                    address = getAddrPageCurrent(inst);
                else
                    address = getAddrPageZero(inst);

                PC = (PC + 1) & 07777;
                if (indirect) address = getIndirectAddress(address);

                inst = memory[address] = ((memory[address] + 1) & 07777);
                if (inst == 0) PC = (PC + 1) & 07777;
                break;

            case OP_DCA:
                // Put accumulator into memory.
                if (page == CURRENT)
                    address = getAddrPageCurrent(inst);
                else
                    address = getAddrPageZero(inst);

                PC = (PC + 1) & 07777;
                if (indirect) address = getIndirectAddress(address);

                memory[address] = AC;
                AC = 0;
                break;

            case OP_JMS:
                // Store return address then JUMP to subroutine.
                if (page == CURRENT)
                    address = getAddrPageCurrent(inst);
                else
                    address = getAddrPageZero(inst);
                if (indirect) address = getIndirectAddress(address);

                address = (address & 07777) | IF;
                memory[address] = (PC + 1) & 07777;
                PC = (address + 1) & 07777;
                break;

            case OP_JMP:
                // JUMP to address.
                if (page == CURRENT)
                    address = getAddrPageCurrent(inst);
                else
                    address = getAddrPageZero(inst);
                if (indirect) address = getIndirectAddress(address);

                PC = address & 07777;
                break;

            case OP_IO:
                // Input/Output Transfer
                break;

            case OP_MICRO:
                // Microcoded operations
                break;
        }
    }
}

unsigned short getAddrPageZero(unsigned short inst) {
    return ((inst & 0177) | IF);
}

unsigned short getAddrPageCurrent(unsigned short inst) {
    return ((inst & 0177) | (PC & 07600) | IF);
}

unsigned short getIndirectAddress(unsigned short address) {
    if ((address & 07770) == 00010) {  // address 010 to 017
        // autoindexed addressing
        memory[address] = (memory[address] + 1) & 07777;
        address = memory[address] | DF;
    } else {
        // immediate addressing
        address = (memory[address]) | DF;
    }
    return address;
}
