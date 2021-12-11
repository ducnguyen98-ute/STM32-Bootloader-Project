#include "app_test.h"
#include "user_uart.h"
#include "user_spi.h"
#include "user_timer.h"

#include "memflash.h"

#include "ufs.h"


#define FIRMWARE_VERSION     0x40     // version 1.0.0
#define PACKET_PONG_SIZE     4

/* Các quy định về thời gian của các ctr */
#define TIMEOUT_SESSION      2000     // 2 s,     Thời gian sống của 1 session, nếu vào trong session mà ko làm gì thì sau 2s sẽ bị thoát
#define TIMELIVE             2000     // 2 s, 	  Time live của Board
#define CYCLE_SENDPONG       200      // 200 ms,  Thời gian giữa các lần Send Pong (có thể hiểu là thời gian Delay giữa các lần Send Pong)

#define NUMBER_SESSION       3
#define SESSION_DEFAULT_ID   0
#define SESSION_RW_ID        1

#define ERROR_CODE_ACCESS_FAIL    0x0001

/* Chế độ tồn tại của Board */
#define ENABLE               1
#define DISABLE              0


typedef void (*Session_Type)(void); // con trỏ hàm có nhiệm vụ switch giữa các Session

Ufs_PathTypes Fpath ={

		.u8PartitionID =  0,
		.u8Path = (uint8_t *)"sys/user/",
		.BytesMode = UFS_FILE_CREATE|UFS_FILE_READ|UFS_FILE_WRITE|UFS_FILE_WRITE_APPEND     // cấp các quyền làm việc với Ufs
};																							// hiện tại đang cấp 4 quyền

Ufs_ItemTypes FileInfo;

void Session_Default(void);
void Session_ReadWrite(void);

const uint8_t PacketPong[PACKET_PONG_SIZE]= {'B','F','4',FIRMWARE_VERSION};
const Session_Type Session_Current[NUMBER_SESSION] = {
		Session_Default,
		Session_ReadWrite
};

uint8_t datar[50];
int16_t remain = 0;
uint64_t id =0;
uint16_t CountTimeLive = 0; // biến đếm dùng để xác định Time Live của Board
uint16_t ErrorCode = 0;

uint8_t BoardEnable = DISABLE;
uint8_t SendPong = DISABLE;

uint8_t SessionID = SESSION_DEFAULT_ID;

uint8_t IDPacket = 0;

void UartRx_Handle(void);
void PingMessHandler(void);

uint8_t data[512] = {0};

uint16_t numberItem;



Ufs_ItemTypes FileInfo;

uint8_t chung[2]= {0,0};
uint32_t ParUsedSize = 0;

FiFoBuffer_Type ReadWriteSessionRxBuffer;

