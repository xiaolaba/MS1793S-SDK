/*
    Copyright (c) 2018 Macrogiga Electronics Co., Ltd.

    Permission is hereby granted, free of charge, to any person 
    obtaining a copy of this software and associated documentation 
    files (the "Software"), to deal in the Software without 
    restriction, including without limitation the rights to use, copy, 
    modify, merge, publish, distribute, sublicense, and/or sell copies 
    of the Software, and to permit persons to whom the Software is 
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be 
    included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF 
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT 
    HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
    WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
    DEALINGS IN THE SOFTWARE.
*/


#include <string.h>
#include "HAL_conf.h"
#include "mg_api.h"
#include "bsp.h"
#include "oled.h"


/// Characteristic Properties Bit
#define ATT_CHAR_PROP_RD                            0x02
#define ATT_CHAR_PROP_W_NORSP                       0x04
#define ATT_CHAR_PROP_W                             0x08
#define ATT_CHAR_PROP_NTF                           0x10
#define ATT_CHAR_PROP_IND                           0x20 
#define GATT_PRIMARY_SERVICE_UUID                   0x2800

#define TYPE_CHAR      0x2803
#define TYPE_CFG       0x2902
#define TYPE_INFO      0x2901
#define TYPE_xRpRef    0x2907
#define TYPE_RpRef     0x2908
#define TYPE_INC       0x2802
#define UUID16_FORMAT  0xff


#define SOFTWARE_INFO "SV3.1.5"
#define MANU_INFO     "MacroGiga Bluetooth"
char DeviceInfo[12] =  "MS1793S-I2C";  /*max len is 24 bytes*/

u16 cur_notifyhandle = 0x12;  //Note: make sure each notify handle by invoking function: set_notifyhandle(hd);

#define FIFOLEN     5
#define MAX_SIZE    20
u8 txBuf[MAX_SIZE],rxBuf[FIFOLEN][MAX_SIZE];
u8 TxLen = 0;
u8 RxCnt[FIFOLEN];
u8 PosW = 0;
//static u8 PowR = 0;

u8* getDeviceInfoData(u8* len)
{
    *len = sizeof(DeviceInfo);
    return (u8*)DeviceInfo;
}

void updateDeviceInfoData(u8* name, u8 len)
{
    memcpy(DeviceInfo,name, len);
    ble_set_name(name,len);
}

/**********************************************************************************
                 *****DataBase****

01 - 06  GAP (Primary service) 0x1800
  03:04  name
07 - 0f  Device Info (Primary service) 0x180a
  0a:0b  firmware version
  0e:0f  software version
10 - 19  LED service (Primary service) 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
  11:12  6E400003-B5A3-F393-E0A9-E50E24DCCA9E(0x04)  RxNotify
  13     cfg
  14:15  6E400002-B5A3-F393-E0A9-E50E24DCCA9E(0x0C)  Tx
  16     cfg
  17:18  6E400004-B5A3-F393-E0A9-E50E24DCCA9E(0x0A)  BaudRate(No used)
  19     0x2901  info
************************************************************************************/

typedef struct ble_character16{
    u16 type16;          //type2
    u16 handle_rec;      //handle
    u8  characterInfo[5];//property1 - handle2 - uuid2
    u8  uuid128_idx;     //0xff means uuid16,other is idx of uuid128
}BLE_CHAR;

typedef struct ble_UUID128{
    u8  uuid128[16];//uuid128 string: little endian
}BLE_UUID128;

