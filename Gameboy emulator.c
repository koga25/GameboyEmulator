// Gameboy emulator.cpp : This file contains the 'main' function. Program execution begins and ends there.
//TODO: finish opcodes: HALT - 0x76

//**** DI INSTRUCTION -> pandocs and GCPUMANUAL incongruences. implementing pandocs instruction
#define _CRT_SECURE_NO_WARNINGS
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "SDL.h"
#include <SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include "SDL_image.h"
#include <Windows.h>

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

struct vulkanContext {

    unsigned long layerCount;
    const char* layers;

    unsigned long extensionsCount;
    const char* extensions;

    unsigned long width;
    unsigned long height;

    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkSurfaceKHR surface;

    VkDevice device;
    VkPhysicalDevice physicalDevice;
    VkPhysicalDeviceProperties physicalDeviceProperties;

    unsigned long presentQueueIdx;
    VkQueue presentQueue;

    VkSwapchainKHR swapChain;
    VkImage* swapChainImages;
    VkFormat swapChainFormat;
    VkExtent2D swapChainExtent;

    VkImageView* swapChainImageViews;

}context;

//cpu
unsigned char memory[0xFFFF + 1] = { 0 }; //65536 bytes 
unsigned char cartridgeMemory[0x3FFF * 128] = { 0 }; //2MB of memory cartridge
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
int scanlineCounter = 0x1c8;
//modes of LCD
unsigned char LCDMode;
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
unsigned char pixelFIFOBG[256][2] = { 0 };

//interrupts and joypad
bool masterInterrupt = false; //Interrupt Master Enable Flag - IME
//when the cpu executes the enable interrupt instruction it will only take effect in the next instruction we need a delay to activate the IME
bool delayMasterInterrupt = false;
unsigned char joypadKeys = 0xFF;

//ram and MBC related parts
bool isRamEnabled = false;
bool MBC2Enabled = false;
bool MBC1Enabled = false;

bool MBC3Enabled = false;
//if this register goes from false to true, update the RTC registers (ram bank 0x8 through 0xC)
//ram bank 0x8 - RTC Seconds (0-59) (0x0-0x3B)
//ram bank 0x9 - RTC Minutes (0-59) (0x0-0x3B)
//ram bank 0xA - RTC Hours (0-23) (0x0-0x17)
//ram bank 0xB - RTC Lower 8bits of Day Counter (0x0-0xFF)
//ram bank 0xC - RTC Higher 8bits of Day Counter. Bits used: 
//                             Bit 0 (Most significant bit of Day Counter, bit 8)
//                             Bit 6 (Halt. 0 = Active, 1 = Stop Timer)
//                             Bit 7 (Day Counter Carry Bit. 1 = Counter Overflow)
bool latchClockRegister = false;
bool RTC_Increase = false;
unsigned char RTC_Seconds = 0;
unsigned char RTC_Minutes = 0;
unsigned char RTC_Hours = 0;
unsigned char RTC_LowDay = 0;
unsigned char RTC_HighDay = 0;

//we will assign the ram the total ram in according to the total ram banks later.
unsigned char* ram;
unsigned char ramBankNumber = 0;
//if false: ROM Banking Mode (up to 8KByte RAM, 2MByte ROM).   if true: RAM Banking Mode (up to 32KByte RAM, 512KByte ROM).
bool RomRamSELECT = false;
//the rom bank begins at 0x01 because rom bank 0 is the memory range 0x0000-0x3FFF of gameboy memory, 
unsigned char romBankNumber = 0x01;
//if we overflow the value it will wrap around. The max value is found 
unsigned short maxRomBankNumber;
unsigned char maxRamBankNumber;


//HALT
bool halt = false;

//SDL
SDL_Window* window = NULL;
SDL_Renderer* renderer = NULL;
SDL_Texture* texture = NULL;
unsigned int* pixels;
unsigned char WIDTH = 160;
unsigned char HEIGHT = 144;
int HandleEventCounter = 0;

bool drawFlag = false;
bool isRunning;
bool debuggingGraphics = false;
bool everytime = false;

//VULKAN

//////////////////////////////////////////////////FUNCTIONS//////////////////////////////////////////////////////////////////////////////////////////////
void print_binary(int number);
//sdl and Vulkan
void setupGraphics();
void setupVulkan();
void createVulkanInstance();
void createSurface();
void setupDebugMessenger();
void pickPhysicalDevice();
void pickLogicalDevice();
void getSwapChain();
void createImageViews();
void createGraphicsPipeline();
/*void vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
                          VkDependencyFlags dependencyFlags, unsigned long memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers,
                          unsigned long bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount,
                          const VkImageMemoryBarrier* pImageMemoryBarriers);*/
void checkVulkanResult(VkResult result, char* String);

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);
int main(int argc, char* argv[]);
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
void incRegisterToMemory(unsigned short memoryLocation, unsigned char cycles);
void decRegister(unsigned char* registerA, unsigned char cycles);
void decRegisterToMemory(unsigned short memoryLocation, unsigned char cycles);
void addRegister16Bit(unsigned short* registerA, unsigned short* registerB, unsigned char cycles);
void swap(unsigned char* registerA, unsigned char cycles);
void swapToMemory(unsigned short memoryLocation, unsigned char cycles);
void rlcRegister(unsigned char* registerA, unsigned char cycles);
void rlcRegisterToMemory(unsigned short memoryLocation, unsigned char cycles);
void RLCA(unsigned char cycles);
void rlRegister(unsigned char* registerA, unsigned char cycles);
void rlRegisterToMemory(unsigned short memoryLocation, unsigned char cycles);
void RLA(unsigned char cycles);
void rrcRegister(unsigned char* registerA, unsigned char cycles);
void rrcRegisterToMemory(unsigned short memoryLocation, unsigned char cycles);
void RRCA(unsigned char cycles);
void rrRegister(unsigned char* registerA, unsigned char cycles);
void rrRegisterToMemory(unsigned short memoryLocation, unsigned char cycles);
void RRA(unsigned char cycles);
void SLA(unsigned char* registerA, unsigned char cycles);
void SLAToMemory(unsigned short memoryLocation, unsigned char cycles);
void SRA(unsigned char* registerA, unsigned char cycles);
void SRAToMemory(unsigned short memoryLocation, unsigned char cycles);
void SRL(unsigned char* registerA, unsigned char cycles);
void SRLToMemory(unsigned short memoryLocation, unsigned char cycles);
void BIT(unsigned char* registerA, unsigned char bit, unsigned char cycles);
void SET(unsigned char* registerA, unsigned char bit, unsigned char cycles);
void SETToMemory(unsigned short memoryLocation, unsigned char bit, unsigned char cycles);
void RES(unsigned char* registerA, unsigned char bit, unsigned char cycles);
void RESToMemory(unsigned short memoryLocation, unsigned char bit, unsigned char cycles);
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
unsigned char readMemory(unsigned short memoryLocation);
void setClockFrequency();
void setLCDSTAT();
void drawScanLine();
void renderTiles();
void renderSprites();
void renderDebug();
void colorDebug(unsigned char MSB, unsigned char LSB, unsigned char xPixel, unsigned char yPixel);
void colorPallete(unsigned char MSB, unsigned char LSB, unsigned char xPixel, unsigned char yPixel, bool sprite);
void doInterrupts();
void setInterruptAddress(unsigned char bit);
void requestInterrupt(unsigned char bit);
bool testBit(unsigned char data, unsigned char bit);
void dmaTransfer(unsigned char data);
void doHalt();
void handleEvents();
void makeJoypadInterrupt(bool directional, unsigned char bit);
void joypad();
void getMBC();
void getMaxRomBankNumber();
void getMaxRamBankNumber();
void getRamEnable(unsigned char data);
void getRomBankNumber(unsigned char data);
void RomRamBankNumber(unsigned char data);
void RomRamModeSelect(unsigned char data);
void increaseRtcTimers();
void updateRtcRegisters();
void memoryCopy(bool copyRam, bool copyRom, unsigned long newOffset, unsigned long oldOffset);
void quitGame();

int main(int argc, char* argv[])
{
    loadGame("oi");
    initialize();
    setupGraphics();
    setupVulkan();

    //seeing what MBC bank is used.
    if (memory[0x147] != 0)
    {
        getMBC();
        getMaxRomBankNumber();
        getMaxRamBankNumber();
    }
    unsigned long ramSize = (maxRamBankNumber * 0x2000);
    ram = (unsigned char*)malloc(sizeof(unsigned char) * ramSize);
    if (ram == NULL)
    {
        printf("Couldn't create ram");
    }
    pixels = (unsigned int*)malloc(sizeof(unsigned int) * (160 * 144));
    if (pixels == NULL)
    {
        printf("couldn't create pixels");
        exit(0);
    }
    while (isRunning == true)
    {
        cyclesBeforeLCDRender = 0;
        while (cyclesBeforeLCDRender < (maxCycleBeforeRender))
        {
            emulateCycle();
            doInterrupts();

        }
        handleEvents();
        SDL_UpdateTexture(texture, NULL, pixels, 160 * sizeof(unsigned int));
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
        //printf("rendered");
        /*SDL_RenderPresent(renderer);
        SDL_RenderClear(renderer);*/
    }
    void quitGame();
    //printf("%c-------%c\n", memory[0xFF01], memory[0xFF02]);
    return 0;
}

