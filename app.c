/***************************************************************************//**
 * @file app.c
 * @brief Silicon Labs Empty Example Project
 *
 * This example demonstrates the bare minimum needed for a Blue Gecko C application
 * that allows Over-the-Air Device Firmware Upgrading (OTA DFU). The application
 * starts advertising after boot and restarts advertising after a connection is closed.
 *******************************************************************************
 * # License
 * <b>Copyright 2018 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

/* Bluetooth stack headers */
#include "bg_types.h"
#include "native_gecko.h"
#include "gatt_db.h"

#include "app.h"

#if DTLS_CLIENT
/// ///////////////////////////////////////////////////////////////////////////
/// DTLS Tunnel Service
/// 86934d83-630e-4f8c-a9a2-82ede9f87aa9
///
static const uint8 dtls_service_uuid[16] = {0x86,0x93,0x4d,0x83,0x63,0x0e,0x4f,0x8c,0xa9,0xa2,0x82,0xed,0xe9,0xf8,0x7a,0xa9};
  /// DTLS Tunnel In Out Characteristic
  /// ddf53708-588f-441a-9dc0-0a6cdefac8e9
  ///
static const uint8 silabs_appsec_characteristic_rd_uuid[16] = {0xdd,0xf5,0x37,0x08,0x58,0x8f,0x44,0x1a,0x9d,0xc0,0x0a,0x6c,0xde,0xfa,0xc8,0xe9};
/// ///////////////////////////////////////////////////////////////////////////

static uint8 scan_uuid[16];

/// ///////////////////////////////////////////////////////////////////////////
///
/// function: print_uuid16
/// Description: helper function to print UUIDs
///
/// ///////////////////////////////////////////////////////////////////////////
void print_uuid16(uint8* uuid){
  
	printLog("UUID: ");
  for(int i=0;i<16;i++){
	  printLog("%x ", *(uuid+i));
  }
  printLog("\r\n");
  
}

/// ///////////////////////////////////////////////////////////////////////////
///
/// function: check_uuid
/// Description: helper function to compare two UUIDs
///
/// ///////////////////////////////////////////////////////////////////////////
static bool check_uuid(uint8 const *uuid1, uint8 *uuid2, uint8 len){

    bool match = false;
    int i;


#if DEBUG_LEVEL
    printLog("comparing \r\n");
    print_uuid16((uint8 *)uuid1);
    printLog("against \r\n");
    print_uuid16(uuid2);
#endif
    /* uuid is in reverse order in advertising packet*/
    for(i= 0;i<len;i++){
	if(*(uuid2 + (len - i - 1)) != *(uuid1+i)){
	  return match;
	}

    }
    match = true;
    return match;
}

/**
 * @brief  Processes advertisement packets looking for HTM (Health Thermometer) service
 */
static int process_scan_response(struct gecko_msg_le_gap_scan_response_evt_t *pResp)
{
  /* Decoding advertising packets is done here. The list of AD types can be found
   * at: https://www.bluetooth.com/specifications/assigned-numbers/Generic-Access-Profile */

  int i = 0, j;
  int ad_len;
  int ad_type;

  while (i < (pResp->data.len - 1)) {
    ad_len = pResp->data.data[i];
    ad_type = pResp->data.data[i + 1];

    if (ad_type == 0x06 || ad_type == 0x07) {
      /* type 0x06 = Incomplete List of 128-bit Service Class UUIDs
       type 0x07 = Complete List of 128-bit Service Class UUIDs */

      /* Look through all the UUIDs looking for DTLS service */
      j = i + 2;
      do {
        if (check_uuid(dtls_service_uuid,(uint8 *)&(pResp->data.data[j]),16)) {
          printLog("DTLS UUID found \r\n");
          return 1;
        }
        j = j + 16;
      }
      while (j < i + ad_len);
    }

    /* Jump to next AD record */
    i = i + ad_len + 1;
  }

  return 0;
}

#endif//#if DTLS_CLIENT

/* Print boot message */
static void bootMessage(struct gecko_msg_system_boot_evt_t *bootevt);

/* Flag for indicating DFU Reset must be performed */
static uint8_t boot_to_dfu = 0;