//
///STEP0:Character declare
//
const BLE_CHAR AttCharList[] = {
// ======  gatt =====  Do NOT Change!!
    {TYPE_CHAR,0x0003, {ATT_CHAR_PROP_RD, 0x04,0, 0x00,0x2a}, UUID16_FORMAT},//name
    //05-06 reserved
// ======  device info =====    Do NOT Change if using the default!!!  
    {TYPE_CHAR,0x0008, {ATT_CHAR_PROP_RD, 0x09,0, 0x29,0x2a}, UUID16_FORMAT},//manufacture
    {TYPE_CHAR,0x000a, {ATT_CHAR_PROP_RD, 0x0b,0, 0x26,0x2a}, UUID16_FORMAT},//firmware version
    {TYPE_CHAR,0x000e, {ATT_CHAR_PROP_RD, 0x0f,0, 0x28,0x2a}, UUID16_FORMAT},//sw version
    
// ======  User service or other services added here =====  User defined  
    {TYPE_CHAR,0x0011, {ATT_CHAR_PROP_NTF,                     0x12,0, 0,0}, 1/*uuid128-idx1*/ },//RxNotify
    {TYPE_CFG, 0x0013, {ATT_CHAR_PROP_RD|ATT_CHAR_PROP_W}},//cfg    
    {TYPE_CHAR,0x0014, {ATT_CHAR_PROP_W|ATT_CHAR_PROP_W_NORSP, 0x15,0, 0,0}, 2/*uuid128-idx2*/ },//Tx    
    {TYPE_CHAR,0x0017, {ATT_CHAR_PROP_W|ATT_CHAR_PROP_RD,      0x18,0, 0,0}, 3/*uuid128-idx3*/ },//BaudRate
    {TYPE_INFO,0x0019, {ATT_CHAR_PROP_RD}}//description,"BaudRate"
};

const BLE_UUID128 AttUuid128List[] = {
    /*for supporting the android app [nRF UART V2.0], one SHOULD using the 0x9e,0xca,0xdc.... uuid128*/
    {0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,1,0,0x40,0x6e}, //idx0,little endian, service uuid
    {0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,3,0,0x40,0x6e}, //idx1,little endian, RxNotify
    {0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,2,0,0x40,0x6e}, //idx2,little endian, Tx
    {0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,4,0,0x40,0x6e}, //idx3,little endian, BaudRate
};

u8 GetCharListDim(void)
{
    return sizeof(AttCharList)/sizeof(AttCharList[0]);
}

//////////////////////////////////////////////////////////////////////////
///STEP1:Service declare
// read by type request handle, primary service declare implementation
void att_server_rdByGrType( u8 pdu_type, u8 attOpcode, u16 st_hd, u16 end_hd, u16 att_type )
{
 //!!!!!!!!  hard code for gap and gatt, make sure here is 100% matched with database:[AttCharList] !!!!!!!!!
                     
    if((att_type == GATT_PRIMARY_SERVICE_UUID) && (st_hd == 1))//hard code for device info service
    {
        //att_server_rdByGrTypeRspDeviceInfo(pdu_type);//using the default device info service
        //GAP Device Name
        u8 t[] = {0x00,0x18};
        att_server_rdByGrTypeRspPrimaryService(pdu_type,0x1,0x6,(u8*)(t),2);
        return;
    }
    else if((att_type == GATT_PRIMARY_SERVICE_UUID) && (st_hd <= 0x07))//hard code for device info service
    {
        //apply user defined (device info)service example
        u8 t[] = {0xa,0x18};
        att_server_rdByGrTypeRspPrimaryService(pdu_type,0x7,0xf,(u8*)(t),2);
        return;
    }
    
    else if((att_type == GATT_PRIMARY_SERVICE_UUID) && (st_hd <= 0x10)) //usr's service
    {
        att_server_rdByGrTypeRspPrimaryService(pdu_type,0x10,0x19,(u8*)(AttUuid128List[0].uuid128),16);
        return;
    }
    //other service added here if any
    //to do....

    ///error handle
    att_notFd( pdu_type, attOpcode, st_hd );
}

