#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>

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

typedef unsigned short uint16;

// I/O Device
uint16 keyboardFlag = 0;
uint16 keyboardBuffer = 0;
uint16 printerFlag = 0;
uint16 printerBuffer = 0;

uint16 memory[MEMSIZE];
uint16 AC = 0;  // accumulator; 12 bits
uint16 PC = 0;  // program counter; 12 bits

uint16 LK = 0;  // link bit; 1 bit shifted 12 bits to the left

uint16 address;      // memory address
uint16 inst;         // instruction from memory
unsigned char I;     // I bit
unsigned char page;  // Z bit

int isDebug = 0;

uint16 getAddrPageZero(uint16 inst);
uint16 getAddrPageCurrent(uint16 inst);
uint16 getIndirectAddress(uint16 address);
uint16 asciiToOctal(char c);
void readCharacter();
void printCharacter();
void printDebug();
void printDebugMicro();

struct termios termios_old;

void reset_termios() { tcsetattr(0, TCSANOW, &termios_old); }

int main() {
    FILE *interpreterFile;
    interpreterFile = fopen("focal.dump.nointerrupts.raw", "r");

    // load interpreter into memory
    char ch;
    int i = 0;
    while (ch != EOF) {
        uint16 word = 0;
        for (int j = 3; j >= 0; j--) {
            if ((ch = fgetc(interpreterFile)) == EOF) break;
            word = word | (asciiToOctal(ch) << (j * 3));
        }
        memory[i] = word;
        i++;
    }
    struct termios tios = termios_old;
    tcgetattr(0, &termios_old);
    atexit(reset_termios);
    cfmakeraw(&tios);
    tcsetattr(0, TCSANOW, &tios);

    PC = 0200;  // start at address 0200 (skip page 0)
    while (1) {
        // fetch instruction from memory
        address = PC;
        inst = memory[address];

        // decode instruction
        I = (inst >> 8) & 00001;
        page = (inst >> 7) & 00001;

        if (isDebug) {
            printf("\n");
            printDebug();
            printDebugMicro();
        }

        // do operation
        switch ((inst >> 9) & 07) {
            case OP_AND:
                // AND the operand into AC.
                if (page == CURRENT)
                    address = getAddrPageCurrent(inst);
                else
                    address = getAddrPageZero(inst);

                PC = (PC + 1) & 07777;
                if (I == INDIRECT) address = getIndirectAddress(address);

                if (isDebug)
                    printf("[AC = AC & M[%o] = %o & %o = %o] ", address, AC,
                           memory[address], (AC & memory[address]));
                AC = AC & memory[address];
                break;

            case OP_TAD:
                // ADD the operand to AC.
                if (page == CURRENT)
                    address = getAddrPageCurrent(inst);
                else
                    address = getAddrPageZero(inst);

                PC = (PC + 1) & 07777;
                if (I == INDIRECT) address = getIndirectAddress(address);

                if (isDebug)
                    printf("[L|AC = L|AC + M[%o] = %o + %o = %o] ", address,
                           (LK | AC), memory[address],
                           ((AC | LK) + memory[address]));
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
                if (I == INDIRECT) address = getIndirectAddress(address);

                if (isDebug) {
                    printf("[M[%o] = M[%o] + 1 = %o + 1 = %o] ", address,
                           address, memory[address],
                           ((memory[address] + 1) & 07777));
                }
                uint16 buf = memory[address] = ((memory[address] + 1) & 07777);
                if (buf == 0) PC = (PC + 1) & 07777;
                break;

            case OP_DCA:
                // Put accumulator into memory.
                if (page == CURRENT)
                    address = getAddrPageCurrent(inst);
                else
                    address = getAddrPageZero(inst);

                PC = (PC + 1) & 07777;
                if (I == INDIRECT) address = getIndirectAddress(address);

                if (isDebug) printf("[M[%o] = AC = %o; AC = 0] ", address, AC);
                memory[address] = AC;
                AC = 0;
                break;

            case OP_JMS:
                // Store return address then JUMP to subroutine.
                if (page == CURRENT)
                    address = getAddrPageCurrent(inst);
                else
                    address = getAddrPageZero(inst);
                if (I == INDIRECT) address = getIndirectAddress(address);

                if (isDebug)
                    printf("[M[%o] = returnPC = %o; new PC = %o] ", address,
                           ((PC + 1) & 07777), ((address + 1) & 07777));
                address = (address & 07777);
                memory[address] = (PC + 1) & 07777;
                PC = (address + 1) & 07777;
                break;

            case OP_JMP:
                // JUMP to address.
                if (page == CURRENT)
                    address = getAddrPageCurrent(inst);
                else
                    address = getAddrPageZero(inst);
                if (I == INDIRECT) address = getIndirectAddress(address);

                if (isDebug) printf("[new PC = %o] ", (address & 07777));
                PC = address & 07777;
                break;

            case OP_IO:  // Input/Output Transfer
                PC = (PC + 1) & 07777;
                switch ((inst >> 3) & 077) {
                    case 003:  // Console keyboard input
                        switch (inst & 07) {
                            case 00:  // KCF
                                keyboardFlag = 0;
                                break;
                            case 01:  // KSF
                                // Always skip next instruction because scanf in
                                // C already waits for keyboard input
                                PC = (PC + 1) & 07777;
                                break;
                            case 02:  // KCC
                                keyboardFlag = 0;
                                AC = 0;
                                readCharacter();
                                break;
                            case 04:  // KRS
                                AC = AC | keyboardBuffer;
                                break;
                            case 06:  // KRB
                                keyboardFlag = 0;
                                readCharacter();
                                AC = keyboardBuffer;
                                break;
                        }
                        break;
                    case 004:  // Console keyboard output
                        switch (inst & 07) {
                            case 00:  // TFL
                                if (printerFlag == 0) {
                                    printerFlag = 1;
                                }
                                break;
                            case 01:  // TSF
                                if (printerFlag) {
                                    PC = (PC + 1) & 07777;
                                }
                                break;
                            case 02:  // TCF
                                printerFlag = 0;
                                break;
                            case 04:  // TPC
                                printerBuffer = AC & 00377;
                                printCharacter();
                                break;
                            case 06:  // TLS
                                printerFlag = 0;
                                printerBuffer = AC & 00377;
                                printCharacter();
                                break;
                        }
                        break;
                }
                break;

            case OP_MICRO:
                PC = (PC + 1) & 07777;

                // Microcoded operations
                if (I == 0) {  // Group 1
                    // CLA CLL
                    switch ((inst >> 6) & 00003) {
                        case 01:  // CLL
                            LK = 000000;
                            break;
                        case 02:  // CLA
                            AC = 00000;
                            break;
                        case 03:  // CLA CLL
                            AC = 00000;
                            LK = 000000;
                            break;
                    }

                    // CMA CML
                    switch ((inst >> 4) & 00003) {
                        case 01:  // CML
                            LK = LK ^ 010000;
                            break;
                        case 02:  // CMA
                            AC = AC ^ 007777;
                            break;
                        case 03:  // CMA CML
                            AC = AC ^ 007777;
                            LK = LK ^ 010000;
                            break;
                    }

                    // IAC
                    if (inst & 00001) {
                        AC = (AC | LK) + 1;
                        LK = AC & 010000;
                        AC = AC & 007777;
                    }

                    // RAR RAL BSW
                    switch ((inst >> 1) & 00007) {
                        case 00:  // nop
                            break;
                        case 01:  // BSW
                            AC = ((AC & 00077) << 6) | ((AC & 07700) >> 6);
                            break;
                        case 02:  // RAL
                            AC = (AC << 1) | (LK >> 12);
                            LK = AC & 010000;
                            AC = AC & 007777;
                            break;
                        case 03:  // RTL (RAL BSW)
                            AC = (AC << 2) | ((LK | AC) >> 11);
                            LK = AC & 010000;
                            AC = AC & 007777;
                            break;
                        case 04:  // RAR
                            AC = ((AC | LK) >> 1) | (AC << 12);
                            LK = AC & 010000;
                            AC = AC & 007777;
                            break;
                        case 05:  // RTR (RAR BSW)
                            AC = ((AC | LK) >> 2) | (AC << 11);
                            LK = AC & 010000;
                            AC = AC & 007777;
                            break;
                        case 06:  // RAR RAL
                            AC = AC & inst;
                            break;
                        case 07:  // RAR RAL BSW
                            break;
                    }
                } else {
                    switch (inst & 00011) {
                        case 000:  // Group 2, Or group
                            // SMA SZA SNL
                            switch ((inst >> 4) & 00007) {
                                case 00:  // nop
                                    break;
                                case 01:  // SNL
                                    if (LK) {
                                        PC = (PC + 1) & 07777;
                                    }
                                    break;
                                case 02:  // SZA
                                    if (AC == 0) {
                                        PC = (PC + 1) & 07777;
                                    }
                                    break;
                                case 03:  // SZA SNL
                                    if (AC == 0 || LK) {
                                        PC = (PC + 1) & 07777;
                                    }
                                    break;
                                case 04:  // SMA
                                    // If negative (sign bit is 1)
                                    if (AC & 04000) {
                                        PC = (PC + 1) & 07777;
                                    }
                                    break;
                                case 05:  // SMA SNL
                                    if ((AC & 04000) || LK) {
                                        PC = (PC + 1) & 07777;
                                    }
                                    break;
                                case 06:  // SMA SZA
                                    if ((AC & 04000) || (AC == 0)) {
                                        PC = (PC + 1) & 07777;
                                    }
                                    break;
                                case 07:  // SMA SZA SNL
                                    if ((AC & 04000) || (AC == 0) || LK) {
                                        PC = (PC + 1) & 07777;
                                    }
                                    break;
                            }

                            // CLA
                            if ((inst >> 7) & 00001) AC = 00000;

                            break;

                        case 010:  // Group 2, And group
                            // SPA SNA SZL
                            switch ((inst >> 4) & 00007) {
                                case 00:
                                    PC = (PC + 1) & 07777;
                                    break;
                                case 01:  // SZL
                                    if (LK == 0) {
                                        PC = (PC + 1) & 07777;
                                    }
                                    break;
                                case 02:  // SNA
                                    if (AC) {
                                        PC = (PC + 1) & 07777;
                                    }
                                    break;
                                case 03:  // SNA SZL
                                    if (AC && (LK == 0)) {
                                        PC = (PC + 1) & 07777;
                                    }
                                    break;
                                case 04:  // SPA
                                    // If positive (sign bit is 0)
                                    if ((AC & 04000) == 0) {
                                        PC = (PC + 1) & 07777;
                                    }
                                    break;
                                case 05:  // SPA SZL
                                    if (((AC & 04000) == 0) && LK == 0) {
                                        PC = (PC + 1) & 07777;
                                    }
                                    break;
                                case 06:  // SPA SNA
                                    if (((AC & 04000) == 0) && AC) {
                                        PC = (PC + 1) & 07777;
                                    }
                                    break;
                                case 07:  // SPA SNA SZL
                                    if (((AC & 04000) == 0) && AC &&
                                        (LK == 0)) {
                                        PC = (PC + 1) & 07777;
                                    }
                                    break;
                            }

                            // CLA
                            if ((inst >> 7) & 00001) AC = 00000;

                            break;

                        case 001:  // Group 3
                            break;
                        case 011:
                            break;
                    }
                }
                break;
        }
    }
}

