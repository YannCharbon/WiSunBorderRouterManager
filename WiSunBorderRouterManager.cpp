/**
 * @author Yann Charbon <yann.charbon@heig-vd.ch>
 * @copyright 2024
*/

#include "WiSunBorderRouterManager.h"

#define TRACE_GROUP "WBRM"

#define NR_BACKHAUL_INTERFACE_PHY_DRIVER_READY 2
#define NR_BACKHAUL_INTERFACE_PHY_DOWN  3

int8_t WiSunBorderRouterManager::br_tasklet_id = -1;
int WiSunBorderRouterManager::ws_if_id = -1;
int WiSunBorderRouterManager::eth_if_id = -1;
eth_mac_api_t * WiSunBorderRouterManager::eth_mac_api = NULL;
int WiSunBorderRouterManager::emac_driver_id = -1;

WiSunBorderRouterManager::WiSunBorderRouterManager(const char *wisun_network_name)
    : mac_api(nullptr), rf_phy(NanostackRfPhy::get_default_instance()) {
    strncpy(ws_network_name, wisun_network_name, 32);
}

int WiSunBorderRouterManager::start() {
    int ret;

    mesh_system_init();

    eventOS_event_handler_create(&WiSunBorderRouterManager::bbr_tasklet, ARM_LIB_TASKLET_INIT_EVENT);

    // Ethernet initialization
    NetworkInterface *net = NetworkInterface::get_default_instance();
    if (!net) {
        tr_error("Default network interface not found");
        return -1;
    }
    EMACInterface *emacif = net->emacInterface();
    if (!emacif) {
        tr_error("Default interface is not EMAC-based");
        return -1;
    }
    EMAC &emac = emacif->get_emac();
    Nanostack::EthernetInterface *ns_if;
    nsapi_error_t err = Nanostack::get_instance().add_ethernet_interface(emac, true, &ns_if);
    if (err == NSAPI_ERROR_OK) {
        ns_if->get_mac_address(mac);
    }
    if (err < 0) {
        tr_error("Backhaul driver init failed, retval = %d", err);
    } else {
        emac_driver_id = ns_if->get_driver_id();
        emac.set_link_state_cb(&WiSunBorderRouterManager::borderrouter_backhaul_phy_status_cb);
    }

    //backhaul_interface_up(emac_driver_id);

    // Wi-SUN interface
    mac_description_storage_size_t storage_sizes;
    storage_sizes.device_decription_table_size = 32;
    storage_sizes.key_description_table_size = 4;
    storage_sizes.key_lookup_size = 1;
    storage_sizes.key_usage_size = 1;

    int8_t rf_driver_id = rf_phy.rf_register();
    MBED_ASSERT(rf_driver_id >= 0);
    if (rf_driver_id >= 0) {
        if (!mac_api) {
            mac_api = ns_sw_mac_create(rf_driver_id, &storage_sizes);
        }

        ws_if_id = arm_nwk_interface_lowpan_init(mac_api, "mesh0");

        if (ws_if_id < 0) {
            tr_error("Wi-SUN interface creation failed");
            return -1;
        }

        ret = arm_nwk_interface_configure_6lowpan_bootstrap_set(
                 ws_if_id,
                 NET_6LOWPAN_BORDER_ROUTER,
                 NET_6LOWPAN_WS);
        if (ret < 0) {
            tr_error("Could not configure Wi-SUN interface (%d)", ret);
        }

        /// @todo add wi-sun config

        wisun_interface_up();
    }

    return 0;
}

void WiSunBorderRouterManager::backhaul_interface_up(int8_t driver_id) {
    if (!eth_mac_api) {
        eth_mac_api = ethernet_mac_create(driver_id);
    }

    eth_if_id = arm_nwk_interface_ethernet_init(eth_mac_api, "bh0");

    MBED_ASSERT(eth_if_id >= 0);
    if (eth_if_id >= 0) {
        tr_warn("Backhaul interface ID: %d", eth_if_id);
        if (ws_if_id > -1) {
            ws_bbr_pan_configuration_set(ws_if_id, 0x1234);
            ws_bbr_start(ws_if_id, eth_if_id);
        }

        uint8_t prefix[16] = {0};
        arm_nwk_interface_configure_ipv6_bootstrap_set(
            eth_if_id, NET_IPV6_BOOTSTRAP_AUTONOMOUS, prefix);
        arm_nwk_interface_up(eth_if_id);
    } else {
        tr_error("Could not init ethernet");
    }
}

