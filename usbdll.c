/* Replace "dll.h" with the name of your header */
#include "dll.h"
#include <windows.h>



#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
//#include <libusb-1.0/libusb.h>
#include "libusb.h"
#include <sys/types.h>
// Device vendor and product id.
#define IDVENDER                    0x2309
#define IDPRODUCT                   0x0606
#define ENCRYPT                     1
#define DECRYPT                     0

#define ENDPOINT_OUT                0x01
#define ENDPOINT_IN                 0x82
#define USB_LENGTH                  2
#define SM3_LENGTH                  34
#define RETRY_MAX                   5
#define ID_LENGTH                   10
// Device of bytes to transfer.
#define BUF_SIZE 64
libusb_device **devs; //pointer to pointer of device, used to retrieve a list of devices
//libusb_device_handle *handle; //a device handle
//libusb_context *ctx = NULL; //a libusb session
inline static int perr(char const *format, ...)
{
	va_list args;
	int r;

	va_start (args, format);
	r = vfprintf(stderr, format, args);
	va_end(args);
	return r;
}
//Command Block Wrapper (CBW)
struct command_block_wrapper {
	uint8_t dCBWSignature[4];
	uint32_t dCBWTag;
	uint32_t dCBWDataTransferLength;
	uint8_t bmCBWFlags;
	uint8_t bCBWLUN;
	uint8_t bCBWCBLength;
	uint8_t CBWCB[16];
};
//Command Status Wrapper (CSW)
struct command_status_wrapper {
	uint8_t dCSWSignature[4];
	uint32_t dCSWTag;
	uint32_t dCSWDataResidue;
	uint8_t bCSWStatus;
};

/******************************************************************************************************************/
/******************************************************Open device*************************************************/
libusb_device_handle * open_dev(libusb_device **devs,libusb_context *ctx)
{
        ssize_t cnt; //holding number of devices in list
	//libusb_reset_device(handle);
	int r; //for return values
       libusb_device_handle *handle;
	r = libusb_init(&ctx); //initialize the library for the session we just declared
    if(r < 0) {
        perror("Init Error\n"); //there was an error
        return NULL;
    }
    libusb_set_debug(ctx, LIBUSB_LOG_LEVEL_INFO); //set verbosity level to 3, as suggested in the documentation

    cnt = libusb_get_device_list(ctx, &devs); //get the list of devices
    if(cnt < 0) {
        perror("Get Device Error\n"); //there was an error
        return NULL;
    }
    printf("%d Devices in list.\n", cnt);

    handle = libusb_open_device_with_vid_pid(ctx, IDVENDER, IDPRODUCT); //these are vendorID and productID I found for my usb device  #liusb
    if(handle == NULL)
        perror("Cannot open device\n");
    else
        printf("Device Opened\n");
    libusb_free_device_list(devs, 1); //free the list, unref the devices in it

    if(libusb_kernel_driver_active(handle, 0) == 1) { //find out if kernel driver is attached
        printf("Kernel Driver Active\n");
        if(libusb_detach_kernel_driver(handle, 0) == 0) //detach it
            printf("Kernel Driver Detached!\n");
    }
    r = libusb_claim_interface(handle, 0); //claim interface 0 (the first) of device (mine had jsut 1)
    if(r < 0) {
        perror("Cannot Claim Interface\n");
        return NULL;
    }
    printf("Claimed Interface\n");
       return handle;
}
void close_dev(libusb_device_handle *handle,libusb_context *ctx)
{
	int r,success;
r = libusb_release_interface(handle,0);

	success = libusb_reset_device(handle);
if(success == 0)
	printf("reset success!!!\n");
	libusb_close(handle);
	libusb_exit(ctx);
}
/***********************************************************************************************************/
/***********************************************************************************************************/

