// Gameboy emulator.cpp : This file contains the 'main' function. Program execution begins and ends there.
//TODO: finish opcodes: HALT - 0x76

//**** DI INSTRUCTION -> pandocs and GCPUMANUAL incongruences. implementing pandocs instruction
#define _CRT_SECURE_NO_WARNINGS
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "SDL.h"
#include "SDL_image.h"

enum Constants
{
    CLOCKSPEED = 4194304,
    FREQUENCY_00 = 4096,
    FREQUENCY_01 = 262144,
    FREQUENCY_10 = 65536,
    FREQUENCY_11 = 16384,
    //Timer counter(R/W), incremented by a clock frequency.
    TIMA = 0xFF05,
    //Timer modulo(R/W), When the TIMA overflows, this data will be loaded.
    TMA = 0xFF06,
    //Timer control (R/W), bit 2 stops(0) and starts(1) the timer, bit 1 and 0 controls the frequencies
    TMC = 0xFF07,
    //Divider register(R/W), incremented at a rate of 16384hz, writing anything at this location resets the register.
    DIV = 0xFF04,
    //LCDC Y-Coordinate (R), indicates the vertical line to which the present data is transfered to the LCD Driver, 0 <= LY <= 153
    LY = 0xFF44,
    //STAT LCDC Status (R/W), bits 6-3 controls interrupts, bit 2 is a coincidence flag (LYC != LY (0)) or (LYC == LY(1)) and bits 1 and 0 
    //tells which mode the LCD is on. HBlank (00) ----- VBLANK (01) ----- OAM SEARCH (10) ----- LCD DATA TRANSFER (11)
    STAT = 0xFF41,
    //Interrupt Enable (R/W), enable the interrupt especified by bit value.
    //BIT 0 - V-Blank  Interrupt Enable  (INT 40h)  (1=Enable).
    //BIT 1 - LCD STAT Interrupt Enable  (INT 48h)  (1=Enable).
    //BIT 2 - TIMER Interrupt Enable  (INT 50h)  (1=Enable).
    //BIT 3 - Serial Interrupt Enable  (INT 58h)  (1=Enable).
    //BIT 4 - Joypad Interrupt Enable  (INT 60h)  (1=Enable).
    IE = 0xFFFF,
    //Interrupt FLag (R/W), requests the interrupt especified by bit value.
    //BIT 0 - V-Blank  Interrupt Enable  (INT 40h)  (1=Enable).
    //BIT 1 - LCD STAT Interrupt Enable  (INT 48h)  (1=Enable).
    //BIT 2 - TIMER Interrupt Enable  (INT 50h)  (1=Enable).
    //BIT 3 - Serial Interrupt Enable  (INT 58h)  (1=Enable).
    //BIT 4 - Joypad Interrupt Enable  (INT 60h)  (1=Enable).
    IF = 0xFF0F,
    //LY Compare (R/W), the LYC compares itself with the LY. if the values are the same it causes the STAT to set the coincident flag
    LYC = 0xFF45,
    //LCD Control (R/W)
    LCDC = 0xFF40
};

union registerAF
{
    struct
    {
        unsigned char F, A;
    };
    unsigned short AF;
};

union registerBC
{
    struct
    {
        unsigned char C, B;
    };
    unsigned short BC;
};

union registerDE
{
    struct
    {
        unsigned char E, D;
    };
    unsigned short DE;
};

union registerHL
{
    struct
    {
        unsigned char L, H;
    };
    unsigned short HL;
};

//cpu
unsigned char memory[0xFFFF + 1] = { 0 };//65536 bytes 
unsigned short opcode;
unsigned char LCDcontroller;
//////////////////////////////////////////////////REGISTERS//////////////////////////////////////////////////////////////////////////////////////////////
union registerAF AF;
union registerBC BC;
union registerDE DE;
union registerHL HL;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//register F = Z, N, H, C, 0, 0, 0, 0;       -> flags
//////////////////////////////////////////////////FLAG REGISTER//////////////////////////////////////////////////////////////////////////////////////////
//functionality of the Z, N, H, C, bits of the flag register;
//Zero Flag (Z): This bit is set when the result of a math operation is zero or two values match when using the CP instruction
//Subtract Flag (N): This bit is set if a subtraction was performed in the last math instruction
//Half Carry FLag(H): This bit is set if a carry occurred from the lower nibble in the last math operation
//Carry Flag(C): This bit is set if a carry occurred from the last math operation or if register A is the smaller value when executing the CP instruction
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
unsigned short pc;//program counter
const unsigned int PCStart = 0x100;//the position in memory that pc starts
unsigned short I;//index counter
unsigned short sp;//stack pointer
const unsigned int StackStart = 0xFFFE;//differing from chip8 where we implemented the stack with stack[16], in gameboy the stack is already implemented
                                       //and it begins in this memory location. the POP function increments the pointer in 2 and the PUT function   
                                       //decrements it by 2;

//video and timings
unsigned char shownScreen[160][144];
unsigned char wholeScreen[256][256];
//scanlineCounter uses a 16 bit variable to represent 456 clocks, so when it becomes lower than 0 it will wrap to 456;
//everytime it wraps around we update the LY register ($ff44) by 1, the range of the LY register is 0-153;
int scanlineCounter = 0;
//cpu does 4194304 cycles in a second and it renders in 60FPS.
const int maxCycleBeforeRender = CLOCKSPEED / 60;
//how many cycles the cpu did in 1 milisecond
int cyclesBeforeLCDRender = 0; 
//the counter is found by the equation CLOCKSPEED/FREQUENCY, clockspeed is 4194304 and the frequency is set by the TMC Register (4096 in startup).
int timerCounter = CLOCKSPEED / FREQUENCY_00;
//the counter of divider register
int divCounter = CLOCKSPEED / FREQUENCY_11;
//the cycles the divider register was on until now
int dividerRegisterCycles = 0;

//interrupts
bool masterInterrupt = false; //Interrupt Master Enable Flag - IME
//when the cpu executes the enable interrupt instruction it will only take effect in the next instruction we need a delay to activate the IME
bool delayMasterInterrupt = false; 

//HALT
bool halt = false;

//SDL
SDL_Window* window = NULL;
SDL_Renderer* renderer = NULL;
bool drawFlag = false;
bool isRunning;

//////////////////////////////////////////////////FUNCTIONS//////////////////////////////////////////////////////////////////////////////////////////////
void print_binary(int number);
//sdl
void drawGraphics();
void setupGraphics();
//gameboy
void initialize();
void loadGame(char* gameName);
void emulateCycle();
void addRegister(unsigned char* registerA, unsigned char* registerB, unsigned char cycles);
void adcRegister(unsigned char* registerA, unsigned char* registerB, unsigned char cycles);
void subRegister(unsigned char* registerA, unsigned char* registerB, unsigned char cycles);
void sbcRegister(unsigned char* registerA, unsigned char* registerB, unsigned char cycles);
void andOperation(unsigned char* registerB, unsigned char cycles);
void orOperation(unsigned char* registerB, unsigned char cycles);
void xorOperation(unsigned char* registerB, unsigned char cycles);
void cpRegister(unsigned char* registerB, unsigned char cycles);
void incRegister(unsigned char* registerA, unsigned char cycles);
void decRegister(unsigned char* registerA, unsigned char cycles);
void addRegister16Bit(unsigned short* registerA, unsigned short* registerB, unsigned char cycles);
void swap(unsigned char* registerA, unsigned char cycles);
void rlcRegister(unsigned char* registerA, unsigned char cycles);
void RLCA(unsigned char cycles);
void rlRegister(unsigned char* registerA, unsigned char cycles);
void RLA(unsigned char cycles);
void rrcRegister(unsigned char* registerA, unsigned char cycles);
void RRCA(unsigned char cycles);
void rrRegister(unsigned char* registerA, unsigned char cycles);
void RRA(unsigned char cycles);
void SLA(unsigned char* registerA, unsigned char cycles);
void SRA(unsigned char* registerA, unsigned char cycles);
void SRL(unsigned char* registerA, unsigned char cycles);
void BIT(unsigned char* registerA, unsigned char bit, unsigned char cycles);
void SET(unsigned char* registerA, unsigned char bit, unsigned char cycles);
void RES(unsigned char* registerA, unsigned char bit, unsigned char cycles);
bool NZCondition();
bool ZCondition();
bool NCCondition();
bool CCondition();
void JP(bool condition, unsigned char cycles);
void JR(bool condition, unsigned char cycles);
void CALL(bool condition, unsigned char cycles);
void RET(bool condition, unsigned char cycles);
void RETI(unsigned char cycles);
void PUSH(unsigned char* highByte, unsigned char* lowByte);
void PUSHAF();
void POP(unsigned char* highByte, unsigned char* lowByte);
void POPAF();
void clockTiming(unsigned char cycles);
void updateTimers(unsigned char cycles);
void writeInMemory(unsigned short memoryLocation, unsigned char registerA);
void readMemory(unsigned short memoryLocation);
void setClockFrequency();
void setLCDSTAT();
void drawScanLine();
void doInterrupts();
void setInterruptAddress(unsigned char bit);
void requestInterrupt(unsigned char bit);
bool testBit(unsigned char data, unsigned char bit);
void dmaTransfer(unsigned char data);

int main(int argc, char* argv[])
{
    initialize();
    loadGame("oi");
    setupGraphics();
    
    for (;;)
    {
        cyclesBeforeLCDRender = 0;
        while (cyclesBeforeLCDRender < maxCycleBeforeRender)
        {
            emulateCycle();
            doInterrupts();
        }   
        //clock cycle timing
        //printf("%c-------%c\n", memory[0xFF01], memory[0xFF02]);
        if (memory[pc] == 0x18)
        {
            drawGraphics();
        }
    }
    SDL_Delay(5000);
    //printf("%c-------%c\n", memory[0xFF01], memory[0xFF02]);
}