void WiSunBorderRouterManager::backhaul_interface_down() {
    if (eth_if_id != -1) {
        arm_nwk_interface_down(eth_if_id);
        eth_if_id = -1;
    } else {
        tr_debug("Could not set eth down");
    }
}

void WiSunBorderRouterManager::print_interface_addr(int id) {
    uint8_t address_buf[128];
    int address_count = 0;
    char buf[128];

    protocol_interface_info_entry_t* prot_stack = protocol_stack_interface_info_get_by_id(id);
    if (!prot_stack) {
        printf("Iface %d does not exists\n", id);
        return;
    }
    printf("%s:", prot_stack->interface_name);

    if (arm_net_address_list_get(id, 128, address_buf, &address_count) == 0) {
        uint8_t *t_buf = address_buf;
        for (int i = 0; i < address_count; ++i) {
            ip6tos(t_buf, buf);
            printf("\t[%d] %s\n", i, buf);
            t_buf += 16;
        }
    }
}

void WiSunBorderRouterManager::bbr_tasklet(arm_event_s *event) {
    arm_library_event_type_e event_type;
    event_type = (arm_library_event_type_e)event->event_type;

    switch (event_type) {

        case ARM_LIB_TASKLET_INIT_EVENT:
            br_tasklet_id = event->receiver;

            eventOS_event_timer_request(9, ARM_LIB_SYSTEM_TIMER_EVENT, br_tasklet_id, 20000);
            break;

        case APPLICATION_EVENT:
            if (event->event_id == NR_BACKHAUL_INTERFACE_PHY_DRIVER_READY) {
                tr_info("Backhaul interface UP");
                backhaul_interface_up(emac_driver_id);
            } else if (event->event_id == NR_BACKHAUL_INTERFACE_PHY_DOWN) {
                tr_warn("Backhaul interface DOWN");
                backhaul_interface_down();
            }
            break;

        case ARM_LIB_SYSTEM_TIMER_EVENT:
        {
            eventOS_event_timer_cancel(event->event_id, event->receiver);

            if (event->event_id == 9) {
                eventOS_event_timer_request(9, ARM_LIB_SYSTEM_TIMER_EVENT, br_tasklet_id, 20000);
            }

            bbr_information_t infos;
            ws_bbr_info_get(ws_if_id, &infos);

            printf("===== Interfaces =============\n");
            print_interface_addr(eth_if_id);
            print_interface_addr(ws_if_id);
            printf("===== DODAG information ======\n");
            printf("DODAG ID = %s\n", mbed_trace_ipv6(infos.dodag_id));
            printf("Gateway = %s\n", mbed_trace_ipv6(infos.gateway));
            printf("Devices in network = %d\n", infos.devices_in_network);
            printf("Instance ID = %d\n", infos.instance_id);
            printf("Timestamp = %lld\n", infos.timestamp);
            printf("Version = %d\n", infos.version);
            printf("===== Routes ==================\n");

            bbr_route_info_t *routes = (bbr_route_info_t *)calloc(infos.devices_in_network + 1, sizeof(bbr_route_info_t));
            if (routes) {
                int entries_count;
                if ((entries_count = ws_bbr_routing_table_get(ws_if_id, routes, infos.devices_in_network + 1)) > 0) {
                    for (int i = 0; i < entries_count; i++) {
                        uint8_t target[16];
                        uint8_t parent[16];

                        memcpy(target, infos.dodag_id, 8);
                        memcpy(target + 8, routes[i].target, 8);
                        memcpy(parent, infos.dodag_id, 8);
                        memcpy(parent + 8, routes[i].parent, 8);

                        printf("\tTarget %s -> Parent %s\n", mbed_trace_ipv6(target), mbed_trace_ipv6(parent));
                    }
                }
                free(routes);
                routes = NULL;
            }
            printf("============================\n");

            break;
        }
        default:
            break;
    }
}