/*host-to-device,设备接收CBW包*/
int send_mass_storage_command(libusb_device_handle *handle, uint8_t endpoint,uint8_t lun,
	uint8_t *cdb, uint8_t direction, int data_length, uint32_t *ret_tag)
{
	static uint32_t tag = 1;
	uint8_t cdb_len = 6;//发送的SCSI命令固定为6个字节
	int i, r, size;
	struct command_block_wrapper cbw;
	if (cdb == NULL) {
		return -1;
	}
	if (endpoint & LIBUSB_ENDPOINT_IN) {
		perr("send_mass_storage_command: cannot send command on IN endpoint\n");
		return -1;
	}
	if ((cdb_len == 0) || (cdb_len > sizeof(cbw.CBWCB))) {
		perr("send_mass_storage_command: don't know how to handle this command (%02X, length %d)\n",
			cdb[0], cdb_len);
		return -1;
	}

	memset(&cbw, 0, sizeof(cbw));
	cbw.dCBWSignature[0] = 'U';//0x55
	cbw.dCBWSignature[1] = 'S';//0x53
	cbw.dCBWSignature[2] = 'B';//0x42
	cbw.dCBWSignature[3] = 'C';//0x43
	*ret_tag = tag;
	cbw.dCBWTag = tag++;
	cbw.dCBWDataTransferLength = data_length;
	cbw.bmCBWFlags = direction;
	cbw.bCBWLUN = lun;
	// Subclass is 1 or 6 => cdb_len
	cbw.bCBWCBLength = cdb_len;
	memcpy(cbw.CBWCB, cdb, cdb_len);
	i = 0;
	do {
		// The transfer length must always be exactly 31 bytes.
		r = libusb_bulk_transfer(handle, endpoint, (unsigned char*)&cbw, 31, &size,0);
		if (r == LIBUSB_ERROR_PIPE) {
			libusb_clear_halt(handle, endpoint);
		}
		i++;
	} while ((r == LIBUSB_ERROR_PIPE) && (i<RETRY_MAX));
	printf("\n contorl message send success!!(sent %d CDB bytes)\n", cdb_len);
	return 0;
}
/****************************************************************************************************/
/****************************************************************************************************/
/*device-to-host,设备向主机返回CSW包*/
int get_mass_storage_status(libusb_device_handle *handle, uint8_t endpoint, uint32_t expected_tag)
{
	int i, r, size;
	struct command_status_wrapper csw;

	// The device is allowed to STALL this transfer. If it does, you have to
	// clear the stall and try again.
	i = 0;
	do {
		r = libusb_bulk_transfer(handle, endpoint, (unsigned char*)&csw, 13, &size, 0);
		if (r == LIBUSB_ERROR_PIPE) {
			libusb_clear_halt(handle, endpoint);
		}
		i++;
	} while ((r == LIBUSB_ERROR_PIPE) && (i<RETRY_MAX));
	if (size != 13) {
		perr("   get_mass_storage_status: received %d bytes (expected 13)\n", size);
		return -1;
	}
	if (csw.dCSWTag != expected_tag) {
		perr("   get_mass_storage_status: mismatched tags (expected %08X, received %08X)\n",
			expected_tag, csw.dCSWTag);
		return -1;
	}
	// For this test, we ignore the dCSWSignature check for validity...
	printf("   Mass Storage Status: %02X (%s)\n", csw.bCSWStatus, csw.bCSWStatus?"FAILED":"Success");
	if (csw.dCSWTag != expected_tag)
		return -1;
	if (csw.bCSWStatus) {
		// REQUEST SENSE is appropriate only if bCSWStatus is 1, meaning that the
		// command failed somehow.  Larger values (2 in particular) mean that
		// the command couldn't be understood.
		if (csw.bCSWStatus == 1)
			return -2;	// request Get Sense
		else
			return -1;
	}

	// In theory we also should check dCSWDataResidue.  But lots of devices
	// set it wrongly.
	return 0;
}
//****************************************************************************************************

void verify_app_password(libusb_device_handle *handle,libusb_context *ctx)
{
	uint32_t expected_tag,size,r;
	uint8_t recv_buffer[1024];
	uint8_t buffer[20];
	uint32_t i;
	uint32_t retcode1,retcode2;
        uint8_t cdb_in[16] = {0xfd,0x10,0x57,0x00,0x00,0x10};
	uint8_t CDB[16] = {0xfe,0x10,0x57,0x00,0x00,0x10};
	memset(buffer,0x31,16);
	memset(recv_buffer,0,sizeof(recv_buffer));
	
	handle=open_dev(devs,ctx);
	/*向设备发送命令和数据*/
        printf("**********************************verify_app_password!!!!*****************************************\n");

	send_mass_storage_command(handle,ENDPOINT_OUT,0, (unsigned char*)&cdb_in, LIBUSB_ENDPOINT_OUT, 16, &expected_tag);
	retcode1 = libusb_bulk_transfer(handle, ENDPOINT_OUT, buffer, 16, &size, 1000);
	

	printf("\n buffer:\n");
        for(i = 0 ;i < 16;i++)
	printf("%02x ",buffer[i]);
        printf("\n");

	printf("The number have read: %d\n",size);
        r=get_mass_storage_status(handle,ENDPOINT_IN, expected_tag);
        if(r==0)
       printf("status success!!");
	/*从设备接收命令和数据*/
	send_mass_storage_command(handle,ENDPOINT_OUT,0,(unsigned char*)&CDB, LIBUSB_ENDPOINT_OUT, 6, &expected_tag);
	retcode2 = libusb_bulk_transfer(handle, ENDPOINT_IN, recv_buffer,2, &size, 1000);
         r=get_mass_storage_status(handle,ENDPOINT_IN, expected_tag);
        if(r==0)
        printf("status success!!");
	i = size;
        printf("The number have read: %d %02x %02x \n",i,recv_buffer[i-2],recv_buffer[i-1]);
	if((recv_buffer[i-2] == 0x90)&&(recv_buffer[i-1] == 0x00))

        printf("verify_app_password success!!!\n");
	//printf("\n");
 close_dev(handle,ctx);
	printf("**********************************END************************************\n\n\n");
}