void initialize()
{
    //Initializing PC
    pc = PCStart;

    //initializing registers
    AF.AF = 0x01B0;
    BC.BC = 0x0013;
    DE.DE = 0x00D8;
    HL.HL = 0x014D;
    memory[0xFF00] = 0xCF; //JOYPAD
    memory[TIMA] = 0x00; //TIMA
    memory[TMA] = 0x00; //TMA
    memory[TMC] = 0x00; //TAC
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
    memory[LCDC] = 0x91; //LCDC
    memory[STAT] = 0x80;
    memory[0xFF42] = 0x00; //SCY
    memory[0xFF43] = 0x00; //SCX
    memory[0xFF45] = 0x00; //LYC
    memory[0xFF47] = 0xFC; //BGP
    memory[0xFF48] = 0xFF; //0BP0
    memory[0xFF49] = 0xFF; //0BP1
    memory[0xFF4A] = 0x00; //WY
    memory[0xFF4B] = 0x00; //WX
    memory[IF] = 0xE1;
    memory[0xFFFF] = 0x00; //IE

    sp = StackStart;
}

void loadGame(char* gameName)
{
    //opening file in binary form
    FILE* file = fopen("C:\\Users\\xerather\\source\\repos\\Gameboy emulator\\Gameboy emulator\\Games\\Akumajou Dracula - Shikkokutaru Zensoukyoku (J) [S][!].gb", "rb");
    //"C:\\Users\\xerather\\source\\repos\\Gameboy emulator\\Gameboy emulator\\Tests\\Gekkio_MooneyeGB_Tests\\ram_64kb.gb"
    if (file == NULL) {
        printf("File not found");
        exit(EXIT_FAILURE);
    }
    else
    {
        //going to end of file
        fseek(file, 0, SEEK_END);
        //getting size of file
        unsigned long long bufferSize = ftell(file);
        printf("file size: %lld", bufferSize);
        rewind(file);
        if (bufferSize > (0xFFFF))
        {
            printf("size of file is too big for gameboy, using cartridgeMemory\n");
            char* inCartridge = (char*)malloc(sizeof(char) * bufferSize);
            if (inCartridge == NULL)
            {
                printf("Couldn't allocate inCartridge\n");
                exit(EXIT_FAILURE);
            }
            unsigned long long cartridgeResult = fread(inCartridge, sizeof(char), bufferSize, file);
            if (cartridgeResult != bufferSize)
            {
                printf("couldn't read file");
                exit(0);
            }

            for (int i = 0; i < bufferSize; i++)
            {
                cartridgeMemory[i] = inCartridge[i];
            }
            free(inCartridge);

            //fitting the size of memory into the gameboy memory;
            bufferSize = 0xFFFF;
        }
        rewind(file);
        //going to beginning of file
        //setting the size of char array and reading the binary file in unsigned char form
        char* in = (char*)malloc(sizeof(char) * bufferSize);
        if (in == NULL)
        {
            printf("out of memory\n");
            exit(EXIT_FAILURE);
        }
        //reading file
        unsigned long long result = fread(in, sizeof(char), bufferSize, file);
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

void setupGraphics()
{
    //SDL_INIT_EVERYTHING
    if (SDL_Init(SDL_INIT_EVENTS) == 0) {
        printf("entering here\n");
        window = SDL_CreateWindow("title", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 320, 288, SDL_WINDOW_VULKAN);
        //window = SDL_CreateWindow("title", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 320, 288, SDL_WINDOW_SHOWN);
        renderer = SDL_CreateRenderer(window, -1, 0);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, 160, 144);
        if (window != NULL)
        {
            printf("window created\n");
        }
        if (renderer != NULL)
        {
            printf("renderer created\n");
        }
        SDL_RenderSetLogicalSize(renderer, 160, 144);
        //SDL_RenderSetLogicalSize(renderer, 160, 144);
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

void setupVulkan()
{
    context.width = 320;
    context.height = 288;

    createVulkanInstance();
    createSurface();
    setupDebugMessenger();
    pickPhysicalDevice();
    pickLogicalDevice();
    getSwapChain();
}

void createVulkanInstance()
{

    //Informations about the APP
    //I'm not sure why we couldn't initialize between braces, = { appInfo.sType = ...}
    VkApplicationInfo appInfo = { 0 };
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pNext = NULL;
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pApplicationName = "GorodBoy";
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;


    //creating the info about the APP
    VkInstanceCreateInfo createInfo = { 0 };
    createInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledLayerCount = 0;
    createInfo.ppEnabledLayerNames = NULL;
    createInfo.enabledExtensionCount = 0;
    createInfo.ppEnabledExtensionNames = NULL;

    //getting the list of all available layers that we have
    unsigned long layerCount = 0;
    checkVulkanResult(vkEnumerateInstanceLayerProperties(&layerCount, NULL), "Couldn't enumerate layer properties of instance");
    VkLayerProperties* layersAvailable = (VkLayerProperties*)malloc(layerCount * sizeof(VkLayerProperties));
    if (layersAvailable == NULL)
    {
        printf("Couldn't allocate layersAvailable");
        exit(0);
    }
    checkVulkanResult(vkEnumerateInstanceLayerProperties(&layerCount, layersAvailable), "Couldn't enumerate layer properties of instance");

    //finding if the layer we want is on the list
    bool foundValidation = false;
    for (unsigned long i = 0; i < layerCount; ++i)
    {
        if (strcmp(layersAvailable[i].layerName, "VK_LAYER_KHRONOS_validation") == 0)
        {
            foundValidation = true;
        }
    }

    //if it is, enable the said layer
    if (!foundValidation)
    {
        printf("Couldn't find the specified layer");
        exit(0);
    }
    const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
    unsigned long numberRequiredLayers = sizeof(layers) / sizeof(char*);
    createInfo.enabledLayerCount = numberRequiredLayers;
    createInfo.ppEnabledLayerNames = layers;

    //saving to use later
    //strlen does not include the NULL character in a string, so we add (+1 * numberRequiredLayers)

    context.layers = (const char*)malloc(numberRequiredLayers * sizeof(layers) + (1 * numberRequiredLayers));
    context.layers = *layers;
    context.layerCount = layerCount;

    //getting the list of all available extensions that we have
    unsigned long extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, NULL);
    VkExtensionProperties* extensionsAvailable = (VkExtensionProperties*)malloc(extensionCount * sizeof(VkExtensionProperties));
    if (extensionsAvailable == NULL)
    {
        printf("Couldn't allocate extensionsAvailable");
        exit(0);
    }
    vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, extensionsAvailable);

    //finding if the extensions we want is available
    const char* extensions[] = { "VK_KHR_surface", "VK_EXT_debug_utils", "VK_KHR_win32_surface" };
    if (extensions == NULL)
    {
        printf("Couldn't allocate extensionsSDL");
        exit(0);

    }
    unsigned long numberRequiredExtensions = sizeof(extensions) / sizeof(char*);
    unsigned long foundExtensions = 0;
    for (unsigned long i = 0; i < extensionCount; ++i)
    {
        for (unsigned long j = 0; j < numberRequiredExtensions; ++j)
        {
            if (strcmp(extensions[j], extensionsAvailable[i].extensionName) == 0)
            {
                foundExtensions++;
            }
        }
    }
    if (foundExtensions != numberRequiredExtensions)
    {
        printf("found extensions doesn't match the required number of extensions");
        exit(0);
    }
    //if it is, enable the extension
    createInfo.enabledExtensionCount = numberRequiredExtensions;
    createInfo.ppEnabledExtensionNames = extensions;

    context.extensions = (const char*)malloc(numberRequiredLayers * sizeof(extensions) + (1 * numberRequiredExtensions));
    context.extensions = *extensions;
    context.extensionsCount = numberRequiredExtensions;

    checkVulkanResult(vkCreateInstance(&createInfo, NULL, &context.instance), "Couldn't create vulkan instance");


}

void createSurface()
{
    if (!SDL_Vulkan_CreateSurface(window, context.instance, &context.surface))
    {
        printf("couldn't create SDL Vulkan surface");
        exit(0);
    }
}

void setupDebugMessenger()
{

    //loading the vkDestroyDebugUtilsMessengerEXT Function
    VkDebugUtilsMessengerCreateInfoEXT createInfo = { 0 };

    PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT;
    *(void**)&vkCreateDebugUtilsMessengerEXT = vkGetInstanceProcAddr(context.instance, "vkCreateDebugUtilsMessengerEXT");

    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
    createInfo.pUserData = NULL;
    checkVulkanResult(vkCreateDebugUtilsMessengerEXT(context.instance, &createInfo, NULL, &context.debugMessenger), "Failed to setup debug messenger");
}

void pickPhysicalDevice()
{
    context.physicalDevice = VK_NULL_HANDLE;

    unsigned long devicesCount = 0;
    vkEnumeratePhysicalDevices(context.instance, &devicesCount, NULL);
    if (devicesCount == 0)
    {
        printf("There is no device with vulkan support");
        exit(0);
    }
    VkPhysicalDevice* physicalDevices = (VkPhysicalDevice*)malloc(devicesCount * sizeof(VkPhysicalDevice));
    if (physicalDevices == NULL)
    {
        printf("couldn't allocate memory for physicalDevices");
        exit(0);
    }
    checkVulkanResult(vkEnumeratePhysicalDevices(context.instance, &devicesCount, physicalDevices), "couldn't enumerate physical devices");

    for (unsigned long i = 0; i < devicesCount; ++i)
    {
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(physicalDevices[i], &deviceProperties);

        unsigned long queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevices[i], &queueFamilyCount, NULL);
        VkQueueFamilyProperties* queueFamilyProperties = (VkQueueFamilyProperties*)malloc(queueFamilyCount * sizeof(VkQueueFamilyProperties));
        if (queueFamilyProperties == NULL)
        {
            printf("couldn't allocate memory for queueFamilyProperties");
            exit(0);
        }
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevices[i], &queueFamilyCount, queueFamilyProperties);
        for (unsigned long j = 0; j < queueFamilyCount; ++j)
        {
            VkBool32 supportsPresent;
            vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevices[i], j, context.surface, &supportsPresent);

            if (supportsPresent && (queueFamilyProperties[j].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            {
                context.physicalDevice = physicalDevices[i];
                context.physicalDeviceProperties = deviceProperties;
                context.presentQueueIdx = j;
                break;
            }
        }
        free(queueFamilyProperties);

        if (context.physicalDevice == VK_NULL_HANDLE)
        {
            printf("couldn't find physicalDevice");
            exit(0);
        }
    }
    free(physicalDevices);
}