void initialize()
{
    //Initializing PC
    pc = PCStart;

    //initializing registers
    /*AF.AF = 0x01;
    AF.F = 0xB0;
    BC.BC = 0x0013;
    DE.DE = 0x00D8;
    HL.HL = 0x014D;*/
    memory[0xFF05] = 0x00; //TIMA
    memory[0xFF06] = 0x00; //TMA
    memory[0xFF07] = 0x00; //TAC
    memory[0xFF10] = 0x80; //NR10
    memory[0xFF11] = 0xBF; //NR11
    memory[0xFF12] = 0xF3; //NR12
    memory[0xFF14] = 0xBF; //NR14
    memory[0xFF16] = 0x3F; //NR21
    memory[0xFF17] = 0x00; //NR22
    memory[0xFF19] = 0xBF; //NR24
    memory[0xFF1A] = 0x7F; //NR30
    memory[0xFF1B] = 0xFF; //NR31
    memory[0xFF1C] = 0x9F; //NR32
    memory[0xFF1E] = 0xBF; //NR33
    memory[0xFF20] = 0xFF; //NR41
    memory[0xFF21] = 0x00; //NR42
    memory[0xFF22] = 0x00; //NR43
    memory[0xFF23] = 0xBF; //NR30
    memory[0xFF24] = 0x77; //NR50
    memory[0xFF25] = 0xF3; //NR51
    memory[0xFF26] = 0xF1; //the value is different for GB and SGB; -> NR52
    memory[0xFF40] = 0x91; //LCDC
    memory[0xFF42] = 0x00; //SCY
    memory[0xFF43] = 0x00; //SCX
    memory[0xFF45] = 0x00; //LYC
    memory[0xFF47] = 0xFC; //BGP
    memory[0xFF48] = 0xFF; //0BP0
    memory[0xFF49] = 0xFF; //0BP1
    memory[0xFF4A] = 0x00; //WY
    memory[0xFF4B] = 0x00; //WX
    memory[0xFFFF] = 0x00; //IE
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    //register of ld 06 test
    AF.AF = 0x1180;
    BC.BC = 0;
    DE.DE = 0xFF56;
    HL.HL = 0x000D;
    scanlineCounter = 288;
    memory[LCDC] = 0x91;
    memory[0xFF44] = 0x90; //LY
    memory[0xFF47] = 0xFC; // BGP
    memory[STAT] = 0x81; //LCD STAT
    memory[0xFF02] = 0x7C; //SC
    memory[0xFF04] = 0x1E; //DIV
    memory[0xFF07] = 0xF8; //TAC
    memory[0xFF0F] = 0xE1; //SC
    memory[0xFF70] = 0xF8; // SVBK
    memory[0xFF4F] = 0xFE; //VBK
    memory[0xFF4D] = 0x7E; //KEY1
    memory[0xFF01] = 0; // SB
    memory[0xFF02] = 0x7C; //SC
    memory[0xFF10] = 0x80; //ENT1
    memory[0xFF11] = 0xBF; //LEN1
    memory[0xFF12] = 0xF3; //ENV1
    memory[0xFF13] = 0xC1; //FRQ1
    memory[0xFF14] = 0xBF; //KIK1
    memory[0xFF15] = 0xFF; //N/A
    memory[0xFF16] = 0x3F; //LEN2
    memory[0xFF17] = 0; //ENV2
    memory[0xFF18] = 0; //FRQ2
    memory[0xFF19] = 0xB8; //KIK2
    memory[0xFF1A] = 0x7F; //ON_3
    memory[0xFF1B] = 0xFF; //LEN3
    memory[0xFF1C] = 0x9F; //ENV3
    memory[0xFF1D] = 0; //FRQ3
    memory[0xFF1E] = 0xB8; //KIK3
    memory[0xFF1F] = 0xFF; //N/A
    memory[0xFF20] = 0xFF; //LEN4
    memory[0xFF21] = 0; //ENV4
    memory[0xFF22] = 0; //FRQ4
    memory[0xFF23] = 0xBF; //KIK4
    memory[0xFF24] = 0x77; //VOL
    memory[0xFF25] = 0xF3; //L/R
    memory[0xFF26] = 0xF1; //ON
    memory[0xFF51] = 0xFF; //src1
    memory[0xFF52] = 0xFF; //src2
    memory[0xFF53] = 0xFF; //dest1
    memory[0xFF54] = 0xFF; //dest2
    memory[0xFF4F] = 0; //VRAM gbc bank
    memory[0xFF70] = 1; //WRAM gbc bank
    memory[0xFF68] = 0xC0; //BCPS - GBC pal
    memory[0xFF69] = 0xFF; //BCPD - GBC pal
    memory[0xFF6A] = 0xC1; //OCPS - GBC pal
    memory[0xFF6B] = 0x46; //OCPD - GBC pal
    //
    memory[0xFF80] = 0xCE;
    memory[0xFF81] = 0xED;
    memory[0xFF82] = 0x66;
    memory[0xFF83] = 0x66;
    memory[0xFF84] = 0xCC;
    memory[0xFF85] = 0x0D;
    memory[0xFF87] = 0x0B;
    memory[0xFF88] = 0x03;
    memory[0xFF89] = 0x73;
    memory[0xFF8B] = 0x83;
    memory[0xFF8D] = 0x0C;
    memory[0xFF8F] = 0x0D;
    //
    memory[IF] = 0xE1;
    memory[IE] = 0;
    //starting stack
    sp = StackStart;
}

void loadGame(char* gameName)
{
    //opening file in binary form
    FILE* file = fopen("C:\\Users\\xerather\\source\\repos\\Gameboy emulator\\Gameboy emulator\\Tests\\02-interrupts.gb", "rb");
    if (file == NULL) {
        printf("File not found");
        exit(EXIT_FAILURE);
    }
    else
    {
        //going to end of file
        fseek(file, 0, SEEK_END);
        //getting size of file
        long bufferSize = ftell(file);
        printf("file size: %d", bufferSize);
        //SDL_Delay(2000);
        if (bufferSize > (0xFFFF))
        {
            printf("size of file is too big");
            exit(EXIT_FAILURE);
        }
        //going to beginning of file
        rewind(file);
        //setting the size of char array and reading the binary file in unsigned char form
        char* in = (char*)malloc(sizeof(char) * bufferSize);
        if (in == NULL)
        {
            printf("out of memory\n");
            exit(EXIT_FAILURE);
        }
        //reading file
        size_t result = fread(in, sizeof(unsigned char), bufferSize, file);
        if (result != bufferSize)
        {
            printf("reading error\n");
            //SDL_Delay(5000);
        }
        //populating memory
        for (int i = 0; i < bufferSize; i++)
        {
            memory[i] = in[i];
        }

        fclose(file);
        free(in);
    }
}