void verify_mana_password(libusb_device_handle *handle,libusb_context *ctx)
{
	uint32_t expected_tag,size,r;
	uint8_t recv_buffer[1024];
	uint8_t buffer[20];
	uint32_t i;
	uint32_t retcode1,retcode2;
        uint8_t cdb_in[16] = {0xfd,0x10,0x58,0x00,0x00,0x10};
	uint8_t CDB[16] = {0xfe,0x10,0x58,0x00,0x00,0x10};
	memset(buffer,0x32,16);
	memset(recv_buffer,0,sizeof(recv_buffer));
	
	handle=open_dev(devs,ctx);
	/*向设备发送命令和数据*/
        printf("*******************************verify_mana_password!!***********************\n");

	send_mass_storage_command(handle,ENDPOINT_OUT,0, (unsigned char*)&cdb_in, LIBUSB_ENDPOINT_OUT, 16, &expected_tag);
	retcode1 = libusb_bulk_transfer(handle, ENDPOINT_OUT, buffer, 16, &size, 1000);
	

	printf("\n buffer:\n");
        for(i = 0 ;i < 16;i++)
	printf("%02x ",buffer[i]);
        printf("\n");

	printf("The number have read: %d\n",size);
        r=get_mass_storage_status(handle,ENDPOINT_IN, expected_tag);
        if(r==0)
       printf("status success!!");
	/*从设备接收命令和数据*/
	send_mass_storage_command(handle,ENDPOINT_OUT,0,(unsigned char*)&CDB, LIBUSB_ENDPOINT_OUT, 6, &expected_tag);
	retcode2 = libusb_bulk_transfer(handle, ENDPOINT_IN, recv_buffer,2, &size, 1000);
         r=get_mass_storage_status(handle,ENDPOINT_IN, expected_tag);
        if(r==0)
        printf("status success!!");
	i = size;
        printf("The number have read: %d %02x %02x \n",i,recv_buffer[i-2],recv_buffer[i-1]);
	if((recv_buffer[i-2] == 0x90)&&(recv_buffer[i-1] == 0x00))

        printf("verify_app_password success!!!\n");
	//printf("\n");
 close_dev(handle,ctx);
	printf("**********************************END*************************************\n\n\n");
}
/****** write x509cert   -  写入x509-CA证书******/

void write_x509cert(libusb_device_handle *handle,libusb_device **devs,libusb_context *ctx,  uint8_t tranbuff[512], uint8_t *dst)
{
	uint32_t expected_tag;
         uint32_t size=0;
       	uint8_t r;
        int rlen;
	uint8_t recv_buffer[64] = {0};
	uint8_t buffer[256];
   	uint32_t i,j;
	uint32_t retcode1,retcode2;
	uint8_t CDB[16] = {0xfe,0x30,0xb5,0x00,0x02,0x00};
	uint8_t cdb_in[16] = {0xfd,0x30,0xb5,0x00,0x02,0x00};
	
	memset(buffer,0,sizeof(buffer));

	handle=open_dev(devs,ctx);
        printf("***************************write x509cert Test*************************\n");
	/*向设备发送命令和数据*/
	send_mass_storage_command(handle,ENDPOINT_OUT,0, (unsigned char*)&cdb_in,  LIBUSB_ENDPOINT_OUT , 512,&expected_tag);
	retcode1 = libusb_bulk_transfer(handle, ENDPOINT_OUT, tranbuff, 512, &size, 1000);
        r=get_mass_storage_status(handle,ENDPOINT_IN, expected_tag);
    // printf("size = %d\n",size);
        printf("The message is:\n");
        for(i=0;i < size;i++)
        	{printf("%02x ",tranbuff[i]);if(i%16==15) printf("\n");}
	/*从设备接收命令和数据*/
	send_mass_storage_command(handle,ENDPOINT_OUT,0,(unsigned char*)&CDB, LIBUSB_ENDPOINT_IN , 2, &expected_tag);
	retcode2 = libusb_bulk_transfer(handle, ENDPOINT_IN, buffer, 2, &size, 1000);
        r=get_mass_storage_status(handle,ENDPOINT_IN, expected_tag);
        if(r==0)
        printf("status success!!");
	i = size;
printf("The number of write x509cert: %d\n",i);
	if((buffer[i-2] == 0x90)&&(buffer[i-1] == 0x00))
		memcpy(dst,buffer,i);
	for(i=0;i < size;i++)
		printf("%02x ",dst[i]);
	printf("\n");
	close_dev(handle,ctx);
	printf("*********************************************************************\n\n\n");
}

