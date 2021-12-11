#include "std_Types.h"

#define FIFO_SIZE_OF_MESSAGE   5
#define BUFFER_SIZE_OF_PACKET  100

typedef struct
{
	uint16_t len;
	uint8_t  Buffer[BUFFER_SIZE_OF_PACKET];
}AppMessage_Type;

typedef struct
{
	AppMessage_Type Message[FIFO_SIZE_OF_MESSAGE];
}FiFoBuffer_Type;


void AppTest_Main(void);