void emulateCycle()
{
    //C01C, C088, cc50, C913, c7f9(the PC that it writes the first line in the screen), c24f
    //020F - start of test 9
    //CB10 - no problem     
    //c06a -> start of srl, rr, rr, rra - clear
    //c01a ->
    //c007
    //pc == 0xc02A, HL.HL = 0X9000, memory[LY] == 0x6B -> ADC and SBC wasnt implementing carry bit when checking the flags
    //pc == 0xc2f6 -> writing to 0xFFFF not working, HL.HL == 0x9000, AF.AF == 0x0080, memory[0xFFFF] == 0;
    //pc == 0xc01c || pc == 0xc01F || pc ==0xc02a
    //pc == 0xc464, AF.AF == 0x0080, BC.BC == 0, DE.DE == 0, HL.HL == 0x8000 -> separate RRA instruction from RR instruction,
    //the flags are different. after fixing the instructions the position DFF9 is differente (FF00 mine) (0000 BGB)
    //AF.AF == 0x00f0 && BC.BC == 0x0001 && DE.DE == 0x1f7f && pc == 0xDEF8
    //pc == 0xDEF8 -> this is where GBG tests the opcodes.
    opcode = memory[pc];
    //memory[0xdef8] == 0x88
    //printf("checking opcode: [%X]\n", opcode);
    switch (opcode & 0xFF)
    {
        case 0x10:
            //implement stop
            pc++;
            break;
        case 0x00:
            clockTiming(4);
            break;
        case 0x06:
            pc++;
            BC.B = memory[pc];
            clockTiming(8);
            break;
        case 0x0E:
            pc++;
            BC.C = memory[pc];
            clockTiming(8);
            break;
        case 0x16:
            pc++;
            DE.D = memory[pc];
            clockTiming(8);
            break;
        case 0x1E:
            pc++;
            DE.E = memory[pc];
            clockTiming(8);
            break;
        case 0x26:
            pc++;
            HL.H = memory[pc];
            clockTiming(8);
            break;
        case 0x2E:
            pc++;
            HL.L = memory[pc];
            clockTiming(8);
            break;
        case 0x7F:
            AF.A = AF.A;
            clockTiming(4);
            break;
        case 0x78:
            AF.A = BC.B;
            clockTiming(4);
            break;
        case 0x79:
            AF.A = BC.C;   
            clockTiming(4);
            break;
        case 0x7A:
            AF.A = DE.D;     
            clockTiming(4);
            break;
        case 0x7B:
            AF.A = DE.E;       
            clockTiming(4);
            break;
        case 0x7C:
            AF.A = HL.H;   
            clockTiming(4);
            break;
        case 0x7D:
            AF.A = HL.L;
            clockTiming(4);
            break;
        case 0x7E:
            AF.A = memory[HL.HL];
            clockTiming(8);
            break;
        case 0x40:
            BC.B = BC.B;
            clockTiming(4);
            break;
        case 0x41:
            BC.B = BC.C;
            clockTiming(4);
            break;
        case 0x42:
            BC.B = DE.D;
            clockTiming(4);
            break;
        case 0x43:
            BC.B = DE.E;
            clockTiming(4);
            break;
        case 0x44:
            BC.B = HL.H;
            clockTiming(4);
            break;
        case 0x45:
            BC.B = HL.L;
            clockTiming(4);
            break;
        case 0x46:
            BC.B = memory[HL.HL];
            clockTiming(8);
            break;
        case 0x48:
            BC.C = BC.B;
            clockTiming(4);
            break;
        case 0x49:
            BC.C = BC.C;
            clockTiming(4);
            break;
        case 0x4A:
            BC.C = DE.D;
            clockTiming(4);
            break;
        case 0x4B:
            BC.C = DE.E;
            clockTiming(4);
            break;
        case 0x4C:
            BC.C = HL.H;
            clockTiming(4);
            break;
        case 0x4D:
            BC.C = HL.L;
            clockTiming(4);
            break;
        case 0x4E:
            BC.C = memory[HL.HL];
            clockTiming(8);
            break;
        case 0x50:
            DE.D = BC.B;
            clockTiming(4);
            break;
        case 0x51:
            DE.D = BC.C;
            clockTiming(4);
            break;
        case 0x52:
            DE.D = DE.D;
            clockTiming(4);
            break;
        case 0x53:
            DE.D = DE.E;
            clockTiming(4);
            break;
        case 0x54:
            DE.D = HL.H;
            clockTiming(4);
            break;
        case 0x55:
            DE.D = HL.L;
            clockTiming(4);
            break;
        case 0x56:
            DE.D = memory[HL.HL];
            clockTiming(8);
            break;
        case 0x58:
            DE.E = BC.B;
            clockTiming(4);
            break;
        case 0x59:
            DE.E = BC.C;
            clockTiming(4);
            break;
        case 0x5A:
            DE.E = DE.D;
            clockTiming(4);
            break;
        case 0x5B:
            DE.E = DE.E;
            clockTiming(4);
            break;
        case 0x5C:
            DE.E = HL.H;
            clockTiming(4);
            break;
        case 0x5D:
            DE.E = HL.L;
            clockTiming(4);
            break;
        case 0x5E:
            DE.E = memory[HL.HL];
            clockTiming(8);
            break;
        case 0x60:
            HL.H = BC.B;
            clockTiming(4);
            break;
        case 0x61:
            HL.H = BC.C;
            clockTiming(4);
            break;
        case 0x62:
            HL.H = DE.D;
            clockTiming(4);
            break;
        case 0x63:
            HL.H = DE.E;
            clockTiming(4);
            break;
        case 0x64:
            HL.H = HL.H;
            clockTiming(4);
            break;
        case 0x65:
            HL.H = HL.L;
            clockTiming(4);
            break;
        case 0x66:
            HL.H = memory[HL.HL];
            clockTiming(8);
            break;
        case 0x68:
            HL.L = BC.B;
            clockTiming(4);
            break;
        case 0x69:
            HL.L = BC.C;
            clockTiming(4);
            break;
        case 0x6A:
            HL.L = DE.D;
            clockTiming(4);
            break;
        case 0x6B:
            HL.L = DE.E;
            clockTiming(4);
            break;
        case 0x6C:
            HL.L = HL.H;
            clockTiming(4);
            break;
        case 0x6D:
            HL.L = HL.L;
            clockTiming(4);
            break;
        case 0x6E:
            HL.L = memory[HL.HL];
            clockTiming(8);
            break;
        case 0x70:
            writeInMemory(HL.HL, BC.B);
            clockTiming(8);
            break;
        case 0x71:
            writeInMemory(HL.HL, BC.C);
            clockTiming(8);
            break;
        case 0x72:
            writeInMemory(HL.HL, DE.D);
            clockTiming(8);
            break;
        case 0x73:
            writeInMemory(HL.HL, DE.E);
            clockTiming(8);
            break;
        case 0x74:
            writeInMemory(HL.HL, HL.H);
            clockTiming(8);
            break;
        case 0x75:
            writeInMemory(HL.HL, HL.L);
            clockTiming(8);
            break;
        case 0x36:
            pc++;
            writeInMemory(HL.HL, memory[pc]);
            clockTiming(12);
            break;
        case 0x0A:
            AF.A = memory[BC.BC];
            clockTiming(8);
            break;
        case 0x1A:
            AF.A = memory[DE.DE];
            clockTiming(8);
            break;
        case 0xFA:
        {
            pc++;
            unsigned char LowerNibble = memory[pc];
            pc++;
            unsigned char HighNibble = memory[pc];
            unsigned short memoryLocation = (HighNibble << 8);
            memoryLocation |= LowerNibble;
            AF.A = memory[memoryLocation];
            clockTiming(16);
            break;
        } 
        case 0x3E:
            pc++;
            AF.A = memory[pc];
            clockTiming(8);
            break;
        case 0x47:
            BC.B = AF.A;
            clockTiming(4);
            break;
        case 0x4F:
            BC.C = AF.A;
            clockTiming(4);
            break;
        case 0x57:
            DE.D = AF.A;
            clockTiming(4);
            break;
        case 0x5F:
            DE.E = AF.A;
            clockTiming(4);
            break;
        case 0x67:
            HL.H = AF.A;
            clockTiming(4);
            break;
        case 0x6F:
            HL.L = AF.A;
            clockTiming(4);
            break;
        case 0x02:
            writeInMemory(BC.BC, AF.A);
            clockTiming(8);
            break;
        case 0x12:
            writeInMemory(DE.DE, AF.A);
            clockTiming(8);
            break;
        case 0x77:
            writeInMemory(HL.HL, AF.A);
            clockTiming(8);
            break;
        case 0xEA:
        {
            pc++;
            unsigned char LowerNibble = memory[pc];
            pc++;
            unsigned char HighNibble = memory[pc];
            unsigned short memoryLocation = (HighNibble << 8);
            memoryLocation |= LowerNibble;
            writeInMemory(memoryLocation, AF.A);
            clockTiming(16);
            break;
        }
        case 0xF2:
        {
            unsigned short memoryLocation = 0xFF00 + BC.C;
            AF.A = memory[memoryLocation];
            clockTiming(8);
            break;
        }
        case 0xE2:
        {
            unsigned short memoryLocation = 0xFF00 + BC.C;
            writeInMemory(memoryLocation, AF.A);
            clockTiming(8);
            break;
        }
        case 0x3A:
            AF.A = memory[HL.HL];
            HL.HL--;
            clockTiming(8);
            break;
        case 0x32:
            writeInMemory(HL.HL, AF.A);
            HL.HL--;
            clockTiming(8);
            break;
        case 0x2A:
            AF.A = memory[HL.HL];
            HL.HL++;
            clockTiming(8);
            break;
        case 0x22:
            writeInMemory(HL.HL, AF.A);
            HL.HL++;
            clockTiming(8);
            break;
        case 0xE0:
        {
            pc++;
            unsigned short memoryLocation = 0xFF00 + memory[pc];
            writeInMemory(memoryLocation, AF.A);
            clockTiming(12);
            break;
        }
        case 0xF0:
        {
            pc++;
            unsigned short memoryLocation = 0xFF00 + memory[pc];
            AF.A = memory[memoryLocation];
            clockTiming(12);
            break;
        }
        case 0x01:
            pc++;
            BC.C = memory[pc];
            pc++;
            BC.B = memory[pc];
            clockTiming(12);
            break;
        case 0x11:
            pc++;
            DE.E = memory[pc];
            pc++;
            DE.D = memory[pc];
            clockTiming(12);
            break;
        case 0x21:
            pc++;
            HL.L = memory[pc];
            pc++;
            HL.H = memory[pc];
            clockTiming(12);
            break;
        case 0x31:
        {
            pc++;
            unsigned char LowerNibble = memory[pc];
            pc++;
            unsigned char HighNibble = memory[pc];
            unsigned short memoryLocation = (HighNibble << 8);
            memoryLocation |= LowerNibble;
            sp = memoryLocation;
            clockTiming(12);
            break;
        }
        case 0xF9:
            sp = HL.HL;
            clockTiming(8);
            break;
        case 0xF8:
        {
            //reseting flags
            AF.F = 0;
            //getting immediate signed data in the form of signed char
            pc++;
            signed char e8value = memory[pc];
            //the test for the CFLAG needs to use 8 total bits, in the signed form the seventh bit is thrown out as a sign value so if you have a signed
            //value of 0xFF it will be -127 instead of 255.
            unsigned char unsigned_e8value = e8value;
            //getting the value of 3...0 bits in SP;
            unsigned char HFlagCheck = sp & 0xF;
            //getting the value of 7...0 bits in SP;
            unsigned char CFlagCheck = sp & 0xFF;
            //checking if theres overflow from bit 3 (H flag)
            if ((HFlagCheck + (e8value & 0xF)) > 0xF)
            {
                AF.F |= 0b00100000;
            }
            // checking if theres overflow from bit 7
            if ((CFlagCheck + unsigned_e8value) > 0xFF)
            {
                AF.F |= 0b00010000;
            }
            HL.HL = (sp + e8value);
            clockTiming(12);
            break;
        }
        case 0x08:
        {
            pc++;
            unsigned char LowerNibble = memory[pc];
            pc++;
            unsigned char HighNibble = memory[pc];
            unsigned short memoryLocation = (HighNibble << 8);
            memoryLocation |= LowerNibble;
            writeInMemory(memoryLocation, sp & 0xFF);
            writeInMemory(memoryLocation + 1, (sp >> 8));
            clockTiming(20);
            break;
        }
        case 0xF5:
            PUSHAF();
            break;
        case 0xC5:
            PUSH(&BC.B, &BC.C);
            break;
        case 0xD5:
            PUSH(&DE.D, &DE.E);
            break;
        case 0xE5:
            PUSH(&HL.H, &HL.L);
            break;
        case 0xF1:
            //pc != 0xc018
            POPAF();
            break;
        case 0xC1:
            POP(&BC.B, &BC.C);
            break;
        case 0xD1:
            POP(&DE.D, &DE.E);
            break;
        case 0xE1:
            POP(&HL.H, &HL.L);
            break;
        case 0x87:
            addRegister(&AF.A, &AF.A, 4);
            break;
        case 0x80:
            addRegister(&AF.A, &BC.B, 4);
            break;
        case 0x81:
            addRegister(&AF.A, &BC.C, 4);
            break;
        case 0x82:
            addRegister(&AF.A, &DE.D, 4);
            break;
        case 0x83:
            addRegister(&AF.A, &DE.E, 4);
            break;
        case 0x84:
            addRegister(&AF.A, &HL.H, 4);
            break;
        case 0x85:
            addRegister(&AF.A, &HL.L, 4);
            break;
        case 0x86:
            addRegister(&AF.A, &memory[HL.HL], 8);
            break;
        case 0xC6:
            pc++;
            addRegister(&AF.A, &memory[pc], 8);
            break;
        case 0x8F:
            adcRegister(&AF.A, &AF.A, 4);
            break;
        case 0x88:
            adcRegister(&AF.A, &BC.B, 4);
            break;
        case 0x89:
            adcRegister(&AF.A, &BC.C, 4);
            break;
        case 0x8A:
            adcRegister(&AF.A, &DE.D, 4);
            break;
        case 0x8B:
            adcRegister(&AF.A, &DE.E, 4);
            break;
        case 0x8C:
            adcRegister(&AF.A, &HL.H, 4);
            break;
        case 0x8D:
            adcRegister(&AF.A, &HL.L, 4);
            break;
        case 0x8E:
            adcRegister(&AF.A, &memory[HL.HL], 8);
            break;
        case 0xCE:
            pc++;
            adcRegister(&AF.A, &memory[pc], 8);
            break;
        case 0x97:
            subRegister(&AF.A, &AF.A, 4);
            break;
        case 0x90:
            subRegister(&AF.A, &BC.B, 4);
            break;
        case 0x91:
            subRegister(&AF.A, &BC.C, 4);
            break;
        case 0x92:
            subRegister(&AF.A, &DE.D, 4);
            break;
        case 0x93:
            subRegister(&AF.A, &DE.E, 4);
            break;
        case 0x94:
            subRegister(&AF.A, &HL.H, 4);
            break;
        case 0x95:
            subRegister(&AF.A, &HL.L, 4);
            break;
        case 0x96:
            subRegister(&AF.A, &memory[HL.HL], 8);
            break;
        case 0xD6:
            pc++;
            subRegister(&AF.A, &memory[pc], 8);
            break;
        case 0x9F:
            sbcRegister(&AF.A, &AF.A, 4);
            break;
        case 0x98:
            sbcRegister(&AF.A, &BC.B, 4);
            break;
        case 0x99:
            sbcRegister(&AF.A, &BC.C, 4);
            break;
        case 0x9A:
            sbcRegister(&AF.A, &DE.D, 4);
            break;
        case 0x9B:
            sbcRegister(&AF.A, &DE.E, 4);
            break;
        case 0x9C:
            sbcRegister(&AF.A, &HL.H, 4);
            break;
        case 0x9D:
            sbcRegister(&AF.A, &HL.L, 4);
            break;
        case 0x9E:
            sbcRegister(&AF.A, &memory[HL.HL], 8);
            break;
        case 0xDE:
            pc++;
            sbcRegister(&AF.A, &memory[pc], 8);
            break;
        case 0xA7:
            andOperation(&AF.A, 4);
            break;
        case 0xA0:
            andOperation(&BC.B, 4);
            break;
        case 0xA1:
            andOperation(&BC.C, 4);
            break;
        case 0xA2:
            andOperation(&DE.D, 4);
            break;
        case 0xA3:
            andOperation(&DE.E, 4);
            break;
        case 0xA4:
            andOperation(&HL.H, 4);
            break;
        case 0xA5:
            andOperation(&HL.L, 4);
            break;
        case 0xA6:
            andOperation(&memory[HL.HL], 8);
            break;
        case 0xE6:
            pc++;
            andOperation(&memory[pc], 8);
            break;
        case 0xB7:
            orOperation(&AF.A, 4);
            break;
        case 0xB0:
            orOperation(&BC.B, 4);
            break;
        case 0xB1:
            orOperation(&BC.C, 4);
            break;
        case 0xB2:
            orOperation(&DE.D, 4);
            break;
        case 0xB3:
            orOperation(&DE.E, 4);
            break;
        case 0xB4:
            orOperation(&HL.H, 4);
            break;
        case 0xB5:
            orOperation(&HL.L, 4);
            break;
        case 0xB6:
            orOperation(&memory[HL.HL], 8);
            break;
        case 0xF6:
            pc++;
            orOperation(&memory[pc], 8);
            break;
        case 0xAF:
            xorOperation(&AF.A, 4);
            break;
        case 0xA8:
            xorOperation(&BC.B, 4);
            break;
        case 0xA9:
            xorOperation(&BC.C, 4);
            break;
        case 0xAA:
            xorOperation(&DE.D, 4);
            break;
        case 0xAB:
            xorOperation(&DE.E, 4);
            break;
        case 0xAC:
            xorOperation(&HL.H, 4);
            break;
        case 0xAD:
            xorOperation(&HL.L, 4);
            break;
        case 0xAE:
            xorOperation(&memory[HL.HL], 8);
            break;
        case 0xEE:
            pc++;
            xorOperation(&memory[pc], 8);
            break;
        case 0xBF:
            cpRegister(&AF.A, 4);
            break;
        case 0xB8:
            cpRegister(&BC.B, 4);
            break;
        case 0xB9:
            cpRegister(&BC.C, 4);
            break;
        case 0xBA:
            cpRegister(&DE.D, 4);
            break;
        case 0xBB:
            cpRegister(&DE.E, 4);
            break;
        case 0xBC:
            cpRegister(&HL.H, 4);
            break;
        case 0xBD:
            cpRegister(&HL.L, 4);
            break;
        case 0xBE:
            cpRegister(&memory[HL.HL], 8);
            break;
        case 0xFE:
            //pc != 0xc368
            pc++;
            cpRegister(&memory[pc], 8);
            break;
        case 0x3C:
            incRegister(&AF.A, 4);
            break;
        case 0x04:
            incRegister(&BC.B, 4);
            break;
        case 0x0C:
            incRegister(&BC.C, 4);
            break;
        case 0x14:
            incRegister(&DE.D, 4);
            break;
        case 0x1C:
            incRegister(&DE.E, 4);
            break;
        case 0x24:
            incRegister(&HL.H, 4);
            break;
        case 0x2C:
            incRegister(&HL.L, 4);
            break;
        case 0x34:
            incRegister(&memory[HL.HL], 12);
            break;
        case 0x3D:
            decRegister(&AF.A, 4);
            break;
        case 0x05:
            decRegister(&BC.B, 4);
            break;
        case 0x0D:
            decRegister(&BC.C, 4);
            break;
        case 0x15:
            decRegister(&DE.D, 4);
            break;
        case 0x1D:
            decRegister(&DE.E, 4);
            break;
        case 0x25:
            decRegister(&HL.H, 4);
            break;
        case 0x2D:
            decRegister(&HL.L, 4);
            break;
        case 0x35:
            decRegister(&memory[HL.HL], 12);
            break;
        case 0x09:
            addRegister16Bit(&HL.HL, &BC.BC, 8);
            break;
        case 0x19:
            addRegister16Bit(&HL.HL, &DE.DE, 8);
            break;
        case 0x29:
            addRegister16Bit(&HL.HL, &HL.HL, 8);
            break;
        case 0x39:
            addRegister16Bit(&HL.HL, &sp, 8);
            break;
        case 0xE8:
        {
            //resetting Flags;
            AF.F = 0;
            pc++;
            signed char e8value = memory[pc];
            //the test for the CFLAG needs to use 8 total bits, in the signed form the seventh bit is thrown out as a sign value so if you have a signed
            //value of 0xFF it will be -127 instead of 255.
            unsigned char unsigned_e8value = e8value;
            //getting bit values 3...0;
            unsigned char HFlagCheck = sp & 0xF;
            //getting bit values 7...0;
            unsigned char CFlagCheck = sp & 0xFF;
            //set flag H if it overflows from bit 3;
            if ((HFlagCheck + (e8value & 0xF)) > 0xF)
            {
                AF.F |= 0b00100000;
            }
            //set flag C if it overflows from bit 7;
            if ((CFlagCheck + unsigned_e8value) > 0xFF)
            {
                AF.F |= 0b00010000;
            }
            sp += e8value;
            clockTiming(16);
            break;
        }
        case 0x03:
            BC.BC++;
            clockTiming(8);
            break;
        case 0x13:
            DE.DE++;
            clockTiming(8);
            break;
        case 0x23:
            HL.HL++;
            clockTiming(8);
            break;
        case 0x33:
            sp++;
            clockTiming(8);
            break;
        case 0x0B:
            BC.BC--;
            clockTiming(8);
            break;
        case 0x1B:
            DE.DE--;
            clockTiming(8);
            break;
        case 0x2B:
            HL.HL--;
            clockTiming(8);
            break;
        case 0x3B:
            sp--;
            clockTiming(8);
            break;
        case 0x27:
            //converting register A to BCD
            if (!testBit(AF.F, 6))
            {
                if (testBit(AF.F, 4) || (AF.A > 0x99))
                {
                    AF.A += 0x60;
                    SET(&AF.F, 4, 0);
                }
                if (testBit(AF.F, 5) || ((AF.A & 0x0F) > 0x09))
                {
                    AF.A += 0x06;
                }
            }
            else
            {
                if (testBit(AF.F, 4))
                {
                    AF.A -= 0x60;
                }
                if (testBit(AF.F, 5))
                {
                    AF.A -= 0x06;
                }
            }
            if (AF.A == 0)
            {
                SET(&AF.F, 7, 0);
            }
            else
            {
                RES(&AF.F, 7, 0);
            }
            RES(&AF.F, 5, 0);
            clockTiming(4);
            break;
        case 0x2F:
            //set the N and H flag
            AF.F |= 0b01100000;
            //complement A
            AF.A = ~AF.A;
            clockTiming(4);
            break;
        case 0x3F:
            //reset the N and H flag
            AF.F &= 0b10010000;
            //complement C flag
            //if C flag is set
            if (testBit(AF.F, 4))
            {
                AF.F &= 0b11100000;
            }
            //if C flag isn't set
            else 
            {
                AF.F |= 0b00010000;
            }
            clockTiming(4);
            break;
        case 0x37:
            //reset N and H flag
            AF.F &= 0b10010000;
            //set C flag
            AF.F |= 0b00010000;
            clockTiming(4);
            break;
        case 0x07:
            RLCA(4);
            break;
        case 0x17:
            RLA(4);
            break;
        case 0x0F:
            RRCA(4);
            break;
        case 0x1F:
            RRA(4);
            break;
        case 0xC3:
            //cycle is 12 for all jumps when non matching and 16 when matching, so we will send 12 and if it passes we will send 4 inside the function
            JP(true, 12);
            break;
        case 0xC2:
            JP(NZCondition(), 12);
            break;
        case 0xCA:
            JP(ZCondition(), 12);
            break;
        case 0xD2:
            JP(NCCondition(), 12);
            break;
        case 0xDA:
            JP(CCondition(), 12);
            break;
        case 0xE9:
            pc = HL.HL - 1;
            clockTiming(4);
            break;
        case 0x18:
            //same logic with JP, but with 4 cycles less
            JR(true, 8);
            break;
        case 0x20:
            JR(NZCondition(), 8);
            break;
        case 0x28:
            JR(ZCondition(), 8);
            break;
        case 0x30:
            JR(NCCondition(), 8);
            break;
        case 0x38:
            JR(CCondition(), 8);
            break;
        case 0xCD:
            //12 with call function and 12 with jp function
            CALL(true, 12);
            break;
        case 0xC4:
            CALL(NZCondition(), 12);
            break;
        case 0xCC:
            CALL(ZCondition(), 12);
            break;
        case 0xD4:
            CALL(NCCondition(), 12);
            break;
        case 0xDC:
            CALL(CCondition(), 12);
            break;
        case 0xC9:
            //logic of cycle equal to JP but with 4 outside and 12 inside
            RET(true, 4);
            break;
        case 0xC0:
            //logic of cycle equal to JP but with 8 outside and 12 inside
            RET(NZCondition(), 8);
            break;
        case 0xC8:
            RET(ZCondition(), 8);
            break;
        case 0xD0:
            RET(NCCondition(), 8);
            break;
        case 0xD8:
            RET(CCondition(), 8);
            break;
        case 0xD9:
            RETI(16);
            break;
        case 0xF3:
            masterInterrupt = false;
            updateTimers(4);
            break;
        case 0xFB:
            delayMasterInterrupt = true;
            updateTimers(4);
            break;
        case 0xC7:
        {
            pc++;
            unsigned char highByte = ((pc >> 8));
            unsigned char lowByte = (pc & 0xFF);
            PUSH(&highByte, &lowByte);
            //when the instruction is finished, pc will overflow and be set to the right position, 0.
            pc = 0xFFFF;
            break;
        }
        case 0xCF:
        {
            pc++;
            unsigned char highByte = (pc >> 8);
            unsigned char lowByte = (pc & 0xFF);
            PUSH(&highByte, &lowByte);
            //same logic of instruction 0xC7 but it doesnt overflow.
            pc = 0x07;
            break;
        }
        case 0xD7:
        {
            pc++;
            unsigned char highByte = (pc >> 8);
            unsigned char lowByte = (pc & 0xFF);
            PUSH(&highByte, &lowByte);
            //same logic of instruction 0xC7 but it doesnt overflow.
            pc = 0x9;
            break;
        }
        case 0xDF:
        {
            pc++;
            unsigned char highByte = (pc >> 8);
            unsigned char lowByte = (pc & 0xFF);
            PUSH(&highByte, &lowByte);
            //same logic of instruction 0xC7 but it doesnt overflow.
            pc = 0x17;
            break;
        }
        case 0xE7:
        {
            pc++;
            unsigned char highByte = (pc >> 8);
            unsigned char lowByte = (pc & 0xFF);
            PUSH(&highByte, &lowByte);
            //same logic of instruction 0xC7 but it doesnt overflow.
            pc = 0x19;
            break;
        }
        case 0xEF:
        {
            pc++;
            unsigned char highByte = (pc >> 8);
            unsigned char lowByte = (pc & 0xFF);
            PUSH(&highByte, &lowByte);
            //same logic of instruction 0xC7 but it doesnt overflow.
            pc = 0x27;
            break;
        }
        case 0xF7:
        {
            pc++;
            unsigned char highByte = (pc >> 8);
            unsigned char lowByte = (pc & 0xFF);
            PUSH(&highByte, &lowByte);
            //same logic of instruction 0xC7 but it doesnt overflow.
            pc = 0x29;
            break;
        }
        case 0xFF:
        {
            pc++;
            unsigned char highByte = (pc >> 8);
            unsigned char lowByte = (pc & 0xFF);
            PUSH(&highByte, &lowByte);
            //same logic of instruction 0xC7 but it doesnt overflow.
            pc = 0x37;
            break;
        }
        case 0x76:
            
            printf("adress of halt is: %04hX\n", pc);
            break;
        case 0xCB:
            pc++;
            opcode = memory[pc];
            clockTiming(4);
            switch (opcode)
            {
                case 0x37:
                    swap(&AF.A, 8);
                    break;
                case 0x30:
                    swap(&BC.B, 8);
                    break;
                case 0x31:
                    swap(&BC.C, 8);
                    break;
                case 0x32:
                    swap(&DE.D, 8);
                    break;
                case 0x33:
                    swap(&DE.E, 8);
                    break;
                case 0x34:
                    swap(&HL.H, 8);
                    break;
                case 0x35:
                    swap(&HL.L, 8);
                    break;
                case 0x36:
                    swap(&memory[HL.HL], 16);
                    break;
                case 0x07:
                    rlcRegister(&AF.A, 8);
                    break;
                case 0x00:
                    rlcRegister(&BC.B, 8);
                    break;
                case 0x01:
                    rlcRegister(&BC.C, 8);
                    break;
                case 0x02:
                    rlcRegister(&DE.D, 8);
                    break;
                case 0x03:
                    rlcRegister(&DE.E, 8);
                    break;
                case 0x04:
                    rlcRegister(&HL.H, 8);
                    break;
                case 0x05:
                    rlcRegister(&HL.L, 8);
                    break;
                case 0x06:
                    rlcRegister(&memory[HL.HL], 16);
                    break;
                case 0x17:
                    rlRegister(&AF.A, 8);
                    break;
                case 0x10:
                    rlRegister(&BC.B, 8);
                    break;
                case 0x11:
                    rlRegister(&BC.C, 8);
                    break;
                case 0x12:
                    rlRegister(&DE.D, 8);
                    break;
                case 0x13:
                    rlRegister(&DE.E, 8);
                    break;
                case 0x14:
                    rlRegister(&HL.H, 8);
                    break;
                case 0x15:
                    rlRegister(&HL.L, 8);
                    break;
                case 0x16:
                    rlRegister(&memory[HL.HL],16);
                    break;
                case 0x0F:
                    rrcRegister(&AF.A, 8);
                    break;
                case 0x08:
                    rrcRegister(&BC.B, 8);
                    break;
                case 0x09:
                    rrcRegister(&BC.C, 8);
                    break;
                case 0x0A:
                    rrcRegister(&DE.D, 8);
                    break;
                case 0x0B:
                    rrcRegister(&DE.E, 8);
                    break;
                case 0x0C:
                    rrcRegister(&HL.H, 8);
                    break;
                case 0x0D:
                    rrcRegister(&HL.L, 8);
                    break;
                case 0x0E:
                    rrcRegister(&memory[HL.HL],16);
                    break;
                case 0x1F:
                    rrRegister(&AF.A, 8);
                    break;
                case 0x18:
                    rrRegister(&BC.B, 8);
                    break;
                case 0x19:
                    rrRegister(&BC.C, 8);
                    break;
                case 0x1A:
                    rrRegister(&DE.D, 8);
                    break;
                case 0x1B:
                    rrRegister(&DE.E, 8);
                    break;
                case 0x1C:
                    rrRegister(&HL.H, 8);
                    break;
                case 0x1D:
                    rrRegister(&HL.L, 8);
                    break;
                case 0x1E:
                    rrRegister(&memory[HL.HL], 16);
                    break;
                case 0x27:
                    SLA(&AF.A, 8);
                    break;
                case 0x20:
                    SLA(&BC.B, 8);
                    break;
                case 0x21:
                    SLA(&BC.C, 8);
                    break;
                case 0x22:
                    SLA(&DE.D, 8);
                    break;
                case 0x23:
                    SLA(&DE.E, 8);
                    break;
                case 0x24:
                    SLA(&HL.H, 8);
                    break;
                case 0x25:
                    SLA(&HL.L, 8);
                    break;
                case 0x26:
                    SLA(&memory[HL.HL], 16);
                    break;
                case 0x2F:
                    SRA(&AF.A, 8);
                    break;
                case 0x28:
                    SRA(&BC.B, 8);
                    break;
                case 0x29:
                    SRA(&BC.C, 8);
                    break;
                case 0x2A:
                    SRA(&DE.D, 8);
                    break;
                case 0x2B:
                    SRA(&DE.E, 8);
                    break;
                case 0x2C:
                    SRA(&HL.H, 8);
                    break;
                case 0x2D:
                    SRA(&HL.L, 8);
                    break;
                case 0x2E:
                    SRA(&memory[HL.HL],16);
                    break;
                case 0x3F:
                    SRL(&AF.A, 8);
                    break;
                case 0x38:
                    SRL(&BC.B, 8);
                    break;
                case 0x39:
                    SRL(&BC.C, 8);
                    break;
                case 0x3A:
                    SRL(&DE.D, 8);
                    break;
                case 0x3B:
                    SRL(&DE.E, 8);
                    break;
                case 0x3C:
                    SRL(&HL.H, 8);
                    break;
                case 0x3D:
                    SRL(&HL.L, 8);
                    break;
                case 0x3E:
                    SRL(&memory[HL.HL],16);
                    break;
                case 0x47:
                    BIT(&AF.A, 0, 8);
                    break;
                case 0x40:
                    BIT(&BC.B, 0, 8); 
                    break;
                case 0x41:
                    BIT(&BC.C, 0, 8);
                    break;
                case 0x42:
                    BIT(&DE.D, 0, 8);
                    break;
                case 0x43:
                    BIT(&DE.E, 0, 8);
                    break;
                case 0x44:
                    BIT(&HL.H, 0, 8);
                    break;
                case 0x45:
                    BIT(&HL.L, 0, 8);
                    break;
                case 0x46:
                    BIT(&memory[HL.HL], 0, 16);
                    break;
                case 0x4F:
                    BIT(&AF.A, 1, 8);
                    break;
                case 0x48:
                    BIT(&BC.B, 1, 8);
                    break;
                case 0x49:
                    BIT(&BC.C, 1, 8);
                    break;
                case 0x4A:
                    BIT(&DE.D, 1, 8);
                    break;
                case 0x4B:
                    BIT(&DE.E, 1, 8);
                    break;
                case 0x4C:
                    BIT(&HL.H, 1, 8);
                    break;
                case 0x4D:
                    BIT(&HL.L, 1, 8);
                    break;
                case 0x4E:
                    BIT(&memory[HL.HL], 1, 16);
                    break;
                case 0x57:
                    BIT(&AF.A, 2, 8);
                    break;
                case 0x50:
                    BIT(&BC.B, 2, 8);
                    break;
                case 0x51:
                    BIT(&BC.C, 2, 8);
                    break;
                case 0x52:
                    BIT(&DE.D, 2, 8);
                    break;
                case 0x53:
                    BIT(&DE.E, 2, 8);
                    break;
                case 0x54:
                    BIT(&HL.H, 2, 8);
                    break;
                case 0x55:
                    BIT(&HL.L, 2, 8);
                    break;
                case 0x56:
                    BIT(&memory[HL.HL], 2, 16);
                    break;
                case 0x5F:
                    BIT(&AF.A, 3, 8);
                    break;
                case 0x58:
                    BIT(&BC.B, 3, 8);
                    break;
                case 0x59:
                    BIT(&BC.C, 3, 8);
                    break;
                case 0x5A:
                    BIT(&DE.D, 3, 8);
                    break;
                case 0x5B:
                    BIT(&DE.E, 3, 8);
                    break;
                case 0x5C:
                    BIT(&HL.H, 3, 8);
                    break;
                case 0x5D:
                    BIT(&HL.L, 3, 8);
                    break;
                case 0x5E:
                    BIT(&memory[HL.HL], 3, 16);
                    break;
                case 0x67:
                    BIT(&AF.A, 4, 8);
                    break;
                case 0x60:
                    BIT(&BC.B, 4, 8);
                    break;
                case 0x61:
                    BIT(&BC.C, 4, 8);
                    break;
                case 0x62:
                    BIT(&DE.D, 4, 8);
                    break;
                case 0x63:
                    BIT(&DE.E, 4, 8);
                    break;
                case 0x64:
                    BIT(&HL.H, 4, 8);
                    break;
                case 0x65:
                    BIT(&HL.L, 4, 8);
                    break;
                case 0x66:
                    BIT(&memory[HL.HL], 4, 16);
                    break;
                case 0x6F:
                    BIT(&AF.A, 5, 8);
                    break;
                case 0x68:
                    BIT(&BC.B, 5, 8);
                    break;
                case 0x69:
                    BIT(&BC.C, 5, 8);
                    break;
                case 0x6A:
                    BIT(&DE.D, 5, 8);
                    break;
                case 0x6B:
                    BIT(&DE.E, 5, 8);
                    break;
                case 0x6C:
                    BIT(&HL.H, 5, 8);
                    break;
                case 0x6D:
                    BIT(&HL.L, 5, 8);
                    break;
                case 0x6E:
                    BIT(&memory[HL.HL], 5, 16);
                    break;
                case 0x77:
                    BIT(&AF.A, 6, 8);
                    break;
                case 0x70:
                    BIT(&BC.B, 6, 8);
                    break;
                case 0x71:
                    BIT(&BC.C, 6, 8);
                    break;
                case 0x72:
                    BIT(&DE.D, 6, 8);
                    break;
                case 0x73:
                    BIT(&DE.E, 6, 8);
                    break;
                case 0x74:
                    BIT(&HL.H, 6, 8);
                    break;
                case 0x75:
                    BIT(&HL.L, 6, 8);
                    break;
                case 0x76:
                    BIT(&memory[HL.HL], 6, 16);
                    break;
                case 0x7F:
                    BIT(&AF.A, 7, 8);
                    break;
                case 0x78:
                    BIT(&BC.B, 7, 8);
                    break;
                case 0x79:
                    BIT(&BC.C, 7, 8);
                    break;
                case 0x7A:
                    BIT(&DE.D, 7, 8);
                    break;
                case 0x7B:
                    BIT(&DE.E, 7, 8);
                    break;
                case 0x7C:
                    BIT(&HL.H, 7, 8);
                    break;
                case 0x7D:
                    BIT(&HL.L, 7, 8);
                    break;
                case 0x7E:
                    BIT(&memory[HL.HL], 7, 16);
                    break;
                case 0xC7:
                    SET(&AF.A, 0, 8);
                    break;
                case 0xC0:
                    SET(&BC.B, 0, 8);
                    break;
                case 0xC1:
                    SET(&BC.C, 0, 8);
                    break;
                case 0xC2:
                    SET(&DE.D, 0, 8);
                    break;
                case 0xC3:
                    SET(&DE.E, 0, 8);
                    break;
                case 0xC4:
                    SET(&HL.H, 0, 8);
                    break;
                case 0xC5:
                    SET(&HL.L, 0, 8);
                    break;
                case 0xC6:
                    SET(&memory[HL.HL], 0, 16);
                    break;
                case 0xCF:
                    SET(&AF.A, 1, 8);
                    break;
                case 0xC8:
                    SET(&BC.B, 1, 8);
                    break;
                case 0xC9:
                    SET(&BC.C, 1, 8);
                    break;
                case 0xCA:
                    SET(&DE.D, 1, 8);
                    break;
                case 0xCB:
                    SET(&DE.E, 1, 8);
                    break;
                case 0xCC:
                    SET(&HL.H, 1, 8);
                    break;
                case 0xCD:
                    SET(&HL.L, 1, 8);
                    break;
                case 0xCE:
                    SET(&memory[HL.HL], 1, 16);
                    break;
                case 0xD7:
                    SET(&AF.A, 2, 8);
                    break;
                case 0xD0:
                    SET(&BC.B, 2, 8);
                    break;
                case 0xD1:
                    SET(&BC.C, 2, 8);
                    break;
                case 0xD2:
                    SET(&DE.D, 2, 8);
                    break;
                case 0xD3:
                    SET(&DE.E, 2, 8);
                    break;
                case 0xD4:
                    SET(&HL.H, 2, 8);
                    break;
                case 0xD5:
                    SET(&HL.L, 2, 8);
                    break;
                case 0xD6:
                    SET(&memory[HL.HL], 2, 16);
                    break; 
                case 0xDF:
                    SET(&AF.A, 3, 8);
                    break;
                case 0xD8:
                    SET(&BC.B, 3, 8);
                    break;
                case 0xD9:
                    SET(&BC.C, 3, 8);
                    break;
                case 0xDA:
                    SET(&DE.D, 3, 8);
                    break;
                case 0xDB:
                    SET(&DE.E, 3, 8);
                    break;
                case 0xDC:
                    SET(&HL.H, 3, 8);
                    break;
                case 0xDD:
                    SET(&HL.L, 3, 8);
                    break;
                case 0xDE:
                    SET(&memory[HL.HL], 3, 16);
                    break;
                case 0xE7:
                    SET(&AF.A, 4, 8);
                    break;
                case 0xE0:
                    SET(&BC.B, 4, 8);
                    break;
                case 0xE1:
                    SET(&BC.C, 4, 8);
                    break;
                case 0xE2:
                    SET(&DE.D, 4, 8);
                    break;
                case 0xE3:
                    SET(&DE.E, 4, 8);
                    break;
                case 0xE4:
                    SET(&HL.H, 4, 8);
                    break;
                case 0xE5:
                    SET(&HL.L, 4, 8);
                    break;
                case 0xE6:
                    SET(&memory[HL.HL], 4, 16);
                    break;
                case 0xEF:
                    SET(&AF.A, 5, 8);
                    break;
                case 0xE8:
                    SET(&BC.B, 5, 8);
                    break;
                case 0xE9:
                    SET(&BC.C, 5, 8);
                    break;
                case 0xEA:
                    SET(&DE.D, 5, 8);
                    break;
                case 0xEB:
                    SET(&DE.E, 5, 8);
                    break;
                case 0xEC:
                    SET(&HL.H, 5, 8);
                    break;
                case 0xED:
                    SET(&HL.L, 5, 8);
                    break;
                case 0xEE:
                    SET(&memory[HL.HL], 5, 16);
                    break;
                case 0xF7:
                    SET(&AF.A, 6, 8);
                    break;
                case 0xF0:
                    SET(&BC.B, 6, 8);
                    break;
                case 0xF1:
                    SET(&BC.C, 6, 8);
                    break;
                case 0xF2:
                    SET(&DE.D, 6, 8);
                    break;
                case 0xF3:
                    SET(&DE.E, 6, 8);
                    break;
                case 0xF4:
                    SET(&HL.H, 6, 8);
                    break;
                case 0xF5:
                    SET(&HL.L, 6, 8);
                    break;
                case 0xF6:
                    SET(&memory[HL.HL], 6, 16);
                    break;
                case 0xFF:
                    SET(&AF.A, 7, 8);
                    break;
                case 0xF8:
                    SET(&BC.B, 7, 8);
                    break;
                case 0xF9:
                    SET(&BC.C, 7, 8);
                    break;
                case 0xFA:
                    SET(&DE.D, 7, 8);
                    break;
                case 0xFB:
                    SET(&DE.E, 7, 8);
                    break;
                case 0xFC:
                    SET(&HL.H, 7, 8);
                    break;
                case 0xFD:
                    SET(&HL.L, 7, 8);
                    break;
                case 0xFE:
                    SET(&memory[HL.HL], 7, 16);
                    break;
                case 0x87:
                    RES(&AF.A, 0, 8);
                    break;
                case 0x80:
                    RES(&BC.B, 0, 8);
                    break;
                case 0x81:
                    RES(&BC.C, 0, 8);
                    break;
                case 0x82:
                    RES(&DE.D, 0, 8);
                    break;
                case 0x83:
                    RES(&DE.E, 0, 8);
                    break;
                case 0x84:
                    RES(&HL.H, 0, 8);
                    break;
                case 0x85:
                    RES(&HL.L, 0, 8);
                    break;
                case 0x86:
                    RES(&memory[HL.HL], 0, 16);
                    break;
                case 0x8F:
                    RES(&AF.A, 1, 8);
                    break;
                case 0x88:
                    RES(&BC.B, 1, 8);
                    break;
                case 0x89:
                    RES(&BC.C, 1, 8);
                    break;
                case 0x8A:
                    RES(&DE.D, 1, 8);
                    break;
                case 0x8B:
                    RES(&DE.E, 1, 8);
                    break;
                case 0x8C:
                    RES(&HL.H, 1, 8);
                    break;
                case 0x8D:
                    RES(&HL.L, 1, 8);
                    break;
                case 0x8E:
                    RES(&memory[HL.HL], 1, 16);
                    break;
                case 0x97:
                    RES(&AF.A, 2, 8);
                    break;
                case 0x90:
                    RES(&BC.B, 2, 8);
                    break;
                case 0x91:
                    RES(&BC.C, 2, 8);
                    break;
                case 0x92:
                    RES(&DE.D, 2, 8);
                    break;
                case 0x93:
                    RES(&DE.E, 2, 8);
                    break;
                case 0x94:
                    RES(&HL.H, 2, 8);
                    break;
                case 0x95:
                    RES(&HL.L, 2, 8);
                    break;
                case 0x96:
                    RES(&memory[HL.HL], 2, 16);
                    break;
                case 0x9F:
                    RES(&AF.A, 3, 8);
                    break;
                case 0x98:
                    RES(&BC.B, 3, 8);
                    break;
                case 0x99:
                    RES(&BC.C, 3, 8);
                    break;
                case 0x9A:
                    RES(&DE.D, 3, 8);
                    break;
                case 0x9B:
                    RES(&DE.E, 3, 8);
                    break;
                case 0x9C:
                    RES(&HL.H, 3, 8);
                    break;
                case 0x9D:
                    RES(&HL.L, 3, 8);
                    break;
                case 0x9E:
                    RES(&memory[HL.HL], 3, 16);
                    break;
                case 0xA7:
                    RES(&AF.A, 4, 8);
                    break;
                case 0xA0:
                    RES(&BC.B, 4, 8);
                    break;
                case 0xA1:
                    RES(&BC.C, 4, 8);
                    break;
                case 0xA2:
                    RES(&DE.D, 4, 8);
                    break;
                case 0xA3:
                    RES(&DE.E, 4, 8);
                    break;
                case 0xA4:
                    RES(&HL.H, 4, 8);
                    break;
                case 0xA5:
                    RES(&HL.L, 4, 8);
                    break;
                case 0xA6:
                    RES(&memory[HL.HL], 4, 16);
                    break;
                case 0xAF:
                    RES(&AF.A, 5, 8);
                    break;
                case 0xA8:
                    RES(&BC.B, 5, 8);
                    break;
                case 0xA9:
                    RES(&BC.C, 5, 8);
                    break;
                case 0xAA:
                    RES(&DE.D, 5, 8);
                    break;
                case 0xAB:
                    RES(&DE.E, 5, 8);
                    break;
                case 0xAC:
                    RES(&HL.H, 5, 8);
                    break;
                case 0xAD:
                    RES(&HL.L, 5, 8);
                    break;
                case 0xAE:
                    RES(&memory[HL.HL], 5, 16);
                    break;
                case 0xB7:
                    RES(&AF.A, 6, 8);
                    break;
                case 0xB0:
                    RES(&BC.B, 6, 8);
                    break;
                case 0xB1:
                    RES(&BC.C, 6, 8);
                    break;
                case 0xB2:
                    RES(&DE.D, 6, 8);
                    break;
                case 0xB3:
                    RES(&DE.E, 6, 8);
                    break;
                case 0xB4:
                    RES(&HL.H, 6, 8);
                    break;
                case 0xB5:
                    RES(&HL.L, 6, 8);
                    break;
                case 0xB6:
                    RES(&memory[HL.HL], 6, 16);
                    break;
                case 0xBF:
                    RES(&AF.A, 7, 8);
                    break;
                case 0xB8:
                    RES(&BC.B, 7, 8);
                    break;
                case 0xB9:
                    RES(&BC.C, 7, 8);
                    break;
                case 0xBA:
                    RES(&DE.D, 7, 8);
                    break;
                case 0xBB:
                    RES(&DE.E, 7, 8);
                    break;
                case 0xBC:
                    RES(&HL.H, 7, 8);
                    break;
                case 0xBD:
                    RES(&HL.L, 7, 8);
                    break;
                case 0xBE:
                    RES(&memory[HL.HL], 7, 16);
                    break;
                default:
                    printf("opcode [%X] not found\n", opcode);
                    break;
            }
            break;
    default:
        printf("opcode [%X] not found\n", opcode);
        break;
    }
    //increasing program counter;
    pc++;
}

