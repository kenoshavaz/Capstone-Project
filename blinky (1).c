#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "user_interface.h"


const u8 mac_addr[][6] = {
		     {0xa2, 0x20, 0xa6, 0x08, 0x6f, 0x3d}, //module #1
		     {0xa2, 0x20, 0xa6, 0x08, 0x6f, 0x70}, //module #2
		     {0xa2, 0x20, 0xa6, 0x14, 0xbf, 0x87}, //module #3
		     {0xa2, 0x20, 0xa6, 0x08, 0x6e, 0xd0}, //module #4
};
#define NUM_MACS (sizeof(mac_addr)/sizeof(mac_addr[0]))
//#define NUM_MACS 4

u8 const *upstream_mac, *downstream_mac;
uint32_t my_index;


/* Return true if both mac addresses match */
uint32_t compare_mac(const u8 *mac1, const u8 *mac2)
{
  uint32_t i;
  uint32_t match;

  /* If either MAC is null, return no match */
  if((mac1 == NULL) || (mac2==NULL))
    return 0;
  
  /* Loop over bytes in MAC address */
  match = 1;
  for(i=0; i<6; i++) {
    //os_printf("%u: %02x ? %02x\n",i,mac1[i],mac2[i]);
    if(mac1[i] != mac2[i]) {
      match = 0;
      break;
    }
  }
  if(match) {
    //os_printf("Matched\n");
    return 1;
  }
  return 0;
}

uint32_t mac_index(u8 *mac)
{
  uint32_t i;
  /* Search through mac list until we find ours */

  /* loop over mac addresses */
  for(i=0; i<NUM_MACS; i++) {
    /* os_printf("Index: %u...",i); */
    if(compare_mac(mac, mac_addr[i])) {
      return i;
    }
  }
  return NUM_MACS;
}


uint32_t ack_count;

void user_pre_init(void)
{
}
// ESP-12 modules have LED on GPIO2. Change to another GPIO
// for other boards.
static const int pin = 2;
//static volatile os_timer_t some_timer;
static os_timer_t some_timer;

void some_timerfunc(void *arg)
{
  uint32_t index;
  uint32_t len;
  uint8_t  *data;
  u8 const *send_to_mac;
  

  //Do blinky stuff
  if (GPIO_REG_READ(GPIO_OUT_ADDRESS) & (1 << pin))
  {
    // set gpio low
    gpio_output_set(0, (1 << pin), 0, 0);
  }
  else
  {
    // set gpio high
    gpio_output_set((1 << pin), 0, 0, 0);
  }

  /* Since we are an end node, send upstream or downstream as
     appropriate */
  if(upstream_mac != NULL) {
    send_to_mac = upstream_mac;
    data = "Sending upstream";
    len = os_strlen(data);
  }
  if(downstream_mac != NULL) {
    send_to_mac = downstream_mac;
    data = "Sending downstream";
    len = os_strlen(data);
  }
  
  esp_now_send(send_to_mac, data, len); /* send to the specified mac_addr */

  os_printf("Sent message: %s\n",data);

}


void ICACHE_FLASH_ATTR simple_cb(u8 *macaddr, u8 *data, u8 len)
{
  int i;
  u8 ack_buf[16];
  u8 recv_buf[17];
  u8 const *send_to;


  os_printf("now from[");
  for (i = 0; i < 6; i++) {
    os_printf("%02X, ", macaddr[i]);
  }
  os_printf(" len: %d]:", len);
  os_printf(" data: ");
  for (i = 0; i < len; i++) {
    os_printf("%c", data[i]);
  }


  
  /* see if data is from upstream or downstream, and if so, forward it */
  send_to = NULL;
  if(compare_mac(macaddr,upstream_mac)) {
    /* From our upstream neighbour, send downstream */
    send_to = downstream_mac;
    os_printf(" [Upstream neighbour]");
  }
  if(compare_mac(macaddr,downstream_mac)) {
    /* From our downstream neighbour, send upstream */
    send_to = upstream_mac;
    os_printf(" [Dowstream neighbour]");
  }

  /* If we don't know who it came from, it dies here */
  if(send_to != NULL) {
    esp_now_send(send_to, data, len);
  } else {
    os_printf(" [Random stranger]");
  }
  os_printf("\n");

}


void ICACHE_FLASH_ATTR user_init()
{
  uint8_t  mac[6];
  

  u8 key[16]= {0x33, 0x44, 0x33, 0x44, 0x33, 0x44, 0x33, 0x44,
	       0x33, 0x44, 0x33, 0x44, 0x33, 0x44, 0x33, 0x44};

  
  // init gpio subsytem
  gpio_init();

  // configure UART TXD to be GPIO1, set as output
  PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0TXD_U, FUNC_GPIO1); 
  gpio_output_set(0, 0, (1 << pin), 0);
  uart_init(115200,115200);
  
  if (esp_now_init()==0) {
    wifi_get_macaddr(SOFTAP_IF,mac);
    my_index = mac_index(mac);
    os_printf("I'm at index %u\n",index);
    
    /* If we are the last in the list, we are an end node, nobody to
       send downstream to. */
    if(my_index >= (NUM_MACS-1)) {
      downstream_mac = NULL;
    } else {
      /* Otherwise, send data upstream and  */
      downstream_mac = mac_addr[my_index+1];
    }
    /* If we are the first on the list, we are an end node and we
       should send upstream */
    if(my_index == 0) {
      upstream_mac = NULL;
    } else {
      upstream_mac = mac_addr[my_index-1];
    }
    
    os_printf("esp_now init ok\n");
    esp_now_register_recv_cb(simple_cb);
    esp_now_set_self_role(1);
 //   esp_now_add_peer(da1, 1, key, 16);
 //   esp_now_add_peer(da2, 2, key, 16);
  

  } else {
    os_printf("esp_now init failed\n");
  }

  /* If we are not an end node, start the timer to send up or downstream */
  if((upstream_mac == NULL) || (downstream_mac == NULL)) {
    // setup timer (500ms, repeating)
    os_timer_setfn(&some_timer, (os_timer_func_t *)some_timerfunc, NULL);
    os_timer_arm(&some_timer, 500, 1);
  }
  
}

void ICACHE_FLASH_ATTR demo_send(u8 *mac_addr, u8 *data, u8 len)
{
  esp_now_send(NULL, data, len);   /* the demo will send to two devices which added by esp_now_add_peer() 

  //esp_now_send(mac_addr, data, len); /* send to the specified mac_addr */

  os_printf("sent \n");
}