/****** read x509 cert   -  读取x509-CA证书******/

void read_x509_CA_cert(libusb_device_handle *handle,libusb_device **devs,libusb_context *ctx, uint8_t *dst)
{
	uint32_t expected_tag;
        uint32_t size=0;
       	uint8_t r;
        int rlen;
	uint8_t buffer[1024];
   	uint32_t i,j;
	uint32_t retcode1,retcode2;
	uint8_t CDB[16] = {0xfe,0x20,0xa0,0x00,0x02,0x00};
	uint8_t cdb_in[16] = {0xfd,0x20,0xa0,0x00,0x02,0x00};
	
	memset(buffer,0,sizeof(buffer));

	handle=open_dev(devs,ctx);
        printf("***************************read x509 cert Test*************************\n");
	/*向设备发送命令和数据*/
	//send_mass_storage_command(handle,ENDPOINT_OUT,0, (unsigned char*)&cdb_in,  LIBUSB_ENDPOINT_OUT , 512,&expected_tag);
	//retcode1 = libusb_bulk_transfer(handle, ENDPOINT_OUT, tranbuff, 512, &size, 1000);
        //r=get_mass_storage_status(handle,ENDPOINT_IN, expected_tag);
   
       
	/*从设备接收命令和数据*/
	send_mass_storage_command(handle,ENDPOINT_OUT,0,(unsigned char*)&CDB, LIBUSB_ENDPOINT_IN , 514, &expected_tag);
	retcode2 = libusb_bulk_transfer(handle, ENDPOINT_IN, buffer, 514, &size, 1000);
        r=get_mass_storage_status(handle,ENDPOINT_IN, expected_tag);
        if(r==0)
        printf("status success!!");
	i = size;
printf("The number of read x509 cert: %d\n",i);
	if((buffer[i-2] == 0x90)&&(buffer[i-1] == 0x00))
		memcpy(dst,buffer,i-2);
	//for(i=0;i < size;i++)
	//	{printf("%02x ",dst[i]);if(i%16==15) printf("\n");}
	printf("\n");
	close_dev(handle,ctx);
	printf("*********************************************************************\n\n\n");
}
/****** read x509 usercert   -  读取x509-用户证书******/

void read_x509_user_cert(libusb_device_handle *handle,libusb_device **devs,libusb_context *ctx, uint8_t *dst)
{
	uint32_t expected_tag;
        uint32_t size=0;
       	uint8_t r;
        int rlen;
	uint8_t buffer[1024];
   	uint32_t i,j;
	uint32_t retcode1,retcode2;
	uint8_t CDB[16] = {0xfe,0x20,0xa4,0x00,0x02,0x00};
	uint8_t cdb_in[16] = {0xfd,0x20,0xa4,0x00,0x02,0x00};
	
	memset(buffer,0,sizeof(buffer));

	handle=open_dev(devs,ctx);
        printf("***************************read x509 usercert Test*************************\n");
	/*向设备发送命令和数据*/
	//send_mass_storage_command(handle,ENDPOINT_OUT,0, (unsigned char*)&cdb_in,  LIBUSB_ENDPOINT_OUT , 512,&expected_tag);
	//retcode1 = libusb_bulk_transfer(handle, ENDPOINT_OUT, tranbuff, 512, &size, 1000);
        //r=get_mass_storage_status(handle,ENDPOINT_IN, expected_tag);
   
       
	/*从设备接收命令和数据*/
	send_mass_storage_command(handle,ENDPOINT_OUT,0,(unsigned char*)&CDB, LIBUSB_ENDPOINT_IN , 514, &expected_tag);
	retcode2 = libusb_bulk_transfer(handle, ENDPOINT_IN, buffer, 514, &size, 1000);
        r=get_mass_storage_status(handle,ENDPOINT_IN, expected_tag);
        if(r==0)
        printf("status success!!");
	i = size;
printf("The number of read x509 usercert: %d\n",i);
	if((buffer[i-2] == 0x90)&&(buffer[i-1] == 0x00))
		memcpy(dst,buffer,i);
	for(i=0;i < size;i++)
		{printf("%02x ",dst[i]);if(i%16==15) printf("\n");}
	printf("\n");
	close_dev(handle,ctx);
	printf("*********************************************************************\n\n\n");
}

/****** write ca pubkey   -  写入CA公钥******/