/* Main application */
void appMain(gecko_configuration_t *pconfig)
{
#if DISABLE_SLEEP > 0
  pconfig->sleep.flags = 0;
#endif

  /* Initialize debug prints. Note: debug prints are off by default. See DEBUG_LEVEL in app.h */
  initLog();

  /* Initialize stack */
  gecko_init(pconfig);

  while (1) {
    /* Event pointer for handling events */
    struct gecko_cmd_packet* evt;

    /* if there are no events pending then the next call to gecko_wait_event() may cause
     * device go to deep sleep. Make sure that debug prints are flushed before going to sleep */
    if (!gecko_event_pending()) {
      flushLog();
    }

    /* Check for stack event. This is a blocking event listener. If you want non-blocking please see UG136. */
    evt = gecko_wait_event();

    /* Handle events */
    switch (BGLIB_MSG_ID(evt->header)) {
      /* This boot event is generated when the system boots up after reset.
       * Do not call any stack commands before receiving the boot event.
       * Here the system is set to start advertising immediately after boot procedure. */
      case gecko_evt_system_boot_id:
        bootMessage(&(evt->data.evt_system_boot));
#if DTLS_SERVER
        printLog("boot event - starting advertising\r\n");

        /* Set advertising parameters. 100ms advertisement interval.
         * The first parameter is advertising set handle
         * The next two parameters are minimum and maximum advertising interval, both in
         * units of (milliseconds * 1.6).
         * The last two parameters are duration and maxevents left as default. */
        gecko_cmd_le_gap_set_advertise_timing(0, 160, 160, 0, 0);

        /* Start general advertising and enable connections. */
        gecko_cmd_le_gap_start_advertising(0, le_gap_general_discoverable, le_gap_connectable_scannable);
#endif

#if DTLS_CLIENT
        printLog("boot event - starting scan\r\n");

        /* 200 ms scan window min/max, passive scan*/
        gecko_cmd_le_gap_set_scan_parameters(320,320,0);
        /* start listening for devices to connect to */
        gecko_cmd_le_gap_discover(le_gap_discover_generic);
#endif
        break;

#if DTLS_CLIENT
      /* GAP scan response event handler */    
      case gecko_evt_le_gap_scan_response_id:
      {
         bd_addr slave_address =  evt->data.evt_le_gap_scan_response.address;
         int index = 0;
    	   printLog("scan response, packet type %d\n", evt->data.evt_le_gap_scan_response.packet_type);

        if (process_scan_response(&(evt->data.evt_le_gap_scan_response)) == 0) {
          // There might be so many scan responses that no point if telling those, just exit event
          break;
        }

    	//  while(index < evt->data.evt_le_gap_scan_response.data.len){
    	// 	 /* is this PDU the complete list of 128 bit services?*/
    	// 	 if(evt->data.evt_le_gap_scan_response.data.data[index+1] == 0x07){
    	//         memcpy(scan_uuid,&evt->data.evt_le_gap_scan_response.data.data[index+2],16);
    	//         break;
    	// 	 }
    	// 	 /* check the next one*/
    	// 	 else {
    	// 		 index = index + evt->data.evt_le_gap_scan_response.data.data[index] + 1;
    	// 	 }
    	//  }
      //    /* check to see if advertising device is advertising the expected service.*/
      //    if( evt->data.evt_le_gap_scan_response.packet_type == 0 &&
      //        check_uuid(dtls_service_uuid,scan_uuid, 16) == true){
                     
      //  	 printLog("found device advertising service uuid %16X\r\n", evt->data.evt_le_gap_scan_response.data.data[5]);
        	 printLog("connecting to remote GATT server with address %2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X\r\n",
            		    slave_address.addr[5],slave_address.addr[4],	slave_address.addr[3],
					          slave_address.addr[2],slave_address.addr[1], slave_address.addr[0]);

               /* stop scanning for now */
               gecko_cmd_le_gap_end_procedure();

               /* and connect to the advertising device*/
               gecko_cmd_le_gap_open(evt->data.evt_le_gap_scan_response.address ,le_gap_address_type_public);
      //    }
      }
      break;
#endif

      case gecko_evt_le_connection_opened_id:

        printLog("connection opened\r\n");

        break;

      case gecko_evt_le_connection_closed_id:

        printLog("connection closed, reason: 0x%2.2x\r\n", evt->data.evt_le_connection_closed.reason);

        /* Check if need to boot to OTA DFU mode */
        if (boot_to_dfu) {
          /* Enter to OTA DFU mode */
          gecko_cmd_system_reset(2);
        } else {
          /* Restart advertising after client has disconnected */
          //gecko_cmd_le_gap_start_advertising(0, le_gap_general_discoverable, le_gap_connectable_scannable);
        }
        break;


      case gecko_evt_gatt_server_user_write_request_id:

        switch(evt->data.evt_gatt_server_user_write_request.characteristic)
        {
              case gattdb_dtls_in_out:
                  printLog("Gatt DB dtls in out written\r\n");
                  break;

              /* Events related to OTA upgrading
              ----------------------------------------------------------------------------- */
              case gattdb_ota_control:
                  /* Set flag to enter to OTA mode */
                  boot_to_dfu = 1;
                  /* Send response to Write Request */
                  gecko_cmd_gatt_server_send_user_write_response(
                    evt->data.evt_gatt_server_user_write_request.connection,
                    gattdb_ota_control,
                    bg_err_success);

                  /* Close connection to enter to DFU OTA mode */
                  gecko_cmd_le_connection_close(evt->data.evt_gatt_server_user_write_request.connection);
                  break;
        }
        break;

      /* Add additional event handlers as your application requires */

      default:
        break;
    }
  }
}

/* Print stack version and local Bluetooth address as boot message */
static void bootMessage(struct gecko_msg_system_boot_evt_t *bootevt)
{
#if DEBUG_LEVEL
  bd_addr local_addr;
  int i;

  printLog("stack version: %u.%u.%u\r\n", bootevt->major, bootevt->minor, bootevt->patch);
  local_addr = gecko_cmd_system_get_bt_address()->address;

  printLog("local BT device address: ");
  for (i = 0; i < 5; i++) {
    printLog("%2.2x:", local_addr.addr[5 - i]);
  }
  printLog("%2.2x\r\n", local_addr.addr[0]);
#endif
}
