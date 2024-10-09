/**
 * @author Yann Charbon <yann.charbon@heig-vd.ch>
 * @copyright 2024
*/

#ifndef WISUN_BORDER_ROUTER_MANAGER_H
#define WISUN_BORDER_ROUTER_MANAGER_H

#include "mbed.h"
#include "mbed-trace/mbed_trace.h"

#include "mesh_system.h"

#include "Nanostack.h"
#include "NanostackEthernetInterface.h"
#include "MeshInterfaceNanostack.h"
#include "EMACInterface.h"
#include "EMAC.h"
#include "ip6string.h"

#include "wisun_certificates.h"

extern "C" {
#include "nsconfig.h"
#include "NWK_INTERFACE/Include/protocol.h"

#include "mac_api.h"
#include "NanostackRfPhy.h"
#include "sw_mac.h"
#include "net_interface.h"
#include "ethernet_mac_api.h"
#include "fhss_config.h"

#include "eventOS_event.h"
#include "eventOS_event_timer.h"

#include "ws_bbr_api.h"
#include "ws_management_api.h"
}

class WiSunBorderRouterManager {
public:
    WiSunBorderRouterManager(const char *wisun_network_name);
    virtual int start();

protected:
    char ws_network_name[32];

private:
    NanostackRfPhy& rf_phy;
    mac_api_t *mac_api;
    static eth_mac_api_t *eth_mac_api;
    static int ws_if_id;
    static int eth_if_id;
    static int emac_driver_id;
    uint8_t mac[6];
    static int8_t br_tasklet_id;

    static void backhaul_interface_up(int8_t driver_id);
    static void backhaul_interface_down();
    static void print_interface_addr(int id);
    int wisun_interface_up();

    static void borderrouter_backhaul_phy_status_cb(bool link_up);
    static void bbr_tasklet(arm_event_s *event);
};

#endif // WISUN_BORDER_ROUTER_MANAGER_H