void Session_ReadWrite(void)
{
   uint16_t CountTimeOut = 0;
   uint8_t CountMessage  = 0;

   uint8_t  FileName[20] = {0};
   uint8_t  NameLen  = 0;
   uint32_t FileLen  = 0;
   Std_ReturnType ret = E_NOT_OK;

   AppMessage_Type TxMessage;
   TxMessage.len = 0;
   CountMessage = 0;

   Ufs_PathTypes FilePath;
   Ufs_ItemTypes FileInfo;

   uint32_t LengData = 0;
   uint32_t BytesOffset = 0;

   FilePath.u8PartitionID = 0;

   uint8_t FolderName[100] = {0};

   memcpy(FolderName,"sys/download/",strlen("sys/download/"));


	while(1)
	{
		// Check Session
		if( SessionID != SESSION_RW_ID)
		{
			break;
		}

		// Timeout session handle
		if(++ CountTimeOut == TIMEOUT_SESSION) // chạy hết 2s thì sẽ chuyển lại về Session Default
		{
			CountTimeOut = 0;
			SessionID = SESSION_DEFAULT_ID;
		}

        // Message Handler
		if(ReadWriteSessionRxBuffer.Message[CountMessage].len != 0)
		{
			CountTimeOut = 0;
			// Handshake
			if(ReadWriteSessionRxBuffer.Message[CountMessage].Buffer[0] == 0x00)
			{
				uint8_t  MemReady = 0x00;
				uint16_t TimeCycleWrite = 0xFFFF;
				uint32_t MemFlashSize = 0;

				TimeCycleWrite =  4;
				MemReady       =  1;

				MemFlashSize = Ufs_GetUsedSize(0);
				MemFlashSize = Ufs_GetMaxSize(0) - MemFlashSize;

				TxMessage.Buffer[0] = 0x00;

				TxMessage.Buffer[1] = (MemFlashSize >> 24) & 0xFF;
				TxMessage.Buffer[2] = (MemFlashSize >> 16) & 0xFF;
				TxMessage.Buffer[3] = (MemFlashSize >> 8)  & 0xFF;
				TxMessage.Buffer[4] = (MemFlashSize >> 0)  & 0xFF;

				TxMessage.Buffer[5] = (TimeCycleWrite >> 8)  & 0xFF;
				TxMessage.Buffer[6] = (TimeCycleWrite >> 0)  & 0xFF;

				TxMessage.Buffer[7] = BUFFER_SIZE_OF_PACKET & 0xFF;
				TxMessage.Buffer[8] = MemReady;

				TxMessage.len = 9;
				User_Uart_AsyncTransmit(USER_UART_CHANNEL_USART1,TxMessage.Buffer,TxMessage.len);
			}

			if(ReadWriteSessionRxBuffer.Message[CountMessage].Buffer[0] == 0x01)
			{
				NameLen = ReadWriteSessionRxBuffer.Message[CountMessage].Buffer[1];
				memcpy(FileName,&ReadWriteSessionRxBuffer.Message[CountMessage].Buffer[2], 20);
				if(NameLen < 16)
				{
					FileName[NameLen] = 0;

					FileLen = ReadWriteSessionRxBuffer.Message[CountMessage].Buffer[22];
					FileLen = (FileLen << 8) | ReadWriteSessionRxBuffer.Message[CountMessage].Buffer[23];
					FileLen = (FileLen << 8) | ReadWriteSessionRxBuffer.Message[CountMessage].Buffer[24];
					FileLen = (FileLen << 8) | ReadWriteSessionRxBuffer.Message[CountMessage].Buffer[25];

					FilePath.u8Path = (char *)FolderName;

					FilePath.BytesMode |= UFS_FILE_CREATE;
					ret = Ufs_Open(FilePath, FileName, &FileInfo);
					FilePath.BytesMode &= ~UFS_FILE_CREATE;

					TxMessage.Buffer[0] = 0x01;
					TxMessage.Buffer[1] = ret;
					TxMessage.len = 2;

					User_Uart_AsyncTransmit(USER_UART_CHANNEL_USART1,TxMessage.Buffer,TxMessage.len);
				}
				else
				{
					TxMessage.Buffer[0] = 0x01;
					TxMessage.Buffer[1] = E_NOT_OK;
					TxMessage.len = 2;

					User_Uart_AsyncTransmit(USER_UART_CHANNEL_USART1,TxMessage.Buffer,TxMessage.len);
				}

			}

			if(ReadWriteSessionRxBuffer.Message[CountMessage].Buffer[0] == 0x02)
			{
				LengData = ReadWriteSessionRxBuffer.Message[CountMessage].Buffer[1];
				LengData = (LengData << 8) | ReadWriteSessionRxBuffer.Message[CountMessage].Buffer[2];

				ret = E_NOT_OK;

				if(FileInfo.status == UFS_FILE_EXIST)
				{
					FilePath.u8Path = (char *)FolderName;
					FilePath.BytesMode |= UFS_FILE_WRITE;
				    FilePath.BytesMode |= UFS_FILE_WRITE_APPEND;

					ret = Ufs_Open(FilePath, FileName, &FileInfo);

					if(FileInfo.size == 0)
					{
						ret = Ufs_Write(&FileInfo,&ReadWriteSessionRxBuffer.Message[CountMessage].Buffer[3],LengData);
					}
					else
					{
						ret = Ufs_WriteAppend(&FileInfo,&ReadWriteSessionRxBuffer.Message[CountMessage].Buffer[3],LengData);
					}
					FilePath.BytesMode &= ~UFS_FILE_WRITE;
				    FilePath.BytesMode &= ~UFS_FILE_WRITE_APPEND;
				}

				TxMessage.Buffer[0] = 0x02;
				TxMessage.Buffer[1] = ret;
				TxMessage.len = 2;
				User_Uart_AsyncTransmit(USER_UART_CHANNEL_USART1,TxMessage.Buffer,TxMessage.len);
			}

			if(ReadWriteSessionRxBuffer.Message[CountMessage].Buffer[0] == 0x03)
			{
				uint8_t FileData[BUFFER_SIZE_OF_PACKET - 1];
				int32_t RemainByte = 0;

				FilePath.u8Path = FolderName;
				FilePath.BytesMode = 0;
				FilePath.BytesMode |= UFS_FILE_READ;

				NameLen = ReadWriteSessionRxBuffer.Message[CountMessage].Buffer[1];
				memcpy(FileName,&ReadWriteSessionRxBuffer.Message[CountMessage].Buffer[2], 20);
				FileName[NameLen] = 0;

				ret = Ufs_Open(FilePath, FileName, &FileInfo);
				BytesOffset = 0;

				do
				{
					if(E_OK == Ufs_Read(&FileInfo,BytesOffset,FileData,BUFFER_SIZE_OF_PACKET - 1,&RemainByte))
					{
						TxMessage.Buffer[0] = 0x03;
						if(RemainByte > 0)
						{
							LengData = BUFFER_SIZE_OF_PACKET - 1;
							BytesOffset += LengData;
							memcpy(&TxMessage.Buffer[1],FileData,LengData);
							TxMessage.len = LengData + 1;
							User_Uart_AsyncTransmit(USER_UART_CHANNEL_USART1,TxMessage.Buffer,TxMessage.len);
						}
						else
						{
							LengData = RemainByte + BUFFER_SIZE_OF_PACKET - 1;
							memcpy(&TxMessage.Buffer[1],FileData,LengData);
							TxMessage.len = LengData + 1;
							User_Uart_AsyncTransmit(USER_UART_CHANNEL_USART1,TxMessage.Buffer,TxMessage.len);
						}
					}
					else
					{
						RemainByte = 0;
					}

				}while(RemainByte > 0);

				FilePath.BytesMode &= ~UFS_FILE_READ;
			}

			if(ReadWriteSessionRxBuffer.Message[CountMessage].Buffer[0] == 0x04)
			{
				memcpy(FolderName,&ReadWriteSessionRxBuffer.Message[CountMessage].Buffer[1], ReadWriteSessionRxBuffer.Message[CountMessage].len - 1);
				FolderName[ReadWriteSessionRxBuffer.Message[CountMessage].len - 1] = 0x00;

				TxMessage.Buffer[0] = 0x04;
				TxMessage.Buffer[1] = E_OK;
				TxMessage.len = 2;
				User_Uart_AsyncTransmit(USER_UART_CHANNEL_USART1,TxMessage.Buffer,TxMessage.len);
			}

			if(ReadWriteSessionRxBuffer.Message[CountMessage].Buffer[0] == 0x05)
			{
				NameLen = ReadWriteSessionRxBuffer.Message[CountMessage].Buffer[1];
				memcpy(FileName,&ReadWriteSessionRxBuffer.Message[CountMessage].Buffer[2], 20);
				if(NameLen < 16)
				{
					FileName[NameLen] = 0;
					ret = Ufs_Delete(&FilePath, FileName);
					FileInfo.ParentDir = 0;
					FileInfo.firtcluster = 0;
					FileInfo.size = 0;
					FileInfo.slotID = 0;
					FileInfo.status = UFS_ITEM_FREE;
				}
				else
				{
					ret = E_NOT_OK;
				}

				TxMessage.Buffer[0] = 0x05;
				TxMessage.Buffer[1] = ret;
				TxMessage.len = 2;
				User_Uart_AsyncTransmit(USER_UART_CHANNEL_USART1,TxMessage.Buffer,TxMessage.len);
			}

			ReadWriteSessionRxBuffer.Message[CountMessage].len = 0;
		}

		if(++ CountMessage == FIFO_SIZE_OF_MESSAGE)
		{
			CountMessage = 0;
		}


		HAL_Delay(1);
	}
}