void addRegister(unsigned char* registerA, unsigned char* registerB, unsigned char cycles)
{
    clockTiming(cycles);
    AF.F = 0;
    //getting immediate data in the form of signed char
    unsigned char e8value = *registerB;
    //getting the value of 3...0 bits in SP;
    unsigned char HFlagCheck = *registerA & 0xF;
    //getting the value of 7...0 bits in SP;
    unsigned char CFlagCheck = *registerA & 0xFF;

    *registerA += e8value;
    //setting Z flag if result is 0
    if (*registerA == 0)
    {
        SET(&AF.F, 7, 0);
    }
    //checking if theres overflow from bit 3 (H flag)
    if ((HFlagCheck + (e8value & 0xF)) > 0xF)
    {
        SET(&AF.F, 5, 0);
    }

    // checking if theres overflow from bit 7
    if ((CFlagCheck + e8value) > 0xFF)
    {
        SET(&AF.F, 4, 0);
    }
}

void adcRegister(unsigned char* registerA, unsigned char* registerB, unsigned char cycles)
{
    clockTiming(cycles);
    RES(&AF.F, 6, 0);
    //getting immediate data in the form of signed char
    unsigned char e8value = *registerB;
    //getting the value of 3...0 bits in SP;
    unsigned char HFlagCheck = *registerA & 0x0F;
    //getting the value of 7...0 bits in SP;
    unsigned char CFlagCheck = *registerA & 0xFF;
    //adding to register before checking if theres overflow because we have to add the previous carry to the value;
    unsigned char carryBit = 0;
    if (testBit(AF.F, 4))
    {
        carryBit = 1;
    }
    *registerA += (e8value + carryBit);
    //if the result is 0, set Z flag
    if (*registerA == 0)
    {
        SET(&AF.F, 7, 0);
    }
    else
    {
        RES(&AF.F, 7, 0);
    }

    //checking if theres overflow from bit 3 (H flag)
    if ((HFlagCheck + ((e8value & 0xF) + carryBit)) > 0xF)
    {
        SET(&AF.F, 5, 0);
    }
    else
    {
        RES(&AF.F, 5, 0);
    }

    // checking if theres overflow from bit 7
    if ((CFlagCheck + (e8value + carryBit)) > 0xFF)
    {
        SET(&AF.F, 4, 0);
    }
    else
    {
        RES(&AF.F, 4, 0);
    }
}