void write_ca_pubkey(libusb_device_handle *handle,libusb_device **devs,libusb_context *ctx,  uint8_t tranbuff[64], uint8_t *dst)
{
	uint32_t expected_tag;
         uint32_t size=0;
       	uint8_t r;
        int rlen;
	uint8_t recv_buffer[64] = {0};
	uint8_t buffer[1024];
   	uint32_t i,j;
	uint32_t retcode1,retcode2;
	uint8_t CDB[16] = {0xfe,0x30,0xb4,0x00,0x00,0x40};
	uint8_t cdb_in[16] = {0xfd,0x30,0xb4,0x00,0x00,0x40};
	
	memset(buffer,0,sizeof(buffer));

	handle=open_dev(devs,ctx);
        printf("***************************write ca pubkey Test*************************\n");
	/*向设备发送命令和数据*/
	send_mass_storage_command(handle,ENDPOINT_OUT,0, (unsigned char*)&cdb_in,  LIBUSB_ENDPOINT_OUT , 64,&expected_tag);
	retcode1 = libusb_bulk_transfer(handle, ENDPOINT_OUT, tranbuff, 64, &size, 1000);
        r=get_mass_storage_status(handle,ENDPOINT_IN, expected_tag);
    // printf("size = %d\n",size);
        printf("The message is:\n");
        for(i=0;i < size;i++)
        	{printf("%02x ",tranbuff[i]);if(i%16==15) printf("\n");}
	/*从设备接收命令和数据*/
	send_mass_storage_command(handle,ENDPOINT_OUT,0,(unsigned char*)&CDB, LIBUSB_ENDPOINT_IN , 2, &expected_tag);
	retcode2 = libusb_bulk_transfer(handle, ENDPOINT_IN, buffer, 2, &size, 1000);
        r=get_mass_storage_status(handle,ENDPOINT_IN, expected_tag);
        if(r==0)
        printf("status success!!");
	i = size;
printf("The number of write ca pubkey: %d\n",i);
	if((buffer[i-2] == 0x90)&&(buffer[i-1] == 0x00))
		memcpy(dst,buffer,i);
	for(i=0;i < size;i++)
		printf("%02x ",dst[i]);
	printf("\n");
	close_dev(handle,ctx);
	printf("*********************************************************************\n\n\n");
}

/****** write userkey   -  写入用户密钥******/

void write_userkey(libusb_device_handle *handle,libusb_device **devs,libusb_context *ctx,  uint8_t tranbuff[100], uint8_t *dst)
{
	uint32_t expected_tag;
         uint32_t size=0;
       	uint8_t r;
        int rlen;
	uint8_t recv_buffer[64] = {0};
	uint8_t buffer[1024];
   	uint32_t i,j;
	uint32_t retcode1,retcode2;
	uint8_t CDB[16] = {0xfe,0x30,0xb7,0x00,0x01,0x00};
	uint8_t cdb_in[16] = {0xfd,0x30,0xb7,0x00,0x01,0x00};
	
	memset(buffer,0,sizeof(buffer));

	handle=open_dev(devs,ctx);
        printf("***************************write userkey Test*************************\n");
	/*向设备发送命令和数据*/
	send_mass_storage_command(handle,ENDPOINT_OUT,0, (unsigned char*)&cdb_in,  LIBUSB_ENDPOINT_OUT , 256,&expected_tag);
	retcode1 = libusb_bulk_transfer(handle, ENDPOINT_OUT, tranbuff, 256, &size, 1000);
        r=get_mass_storage_status(handle,ENDPOINT_IN, expected_tag);
    // printf("size = %d\n",size);
        printf("The message is:\n");
        for(i=0;i < size;i++)
        	{printf("%02x ",tranbuff[i]);if(i%16==15) printf("\n");}
	/*从设备接收命令和数据*/
	send_mass_storage_command(handle,ENDPOINT_OUT,0,(unsigned char*)&CDB, LIBUSB_ENDPOINT_IN , 2, &expected_tag);
	retcode2 = libusb_bulk_transfer(handle, ENDPOINT_IN, buffer, 2, &size, 1000);
        r=get_mass_storage_status(handle,ENDPOINT_IN, expected_tag);
        if(r==0)
        printf("status success!!");
	i = size;
printf("The number of write userkey: %d\n",i);
	if((buffer[i-2] == 0x90)&&(buffer[i-1] == 0x00))
		memcpy(dst,buffer,i);
	for(i=0;i < size;i++)
		printf("%02x ",dst[i]);
	printf("\n");
	close_dev(handle,ctx);
	printf("*********************************************************************\n\n\n");
}
/****** read user key   -  读取用户密钥******/