void Session_Default(void) // Session xử lý bảng tin Pong và giữ "Keep Alive" cho Board
{
	uint16_t CountTimeSendPong = 0; // biến đếm dùng để xác định thời gian khi nào Send Pong

	SendPong = ENABLE;

 	while(1)
	{

		if( SessionID != SESSION_DEFAULT_ID) // điều kiện để thoát Session Default
		{
			break;
		}

		if(++ CountTimeLive == TIMELIVE) 					/* nếu ctr chạy hết 2 s thì ngắt kết nối Board */
		{
			CountTimeLive = 0; 			// đưa biến đếm CountTimeLive về lại ban đầu = 0
			BoardEnable = DISABLE; 		// khi hết Time Live thì DISABLE Board
		}

		if(++ CountTimeSendPong == CYCLE_SENDPONG) 			/* nếu ctr chạy hết 200 ms thì Send Pong */
		{
			CountTimeSendPong = 0;		// đưa biến đếm CountTimeSendPong về lại = 0
			if(BoardEnable == ENABLE)	// kích hoạt Board
			{
				SendPong = ENABLE;
			}
		}

		HAL_Delay(1);
	}
}

void AppTest_Main(void)
{
     User_Uart_Init(&User_Uart_ConfigPtr);

     Init_Timer(&Timer2_Cfg);
     Tim_AddEvent(TIMER2_HW,UartRx_Handle);
     Tim_AddEvent(TIMER2_HW,PingMessHandler);

     Ufs_Init(&Ufs_Cfg);

     for(uint16_t CountMessage = 0; CountMessage < FIFO_SIZE_OF_MESSAGE; CountMessage ++)
     {
  	   ReadWriteSessionRxBuffer.Message[CountMessage].len = 0;
     }


    while(1)
    {
    	Session_Current[SessionID](); // câu lệnh switch giữa các session (tại 1 thời điểm chỉ chạy 1 Session => Code sẽ rất nhẹ và nhanh)
    }

}