void subRegister(unsigned char* registerA, unsigned char* registerB, unsigned char cycles)
{
    clockTiming(cycles);
    AF.F = 0;
    SET(&AF.F, 6, 0);
    //getting immediate data in the form of signed char
    unsigned char e8value = *registerB;
    //getting the value of 3...0 bits in SP;
    unsigned char HFlagCheck = *registerA & 0xF;
    //getting the value of 7...0 bits in SP;
    unsigned char CFlagCheck = *registerA & 0xFF;

    *registerA -= e8value;
    //if the result is 0, set Z flag
    if (*registerA == 0)
    {
        SET(&AF.F, 7, 0);
    }

    if (HFlagCheck < (e8value & 0xF))
    {
        SET(&AF.F, 5, 0);
    }

    if (CFlagCheck < e8value)
    {
        SET(&AF.F, 4, 0);
    }
}

void sbcRegister(unsigned char* registerA, unsigned char* registerB, unsigned char cycles)
{
    clockTiming(cycles);
    SET(&AF.F, 6, 0);
    //getting immediate data in the form of signed char
    unsigned char e8value = *registerB;
    //getting the value of 3...0 bits in SP;
    unsigned char HFlagCheck = *registerA & 0xF;
    //getting the value of 7...0 bits in SP;
    unsigned char CFlagCheck = *registerA & 0xFF;
    unsigned char carryBit = 0;
    if (testBit(AF.F, 4))
    {
        carryBit = 1;
    }
 
    *registerA -= (e8value + carryBit);
    //if the result is 0, set the Z flag
    if (*registerA == 0)
    {
        SET(&AF.F, 7, 0);
    }
    else
    {
        RES(&AF.F, 7, 0);
    }

    if (HFlagCheck < ((e8value  & 0xF) + carryBit))
    {
        SET(&AF.F, 5, 0);
    }
    else
    {
        RES(&AF.F, 5, 0);
    }

    if (CFlagCheck < (e8value + carryBit))
    {
        SET(&AF.F, 4, 0);
    }
    else
    {
        RES(&AF.F, 4, 0);
    }
}