void pickLogicalDevice()
{
    VkDeviceQueueCreateInfo queueCreateInfo = { 0 };
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = context.presentQueueIdx;
    queueCreateInfo.queueCount = 1;
    const float queuePriorities = 0.0f;
    queueCreateInfo.pQueuePriorities = &queuePriorities;

    VkDeviceCreateInfo deviceInfo = { 0 };
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceInfo.enabledLayerCount = 1;
    deviceInfo.ppEnabledLayerNames = &context.layers;

    const char* deviceExtensions[] = { "VK_KHR_swapchain" };
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = deviceExtensions;

    VkPhysicalDeviceFeatures features = { 0 };
    features.shaderClipDistance = VK_TRUE;
    deviceInfo.pEnabledFeatures = &features;

    checkVulkanResult(vkCreateDevice(context.physicalDevice, &deviceInfo, NULL, &context.device), "couldn't create logical device");

    //storing present queue
    vkGetDeviceQueue(context.device, context.presentQueueIdx, 0, &context.presentQueue);
}

void getSwapChain()
{
    unsigned long formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(context.physicalDevice, context.surface, &formatCount, NULL);
    VkSurfaceFormatKHR* surfaceFormats = (VkSurfaceFormatKHR*)malloc(formatCount * sizeof(VkSurfaceFormatKHR));
    vkGetPhysicalDeviceSurfaceFormatsKHR(context.physicalDevice, context.surface, &formatCount, surfaceFormats);
    if (surfaceFormats == NULL)
    {
        printf("couldn't allocate memory to surfaceFormats");
        exit(0);
    }
    VkFormat colorFormat;
    if (formatCount == 1 && surfaceFormats[0].format == VK_FORMAT_UNDEFINED) {
        colorFormat = VK_FORMAT_B8G8R8_UNORM;
    }
    else
    {
        colorFormat = surfaceFormats[0].format;
    }
    VkColorSpaceKHR colorSpace;
    colorSpace = surfaceFormats[0].colorSpace;


    VkSurfaceCapabilitiesKHR surfaceCapabilities = { 0 };
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context.physicalDevice, context.surface, &surfaceCapabilities);


    unsigned long desiredImageCount = 2;
    if (desiredImageCount < surfaceCapabilities.minImageCount)
    {
        desiredImageCount = surfaceCapabilities.minImageCount;
    }
    else if (surfaceCapabilities.minImageCount != 0 && desiredImageCount > surfaceCapabilities.maxImageCount)
    {
        desiredImageCount = surfaceCapabilities.maxImageCount;
    }

    VkExtent2D surfaceResolution = surfaceCapabilities.currentExtent;
    if (surfaceResolution.width == -1)
    {
        surfaceResolution.width = context.width;
        surfaceResolution.height = context.height;
    }
    else
    {
        context.width = surfaceResolution.width;
        context.height = surfaceResolution.height;
    }

    VkSurfaceTransformFlagBitsKHR preTransform = surfaceCapabilities.currentTransform;
    if (surfaceCapabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
    {
        preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    }

    unsigned long presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(context.physicalDevice, context.surface, &presentModeCount, NULL);
    VkPresentModeKHR* presentModes = (VkPresentModeKHR*)malloc(presentModeCount * sizeof(VkPresentModeKHR));
    if (presentModes == NULL)
    {
        printf("couldn't allocate memory to presentModes");
        exit(0);
    }
    vkGetPhysicalDeviceSurfacePresentModesKHR(context.physicalDevice, context.surface, &presentModeCount, presentModes);

    VkPresentModeKHR presentationMode = VK_PRESENT_MODE_FIFO_KHR;

    for (unsigned long i = 0; i < presentModeCount; ++i)
    {
        if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            presentationMode = VK_PRESENT_MODE_MAILBOX_KHR;
            break;
        }
    }
    free(presentModes);

    VkSwapchainCreateInfoKHR swapChainCreateInfo = { 0 };
    swapChainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapChainCreateInfo.surface = context.surface;
    swapChainCreateInfo.minImageCount = desiredImageCount;
    swapChainCreateInfo.imageFormat = colorFormat;
    swapChainCreateInfo.imageColorSpace = colorSpace;
    swapChainCreateInfo.imageArrayLayers = 1;
    swapChainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapChainCreateInfo.preTransform = preTransform; // 90 degree rotation
    swapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapChainCreateInfo.presentMode = presentationMode;
    swapChainCreateInfo.clipped = VK_TRUE;
    swapChainCreateInfo.pQueueFamilyIndices = NULL;
    swapChainCreateInfo.imageExtent = surfaceResolution; // it doesnt work if we put it between the braces;

    checkVulkanResult(vkCreateSwapchainKHR(context.device, &swapChainCreateInfo, NULL, &context.swapChain), "Couldn't create swapChain");

    vkGetSwapchainImagesKHR(context.device, context.swapChain, &desiredImageCount, NULL);
    context.swapChainImages = (VkImage*)malloc(desiredImageCount * sizeof(VkImage));
    if (context.swapChainImages == NULL)
    {
        printf("couldn't create swapChainImages");
        exit(0);
    }
    vkGetSwapchainImagesKHR(context.device, context.swapChain, &desiredImageCount, context.swapChainImages);

    context.swapChainFormat = surfaceFormats->format;
    context.swapChainExtent = surfaceResolution;
    free(surfaceFormats);
}

void createImageViews()
{
    context.swapChainImageViews = (VkImageView*)malloc(sizeof(context.swapChainImages));
    if (context.swapChainImageViews == NULL)
    {
        printf("Couldn't create swap chain image views");
        exit(0);
    }

    unsigned int swapChainImagesSize = sizeof(context.swapChainImages) / sizeof(*context.swapChainImages);
    for (unsigned int i = 0; i < swapChainImagesSize; i++)
    {
        VkImageViewCreateInfo createInfo = { 0 };
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = context.swapChainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = context.swapChainFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;
        checkVulkanResult(vkCreateImageView(context.device, &createInfo, NULL, &context.swapChainImageViews[i]), "Couldn't create imageView");
    }
}

void createGraphicsPipeline()
{

}

/*void vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
                          VkDependencyFlags dependencyFlags, unsigned long memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers,
                          unsigned long bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount,
                          const VkImageMemoryBarrier* pImageMemoryBarriers)
{

}*/

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
    fprintf(stderr, "validation layer: %s\n", pCallbackData->pMessage);
    return VK_FALSE;
}