void UartRx_Handle(void)
{
	int16_t remain = 0;
	uint16_t length = 0;
	uint8_t datarx[BUFFER_SIZE_OF_PACKET + 1];


	if(E_OK == User_Uart_Received(USER_UART_CHANNEL_USART1,datarx,BUFFER_SIZE_OF_PACKET + 1,&remain))
	{
		length = remain + BUFFER_SIZE_OF_PACKET + 1;
		IDPacket = datarx[0];

		if(IDPacket == 0x00)
		{
			//SessionID = SESSION_DEFAULT_ID;

		}
		else if(IDPacket == 0x01)
		{

		}
		else if(IDPacket == 0x02)
		{
		    for(uint8_t CountMessage = 0; CountMessage < FIFO_SIZE_OF_MESSAGE; CountMessage ++)
			{
		    	if(ReadWriteSessionRxBuffer.Message[CountMessage].len == 0 && length > 1 && length < BUFFER_SIZE_OF_PACKET)
		    	{
		    		for(uint16_t CountByte = 0; CountByte < (length - 1); CountByte ++)
		    		{
		    			ReadWriteSessionRxBuffer.Message[CountMessage].Buffer[CountByte] = datarx[CountByte + 1];
		    		}
		    		ReadWriteSessionRxBuffer.Message[CountMessage].len = length - 1;
		    		break;
		    	}
			}
		    SessionID = SESSION_RW_ID;
		}
		else
		{

		}

	}
}

void PingMessHandler(void) 						/* Xử lý bản tin Ping từ PC xuống */
{
	if(SendPong == ENABLE)	// nếu biến SendPong đc bật
	{
		uint8_t DataTx[PACKET_PONG_SIZE] = {'B','F','4',FIRMWARE_VERSION}; // tạo bản tin Pong bắn lên PC

		SendPong = DISABLE;	// tắt SendPong
		User_Uart_AsyncTransmit(USER_UART_CHANNEL_USART1,DataTx,PACKET_PONG_SIZE); // gửi đi bản tin Pong
	}

}