//AND operation of register A with other 8bit values, puts the result in register A
void andOperation(unsigned char* registerB, unsigned char cycles)
{
    clockTiming(cycles);
    //resetting flags
    AF.F = 0;
    //setting the H flag
    SET(&AF.F, 5, 0);
    AF.A &= *registerB;

    //if the result is zero, set the Z flag
    if (AF.A == 0)
    {
        AF.F |= 0b10000000;
    }
}

//OR operation of register A with other 8bit values, puts the result in register A
void orOperation(unsigned char* registerB, unsigned char cycles)
{
    clockTiming(cycles);
    //resetting flags
    AF.F = 0;


    AF.A |= *registerB;

    //if the result is zero, set the Z flag
    if (AF.A == 0)
    {
        SET(&AF.F, 7, 0);
    }
}

//XOR operation of register A with other 8bit values, puts the result in register A
void xorOperation(unsigned char* registerB, unsigned char cycles)
{
    clockTiming(cycles);
    //resetting flags
    AF.F = 0;
    AF.A = (*registerB ^ AF.A);

    //if the result is zero, set the Z flag
    if (AF.A == 0)
    {
        SET(&AF.F, 7, 0);
    }
}

//compare A with n.  This is basically an A - n subtraction instruction but the results are thrown away
void cpRegister(unsigned char* registerB, unsigned char cycles)
{
    clockTiming(cycles);
    AF.F = 0;
    //setting N flag
    AF.F |= 0b01000000;
    //getting immediate data in the form of signed char
    unsigned char e8value = *registerB;
    //getting the value of 3...0 bits in A;
    unsigned char HFlagCheck = AF.A & 0xF;
    //getting the value of 7...0 bits in A;
    unsigned char CFlagCheck = AF.A & 0xFF;

    //if the value are equal, set Z flag
    if ( AF.A == *registerB)
    {
        AF.F |= 0b10000000;
    }

    if (HFlagCheck < (e8value & 0xF))
    {
        AF.F |= 0b00100000;
    }

    if (CFlagCheck < e8value)
    {
        AF.F |= 0b00010000;
    }
}