#ifdef I2CMASTER
extern unsigned char DispLen,DispUpdate,DispStr[];
#endif
///STEP2:data coming
///write response, data coming....
void ser_write_rsp(u8 pdu_type/*reserved*/, u8 attOpcode/*reserved*/, 
                   u16 att_hd, u8* attValue/*app data pointer*/, u8 valueLen_w/*app data size*/)
{
    u8 i;
    switch(att_hd)
    {
        case 0x18://BaudRate
            ser_write_rsp_pkt(pdu_type);
            break;
        case 0x15://Tx
#ifdef I2CMASTER
            for (i=0;i<valueLen_w;i++)
            {
                DispStr[i] = *(attValue+i);
            }
            DispStr[i] = '\0';
            
            DispLen++;
            if (DispLen > 7)
                DispLen = 0;
            DispUpdate = 1;
#else
            if ((TxLen+3+valueLen_w)<MAX_SIZE)//buff not overflow
            {
                txBuf[TxLen] = valueLen_w;
                txBuf[TxLen+1] = 0;	//dummy
                txBuf[TxLen+2] = 0;	//dummy
                for (i=0;i<valueLen_w;i++)
                {
                    txBuf[TxLen+i+3] = *(attValue+i);
                }
                TxLen += valueLen_w;
                TxLen += 3;
                PosW = 0;
                GPIO_ResetBits(GPIOA, GPIO_Pin_13);
            }
#endif
        case 0x12://cmd
        case 0x13://cfg  
            ser_write_rsp_pkt(pdu_type);  /*if the related character has the property of WRITE(with response) or TYPE_CFG, one MUST invoke this func*/      
            break;
        
        default:
            att_notFd( pdu_type, attOpcode, att_hd );	/*the default response, also for the purpose of error robust */
            break;
    }
 }

///STEP2.1:Queued Writes data if any
void ser_prepare_write(u16 handle, u8* attValue, u16 attValueLen, u16 att_offset)//user's call back api 
{
    //queued data:offset + data(size)
    //when ser_execute_write() is invoked, means end of queue write.
    
    //to do    
}
 
void ser_execute_write(void)//user's call back api 
{
    //end of queued writes  
    
    //to do...    
}

///STEP3:Read data
//// read response
void server_rd_rsp(u8 attOpcode, u16 attHandle, u8 pdu_type)
{
    u8 tab[3];
    u8  d_len;
    u8* ble_name = getDeviceInfoData(&d_len);
    
    switch(attHandle) //hard code
    {
        case 0x04: //GAP name
            att_server_rd( pdu_type, attOpcode, attHandle, ble_name, d_len);
            break;
                
        case 0x09: //MANU_INFO
            //att_server_rd( pdu_type, attOpcode, attHandle, (u8*)(MANU_INFO), sizeof(MANU_INFO)-1);
            att_server_rd( pdu_type, attOpcode, attHandle, get_ble_version(), strlen((const char*)get_ble_version())); //ble lib build version
            break;
        
        case 0x0b: //FIRMWARE_INFO
        {
            //do NOT modify this code!!!
            att_server_rd( pdu_type, attOpcode, attHandle, GetFirmwareInfo(),strlen((const char*)GetFirmwareInfo()));
            break;
        }
        
        case 0x0f://SOFTWARE_INFO
            att_server_rd( pdu_type, attOpcode, attHandle, (u8*)(SOFTWARE_INFO), sizeof(SOFTWARE_INFO)-1);
            break;
        
        case 0x13://cfg
            {
                u8 t[2] = {0,0};
                att_server_rd( pdu_type, attOpcode, attHandle, t, 2);
            }
            break;
        
        case 0x18://BaudRate
            att_server_rd( pdu_type, attOpcode, attHandle, tab, 3);
            break;
        
        case 0x19: //description
            #define MG_BaudRate "BaudRate"
            att_server_rd( pdu_type, attOpcode, attHandle, (u8*)(MG_BaudRate), sizeof(MG_BaudRate)-1);
            break;
        
        default:
            att_notFd( pdu_type, attOpcode, attHandle );/*the default response, also for the purpose of error robust */
            break;
    }
}

void server_blob_rd_rsp(u8 attOpcode, u16 attHandle, u8 dataHdrP,u16 offset)
{
}