uint16 getAddrPageZero(uint16 inst) { return ((inst & 0177)); }

uint16 getAddrPageCurrent(uint16 inst) {
    return ((inst & 0177) | (PC & 07600));
}

uint16 getIndirectAddress(uint16 address) {
    if ((address & 07770) == 00010) {  // address 010 to 017
        // autoindexed addressing
        if (isDebug)
            printf("[autoindex: address = M[%o] + 1 = %o + 1] ", address,
                   memory[address]);
        memory[address] = (memory[address] + 1) & 07777;
        address = memory[address];
    } else {
        // immediate addressing
        if (isDebug)
            printf("[immediate: address = M[%o] = %o] ", address,
                   memory[address]);
        address = (memory[address]) & 07777;
    }
    return address;
}

uint16 asciiToOctal(char c) {
    uint16 ascii = (int)c;
    return ascii - 48;
}

void readCharacter() {
    unsigned char input;
    scanf("%c", &input);
    keyboardBuffer = input;
    keyboardFlag = 1;
}

void printCharacter() {
    unsigned char ch = printerBuffer & 0177;
    printf("%c", ch);
    printerFlag = 1;
}

void printDebug() {
    char *op;
    unsigned char I_str = I ? 'I' : 'D';
    unsigned char page_str = page ? 'C' : 'Z';
    switch (inst >> 9) {
        case OP_AND:
            op = "OP_AND";
            break;
        case OP_TAD:
            op = "OP_TAD";
            break;
        case OP_ISZ:
            op = "OP_ISZ";
            break;
        case OP_DCA:
            op = "OP_DCA";
            break;
        case OP_JMS:
            op = "OP_JMS";
            break;
        case OP_JMP:
            op = "OP_JMP";
            break;
        case OP_IO:
            op = "OP_IO";
            break;
        case OP_MICRO:
            op = "OP_MICRO";
            I_str = '-';
            page_str = '-';
            break;
    }
    printf("PC: %o, INST: %o, AC: %o, LK: %o; (%s %c %c) ", PC, inst, AC, LK,
           op, I_str, page_str);
}