void read_userkey(libusb_device_handle *handle,libusb_device **devs,libusb_context *ctx, uint8_t *dst)
{
	uint32_t expected_tag;
        uint32_t size=0;
       	uint8_t r;
        int rlen;
	uint8_t buffer[1024];
   	uint32_t i,j;
	uint32_t retcode1,retcode2;
	uint8_t CDB[16] = {0xfe,0x20,0xa5,0x00,0x01,0x00};
	uint8_t cdb_in[16] = {0xfd,0x20,0xa5,0x00,0x01,0x00};
	
	memset(buffer,0,sizeof(buffer));

	handle=open_dev(devs,ctx);
        printf("***************************read user key Test*************************\n");
	/*向设备发送命令和数据*/
	//send_mass_storage_command(handle,ENDPOINT_OUT,0, (unsigned char*)&cdb_in,  LIBUSB_ENDPOINT_OUT , 6,&expected_tag);
	//retcode1 = libusb_bulk_transfer(handle, ENDPOINT_OUT, tranbuff, 512, &size, 1000);
        //r=get_mass_storage_status(handle,ENDPOINT_IN, expected_tag);
   
       
	/*从设备接收命令和数据*/
	send_mass_storage_command(handle,ENDPOINT_OUT,0,(unsigned char*)&CDB, LIBUSB_ENDPOINT_IN , 258, &expected_tag);
	retcode2 = libusb_bulk_transfer(handle, ENDPOINT_IN, buffer, 258, &size, 1000);
        r=get_mass_storage_status(handle,ENDPOINT_IN, expected_tag);
        if(r==0)
        printf("status success!!");
	i = size;
printf("The number of read user key: %d\n",i);
	if((buffer[i-2] == 0x90)&&(buffer[i-1] == 0x00))
		memcpy(dst,buffer,i);
	for(i=0;i < size;i++)
		{printf("%02x ",dst[i]);if(i%16==15) printf("\n");}
	printf("\n");
	close_dev(handle,ctx);
	printf("*********************************************************************\n\n\n");
}


/****** write x509 usercert   -  写入x509-用户证书******/

void write_x509usercert(libusb_device_handle *handle,libusb_device **devs,libusb_context *ctx,  uint8_t tranbuff[512], uint8_t *dst)
{
	uint32_t expected_tag;
         uint32_t size=0;
       	uint8_t r;
        int rlen;
	uint8_t recv_buffer[64] = {0};
	uint8_t buffer[256];
   	uint32_t i,j;
	uint32_t retcode1,retcode2;
	uint8_t CDB[16] = {0xfe,0x30,0xb6,0x00,0x02,0x00};
	uint8_t cdb_in[16] = {0xfd,0x30,0xb6,0x00,0x02,0x00};
	
	memset(buffer,0,sizeof(buffer));

	handle=open_dev(devs,ctx);
        printf("***************************write x509 usercert Test*************************\n");
	/*向设备发送命令和数据*/
	send_mass_storage_command(handle,ENDPOINT_OUT,0, (unsigned char*)&cdb_in,  LIBUSB_ENDPOINT_OUT , 512,&expected_tag);
	retcode1 = libusb_bulk_transfer(handle, ENDPOINT_OUT, tranbuff, 512, &size, 1000);
        r=get_mass_storage_status(handle,ENDPOINT_IN, expected_tag);
    // printf("size = %d\n",size);
        printf("The message is:\n");
        for(i=0;i < size;i++)
        	{printf("%02x ",tranbuff[i]);if(i%16==15) printf("\n");}
	/*从设备接收命令和数据*/
	send_mass_storage_command(handle,ENDPOINT_OUT,0,(unsigned char*)&CDB, LIBUSB_ENDPOINT_IN , 2, &expected_tag);
	retcode2 = libusb_bulk_transfer(handle, ENDPOINT_IN, buffer, 2, &size, 1000);
        r=get_mass_storage_status(handle,ENDPOINT_IN, expected_tag);
        if(r==0)
        printf("status success!!");
	i = size;
printf("The number of write x509 usercert: %d\n",i);
	if((buffer[i-2] == 0x90)&&(buffer[i-1] == 0x00))
		memcpy(dst,buffer,i);
	for(i=0;i < size;i++)
		printf("%02x ",dst[i]);
	printf("\n");
	close_dev(handle,ctx);
	printf("*********************************************************************\n\n\n");
}