void incRegister(unsigned char* registerA, unsigned char cycles)
{
    clockTiming(cycles);
    //resetting N, Z and H flags
    AF.F &= 0b00010000;
    
    //check if the lower nibble is 0b1111 to see if it will carry from bit 3. if it carries, set flag H;
    if ((*registerA & 0xF) == 0xF)
    {
        AF.F |= 0b00100000;
    }

    //increments register
    *registerA += 1;
    //if it overflows, set flag Z;
    if (*registerA == 0)
    {
        AF.F |= 0b10000000;
    }
}

void decRegister(unsigned char* registerA, unsigned char cycles)
{
    clockTiming(cycles);
    //setting N flag
    AF.F |= 0b01000000;

    //check if the lower nibble is 0b0000 to see if it will borrow from bit 4. if it carries, set flag H;
    if ((*registerA & 0xF) == 0)
    {
        AF.F |= 0b00100000;
    }
    //if it doesnt carry, reset flag
    else
    {
        AF.F &= 0b11010000;
    }
    //decrements register
    *registerA -= 1;
    //if the result is 0, set flag Z
    if (*registerA == 0)
    {
        AF.F |= 0b10000000;
    }
    else
    {
        AF.F &= 0b01110000;
    }
}

void addRegister16Bit(unsigned short* registerA, unsigned short* registerB, unsigned char cycles)
{
    clockTiming(cycles);
    //resetting N flag
    AF.F &= 0b10110000;

    unsigned short e16value = *registerB;

    //getting the value of bits 11...0;
    unsigned short HFlagCheck = *registerA & 0xFFF;
    //getting the value of bits 15...0;
    unsigned short CFlagCheck = *registerA & 0xFFFF;

    //set H flag if bit 11 overflows
    if ((HFlagCheck + (e16value & 0xFFF)) > 0xFFF)
    {
        AF.F |= 0b00100000;
    }
    else
    {
        AF.F &= 0b11010000;
    }

    //set C flag if bit 15 overflows
    if ((CFlagCheck + e16value) > 0xFFFF)
    {
        AF.F |= 0b00010000;
    }
    else
    {
        AF.F &= 0b11100000;
    }

    *registerA += *registerB;
}

void swap(unsigned char* registerA, unsigned char cycles)
{
    clockTiming(cycles);
    AF.F = 0;
    if (*registerA == 0)
    {
        //set Z flag
        AF.F |= 0b10000000;
    }
    else 
    {
        //swap lower nibble with upper nibble
        *registerA = ((*registerA & 0x0F) << 4) | ((*registerA & 0xF0) >> 4);
    }
}

void rlcRegister(unsigned char* registerA, unsigned char cycles)
{
    clockTiming(cycles);
    //storing the seventh bit of registerA
    unsigned char seventhBit = (*registerA & 0b10000000);
    //copying the seventh bit of registerA to carry flag
    AF.F = ((seventhBit >> 3) | (AF.F & 0b11100000));
    //rotating registerA to the left
    *registerA <<= 1;
    //setting bit 0 to the previous seventh bit
    *registerA |= (seventhBit >> 7);

    //resetting N and H and Z flags
    AF.F &= 0b00010000;
    //if result of rotation is 0, set Z flag
    if(*registerA == 0)
    {
        AF.F |= 0b10000000;
    }
}

void RLCA(unsigned char cycles)
{
    clockTiming(cycles);
    //storing the seventh bit of registerA
    unsigned char seventhBit = (AF.A & 0b10000000);
    //copying the seventh bit of registerA to carry flag
    AF.F = ((seventhBit >> 3) | (AF.F & 0b11100000));
    //rotating registerA to the left
    AF.A <<= 1;
    //setting bit 0 to the previous seventh bit
    AF.A |= (seventhBit >> 7);
    //resetting N and H and Z flags
    AF.F &= 0b00010000;
}

void rlRegister(unsigned char* registerA, unsigned char cycles)
{
    clockTiming(cycles);
    //storing the carry flag
    unsigned char CFlag = (AF.F & 0b00010000);
    //copying the seventh bit of registerA to carry flag
    AF.F = (((*registerA & 0b10000000) >> 3) | (AF.F & 0b11100000));
    //rotating registerA to the left
    *registerA <<= 1;
    //setting bit 0 to previous carry flag
    *registerA |= (CFlag >> 4);

    //resetting N and H flags
    AF.F &= 0b00010000;
    //if result of rotation is 0, set Z flag
    if (*registerA == 0)
    {
        AF.F |= 0b10000000;
    }
}

void RLA(unsigned char cycles)
{
    clockTiming(cycles);
    //storing the carry flag
    unsigned char CFlag = (AF.F & 0b00010000);
    //copying the seventh bit of registerA to carry flag
    AF.F = (((AF.A & 0b10000000) >> 3) | (AF.F & 0b11100000));
    //rotating registerA to the left
    AF.A <<= 1;
    //setting bit 0 to previous carry flag
    AF.A |= (CFlag >> 4);

    //resetting N and H and Z flags
    AF.F &= 0b00010000;
}

void rrcRegister(unsigned char* registerA, unsigned char cycles)
{
    clockTiming(cycles);
    //storing the zeroth bit of registerA
    unsigned char zerothBit = (*registerA & 0b00000001);
    //copying the zeroth bit of registerA to carry flag
    AF.F = ((zerothBit << 4) | (AF.F & 0b11100000));
    //rotating registerA to the right
    *registerA >>= 1;
    //setting bit 7 to the previous zeroth bit
    *registerA |= (zerothBit << 7);

    //resetting N and H flags
    AF.F &= 0b00010000;
    //if result of rotation is 0, set Z flag
    if (*registerA == 0)
    {
        AF.F |= 0b10000000;
    }
}

void RRCA(unsigned char cycles)
{
    clockTiming(cycles);
    //storing the zeroth bit of registerA
    unsigned char zerothBit = (AF.A & 0b00000001);
    //copying the zeroth bit of registerA to carry flag
    AF.F = ((zerothBit << 4) | (AF.F & 0b11100000));
    //rotating registerA to the right
    AF.A >>= 1;
    //setting bit 7 to the previous zeroth bit
    AF.A |= (zerothBit << 7);

    //resetting N and H and Zflags
    AF.F &= 0b00010000;
}

void rrRegister(unsigned char* registerA, unsigned char cycles)
{
    clockTiming(cycles);
    //storing the carry flag
    unsigned char CFlag = (AF.F & 0b00010000);
    //copying the zeroth bit of registerA to carry flag
    AF.F = (((*registerA & 0b00000001) << 4) | (AF.F & 0b11100000));
    //rotating registerA to the right
    *registerA >>= 1;
    //setting bit 7 to the previous carry flag
    *registerA |= (CFlag << 3);

    //resetting N and H and Z flags
    AF.F &= 0b00010000;
    //if result of rotation is 0, set Z flag
    if (*registerA == 0)
    {
        AF.F |= 0b10000000;
    }
}

void RRA(unsigned char cycles)
{
    clockTiming(cycles);
    //storing the carry flag
    unsigned char CFlag = (AF.F & 0b00010000);
    //copying the zeroth bit of registerA to carry flag
    AF.F = (((AF.A & 0b00000001) << 4) | (AF.F & 0b11100000));
    //rotating registerA to the right
    AF.A >>= 1;
    //setting bit 7 to the previous carry flag
    AF.A |= (CFlag << 3);
    //resetting N and H and Zflags
    AF.F &= 0b00010000;
}

void SLA(unsigned char* registerA, unsigned char cycles)
{
    clockTiming(cycles);
    //copying the seventh bit of registerA to carry flag
    AF.F = (((*registerA & 0b10000000) >> 3) | (AF.F & 0b11100000));
    //resetting N and H and Z flags
    AF.F &= 0b00010000;

    //shifting bits to the left
    *registerA <<= 1;

    //if result of rotation is 0, set Z flag
    if (*registerA == 0)
    {
        AF.F |= 0b10000000;
    }
}

void SRA(unsigned char* registerA, unsigned char cycles)
{
    clockTiming(cycles);
    //copying the zeroth bit of registerA to carry flag
    AF.F = (((*registerA & 0b00000001) << 4) | (AF.F & 0b11100000));

    //resetting N and H and Z flags
    AF.F &= 0b00010000;

    *registerA >>= 1;

    //copying the old MSB to the MSB position
    *registerA |= ((*registerA & 0b01000000) << 1);

    if (*registerA == 0)
    {
        AF.F |= 0b10000000;
    }
}

void SRL(unsigned char* registerA, unsigned char cycles)
{
    clockTiming(cycles);
    //copying the zeroth bit of registerA to carry flag
    AF.F = (((*registerA & 0b00000001) << 4) | (AF.F & 0b11100000));
    //resetting N and H and Z flags
    AF.F &= 0b00010000;
    *registerA >>= 1;
    if (*registerA == 0)
    {
        AF.F |= 0b10000000;
    }
}

void BIT(unsigned char* registerA, unsigned char bit, unsigned char cycles)
{
    clockTiming(cycles);
    //getting the value of b, b is the position of the bit that needs to be tested, b = 0 - 7;

    //if the value of bit is 0, set Z flag
    if (!testBit(*registerA, bit))
    {
        AF.F |= 0b10000000;
    }
    else
    {
        AF.F &= 0b01110000;
    }

    //reset N flag and set H flag
    AF.F &= 0b10110000;

    AF.F |= 0b00100000;
}

void SET(unsigned char* registerA, unsigned char bit, unsigned char cycles)
{
    clockTiming(cycles);
    //getting the value of b, b is the position of the bit that needs to be set, b = 0 - 7;

    *registerA |= ((0b00000001) << bit);
}

void RES(unsigned char* registerA, unsigned char bit, unsigned char cycles)
{
    clockTiming(cycles);
    //getting the value of b, b is the position of the bit that needs to be reset, b = 0 - 7;

    *registerA &= ~(1 << bit);

}

bool NZCondition()
{

    if ((AF.F & 0b10000000) == 0)
    {
        return true;
    }
    return false;
}

bool ZCondition()
{
    if ((AF.F & 0b10000000) != 0)
    {
        return true;
    }
    return false;
}

bool NCCondition()
{
    if ((AF.F & 0b00010000) == 0)
    {
        return true;
    }
    return false;
}

bool CCondition()
{
    if ((AF.F & 0b00010000) != 0)
    {
        return true;
    }
    return false;
}

void JP(bool condition, unsigned char cycles)
{
    clockTiming(cycles);
    if (condition)
    {
        clockTiming(4);
        pc++;
        char LowNibble = memory[pc];
        pc++;
        char HighNibble = memory[pc];
        //printf("Going to adress %01hX%01hX, from adress %02hX \n", HighNibble, LowNibble, pc-2);
        pc = ((LowNibble & 0xFF) | (HighNibble << 8)) & 0xFFFF;
        //decreasing pc counter because after the execution it will automatically increase and when the code jumps we cant increase.
        pc--;
    }
    else
    {
        pc += 2;
    }
}

void JR(bool condition, unsigned char cycles)
{
    clockTiming(cycles);
    //after debugging it seems that we need to increase pc before
    pc++;
    if (condition)
    {
        clockTiming(4);
        signed char n = memory[pc];
        pc += n;
        //don't need to decrease pc because its a relative jump;
    }
}

void CALL(bool condition, unsigned char cycles)
{
    clockTiming(cycles);
    if (condition)
    {
        //getting addres of next instruction
        sp--;
        memory[sp] = (((pc + 3) >> 8) & 0xFF);
        sp--;
        memory[sp] = ((pc + 3)& 0xFF);
        JP(true, 8);
    }
    else
    {
        pc += 2;
    }
}