//return 1 means found
int GetPrimaryServiceHandle(unsigned short hd_start, unsigned short hd_end,
                            unsigned short uuid16,   
                            unsigned short* hd_start_r,unsigned short* hd_end_r)
{
// example    
//    if((uuid16 == 0x1812) && (hd_start <= 0x19))// MUST keep match with the information save in function  att_server_rdByGrType(...)
//    {
//        *hd_start_r = 0x19;
//        *hd_end_r = 0x2a;
//        return 1;
//    }
    
    return 0;
}


//本回调函数可用于蓝牙模块端主动发送数据之用，协议栈会在系统允许的时候（异步）回调本函数，不得阻塞！！
void gatt_user_send_notify_data_callback(void)
{
    //to do if any ...
    //add user sending data notify operation ....
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
u8* getsoftwareversion(void)
{
    return (u8*)SOFTWARE_INFO;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////

static unsigned char gConnectedFlag=0;
char GetConnectedStatus(void)
{
    return gConnectedFlag;
}

void ConnectStausUpdate(unsigned char IsConnectedFlag) //porting api
{
    //[IsConnectedFlag] indicates the connection status
    
    LED_ONOFF(!IsConnectedFlag);

    if (IsConnectedFlag != gConnectedFlag)
    {
        gConnectedFlag = IsConnectedFlag;
    }

#ifdef I2CMASTER
    if (IsConnectedFlag)
        DispUpdate = 0xff;
    else{
        OLED_Init();
        OLED_DispLogo();
    }
#else
    if (IsConnectedFlag)
    {
        memset(RxCnt,0,FIFOLEN);
    }
#endif
}
unsigned char RevOverNo=0;
unsigned char BleSendNo=0;

void UsrProcCallback(void) //porting api
{
    u8 SendNum;
    
    IWDG_ReloadCounter();
    
    if (GetConnectedStatus())
    {     
        if (RevOverNo > BleSendNo)
        {
            SendNum = sconn_notifydata(rxBuf[BleSendNo],RxCnt[BleSendNo]);
            if (SendNum == RxCnt[BleSendNo])
            {
                RxCnt[BleSendNo] = 0;
                BleSendNo++;
            }
            
            if (BleSendNo == RevOverNo)
            {
                BleSendNo = 0;
                RevOverNo = 0;
            }
        }
    }
}

void UsrProcCallback_Central(u8 fin, u8* dat_rcv, u8 dat_len)
{
}
void gatt_client_send_callback(void)
{
}
void att_cli_receive_callback(u16 att_hd, u8* attValue/*app data pointer*/, u8 valueLen_w/*app data size*/)
{
}


#ifdef I2CSLAVE
void I2C1_IRQHandler(void)
{
    u8 temp = 0;
    
    if ((I2C1->IC_INTR_STAT & 0x0004)==0x0004)
    {
        I2C_ClearITPendingBit(I2C1,I2C_IT_RX_FULL);
        rxBuf[RevOverNo][RxCnt[RevOverNo]++] = I2C1->IC_DATA_CMD;
        if(RxCnt[RevOverNo] > MAX_SIZE)
        {
            RxCnt[RevOverNo] = 0;
        }
    }
    if((I2C1->IC_INTR_STAT & 0x0020)==0x0020)
    {
        I2C_ClearITPendingBit(I2C1,I2C_IT_RD_REQ);
        if((I2C1->IC_RAW_INTR_STAT & 0x0010)==0x0010)
        {
            if (PosW < TxLen)
            {
                I2C1->IC_DATA_CMD = txBuf[PosW++];
                temp = I2C1->IC_CLR_RD_REQ;//dummy
                if (PosW == TxLen)
                {
                    TxLen = 0;
                    GPIO_SetBits(GPIOA, GPIO_Pin_13);
                }
            }
            else//over
            {
            }
        }
    }
    
    if((I2C1->IC_INTR_STAT & 0x0200)==0x0200)
    {
        I2C_ClearITPendingBit(I2C1,I2C_IT_STOP_DET);
        
        if (RevOverNo < FIFOLEN)
            RevOverNo++;
    }
}
#endif