DLLIMPORT int main()
{
	int i=0,j=0;
	printf("1");
	uint8_t ca_pubkey[66]={0};
	printf("2");
	uint8_t user_pubkey[256]={0};
	printf("3");
	uint8_t buffer[2048] ;
	printf("4");


	libusb_device **devs; //pointer to pointer of device, used to retrieve a list of devices
	printf("5");
        libusb_device_handle *handle; //a device handle
        libusb_context *ctx = NULL; 	//a libusb session
	uint8_t CA_Buff[514] = 
{
0x30,0x82,0x01,0x97,0x30,0x82,0x01,0x3C,0xA0,0x03,0x02,0x01,0x02,0x02,0x08,0x6D,
0x50,0x2E,0xBC,0x5E,0xF2,0xBD,0x98,0x30,0x0A,0x06,0x08,0x2A,0x86,0x48,0xCE,0x3D,
0x04,0x03,0x02,0x30,0x2F,0x31,0x0B,0x30,0x09,0x06,0x03,0x55,0x04,0x06,0x13,0x02,
0x43,0x48,0x31,0x13,0x30,0x11,0x06,0x03,0x55,0x04,0x0A,0x13,0x0A,0x73,0x74,0x72,
0x6F,0x6E,0x67,0x53,0x77,0x61,0x6E,0x31,0x0B,0x30,0x09,0x06,0x03,0x55,0x04,0x03,
0x13,0x02,0x43,0x41,0x30,0x1E,0x17,0x0D,0x31,0x38,0x30,0x33,0x32,0x31,0x31,0x30,
0x34,0x35,0x35,0x33,0x5A,0x17,0x0D,0x32,0x31,0x30,0x33,0x32,0x30,0x31,0x30,0x34,
0x35,0x35,0x33,0x5A,0x30,0x2F,0x31,0x0B,0x30,0x09,0x06,0x03,0x55,0x04,0x06,0x13,
0x02,0x43,0x48,0x31,0x13,0x30,0x11,0x06,0x03,0x55,0x04,0x0A,0x13,0x0A,0x73,0x74,
0x72,0x6F,0x6E,0x67,0x53,0x77,0x61,0x6E,0x31,0x0B,0x30,0x09,0x06,0x03,0x55,0x04,
0x03,0x13,0x02,0x43,0x41,0x30,0x59,0x30,0x13,0x06,0x07,0x2A,0x86,0x48,0xCE,0x3D,
0x02,0x01,0x06,0x08,0x2A,0x86,0x48,0xCE,0x3D,0x03,0x01,0x07,0x03,0x42,0x00,0x04,
0x34,0x2B,0xB4,0xB5,0x19,0x72,0xAA,0x30,0x7A,0x65,0xDA,0x96,0xB8,0x90,0x47,0xC9,
0x05,0x62,0xC7,0x28,0xF4,0x88,0x96,0x75,0xD6,0x8C,0xB5,0x10,0x96,0xFF,0x8F,0x66,
0xDB,0x2C,0x1A,0x07,0x77,0x1C,0x74,0x57,0xF4,0x4B,0x02,0x05,0xBB,0x0D,0xDE,0x82,
0x88,0xD4,0x65,0x51,0xA6,0xF4,0x0F,0x41,0x8B,0xC3,0x83,0xB8,0x5A,0x2D,0x42,0x88,
0xA3,0x42,0x30,0x40,0x30,0x0F,0x06,0x03,0x55,0x1D,0x13,0x01,0x01,0xFF,0x04,0x05,
0x30,0x03,0x01,0x01,0xFF,0x30,0x0E,0x06,0x03,0x55,0x1D,0x0F,0x01,0x01,0xFF,0x04,
0x04,0x03,0x02,0x01,0x06,0x30,0x1D,0x06,0x03,0x55,0x1D,0x0E,0x04,0x16,0x04,0x14,
0x29,0xE1,0x96,0xBB,0x32,0xC2,0x92,0x17,0x8C,0x6D,0x9B,0x35,0x46,0x04,0x13,0x47,
0x92,0x85,0x4B,0xAA,0x30,0x0A,0x06,0x08,0x2A,0x86,0x48,0xCE,0x3D,0x04,0x03,0x02,
0x03,0x49,0x00,0x30,0x46,0x02,0x21,0x00,0xE8,0x09,0xFA,0x03,0xC3,0xBC,0xE1,0x1D,
0x7A,0x27,0xFA,0x55,0xCE,0x87,0x3A,0x87,0x8D,0x42,0x91,0xB9,0x90,0x84,0x7E,0xB3,
0x9A,0xD4,0x9A,0x66,0x76,0xFD,0x03,0x89,0x02,0x21,0x00,0xFD,0x82,0x23,0xB9,0xB5,
0xAD,0xB6,0x0B,0x67,0xDC,0xFD,0xF9,0x88,0x70,0x54,0xCA,0x87,0xF2,0x4C,0x96,0xCC,
0x7C,0xEE,0x2F,0xEA,0xA9,0x63,0x88,0xE8,0x67,0xA1,0x0A,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,
0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,
0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,
0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,
0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,
0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,
0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0 ,0x0  

};
uint8_t user_Buff[514] = 
{
0x30,0x82,0x01,0x8F,0x30,0x82,0x01,0x36,0xA0,0x03,0x02,0x01,0x02,0x02,0x08,0x25,
0xED,0x7E,0x87,0x80,0x33,0xA2,0x37,0x30,0x0A,0x06,0x08,0x2A,0x86,0x48,0xCE,0x3D,
0x04,0x03,0x02,0x30,0x2F,0x31,0x0B,0x30,0x09,0x06,0x03,0x55,0x04,0x06,0x13,0x02,
0x43,0x48,0x31,0x13,0x30,0x11,0x06,0x03,0x55,0x04,0x0A,0x13,0x0A,0x73,0x74,0x72,
0x6F,0x6E,0x67,0x53,0x77,0x61,0x6E,0x31,0x0B,0x30,0x09,0x06,0x03,0x55,0x04,0x03,
0x13,0x02,0x43,0x41,0x30,0x1E,0x17,0x0D,0x31,0x38,0x30,0x33,0x32,0x31,0x31,0x30,
0x34,0x35,0x35,0x33,0x5A,0x17,0x0D,0x32,0x31,0x30,0x33,0x32,0x30,0x31,0x30,0x34,
0x35,0x35,0x33,0x5A,0x30,0x34,0x31,0x0B,0x30,0x09,0x06,0x03,0x55,0x04,0x06,0x13,
0x02,0x43,0x48,0x31,0x13,0x30,0x11,0x06,0x03,0x55,0x04,0x0A,0x13,0x0A,0x73,0x74,
0x72,0x6F,0x6E,0x67,0x53,0x77,0x61,0x6E,0x31,0x10,0x30,0x0E,0x06,0x03,0x55,0x04,
0x03,0x13,0x07,0x63,0x6C,0x69,0x65,0x6E,0x74,0x31,0x30,0x59,0x30,0x13,0x06,0x07,
0x2A,0x86,0x48,0xCE,0x3D,0x02,0x01,0x06,0x08,0x2A,0x86,0x48,0xCE,0x3D,0x03,0x01,
0x07,0x03,0x42,0x00,0x04,0xAE,0x56,0xB8,0x04,0x20,0x56,0x01,0xE0,0xAB,0x16,0xE3,
0xF7,0x92,0xA0,0x07,0xB8,0xEE,0xA2,0x13,0x83,0xA2,0xCB,0x9A,0x89,0x98,0xF1,0x53,
0xEA,0x2C,0xD7,0x5A,0x46,0x28,0x63,0x79,0x9F,0x31,0x1D,0x00,0x1F,0x7E,0x05,0xF2,
0xC7,0x34,0xB4,0xFC,0xF1,0x48,0xF9,0xDB,0x78,0xB5,0xB1,0x24,0x6F,0xC4,0x52,0x5D,
0x02,0xC2,0xAE,0xB8,0x28,0xA3,0x37,0x30,0x35,0x30,0x1F,0x06,0x03,0x55,0x1D,0x23,
0x04,0x18,0x30,0x16,0x80,0x14,0x29,0xE1,0x96,0xBB,0x32,0xC2,0x92,0x17,0x8C,0x6D,
0x9B,0x35,0x46,0x04,0x13,0x47,0x92,0x85,0x4B,0xAA,0x30,0x12,0x06,0x03,0x55,0x1D,
0x11,0x04,0x0B,0x30,0x09,0x82,0x07,0x63,0x6C,0x69,0x65,0x6E,0x74,0x31,0x30,0x0A,
0x06,0x08,0x2A,0x86,0x48,0xCE,0x3D,0x04,0x03,0x02,0x03,0x47,0x00,0x30,0x44,0x02,
0x20,0x06,0x5F,0xC2,0x03,0xE4,0xF9,0x3B,0xF9,0xC7,0x42,0x4A,0x10,0x17,0x59,0xC5,
0x8D,0xD2,0x35,0xEC,0x6B,0x44,0xBA,0xEF,0xFE,0xC6,0x1F,0xA8,0x9F,0xBB,0x64,0xF1,
0x16,0x02,0x20,0x5E,0x0D,0xA2,0x36,0xFA,0x27,0xBE,0x74,0x2A,0xE6,0x92,0xBB,0x3F,
0xFF,0x18,0xCE,0x14,0x54,0xC5,0xF0,0x4D,0x45,0x4F,0x1E,0x52,0x17,0x16,0xC6,0x1D,
0xD8,0x4B,0xC3,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};
	uint8_t user_cert_Buff[514] = {0};
	verify_app_password(handle,ctx);
	verify_mana_password(handle,ctx);
	//verify_rec_password(handle,ctx);
	write_x509cert(handle,devs,ctx, CA_Buff, buffer);
	write_x509usercert(handle,devs,ctx, user_Buff, buffer);
	read_x509_user_cert(handle,devs,ctx, buffer);
}


BOOL WINAPI DllMain(HINSTANCE hinstDLL,DWORD fdwReason,LPVOID lpvReserved)
{
	switch(fdwReason)
	{
		case DLL_PROCESS_ATTACH:
		{
			break;
		}
		case DLL_PROCESS_DETACH:
		{
			break;
		}
		case DLL_THREAD_ATTACH:
		{
			break;
		}
		case DLL_THREAD_DETACH:
		{
			break;
		}
	}
	
	/* Return TRUE on success, FALSE on failure */
	return TRUE;
}
