/* The few mupen64plus plugin API declarations the audio test plugins need
 * (types as in mupen64plus-core's api/m64p_plugin.h, API version 2). */
#ifndef M64P_MIN_H
#define M64P_MIN_H

#define EXPORT __attribute__((visibility("default")))
#define CALL

typedef void *m64p_dynlib_handle;
typedef int m64p_error;
typedef int m64p_plugin_type;
#define M64ERR_SUCCESS 0
#define M64ERR_INPUT_INVALID 4
#define M64PLUGIN_RSP 1
#define M64PLUGIN_AUDIO 3

typedef struct {
    unsigned char *RDRAM;
    unsigned char *DMEM;
    unsigned char *IMEM;
    unsigned int *MI_INTR_REG;
    unsigned int *SP_MEM_ADDR_REG;
    unsigned int *SP_DRAM_ADDR_REG;
    unsigned int *SP_RD_LEN_REG;
    unsigned int *SP_WR_LEN_REG;
    unsigned int *SP_STATUS_REG;
    unsigned int *SP_DMA_FULL_REG;
    unsigned int *SP_DMA_BUSY_REG;
    unsigned int *SP_PC_REG;
    unsigned int *SP_SEMAPHORE_REG;
    unsigned int *DPC_START_REG;
    unsigned int *DPC_END_REG;
    unsigned int *DPC_CURRENT_REG;
    unsigned int *DPC_STATUS_REG;
    unsigned int *DPC_CLOCK_REG;
    unsigned int *DPC_BUFBUSY_REG;
    unsigned int *DPC_PIPEBUSY_REG;
    unsigned int *DPC_TMEM_REG;
    void (*CheckInterrupts)(void);
    void (*ProcessDlistList)(void);
    void (*ProcessAlistList)(void);
    void (*ProcessRdpList)(void);
    void (*ShowCFB)(void);
} RSP_INFO;

typedef struct {
    unsigned char *RDRAM;
    unsigned char *DMEM;
    unsigned char *IMEM;
    unsigned int *MI_INTR_REG;
    unsigned int *AI_DRAM_ADDR_REG;
    unsigned int *AI_LEN_REG;
    unsigned int *AI_CONTROL_REG;
    unsigned int *AI_STATUS_REG;
    unsigned int *AI_DACRATE_REG;
    unsigned int *AI_BITRATE_REG;
    void (*CheckInterrupts)(void);
} AUDIO_INFO;

#endif