void WiSunBorderRouterManager::borderrouter_backhaul_phy_status_cb(bool link_up) {
    arm_event_s event = {
        .receiver = br_tasklet_id,
        .sender = br_tasklet_id,
        .event_type = APPLICATION_EVENT,
        .event_id = (link_up ? NR_BACKHAUL_INTERFACE_PHY_DRIVER_READY : NR_BACKHAUL_INTERFACE_PHY_DOWN),
        .priority = ARM_LIB_MED_PRIORITY_EVENT,
        .event_data = 0,
    };

    eventOS_event_send(&event);
}

extern fhss_timer_t fhss_functions;

int WiSunBorderRouterManager::wisun_interface_up() {
    int32_t ret;

    fhss_timer_t *fhss_timer_ptr = NULL;

    fhss_timer_ptr = &fhss_functions;

    ret = ws_management_node_init(ws_if_id, 0x3, ws_network_name, fhss_timer_ptr);
    if (0 != ret) {
        tr_error("WS node init fail - code %"PRIi32"", ret);
        return -1;
    }

    ret = ws_management_fhss_unicast_channel_function_configure(ws_if_id, 0xff, 0xffff, 0);
    if (ret != 0) {
        tr_error("Unicast channel function configuration failed %"PRIi32"", ret);
        return -1;
    }

    ret = ws_management_fhss_broadcast_channel_function_configure(ws_if_id, 0xff, 0xffff, 0, 0);
    if (ret != 0) {
        tr_error("Broadcast channel function configuration failed %"PRIi32"", ret);
        return -1;
    }

    ret = ws_management_fhss_timing_configure(ws_if_id, 0, 0, 0);
    if (ret != 0) {
        tr_error("fhss configuration failed %"PRIi32"", ret);
        return -1;
    }

    ret = ws_management_regulatory_domain_set(ws_if_id, 0x3, 0x2, 0x3);
    if (ret != 0) {
        tr_error("Regulatory domain configuration failed %"PRIi32"", ret);
        return -1;
    }

    /*tr_info("Setting Wi-SUN regulatory information: domain = %d, phy mode id = %d, channel plan id = %d", ws_conf.regulatory_domain, ws_conf.phy_mode_id, ws_conf.channel_plan_id);
    ret = ws_management_domain_configuration_set(ws_if_id, ws_conf.regulatory_domain, ws_conf.phy_mode_id, ws_conf.channel_plan_id);
    if (ret != 0) {
        tr_error("Regulatory domain configuration failed %"PRIi32"", ret);
        return -1;
    }*/

    ret = ws_management_network_size_set(ws_if_id, 1);
    if (ret != 0) {
        tr_error("Network size configuration failed %"PRIi32"", ret);
        return -1;
    }

    /*uint8_t gtk0_value[16] = MBED_CONF_APP_GTK0;
    uint8_t *gtks[4] = {gtk0_value, NULL, NULL, NULL};
    ret = ws_test_gtk_set(ws_if_id, gtks);
    if (ret != 0) {
        tr_error("Test GTK set failed %"PRIi32"", ret);
        return -1;
    }
    tr_info("Test GTK set: %s", trace_array(gtk0_value, 16));*/


    arm_certificate_chain_entry_s chain_info;
    memset(&chain_info, 0, sizeof(arm_certificate_chain_entry_s));
    chain_info.cert_chain[0] = (const uint8_t *) WISUN_ROOT_CERTIFICATE;
    chain_info.cert_len[0] = strlen((const char *) WISUN_ROOT_CERTIFICATE) + 1;
    chain_info.cert_chain[1] = (const uint8_t *) WISUN_SERVER_CERTIFICATE;
    chain_info.cert_len[1] = strlen((const char *) WISUN_SERVER_CERTIFICATE) + 1;
    chain_info.key_chain[1] = (const uint8_t *) WISUN_SERVER_KEY;
    chain_info.chain_length = 2;
    arm_network_certificate_chain_set((const arm_certificate_chain_entry_s *) &chain_info);

    ret = arm_nwk_interface_up(ws_if_id);
    if (ret != 0) {
        tr_error("mesh0 up Fail with code: %"PRIi32"", ret);
        return ret;
    }
    tr_info("mesh0 bootstrap ongoing..");
    return 0;
}