void printDebugMicro() {
    if (inst >> 9 != OP_MICRO) return;
    if (I == 0) {  // Group 1
        printf("Grp 1, ");
        // CLA CLL
        switch ((inst >> 6) & 00003) {
            case 01:  // CLL
                printf("CLL ");
                break;
            case 02:  // CLA
                printf("CLA ");
                break;
            case 03:  // CLA CLL
                printf("CLA CLL ");
                break;
        }

        // CMA CML
        switch ((inst >> 4) & 00003) {
            case 01:  // CML
                printf("CML ");
                break;
            case 02:  // CMA
                printf("CMA ");
                break;
            case 03:  // CMA CML
                printf("CMA CML ");
                break;
        }

        // IAC
        if (inst & 00001) {
            printf("IAC ");
        }

        // RAR RAL BSW
        switch ((inst >> 1) & 00007) {
            case 01:  // BSW
                printf("BSW ");
                break;
            case 02:  // RAL
                printf("RAL ");
                break;
            case 03:  // RTL (RAL BSW)
                printf("RTL (RAL BSW) ");
                break;
            case 04:  // RAR
                printf("RAR ");
                break;
            case 05:  // RTR (RAR BSW)
                printf("RTR (RAR BSW) ");
                break;
            case 06:  // RAR RAL
                printf("RAR RAL ");
                break;
            case 07:  // RAR RAL BSW
                printf("RAR RAL BSW ");
                break;
        }
    } else {
        switch (inst & 00011) {
            printf("Grp 2, or: ");
            case 000:  // Group 2, Or group
                // SMA SZA SNL
                switch ((inst >> 4) & 00007) {
                    case 00:
                        printf("NOP ");
                        break;
                    case 01:  // SNL
                        printf("SNL ");
                        break;
                    case 02:  // SZA
                        printf("SZA ");
                        break;
                    case 03:  // SZA SNL
                        printf("SZA SNL ");
                        break;
                    case 04:  // SMA
                        // If negative (sign bit is 1)
                        printf("SMA ");
                        break;
                    case 05:  // SMA SNL
                        printf("SMA SNL ");
                        break;
                    case 06:  // SMA SZA
                        printf("SMA SZA ");
                        break;
                    case 07:  // SMA SZA SNL
                        printf("SMA SZA SNL ");
                        break;
                }

                // CLA
                if ((inst >> 7) & 00001) printf("CLA ");

                break;

            case 010:  // Group 2, And group
                printf("Grp 2, and: ");
                // SPA SNA SZL
                switch ((inst >> 4) & 00007) {
                    case 00:
                        printf("unconditional skp ");
                        break;
                    case 01:  // SZL
                        printf("SZL ");
                        break;
                    case 02:  // SNA
                        printf("SNA ");
                        break;
                    case 03:  // SNA SZL
                        printf("SNA SZL ");
                        break;
                    case 04:  // SPA
                        // If positive (sign bit is 0)
                        printf("SPA ");
                        break;
                    case 05:  // SPA SZL
                        printf("SPA SZL ");
                        break;
                    case 06:  // SPA SNA
                        printf("SPA SNA ");
                        break;
                    case 07:  // SPA SNA SZL
                        printf("SPA SNA SZL ");
                        break;
                }

                // CLA
                if ((inst >> 7) & 00001) printf("CLA ");

                break;

            case 001:  // Group 3
                printf("Grp 3 ");
                break;
            case 011:
                break;
        }
    }
}