void RET(bool condition, unsigned char cycles)
{
    clockTiming(cycles);
    if (condition)
    {
        clockTiming(12);
        char LowNibble = memory[sp];
        sp++;
        char HighNibble = memory[sp];
        sp++;
        //printf("returning to adress %01hX%01hX, from adress %02hX\n", HighNibble, LowNibble, pc);
        pc = (HighNibble << 8);
        pc |= (LowNibble & 0xFF);
        //pc = ((LowNibble & 0xFF) | (HighNibble << 8)) & 0xFFFF;
        //decreasing pc because we already are in the next instruction
        pc--;
    }
}

void RETI(unsigned char cycles)
{
    clockTiming(cycles);
    char LowNibble = memory[sp];
    sp++;
    char HighNibble = memory[sp];
    sp++;
    pc = ((LowNibble & 0xFF) | (HighNibble << 8)) & 0xFFFF;
    //decreasing pc because we already are in the next instruction
    pc--;
    masterInterrupt = true;
}

void PUSH(unsigned char* highByte, unsigned char* lowByte)
{
    sp--;
    writeInMemory(sp, *highByte);
    sp--;
    writeInMemory(sp, *lowByte);
    clockTiming(16);
}

void PUSHAF()
{
    sp--;
    writeInMemory(sp, AF.A);
    unsigned char flagByte = (AF.F & 0b11110000);
    sp--;
    writeInMemory(sp, flagByte);
    clockTiming(16);
}

void POP(unsigned char* highByte, unsigned char* lowByte)
{
    *lowByte = memory[sp];
    sp++;
    *highByte = memory[sp];
    sp++;
    clockTiming(12);
}

void POPAF()
{
    AF.F = memory[sp];
    AF.F &= 0b11110000;
    sp++;
    AF.A = memory[sp];
    sp++;
    clockTiming(12);
}

void setupGraphics()
{
    //SDL_INIT_EVERYTHING
    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_EVENTS | SDL_INIT_TIMER) == 0) {
        printf("entering here\n");
        window = SDL_CreateWindow("title", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 512, 512, SDL_WINDOW_SHOWN);
        renderer = SDL_CreateRenderer(window, -1, 0);
        if (window != NULL)
        {
            printf("window created\n");
        }
        if (renderer != NULL)
        {
            printf("renderer created\n");
        }
        //SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
        //SDL_RenderSetLogicalSize(renderer, 320, 240);
        SDL_RenderSetLogicalSize(renderer, 256, 256);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
        isRunning = true;


    }
    else
    {

        isRunning = false;
    }
}

void drawGraphics()
{
    //the position of memory of the tile number XX is: 0x8XX0;
    char tileNumber = 0;
    short colorOfPixel = 0;
    char MSB = 0;//most significant bit
    char LSB = 0;//less significant bit
    char lineOfTile[2];
    //in the BG map tile numbers every byte contains the number of the tile (position of tile in memory) to be displayed in a 32*32 grid, 
    //every tile hax 8*8 pixels, totaling 256*256 pixels that is drawn to the screen.
    for (int y = 0; y < 32; y++)
    {
        //printf("%x", (0x9800) + (32 * y));
        for (int x = 0; x < 32; x++)
        {
            
            tileNumber = memory[(0x9800 + x) + (32 * y)];
            //printf("tileNumber: %X   lineOfTile[0]: ", tileNumber);
            
            //for every tile there is 16 bytes, for 2 bytes there is 8 pixels to be drawn. 
            for (int yPixel = 0; yPixel < 8; yPixel++)
            {
                //getting first byte of the line
                unsigned short memoryPosition = 0x8000 + (tileNumber * 16) + (2 * yPixel);
                lineOfTile[0] = memory[memoryPosition];
                //printf("%X", 0x8000 + (tileNumber * 10) + (2 * yPixel));
                //getting second byte of the line
                memoryPosition = 0x8000 + (tileNumber * 16) + (2 * yPixel) + 1;
                lineOfTile[1] = memory[memoryPosition];
                for (int xPixel = 0; xPixel < 8; xPixel++)
                {
                    MSB = (lineOfTile[1] & (0b10000000 >>  xPixel));
                    MSB >>= (7 - xPixel);
                    LSB = (lineOfTile[0] & (0b10000000 >> xPixel));
                    LSB >>= (7 - xPixel);
                    switch (MSB)
                    {
                    case 0:
                        switch (LSB)
                        {
                        case 0:
                            //white color
                            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                            break;
                        case 1:
                            //blue-green color
                            SDL_SetRenderDrawColor(renderer, 51, 97, 103, 255);
                            break;
                        }
                        break;
                    case 1:
                        switch (LSB)
                        {
                        case 0:
                            //light green color
                            SDL_SetRenderDrawColor(renderer, 82, 142, 21, 255);
                            break;
                        case 1:
                            //dark green color
                            SDL_SetRenderDrawColor(renderer, 20, 48, 23, 255);
                            break;
                        }
                        break;
                    }
                    SDL_RenderDrawPoint(renderer, ((x * 8) + xPixel), ((y * 8) +  yPixel));
                }
                //printf("\n");
            }  
        }
    }
    SDL_RenderPresent(renderer);
}

void clockTiming(unsigned char cycles)
{
    if (cycles != 0)
    {
        setLCDSTAT();
        cyclesBeforeLCDRender += cycles;
        updateTimers(cycles);
        //if bit 2 is 1, the clocks are active and will change values
        if (testBit(memory[LCDC], 7))
        {
            scanlineCounter -= cycles;
            if (scanlineCounter <= 0)
            {
                memory[LY]++;
                scanlineCounter = 456;
                if (memory[LY] == 144)
                {
                    requestInterrupt(0);
                    //IMPLEMENT INTERRUPT
                }
                else if (memory[LY] > 153)
                {
                    memory[LY] = 0;
                }
                else if (memory[LY] <= 143)
                {
                    drawScanLine();
                }
            }
        }
    }
}

void drawScanLine()
{

}

void setLCDSTAT()
{
    if (!testBit(memory[LCDC], 7))
    {
        memory[LY] = 0;
        scanlineCounter = 456;
        memory[STAT] &= 0b11111100;
        SET(&memory[STAT], 0, 0);
        return;
    }
    unsigned char currentMode = (memory[STAT] & 0b00000011);
    unsigned char mode = 0;
    bool reqInterrupt = false;
    if (memory[LY] >= 144)
    {
        //setting mode
        mode = 1;
        //setting mode bits
        SET(&memory[STAT], 0, 0);
        RES(&memory[STAT], 1, 0);
        //check if bit 4 is active. If it is, request an LCD interrupt
        reqInterrupt = testBit(memory[STAT], 4);
    }
    else
    {
        if (scanlineCounter >= 376)
        {
            mode = 2;
            SET(&memory[STAT], 1, 0);
            RES(&memory[STAT], 0, 0);
            reqInterrupt = testBit(memory[STAT], 5);
        }
        else if (scanlineCounter >= 204)
        {
            mode = 3;
            SET(&memory[STAT], 1, 0);
            SET(&memory[STAT], 0, 0);
        }
        else
        {
            mode = 0;
            RES(&memory[STAT], 1, 0);
            RES(&memory[STAT], 0, 0);
            reqInterrupt = testBit(memory[STAT], 3);
        }

        //the mode interrupt in STAT is enabled and there was a transition of the mode selected. request an interrupt
        if (reqInterrupt && (mode != currentMode))
        {
            requestInterrupt(1);
        }

        if (memory[LY] == memory[LYC])
        {
            SET(&memory[STAT], 2, 0);
            if (testBit(memory[STAT], 6))
            {
                requestInterrupt(1);
            }
        }
        else 
        {
            RES(&memory[STAT], 2, 0);
        }
    }
}

void updateTimers(unsigned char cycles)
{
    //updating divider register
    divCounter -= cycles;
    if (divCounter <= 0)
    {
        divCounter = CLOCKSPEED/FREQUENCY_11;
        memory[DIV]++;
    }

    //if bit 2 of register FF07 is 1, then the timer is enabled
    if (((memory[TMC] & 0b00000100) >> 2) == 1)
    {
        timerCounter -= cycles;
        if (timerCounter <= 0)
        {
            //setting clock frequency
            setClockFrequency();
            //updating timer
            if (memory[TIMA] == 255)
            {
                writeInMemory(TIMA, memory[TMA]);
                requestInterrupt(2);
            }else
            {
                memory[TIMA]++;
            }
        }
    }
}

void requestInterrupt(unsigned char bit)
{
    SET(&memory[IF], bit, 0);
}

void doInterrupts()
{
    if (masterInterrupt == true)
    {
        if (IF != 0)
        {
            unsigned char interruptsRequested = memory[IF];
            unsigned char interruptsEnabled = memory[IE];
            switch (interruptsEnabled & interruptsRequested)
            {
            case 0b00000001:
                
                setInterruptAddress(0);
                break;
            case 0b00000010:
                setInterruptAddress(1);
                break;
            case 0b00000100:
                setInterruptAddress(2);
                break;
            case 0b00010000:
                setInterruptAddress(4);
                break;
            }
        }
    }
    else if (delayMasterInterrupt == true)
    {
        delayMasterInterrupt = false;
        masterInterrupt = true;
    }
}

void setInterruptAddress(unsigned char bit)
{
    masterInterrupt = false;
    RES(&memory[IF], bit, 0);
    unsigned char highByte = ((pc & 0xFF00) >> 8);
    unsigned char lowByte = (pc & 0xFF);
    PUSH(&lowByte, &highByte);
    switch (bit)
    {
    case 0:
        pc = 0x40;
        break;
    case 1:
        pc = 0x48;
        break;
    case 2:
        pc = 0x50;
        break;
    case 4:
        pc = 0x60;
        break;
    }
}

void writeInMemory(unsigned short memoryLocation, unsigned char data)
{
    //!!!!!!!!!!implement register ff68 and ff69.
    if (memoryLocation <= 0x8000)
    {
        //this memory location is read only
    }
    else if ((memoryLocation >= 0xFEA0) && (memoryLocation <= 0xFEFF))
    {
        //this memory location is not usable 
    }
    else if ((memoryLocation >= 0xE000) && (memoryLocation <= 0xFDFF))
    {
        memory[memoryLocation] = data;
        memory[memoryLocation - 0x200] = data;
    }
    else if (memoryLocation == DIV)
    {
        memory[DIV] = 0;
    }
    else if (memoryLocation == LY)
    {
        memory[memoryLocation] = 0;
    }
    else if (memoryLocation == TMC)
    {
        unsigned char currentFrequency = memory[TMC] & 0b00000011;
        memory[TMC] = data;
        unsigned char newFrequency = memory[TMC] & 0b00000011;
        if (newFrequency != currentFrequency)
        {
            setClockFrequency();
        }
    }
    else if (memoryLocation == 0xFF68)
    {
        memory[memoryLocation]++;
    }
    else if (memoryLocation == 0xFF69)
    {
        //increment 0xFF68
        memory[0xFF68]++;
    }
    else if (memoryLocation == 0xFF46)
    {
        dmaTransfer(data);
    }
    else
    {
        memory[memoryLocation] = data;
    }
}

void readMemory(unsigned short memoryLocation)
{

}

void setClockFrequency()
{
    switch (memory[TMC] & 0b00000011)
    {
    case 00:
        timerCounter = CLOCKSPEED / FREQUENCY_00;
        break;
    case 01:
        timerCounter = CLOCKSPEED / FREQUENCY_01;
        break;
    case 02:
        timerCounter = CLOCKSPEED / FREQUENCY_10;
        break;
    case 03:
        timerCounter = CLOCKSPEED / FREQUENCY_11;
        break;
    }
}

bool testBit(unsigned char data, unsigned char bit)
{
    unsigned char testChar = (data & (0b00000001 << bit));
    if (testChar != 0)
    {
        return true;
    }
    return false;
}

void dmaTransfer(unsigned char data)
{
    //multiplying by 100
    unsigned short dataAdress = data << 8;
    for (int x = 0; x < 0xA0; x++)
    {
        writeInMemory(0xFE00 + x, memory[dataAdress + x]);
    }
}

void print_binary(int number)
{
    if (number) {
        print_binary(number >> 1);
        putc((number & 1) ? '1' : '0', stdout);
    }
}