void checkVulkanResult(VkResult result, char* String)
{
    if (result != VK_SUCCESS)
    {
        printf("%s\n", String);
        exit(0);
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
    //pc == 0x0215
    //pc == 0x20A3
    //registers that isnt equal -> ff14, ff19, ff23, ff10
    //pc == 0x294
    //pc == 0x0434
    //pc 0x231c -> register FF00 wrong value -> ff00 == joypad
    //pc == 0x16b
    //pc == 0x131 && isRamEnabled == false
    //pc == 0xff84. -> pc == 0x48d5
    //pokemon red ----- second pc == 0x1D00
    //pc == 0xB5 && DE.DE == 0x8027
    //!RomRamSELECT && pc == 0xff84
    opcode = memory[pc];
    //memory[0xdef8] == 0x88
    //printf("checking opcode: [%X], pc: [%X], romBankNumber[%X], ramBankNumber[%X], LY[%X], AF.AF:[%X], BC.BC:[%X], DE.DE:[%X], HL.HL:[%X]\n", opcode, pc, romBankNumber, ramBankNumber, memory[LY], AF.AF, BC.BC, DE.DE, HL.HL);
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
        BC.B = readMemory(pc);
        clockTiming(8);
        break;
    case 0x0E:
        pc++;
        BC.C = readMemory(pc);
        clockTiming(8);
        break;
    case 0x16:
        pc++;
        DE.D = readMemory(pc);
        clockTiming(8);
        break;
    case 0x1E:
        pc++;
        DE.E = readMemory(pc);
        clockTiming(8);
        break;
    case 0x26:
        pc++;
        HL.H = readMemory(pc);
        clockTiming(8);
        break;
    case 0x2E:
        pc++;
        HL.L = readMemory(pc);
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
        AF.A = readMemory(HL.HL);
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
        BC.B = readMemory(HL.HL);
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
        BC.C = readMemory(HL.HL);
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
        DE.D = readMemory(HL.HL);
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
        DE.E = readMemory(HL.HL);
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
        HL.H = readMemory(HL.HL);
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
        HL.L = readMemory(HL.HL);
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
        writeInMemory(HL.HL, readMemory(pc));
        clockTiming(12);
        break;
    case 0x0A:
        AF.A = readMemory(BC.BC);
        clockTiming(8);
        break;
    case 0x1A:
        AF.A = readMemory(DE.DE);
        clockTiming(8);
        break;
    case 0xFA:
    {
        pc++;
        unsigned char LowerNibble = readMemory(pc);
        pc++;
        unsigned char HighNibble = readMemory(pc);
        unsigned short memoryLocation = (HighNibble << 8);
        memoryLocation |= LowerNibble;
        AF.A = readMemory(memoryLocation);;
        clockTiming(16);
        break;
    }
    case 0x3E:
        pc++;
        AF.A = readMemory(pc);
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
        unsigned char LowerNibble = readMemory(pc);
        pc++;
        unsigned char HighNibble = readMemory(pc);
        unsigned short memoryLocation = (HighNibble << 8);
        memoryLocation |= LowerNibble;
        writeInMemory(memoryLocation, AF.A);
        clockTiming(16);
        break;
    }
    case 0xF2:
    {
        unsigned short memoryLocation = 0xFF00 + BC.C;
        AF.A = readMemory(memoryLocation);
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
        AF.A = readMemory(HL.HL);
        HL.HL--;
        clockTiming(8);
        break;
    case 0x32:
        writeInMemory(HL.HL, AF.A);
        HL.HL--;
        clockTiming(8);
        break;
    case 0x2A:
        AF.A = readMemory(HL.HL);
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
        unsigned short memoryLocation = 0xFF00 + readMemory(pc);;
        writeInMemory(memoryLocation, AF.A);
        clockTiming(12);
        break;
    }
    case 0xF0:
    {
        pc++;
        unsigned short memoryLocation = 0xFF00 + memory[pc];
        AF.A = readMemory(memoryLocation);
        clockTiming(12);
        break;
    }
    case 0x01:
        pc++;
        BC.C = readMemory(pc);
        pc++;
        BC.B = readMemory(pc);
        clockTiming(12);
        break;
    case 0x11:
        pc++;
        DE.E = readMemory(pc);
        pc++;
        DE.D = readMemory(pc);
        clockTiming(12);
        break;
    case 0x21:
        pc++;
        HL.L = readMemory(pc);
        pc++;
        HL.H = readMemory(pc);
        clockTiming(12);
        break;
    case 0x31:
    {
        pc++;
        unsigned char LowerNibble = readMemory(pc);
        pc++;
        unsigned char HighNibble = readMemory(pc);
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
        signed char e8value = (signed char)readMemory(pc);
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
        unsigned char LowerNibble = readMemory(pc);
        pc++;
        unsigned char HighNibble = readMemory(pc);
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
    {
        unsigned char readValue = readMemory(HL.HL);
        addRegister(&AF.A, &readValue, 8);
        break;
    }
    case 0xC6:
    {
        pc++;
        unsigned char readValue = readMemory(pc);
        addRegister(&AF.A, &readValue, 8);
        break;
    }
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
    {
        unsigned char readValue = readMemory(HL.HL);
        adcRegister(&AF.A, &readValue, 8);
        break;
    }
    case 0xCE:
    {
        pc++;
        unsigned char readValue = readMemory(pc);
        adcRegister(&AF.A, &readValue, 8);
        break;
    }
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
    {
        unsigned char readValue = readMemory(HL.HL);
        subRegister(&AF.A, &readValue, 8);
        break;
    }
    case 0xD6:
    {
        pc++;
        unsigned char readValue = readMemory(pc);
        subRegister(&AF.A, &readValue, 8);
        break;
    }
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
    {
        unsigned char readValue = readMemory(HL.HL);
        sbcRegister(&AF.A, &readValue, 8);
        break;
    }
    case 0xDE:
    {
        pc++;
        unsigned char readValue = readMemory(pc);
        sbcRegister(&AF.A, &readValue, 8);
        break;
    }
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
    {
        unsigned char readValue = readMemory(HL.HL);
        andOperation(&readValue, 8);
        break;
    }
    case 0xE6:
    {
        pc++;
        unsigned char readValue = readMemory(pc);
        andOperation(&readValue, 8);
        break;
    }
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
    {
        unsigned char readValue = readMemory(HL.HL);
        orOperation(&readValue, 8);
        break;
    }
    case 0xF6:
    {
        pc++;
        unsigned char readValue = readMemory(pc);
        orOperation(&readValue, 8);
        break;
    }
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
    {
        unsigned char readValue = readMemory(HL.HL);
        xorOperation(&readValue, 8);
        break;
    }
    case 0xEE:
    {
        pc++;
        unsigned char readValue = readMemory(pc);
        xorOperation(&readValue, 8);
        break;
    }
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
    {
        unsigned char readValue = readMemory(HL.HL);
        cpRegister(&readValue, 8);
        break;
    }
    case 0xFE:
    {
        pc++;
        unsigned char readValue = readMemory(pc);
        cpRegister(&readValue, 8);
        break;
    }
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
        incRegisterToMemory(HL.HL, 12);
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
        decRegisterToMemory(HL.HL, 12);
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
        signed char e8value = (signed char)readMemory(pc);
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
        halt = true;
        doHalt();
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
            swapToMemory(HL.HL, 16);
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
            rlcRegisterToMemory(HL.HL, 16);
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
            rlRegisterToMemory(HL.HL, 16);
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
            rrcRegisterToMemory(HL.HL, 16);
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
            rrRegisterToMemory(HL.HL, 16);
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
            SLAToMemory(HL.HL, 16);
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
            SRAToMemory(HL.HL, 16);
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
            SRLToMemory(HL.HL, 16);
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
        {
            unsigned char readValue = readMemory(HL.HL);
            BIT(&readValue, 0, 16);
            break;
        }
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
        {
            unsigned char readValue = readMemory(HL.HL);
            BIT(&readValue, 1, 16);
            break;
        }
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
        {
            unsigned char readValue = readMemory(HL.HL);
            BIT(&readValue, 2, 16);
            break;
        }
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
        {
            unsigned char readValue = readMemory(HL.HL);
            BIT(&readValue, 3, 16);
            break;
        }
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
        {
            unsigned char readValue = readMemory(HL.HL);
            BIT(&readValue, 4, 16);
            break;
        }
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
        {
            unsigned char readValue = readMemory(HL.HL);
            BIT(&readValue, 6, 16);
            break;
        }
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
        {
            unsigned char readValue = readMemory(HL.HL);
            BIT(&readValue, 6, 16);
            break;
        };
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
        {
            unsigned char readValue = readMemory(HL.HL);
            BIT(&readValue, 8, 16);
            break;
        }
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
            SETToMemory(HL.HL, 0, 16);
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
            SETToMemory(HL.HL, 1, 16);
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
            SETToMemory(HL.HL, 2, 16);
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
            SETToMemory(HL.HL, 3, 16);
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
            SETToMemory(HL.HL, 4, 16);
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
            SETToMemory(HL.HL, 5, 16);
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
            SETToMemory(HL.HL, 6, 16);
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
            SETToMemory(HL.HL, 7, 16);
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
            RESToMemory(HL.HL, 0, 16);
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
            RESToMemory(HL.HL, 1, 16);
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
            RESToMemory(HL.HL, 2, 16);
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
            RESToMemory(HL.HL, 3, 16);
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
            RESToMemory(HL.HL, 4, 16);
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
            RESToMemory(HL.HL, 5, 16);
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
            RESToMemory(HL.HL, 6, 16);
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
            RESToMemory(HL.HL, 7, 16);
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

    if (HFlagCheck < ((e8value & 0xF) + carryBit))
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
    if (AF.A == *registerB)
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

void incRegisterToMemory(unsigned short memoryLocation, unsigned char cycles)
{
    clockTiming(cycles);
    //resetting N, Z and H flags
    AF.F &= 0b00010000;
    unsigned char registerA = readMemory(memoryLocation);
    //check if the lower nibble is 0b1111 to see if it will carry from bit 3. if it carries, set flag H;
    if ((registerA & 0xF) == 0xF)
    {
        AF.F |= 0b00100000;
    }

    //increments register
    registerA += 1;
    //if it overflows, set flag Z;
    if (registerA == 0)
    {
        AF.F |= 0b10000000;
    }
    writeInMemory(memoryLocation, registerA);
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

void decRegisterToMemory(unsigned short memoryLocation, unsigned char cycles)
{
    clockTiming(cycles);
    //setting N flag
    AF.F |= 0b01000000;

    unsigned char registerA = readMemory(memoryLocation);
    //check if the lower nibble is 0b0000 to see if it will borrow from bit 4. if it carries, set flag H;
    if ((registerA & 0xF) == 0)
    {
        AF.F |= 0b00100000;
    }
    //if it doesnt carry, reset flag
    else
    {
        AF.F &= 0b11010000;
    }
    //decrements register
    registerA -= 1;
    //if the result is 0, set flag Z
    if (registerA == 0)
    {
        AF.F |= 0b10000000;
    }
    else
    {
        AF.F &= 0b01110000;
    }
    writeInMemory(memoryLocation, registerA);
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

void swapToMemory(unsigned short memoryLocation, unsigned char cycles)
{
    clockTiming(cycles);
    AF.F = 0;

    unsigned char registerA = readMemory(memoryLocation);

    if (registerA == 0)
    {
        //set Z flag
        AF.F |= 0b10000000;
    }
    else
    {
        //swap lower nibble with upper nibble
        registerA = ((registerA & 0x0F) << 4) | ((registerA & 0xF0) >> 4);
    }
    writeInMemory(memoryLocation, registerA);
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
    if (*registerA == 0)
    {
        AF.F |= 0b10000000;
    }
}

void rlcRegisterToMemory(unsigned short memoryLocation, unsigned char cycles)
{
    clockTiming(cycles);

    unsigned char registerA = readMemory(memoryLocation);

    //storing the seventh bit of registerA
    unsigned char seventhBit = (registerA & 0b10000000);
    //copying the seventh bit of registerA to carry flag
    AF.F = ((seventhBit >> 3) | (AF.F & 0b11100000));
    //rotating registerA to the left
    registerA <<= 1;
    //setting bit 0 to the previous seventh bit
    registerA |= (seventhBit >> 7);

    //resetting N and H and Z flags
    AF.F &= 0b00010000;
    //if result of rotation is 0, set Z flag
    if (registerA == 0)
    {
        AF.F |= 0b10000000;
    }

    writeInMemory(memoryLocation, registerA);
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

void rlRegisterToMemory(unsigned short memoryLocation, unsigned char cycles)
{
    clockTiming(cycles);

    unsigned char registerA = readMemory(memoryLocation);

    //storing the carry flag
    unsigned char CFlag = (AF.F & 0b00010000);
    //copying the seventh bit of registerA to carry flag
    AF.F = (((registerA & 0b10000000) >> 3) | (AF.F & 0b11100000));
    //rotating registerA to the left
    registerA <<= 1;
    //setting bit 0 to previous carry flag
    registerA |= (CFlag >> 4);

    //resetting N and H flags
    AF.F &= 0b00010000;
    //if result of rotation is 0, set Z flag
    if (registerA == 0)
    {
        AF.F |= 0b10000000;
    }

    writeInMemory(memoryLocation, registerA);
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

void rrcRegisterToMemory(unsigned short memoryLocation, unsigned char cycles)
{
    clockTiming(cycles);

    unsigned char registerA = readMemory(memoryLocation);

    //storing the zeroth bit of registerA
    unsigned char zerothBit = (registerA & 0b00000001);
    //copying the zeroth bit of registerA to carry flag
    AF.F = ((zerothBit << 4) | (AF.F & 0b11100000));
    //rotating registerA to the right
    registerA >>= 1;
    //setting bit 7 to the previous zeroth bit
    registerA |= (zerothBit << 7);

    //resetting N and H flags
    AF.F &= 0b00010000;
    //if result of rotation is 0, set Z flag
    if (registerA == 0)
    {
        AF.F |= 0b10000000;
    }

    writeInMemory(memoryLocation, registerA);
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

void rrRegisterToMemory(unsigned short memoryLocation, unsigned char cycles)
{
    clockTiming(cycles);

    unsigned char registerA = readMemory(memoryLocation);

    //storing the carry flag
    unsigned char CFlag = (AF.F & 0b00010000);
    //copying the zeroth bit of registerA to carry flag
    AF.F = (((registerA & 0b00000001) << 4) | (AF.F & 0b11100000));
    //rotating registerA to the right
    registerA >>= 1;
    //setting bit 7 to the previous carry flag
    registerA |= (CFlag << 3);

    //resetting N and H and Z flags
    AF.F &= 0b00010000;
    //if result of rotation is 0, set Z flag
    if (registerA == 0)
    {
        AF.F |= 0b10000000;
    }

    writeInMemory(memoryLocation, registerA);
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

void SLAToMemory(unsigned short memoryLocation, unsigned char cycles)
{
    clockTiming(cycles);

    unsigned char registerA = readMemory(memoryLocation);

    //copying the seventh bit of registerA to carry flag
    AF.F = (((registerA & 0b10000000) >> 3) | (AF.F & 0b11100000));
    //resetting N and H and Z flags
    AF.F &= 0b00010000;

    //shifting bits to the left
    registerA <<= 1;

    //if result of rotation is 0, set Z flag
    if (registerA == 0)
    {
        AF.F |= 0b10000000;
    }

    writeInMemory(memoryLocation, registerA);
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

void SRAToMemory(unsigned short memoryLocation, unsigned char cycles)
{
    clockTiming(cycles);

    unsigned char registerA = readMemory(memoryLocation);

    //copying the zeroth bit of registerA to carry flag
    AF.F = (((registerA & 0b00000001) << 4) | (AF.F & 0b11100000));

    //resetting N and H and Z flags
    AF.F &= 0b00010000;

    registerA >>= 1;

    //copying the old MSB to the MSB position
    registerA |= ((registerA & 0b01000000) << 1);

    if (registerA == 0)
    {
        AF.F |= 0b10000000;
    }

    writeInMemory(memoryLocation, registerA);
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

void SRLToMemory(unsigned short memoryLocation, unsigned char cycles)
{
    unsigned char registerA = readMemory(memoryLocation);

    clockTiming(cycles);
    //copying the zeroth bit of registerA to carry flag
    AF.F = (((registerA & 0b00000001) << 4) | (AF.F & 0b11100000));
    //resetting N and H and Z flags
    AF.F &= 0b00010000;
    registerA >>= 1;
    if (registerA == 0)
    {
        AF.F |= 0b10000000;
    }

    writeInMemory(memoryLocation, registerA);
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

void SETToMemory(unsigned short memoryLocation, unsigned char bit, unsigned char cycles)
{
    clockTiming(cycles);
    //getting the value of b, b is the position of the bit that needs to be set, b = 0 - 7;

    unsigned char registerA = readMemory(memoryLocation);

    registerA |= ((0b00000001) << bit);

    writeInMemory(memoryLocation, registerA);
}

void RES(unsigned char* registerA, unsigned char bit, unsigned char cycles)
{
    clockTiming(cycles);
    //getting the value of b, b is the position of the bit that needs to be reset, b = 0 - 7;

    *registerA &= ~(1 << bit);

}

void RESToMemory(unsigned short memoryLocation, unsigned char bit, unsigned char cycles)
{
    clockTiming(cycles);
    //getting the value of b, b is the position of the bit that needs to be reset, b = 0 - 7;

    unsigned char registerA = readMemory(memoryLocation);

    registerA &= ~(1 << bit);

    writeInMemory(memoryLocation, registerA);
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
        memory[sp] = ((pc + 3) & 0xFF);
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
            unsigned char scanlineUnderflow = cycles - scanlineCounter;
            scanlineCounter -= cycles;
            if (scanlineCounter <= 0)
            {
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
                memory[LY]++;
                scanlineCounter = 456;
                scanlineCounter -= scanlineUnderflow;
            }
        }
    }
}

void drawScanLine()
{
    if (testBit(memory[LCDC], 0))
    {
        renderTiles();
    }

    if (testBit(memory[LCDC], 1))
    {
        renderSprites();
    }
}

void renderTiles()
{
    unsigned short locationOfTileData = 0;
    unsigned char offset = 0;
    bool unsign = true;
    //checking the position of memory the BG tile data is located, if testBit is true, BG = 0x8000-0x8FFF, else, 0x8800-0x97FF
    //this will determine where to search for the data of the tile number that needs to be displayed.
    if (testBit(memory[LCDC], 4))
    {
        locationOfTileData = 0x8000;
    }
    else
    {
        offset = 128;
        unsign = false;
        locationOfTileData = 0x8800;
    }

    unsigned short locationOfTileNumber = 0;
    //checking the position of memory the BG Tile Map Display is located, if testBit is true, BG = 9C00-9FFF, else, 9800-9BFF
    //this will determine where to search for the number of the tile that we will need to search.
    if (testBit(memory[LCDC], 3))
    {
        locationOfTileNumber = 0x9C00;
    }
    else
    {

        locationOfTileNumber = 0x9800;
    }
    //the position of memory of the tile number XX is: 0x8XX0;
    unsigned char tileNumber = 0;
    unsigned char MSB = 0;//most significant bit
    unsigned char LSB = 0;//less significant bit
    unsigned char lineOfTile[2];
    unsigned short memoryPosition;

    unsigned char ScrollY = memory[0xFF42];
    unsigned char ScrollX = memory[0xFF43];
    //in the BG map tile numbers every byte contains the number of the tile (position of tile in memory) to be displayed in a 32*32 grid, 
    //every tile hax 8*8 pixels, totaling 256*256 pixels that is drawn to the screen.
    //printf("%x", (0x9800) + (32 * y));
    for (int x = 0; x < 160; x++)
    {
        unsigned char xPos = (((x + ScrollX) / 8) & 0x1F);
        unsigned char yPos = ((memory[LY] + ScrollY));
        if (unsign)
        {
            tileNumber = memory[((locationOfTileNumber)+xPos) + ((yPos / 8) * 32)];
        }
        else
        {
            tileNumber = (signed char)memory[((locationOfTileNumber)+xPos) + ((yPos / 8) * 32)];
        }
        //printf("tileNumber: %X   lineOfTile[0]: ", tileNumber);

        //for every tile there is 16 bytes, for 2 bytes there is 8 pixels to be drawn. 
        //getting first byte of the line
        unsigned short tileLocation = locationOfTileData;

        if (unsign)
        {
            tileLocation += tileNumber * 16;
        }
        else
        {
            tileNumber += 128;
            tileLocation += tileNumber * 16;
        }
        //unsigned short bytePos = ((yPos % 8) * 2);
        unsigned char bytePos = (((yPos) % 8) * 2);
        memoryPosition = tileLocation + (bytePos);
        lineOfTile[0] = memory[memoryPosition];
        //printf("%X", 0x8000 + (tileNumber * 10) + (2 * yPixel));
        //getting second byte of the line
        memoryPosition = tileLocation + 1 + (bytePos);
        lineOfTile[1] = memory[memoryPosition];
        unsigned char pixelPosition = ((ScrollX + x) % 8);
        MSB = (lineOfTile[1] & (0b10000000 >> pixelPosition));
        MSB >>= (7 - pixelPosition);
        LSB = (lineOfTile[0] & (0b10000000 >> pixelPosition));
        LSB >>= (7 - pixelPosition);
        //ypos=2 && xpos = 2
        colorPallete(MSB, LSB, x, memory[LY], false);
        /*MSB = (lineOfTile[1] & (0b10000000 >> (x % 8)));
        MSB >>= (7 - (x%8));
        LSB = (lineOfTile[0] & (0b10000000 >> (x % 8)));
        LSB >>= (7 - (x % 8));
        colorPallete(MSB, LSB, x, memory[LY], false);*/
        /*for (int xPixel = 0; xPixel < 8; xPixel++)
        {
            MSB = (lineOfTile[1] & (0b10000000 >> xPixel));
            MSB >>= (7 - xPixel);
            LSB = (lineOfTile[0] & (0b10000000 >> xPixel));
            LSB >>= (7 - xPixel);
            colorPallete(MSB, LSB, x, memory[LY], false);
        }*/
        //printf("\n");
    }
}

void renderDebug()
{
    unsigned short locationOfTileData = 0;
    unsigned short memoryPosition;
    unsigned char bytePos = 0;
    unsigned char lineOfTile[2];
    unsigned char MSB = 0;
    unsigned char LSB = 0;
    //theres 320 bytes in the first 20 tiles.
    unsigned short yOffset = 320;
    unsigned char xOffset = 16;
    SDL_SetWindowSize(window, 320, 294);
    for (int y = 0; y < 18; y++)
    {
        for (int x = 0; x < 20; x++)
        {
            for (unsigned char yPixel = 0; yPixel < 8; yPixel++)
            {
                memoryPosition = 0x8000 + (yOffset * y) + (yPixel * 2) + (x * xOffset);
                lineOfTile[0] = memory[memoryPosition];
                //printf("%X", 0x8000 + (tileNumber * 10) + (2 * yPixel));
                //getting second byte of the line
                memoryPosition = 0x8000 + (yOffset * y) + (yPixel * 2) + (x * xOffset) + 1;
                lineOfTile[1] = memory[memoryPosition];
                for (unsigned char xPixel = 0; xPixel < 8; xPixel++)
                {
                    MSB = (lineOfTile[1] & (0b10000000 >> xPixel));
                    MSB >>= (7 - xPixel);
                    LSB = (lineOfTile[0] & (0b10000000 >> xPixel));
                    LSB >>= (7 - xPixel);
                    colorDebug(MSB, LSB, (x * 8) + xPixel, (y * 8) + yPixel);
                }
            }

        }
        SDL_RenderPresent(renderer);
    }

    while (debuggingGraphics)
    {
        handleEvents();
    }
    SDL_SetWindowSize(window, WIDTH * 2, HEIGHT * 2);
}

void colorDebug(unsigned char MSB, unsigned char LSB, unsigned char xPixel, unsigned char yPixel)
{
    switch (MSB)
    {
    case 0:
        switch (LSB)
        {
        case 0:
            //white color
                //SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_SetRenderDrawColor(renderer, 220, 255, 220, 255);
            return;
        case 1:
            //blue-green color
            //SDL_SetRenderDrawColor(renderer, 51, 97, 103, 255);
            SDL_SetRenderDrawColor(renderer, 41, 78, 82, 255);
            break;
        }
        break;
    case 1:
        switch (LSB)
        {
        case 0:
            //light green color
            //SDL_SetRenderDrawColor(renderer, 82, 142, 21, 255);
            SDL_SetRenderDrawColor(renderer, 66, 114, 17, 255);
            break;
        case 1:
            //dark green color
            //SDL_SetRenderDrawColor(renderer, 20, 48, 23, 255);
            SDL_SetRenderDrawColor(renderer, 16, 38, 18, 255);
            break;
        }
        break;
    }
    SDL_RenderDrawPoint(renderer, xPixel, yPixel);
}

void renderSprites()
{
    if (testBit(memory[LCDC], 1))
    {
        unsigned char spriteBytes[4] = { 0 };
        unsigned short memoryLocation;
        unsigned short tileNumber;

        for (int x = 0; x < 40; x++)
        {
            unsigned short byteStartPoint = 0xFE00 + (x << 2);
            for (int bytes = 0; bytes < 4; bytes++)
            {
                memoryLocation = byteStartPoint + bytes;
                spriteBytes[bytes] = memory[memoryLocation];
            }
            unsigned char spriteBytes0Minus16 = spriteBytes[0] - 16;
            if (spriteBytes0Minus16 > 0 && spriteBytes0Minus16 < 160)
            {
                unsigned char yPixel = 0;
                while ((spriteBytes0Minus16 + yPixel) <= memory[LY] && yPixel != 8)
                {
                    unsigned char MSB = 0;//most significant bit
                    unsigned char LSB = 0;//less significant bit
                    unsigned char lineOfTile[2];
                    unsigned char xPosition = spriteBytes[1];
                    unsigned char yPosition = memory[LY] - spriteBytes[0];

                    tileNumber = (0x8000 + (spriteBytes[2] << 4));
                    unsigned short yPixelX2 = yPixel << 1;
                    memoryLocation = (tileNumber)+(yPixelX2);
                    lineOfTile[0] = memory[memoryLocation];
                    memoryLocation = (tileNumber)+(yPixelX2)+1;
                    lineOfTile[1] = memory[memoryLocation];

                    for (unsigned char xPixel = 0; xPixel < 8; xPixel++)
                    {
                        //flip vertically
                        if (testBit(spriteBytes[3], 5))
                        {
                            MSB = (lineOfTile[1] & (0b00000001 << xPixel));
                            MSB >>= (xPixel);
                            LSB = (lineOfTile[0] & (0b00000001 << xPixel));
                            LSB >>= (xPixel);
                            //flip horizontaly
                            if (testBit(spriteBytes[3], 6))
                            {
                                colorPallete(MSB, LSB, ((xPosition - xPixel) + 8), (spriteBytes0Minus16 + yPixel), true);
                            }
                            else
                            {
                                colorPallete(MSB, LSB, ((xPosition + xPixel) - 8), (spriteBytes0Minus16 + yPixel), true);
                            }

                        }
                        else
                        {
                            MSB = (lineOfTile[1] & (0b10000000 >> xPixel));
                            MSB >>= (7 - xPixel);
                            LSB = (lineOfTile[0] & (0b10000000 >> xPixel));
                            LSB >>= (7 - xPixel);
                            if (testBit(spriteBytes[3], 6))
                            {
                                colorPallete(MSB, LSB, ((xPosition - xPixel) + 8), (spriteBytes0Minus16 + yPixel), true);
                            }
                            else
                            {
                                colorPallete(MSB, LSB, ((xPosition + xPixel) - 8), (spriteBytes0Minus16 + yPixel), true);
                            }
                        }
                    }
                    yPixel++;
                }
            }
        }
    }
}

void colorPallete(unsigned char MSB, unsigned char LSB, unsigned char xPixel, unsigned char yPixel, bool sprite)
{
    if (sprite)
    {
        switch (MSB)
        {
        case 0:
            switch (LSB)
            {
            case 0:
                //00 is transparent.
                return;
            case 1:
                //blue-green color
                //SDL_SetRenderDrawColor(renderer, 51, 97, 103, 255);
                pixels[(WIDTH * yPixel) + xPixel] = 0x294E52FF;
                //SDL_SetRenderDrawColor(renderer, 41, 78, 82, 255);
                break;
            }
            break;
        case 1:
            switch (LSB)
            {
            case 0:
                //light green color
                //SDL_SetRenderDrawColor(renderer, 82, 142, 21, 255);
                pixels[(WIDTH * yPixel) + xPixel] = 0x427211FF;
                //SDL_SetRenderDrawColor(renderer, 66, 114, 17, 255);
                break;
            case 1:
                //dark green color
                //SDL_SetRenderDrawColor(renderer, 20, 48, 23, 255);
                pixels[(WIDTH * yPixel) + xPixel] = 0x102612FF;
                //SDL_SetRenderDrawColor(renderer, 16, 38, 18, 255);
                break;
            }
            break;
        }
    }
    else
    {
        switch (MSB)
        {
        case 0:
            switch (LSB)
            {
            case 0:
                //white color
                //SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
                pixels[(WIDTH * yPixel) + xPixel] = 0xDCFFDCFF;
                //SDL_SetRenderDrawColor(renderer, 220, 255, 220, 255);
                break;
            case 1:
                //blue-green color
                //SDL_SetRenderDrawColor(renderer, 51, 97, 103, 255);
                pixels[(WIDTH * yPixel) + xPixel] = 0x294E52FF;
                //SDL_SetRenderDrawColor(renderer, 41, 78, 82, 255);
                break;
            }
            break;
        case 1:
            switch (LSB)
            {
            case 0:
                //light green color
                //SDL_SetRenderDrawColor(renderer, 82, 142, 21, 255);
                pixels[(WIDTH * yPixel) + xPixel] = 0x427211FF;
                //SDL_SetRenderDrawColor(renderer, 66, 114, 17, 255);
                break;
            case 1:
                //dark green color
                //SDL_SetRenderDrawColor(renderer, 20, 48, 23, 255);
                pixels[(WIDTH * yPixel) + xPixel] = 0x102612FF;
                //SDL_SetRenderDrawColor(renderer, 16, 38, 18, 255);
                break;
            }
            break;
        }
    }
    /*SDL_RenderDrawPoint(renderer, xPixel, yPixel);
    if (everytime)
    {

        SDL_RenderPresent(renderer);
    }*/
}

void setLCDSTAT()
{
    if (!testBit(memory[LCDC], 7))
    {
        memory[LY] = 0;
        scanlineCounter = 456;
        memory[STAT] &= 0b11111100;
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
        divCounter = CLOCKSPEED / FREQUENCY_11;
        memory[DIV]++;

        //RtcTimers increases in a rate of 32,768Hz, DIV increases in exactly half the time, so we can check if DIV increased 2 times after the last
        //increase of the RTC Timers and increase it. 
        if (MBC3Enabled)
        {
            increaseRtcTimers();
        }
    }

    //if bit 2 of register FF07 is 1, then the timer is enabled
    if (testBit(memory[TMC], 2))
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
            }
            else
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
            unsigned char interruptTest = memory[IF] & memory[IE];
            for (int i = 0; i < 5; i++)
            {
                if (testBit(interruptTest, i))
                {
                    setInterruptAddress(i);
                }
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
    RES(&memory[IF], bit, 4);
    unsigned char highByte = (pc >> 8);
    unsigned char lowByte = (pc & 0xFF);
    PUSH(&highByte, &lowByte);
    //if cpu is in halt mode, it takes 4 more cycles to complete the interrupt, to a total of 24 cycles.
    if (halt)
    {
        halt = false;
        clockTiming(4);
    }
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
    //this memory location is read only, we can write to specific memory locations to enable things of the MBC, but the values doesn't go through, 
    //it is intercepted and then interpreted by the MBC.
    if (memoryLocation < 0x8000)
    {
        if (memory[0x147] != 0)
        {
            if (memoryLocation <= 0x1FFF)
            {
                getRamEnable(data);
            }
            else if (memoryLocation <= 0x3FFF)
            {
                getRomBankNumber(data);
            }
            else if (memoryLocation <= 0x5FFF)
            {
                RomRamBankNumber(data);
            }
            else if (memoryLocation <= 0x7FFF)
            {
                RomRamModeSelect(data);
            }
        }
    }
    else if (memoryLocation >= 0xA000 && memoryLocation <= 0xBFFF)
    {
        if (isRamEnabled)
        {
            if (MBC1Enabled || MBC3Enabled)
            {
                /*if (ramBankNumber == 0 || !RomRamSELECT)
                {
                     cartridgeMemory[(memoryLocation - 0xA000)] = data;
                     memory[memoryLocation] = data;
                     return;
                }
                cartridgeMemory[(memoryLocation - 0xA000) + (ramBankNumber * 0x2000)] = data;*/
                ram[(memoryLocation - 0xA000) + (ramBankNumber * 0x2000)] = data;

                //memory[memoryLocation] = data;
                //cartridgeMemory[(memoryLocation - 0xA000) + (ramBankNumber * 0xA000)] = data;
            }
            else if (MBC2Enabled)
            {
                unsigned char memoryData = (data & 0x0F);
                ram[(memoryLocation - 0xA000) + (ramBankNumber * 0x2000)] = data;
                //memory[memoryLocation] = memoryData;
            }
        }
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
        unsigned char currentFrequency = memory[TMC] & 0b00000111;
        memory[TMC] = data;
        unsigned char newFrequency = memory[TMC] & 0b00000111;
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
    //bug on DMG models that triggers a STAT interrupt anytime the STAT register is written.
    else if (memoryLocation == STAT)
    {
        requestInterrupt(1);
    }
    else if (memoryLocation == 0xFF46)
    {
        dmaTransfer(data);
    }
    else if (memoryLocation == 0xFF00)
    {
        if ((data & 0x30) != 0x30)
        {
            memory[0xFF00] |= 0xF;
        }
        memory[0xFF00] = ((data & 0b00110000) | (memory[0xFF00] & 0b11001111));
    }
    else
    {
        memory[memoryLocation] = data;
    }
}

unsigned char readMemory(unsigned short memoryLocation)
{
    if (memoryLocation >= 0xA000 && memoryLocation <= 0xBFFF)
    {
        if (isRamEnabled)
        {
            if (MBC1Enabled)
            {
                return ram[(memoryLocation - 0xA000) + (ramBankNumber * 0x2000)];
                /*if (ramBankNumber == 0 || !RomRamSELECT)
                {
                    return cartridgeMemory[memoryLocation];
                }
                return cartridgeMemory[(memoryLocation - 0xA000) + (ramBankNumber * 0x2000)];*/
            }
            else if (MBC2Enabled)
            {
                unsigned char data = (ram[(memoryLocation - 0xA000) + (ramBankNumber * 0x2000)] & 0x0F);
                //(memory[(memoryLocation - 0xA000) + (ramBankNumber * 0xA000)] & 0x0F);
                data |= 0xF0;
                return data;
            }
            else if (MBC3Enabled)
            {
                if (ramBankNumber <= 0x7)
                {
                    return ram[(memoryLocation - 0xA000) + (ramBankNumber * 0x2000)];
                }
                else
                {
                    if (ramBankNumber == 0x8)
                    {
                        return RTC_Seconds;
                    }
                    if (ramBankNumber == 0x9)
                    {
                        return RTC_Minutes;
                    }
                    if (ramBankNumber == 0xA)
                    {
                        return RTC_Hours;
                    }
                    if (ramBankNumber == 0xB)
                    {
                        return RTC_LowDay;
                    }
                    if (ramBankNumber == 0xC)
                    {
                        return (RTC_HighDay & 0x1);
                    }
                }
            }
        }
        else
        {
            return 0xFF;
        }
    }
    if (memoryLocation >= 0x4000 && memoryLocation <= 0x7FFF)
    {
        return cartridgeMemory[(memoryLocation - 0x4000) + (romBankNumber * 0x4000)];
    }
    if (memoryLocation == 0xFF00)
    {
        joypad();
        return memory[memoryLocation];
    }
    if (memoryLocation == 0xFF70 || memoryLocation == 0xFF4F || memoryLocation == 0xFF4D)
    {
        return 0xFF;
    }
    if (memoryLocation == TMC)
    {
        unsigned char data = 0b11111000;
        data |= memory[TMC];
        return data;
    }
    if (memoryLocation == 0xFF0F)
    {
        unsigned char data = 0b11100000;
        data |= memory[0xFF0F];
        return data;
    }
    if (memoryLocation == STAT)
    {
        unsigned char data = 0b10000000;
        data |= memory[STAT];
        return data;
    }

    return memory[memoryLocation];

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

void doHalt()
{
    //halt has 2 possible interactions with the IME, if it is true then HALT is executed normally, cpu will stop executing instructions until an
    //interrupt is enabled and requested. When that happens the adress next to the HALT instruction is pushed onto the stack and the CPU will jump
    //to the interrupt adress. The IF flag of the interrupt is reset.

    if (masterInterrupt)
    {
        //it needs to be set true because we will check for it in the interrupt function and if it is true it will take 4 more cycles to complete.
        halt = true;
        if ((memory[IE] & memory[IF] & 0x1F) == 0)
        {
            while ((memory[IE] & memory[IF] & 0x1F) == 0)
            {
                if (scanlineCounter >= 114 && divCounter >= 114)
                {
                    clockTiming(114);
                }
                else if (scanlineCounter >= 32 && divCounter >= 32)
                {
                    clockTiming(40);
                }
                else
                {
                    clockTiming(4);
                }
                //increasing clock cycle by 4 until an interrupt is made;
            }
        }
        else
        {
            clockTiming(4);
        }
    }
    //if IME is false, it tests for 2 possible interactions. 
    //(IE & IF & 0x1F) = 0, it waits for an interrupt but doesnt jump or resets the flag, it just continues to the next instruction.
    //(IE & IF & 0x1F) != 0 HALT bug, the cpu fails to increase pc when executing the next instruction so it will execute twice. IF flags aren't cleared
    else
    {
        if ((memory[IE] & memory[IF] & 0x1F) == 0)
        {
            halt = true;
            while ((memory[IE] & memory[IF] & 0x1F) == 0)
            {
                //increasing clock cycle by 4 until an interrupt is made;
                if (scanlineCounter >= 114 && divCounter >= 114)
                {
                    clockTiming(114);
                }
                else if (scanlineCounter >= 20 && divCounter >= 20)
                {
                    clockTiming(20);
                }
                else
                {
                    clockTiming(4);
                }
            }
            //it takes 4 clock cycles to exit HALT mode after an interrupt is made.
            clockTiming(4);
        }
        else
        {
            //gets the instrunction after the HALT.
            pc++;
            //executes the instruction and increases pc by 1
            emulateCycle();
            //decreases by 2 because when HALT instrunction executes it will increment pc by 1.
            pc -= 2;
        }
    }
    halt = false;
}

void handleEvents()
{
    SDL_Event event;

    if (!testBit(memory[0xFF00], 4) && !testBit(memory[0xFF00], 5))
    {
        return;
    }
    while (SDL_PollEvent(&event))
    {

        switch (event.type)
        {
        case SDL_QUIT:
            isRunning = false;
            break;
        case SDL_KEYDOWN:
            switch (event.key.keysym.sym)
            {
            case SDLK_z:
                if (testBit(joypadKeys, 4))
                {
                    RES(&joypadKeys, 4, 0);
                    makeJoypadInterrupt(false, 4);
                }
                break;
            case SDLK_x:
                if (testBit(joypadKeys, 5))
                {
                    RES(&joypadKeys, 5, 0);
                    makeJoypadInterrupt(false, 1);
                }
                break;
            case SDLK_a:
                if (testBit(joypadKeys, 6))
                {
                    RES(&joypadKeys, 6, 0);
                    makeJoypadInterrupt(false, 2);
                }
                break;
            case SDLK_s:
                if (testBit(joypadKeys, 7))
                {
                    RES(&joypadKeys, 7, 0);
                    makeJoypadInterrupt(false, 3);
                }
                break;
            case SDLK_UP:
                if (testBit(joypadKeys, 2))
                {
                    RES(&joypadKeys, 2, 0);
                    makeJoypadInterrupt(false, 2);
                }
                break;
            case SDLK_DOWN:
                if (testBit(joypadKeys, 3))
                {
                    RES(&joypadKeys, 3, 0);
                    makeJoypadInterrupt(false, 3);
                }
                break;
            case SDLK_LEFT:
                if (testBit(joypadKeys, 1))
                {
                    RES(&joypadKeys, 1, 0);
                    makeJoypadInterrupt(false, 1);
                }
                break;
            case SDLK_RIGHT:
                if (testBit(joypadKeys, 0))
                {
                    RES(&joypadKeys, 0, 0);
                    makeJoypadInterrupt(false, 0);
                }
                break;
            }
            break;
            break;
        case SDL_KEYUP:
            switch (event.key.keysym.sym)
            {
            case SDLK_TAB:
                if (everytime)
                {
                    everytime = false;
                }
                else
                {
                    everytime = true;
                }
                break;
            case SDLK_SPACE:
                if (debuggingGraphics)
                {
                    debuggingGraphics = false;
                }
                else
                {
                    debuggingGraphics = true;
                    renderDebug();
                }
                break;
            case SDLK_UP:
                SET(&joypadKeys, 2, 0);
                break;
            case SDLK_DOWN:
                SET(&joypadKeys, 3, 0);
                break;
            case SDLK_LEFT:
                SET(&joypadKeys, 1, 0);
                break;
            case SDLK_RIGHT:
                SET(&joypadKeys, 0, 0);
                break;
            case SDLK_z:
                SET(&joypadKeys, 4, 0);
                break;
            case SDLK_x:
                SET(&joypadKeys, 5, 0);
                break;
            case SDLK_a:
                SET(&joypadKeys, 6, 0);
                break;
            case SDLK_s:
                SET(&joypadKeys, 7, 0);
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }

}

void makeJoypadInterrupt(bool directional, unsigned char bit)
{
    if (directional && !testBit(memory[0xFF00], 4))
    {
        requestInterrupt(4);
    }
    else if (!directional && !testBit(memory[0xFF00], 5))
    {
        requestInterrupt(4);
    }
}

void joypad()
{

    if (!testBit(memory[0xFF00], 4) && ((joypadKeys & 0x0F) != 0x0F))
    {
        unsigned char lowerByte = joypadKeys & 0x0F;
        lowerByte |= 0xF0;
        memory[0xFF00] |= 0xF;
        memory[0xFF00] &= lowerByte;
    }
    else if (!testBit(memory[0xFF00], 5) && ((joypadKeys & 0xF0) != 0xF0))
    {
        unsigned char lowerByte = (joypadKeys >> 4);
        lowerByte |= 0xF0;
        memory[0xFF00] |= 0xF;
        memory[0xFF00] &= lowerByte;
    }
}

void getMBC()
{
    if (memory[0x147] <= 0x3 && memory[0x147] != 0)
    {
        MBC1Enabled = true;
        return;
    }
    else if (memory[0x147] == 0x5 || memory[0x147] == 0x6)
    {
        MBC2Enabled = true;
    }
    else if (memory[0x147] >= 0xF && memory[0x147] <= 0x13)
    {
        MBC3Enabled = true;
    }

    return;
}

void getMaxRomBankNumber()
{
    switch (memory[0x148])
    {
    case 0x00:
        maxRomBankNumber = 0;
        break;
    case 0x01:
        maxRomBankNumber = 4;
        break;
    case 0x02:
        maxRomBankNumber = 8;
        break;
    case 0x03:
        maxRomBankNumber = 16;
        break;
    case 0x04:
        maxRomBankNumber = 32;
        break;
    case 0x05:
        maxRomBankNumber = 64;
        break;
    case 0x06:
        maxRomBankNumber = 128;
        break;
    case 0x07:
        maxRomBankNumber = 256;
        break;
    case 0x08:
        maxRomBankNumber = 512;
        break;
    case 0x52:
        maxRomBankNumber = 72;
        break;
    case 0x53:
        maxRomBankNumber = 80;
        break;
    case 0x54:
        maxRomBankNumber = 96;
        break;
    default:
        break;
    }
}

void getMaxRamBankNumber()
{
    switch (memory[0x0149])
    {
    case 00:
        maxRamBankNumber = 0;
        break;
    case 02:
        maxRamBankNumber = 1;
        break;
    case 03:
        maxRamBankNumber = 4;
        break;
    case 04:
        maxRamBankNumber = 16;
        break;
    case 05:
        maxRamBankNumber = 8;
        break;
    }
}

void getRamEnable(unsigned char data)
{
    if (MBC2Enabled)
    {
        if (testBit(data, 4))
        {
            return;
        }
    }
    if ((data & 0x0F) == 0x0A)
    {
        isRamEnabled = true;
    }
    else
    {
        isRamEnabled = false;
    }
}

void getRomBankNumber(unsigned char data)
{
    //storing bits 6 and 7 of romBankNumber

    if (MBC2Enabled)
    {
        if (!testBit(data, 4))
        {
            return;
        }
    }
    unsigned char oldBankNumber = romBankNumber;
    unsigned char upperBits = (romBankNumber & 0x60);
    romBankNumber = (data & 0x1F);
    if (MBC1Enabled && (romBankNumber & 0x1F) == 0)
    {
        romBankNumber = 0x01;
    }
    if (MBC3Enabled && romBankNumber == 0)
    {
        romBankNumber = 0x01;
        return;
    }
    if (!RomRamSELECT)
    {
        romBankNumber |= upperBits;
    }
    if (romBankNumber > maxRomBankNumber)
    {
        romBankNumber = (romBankNumber % maxRomBankNumber);
    }
    if (oldBankNumber != romBankNumber)
    {
        memoryCopy(false, true, (0x4000 * romBankNumber), (0x4000 * oldBankNumber));
    }
}

void RomRamBankNumber(unsigned char data)
{
    if (MBC2Enabled)
    {
        return;
    }
    //select the ram bank and resets bit 5 and 6 of romBankNumber
    if (MBC1Enabled)
    {
        if (RomRamSELECT)
        {
            ramBankNumber = (data & 0x03);
        }
        else
        {
            unsigned char upperBits = ((data & 0x03) << 5);
            romBankNumber |= (upperBits & 0x1F);
            if (maxRomBankNumber == 0)
            {
                romBankNumber = 1;
            }
            else if (romBankNumber > maxRomBankNumber)
            {
                romBankNumber = (romBankNumber % maxRomBankNumber);
            }
        }
    }
    else if (MBC3Enabled)
    {
        if (data <= 0x3)
        {
            ramBankNumber = (data & 0x03);
        }
        else if (data >= 0x8 && data <= 0x0C)
        {
            ramBankNumber = (data & 0x0F);
        }
    }
}

void RomRamModeSelect(unsigned char data)
{
    if (MBC1Enabled)
    {
        if (testBit(data, 0))
        {
            RomRamSELECT = true;
            romBankNumber &= 0x1F;
            if (romBankNumber == 0)
            {
                romBankNumber = 0x01;
            }
        }
        else
        {
            romBankNumber &= (ramBankNumber << 5);
            RomRamSELECT = false;
            ramBankNumber = 0;
        }
    }
    else if (MBC3Enabled)
    {
        if (testBit(data, 0))
        {
            if (latchClockRegister)
            {
                latchClockRegister = false;
            }
        }
        else
        {
            if (!latchClockRegister)
            {
                updateRtcRegisters();
            }
        }
    }
}

void increaseRtcTimers()
{
    if (testBit(RTC_HighDay, 6))
    {
        return;
    }
    else
    {
        if (RTC_Increase)
        {
            RTC_Seconds++;
            if (RTC_Seconds == 60)
            {
                RTC_Seconds = 0;
                RTC_Minutes++;
                if (RTC_Minutes == 60)
                {
                    RTC_Minutes = 0;
                    RTC_Hours++;
                    if (RTC_Hours == 24)
                    {
                        RTC_Hours = 0;
                        RTC_LowDay++;
                        if (RTC_LowDay == 255)
                        {
                            RTC_LowDay = 0;
                            if (testBit(RTC_HighDay, 0))
                            {
                                RES(&RTC_HighDay, 0, 0);
                                SET(&RTC_HighDay, 7, 0);
                            }
                            else
                            {
                                SET(&RTC_HighDay, 0, 0);
                            }
                        }
                    }
                }
            }
            RTC_Increase = false;
        }
        else
        {
            RTC_Increase = true;
        }
    }

}

void updateRtcRegisters()
{

}

void quitGame()
{
    //SDL
    SDL_DestroyWindow(window);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_Quit();

    //Vulkan
    unsigned int swapChainImagesSize = sizeof(context.swapChainImages) / sizeof(*context.swapChainImages);
    for (unsigned int i = 0; i < swapChainImagesSize; i++)
    {
        vkDestroyImageView(context.device, context.swapChainImageViews[i], NULL);
    }
    vkDestroyDevice(context.device, NULL);
    vkDestroySurfaceKHR(context.instance, context.surface, NULL);
    vkDestroyInstance(context.instance, NULL);
    //loading the vkDestroyDebugUtilsMessengerEXT Function
    //using the vkDestroyDebugUtilsMessengerEXT function
    PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT;
    *(void**)&vkDestroyDebugUtilsMessengerEXT = vkGetInstanceProcAddr(context.instance, "vkDestroyDebugUtilsMessengerEXT");
    vkDestroyDebugUtilsMessengerEXT(context.instance, context.debugMessenger, NULL);


    //pixel array buffer
    free(pixels);
}

void memoryCopy(bool copyRam, bool copyRom, unsigned long newOffset, unsigned long oldOffset)
{
    unsigned char tempValue = 0;
    if (copyRom)
    {
        for (unsigned int i = 0; i <= 0x3FFF; i++)
        {
            //tempValue = memory[0x4000 + i];
            memory[0x4000 + i] = cartridgeMemory[newOffset + i];
            // cartridgeMemory[oldOffset + i] = tempValue;
        }
    }
    else if (copyRam)
    {
        for (unsigned int i = 0; i <= 0x1FFF; i++)
        {
            //tempValue = memory[0xA000 + i];
            memory[0xA000 + i] = cartridgeMemory[oldOffset + i];
            //cartridgeMemory[oldOffset + i] = tempValue;
        }
    }
}

void print_binary(int number)
{
    if (number) {
        print_binary(number >> 1);
        putc((number & 1) ? '1' : '0', stdout);
    }
}