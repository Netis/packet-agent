#include <iostream>
#include <cstring>
#include <string>
#include <chrono>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <pcap/pcap.h>

#include <netdb.h>

#include "packet_agent_extension.h"
#include "utils.h"

// using namespace std;


#ifdef __cplusplus
extern "C"
{
#endif

/*
# proto_erspan_type3 
JSON_STR=$(cat << EOF
{
    "ext_file_path": "libproto_erspan_type3.so",
    "ext_params": {
        "remoteips": [
            "10.1.1.37"
        ],
        "bind_device": "eno16777984",
        "pmtudisc_option": "dont",
        "use_default_header": false,
        "enable_spanid": true,
        "spanid": 1020,
        "enable_sequence": true,
        "sequence_begin": 10000,
        "enable_timestamp": true,
        "timestamp_type": 0,
        "enable_security_grp_tag": true,
        "security_grp_tag": 32768,
        "enable_hw_id": true,
        "hw_id": 31        
    }
}
EOF
)
./pktminerg -i eno16777984 --proto-config "${JSON_STR}"
*/

#define ETHERNET_TYPE_ERSPAN_TYPE3    0x22eb
#define INVALIDE_SOCKET_FD  -1

#define EXT_CONFIG_KEY_OF_EXT_PARAMS_REMOTE_IPS         "remoteips"
#define EXT_CONFIG_KEY_OF_EXT_PARAMS_BIND_DEVICE        "bind_device"
#define EXT_CONFIG_KEY_OF_EXT_PARAMS_PMTUDISC_OPTION    "pmtudisc_option"

#define EXT_CONFIG_KEY_OF_EXT_PARAMS_USE_DEFAULT         "use_default_header"
#define EXT_CONFIG_KEY_OF_EXT_PARAMS_ENABLE_SPANID       "enable_spanid"
#define EXT_CONFIG_KEY_OF_EXT_PARAMS_SPANID              "spanid"
#define EXT_CONFIG_KEY_OF_EXT_PARAMS_ENABLE_SEQ          "enable_sequence"
#define EXT_CONFIG_KEY_OF_EXT_PARAMS_SEQ_INIT_VALUE      "sequence_begin"
#define EXT_CONFIG_KEY_OF_EXT_PARAMS_ENABLE_TIMESTAMP    "enable_timestamp"
#define EXT_CONFIG_KEY_OF_EXT_PARAMS_TIMESTAMP_TYPE      "timestamp_type"
#define EXT_CONFIG_KEY_OF_EXT_PARAMS_ENABLE_SECURITY_GRP_TAG "enable_security_grp_tag"
#define EXT_CONFIG_KEY_OF_EXT_PARAMS_SECURITY_GRP_TAG    "security_grp_tag"
#define EXT_CONFIG_KEY_OF_EXT_PARAMS_ENABLE_HW_ID        "enable_hw_id"
#define EXT_CONFIG_KEY_OF_EXT_PARAMS_HW_ID               "hw_id"


#define LOG_MODULE_NAME "proto_erspan_type3: "


typedef enum _timestamp_type {
    GRA_100_MICROSECONDS = 0,
    GRA_100_NANOSECONDS,
    GRA_IEEE_1588,
    GRA_USER_DEFINED
}TimestampType;

typedef struct _ERSpanType23GREHdr {
    uint16_t flags;
    uint16_t protocol;
    uint32_t sequence;
} ERSpanType23GREHdr;

typedef struct _ERSpanType3Hdr {
    uint16_t ver_vlan;
    uint16_t flags_spanId;
    uint32_t timestamp; // granularity : 100 microseconds
    uint16_t security_grp_tag;
    uint16_t hwid_flags;
} ERSpanType3Hdr;

typedef struct _extension_ctx {
    uint8_t use_default_header;
    uint8_t enable_sequence;
    uint8_t enable_spanid;
    uint8_t enable_timestamp;
    uint8_t timestamp_type;
    uint32_t sequence_begin;
    uint16_t spanid;
    uint8_t enable_security_grp_tag;
    uint8_t enable_hw_id;
    uint8_t hw_id;
    uint16_t security_grp_tag;
    uint8_t need_update_header;    
    std::chrono::high_resolution_clock::time_point time_begin;
    std::vector<std::string> remoteips;
    std::vector<int> socketfds;
    std::vector<struct AddressV4V6> remote_addrs;
    std::vector<std::vector<char>> buffers;
    uint32_t proto_header_len;
    std::string bind_device;
    int pmtudisc;
}extension_ctx_t;



// tunnel header in outer L3 payload
int get_proto_header_size(void* ext_handle, uint8_t* packet, uint32_t* len) {
    // TBD: support optional header fields
    return sizeof(ERSpanType23GREHdr) + sizeof(ERSpanType3Hdr);
}


int _reset_context(extension_ctx_t* ctx) {
    ctx->use_default_header = 0;
    ctx->enable_sequence = 0;
    ctx->enable_spanid = 0;
    ctx->enable_timestamp = 0;
    ctx->sequence_begin = 0;
    ctx->spanid = 0;
    ctx->timestamp_type = GRA_100_MICROSECONDS;
    ctx->enable_security_grp_tag = 0;
    ctx->enable_hw_id = 0;
    ctx->hw_id = 0;
    ctx->security_grp_tag = 0;
    ctx->need_update_header = 0;
    ctx->remoteips.clear();
    ctx->socketfds.clear();
    ctx->remote_addrs.clear();
    ctx->buffers.clear();
    ctx->proto_header_len = 0;
    ctx->bind_device = "";
    ctx->pmtudisc = -1;
    return 0;
}

int _init_proto_config(extension_ctx_t* ctx, std::string& proto_config) {
    std::stringstream ss(proto_config);
    boost::property_tree::ptree proto_config_tree;

    try {
        boost::property_tree::read_json(ss, proto_config_tree);
    } catch (boost::property_tree::ptree_error & e) {
        std::cerr << LOG_MODULE_NAME << "Parse proto_config json string failed!" << std::endl;
        return -1;
    }

    if (proto_config_tree.get_child_optional(EXT_CONFIG_KEY_OF_EXTERN_PARAMS)) {
        boost::property_tree::ptree& config_items = proto_config_tree.get_child(EXT_CONFIG_KEY_OF_EXTERN_PARAMS);
        // ext_params.remoteips[]
        if (config_items.get_child_optional(EXT_CONFIG_KEY_OF_EXT_PARAMS_REMOTE_IPS)) {
            boost::property_tree::ptree& remote_ip_tree = config_items.get_child(EXT_CONFIG_KEY_OF_EXT_PARAMS_REMOTE_IPS);

            for (auto it = remote_ip_tree.begin(); it != remote_ip_tree.end(); it++) {
                ctx->remoteips.push_back(it->second.get_value<std::string>());
            }
        }

        // ext_params.bind_device
        if (config_items.get_child_optional(EXT_CONFIG_KEY_OF_EXT_PARAMS_BIND_DEVICE)) {
            ctx->bind_device = config_items.get<std::string>(EXT_CONFIG_KEY_OF_EXT_PARAMS_BIND_DEVICE);
        }

        // ext_params.pmtudisc
        if (config_items.get_child_optional(EXT_CONFIG_KEY_OF_EXT_PARAMS_PMTUDISC_OPTION)) {
            std::string pmtudisc_option = config_items.get<std::string>(EXT_CONFIG_KEY_OF_EXT_PARAMS_PMTUDISC_OPTION);
            if (pmtudisc_option == "do") {
                ctx->pmtudisc = IP_PMTUDISC_DO;
            } else if (pmtudisc_option == "dont") {
                ctx->pmtudisc = IP_PMTUDISC_DONT;
            } else if (pmtudisc_option == "want") {
                ctx->pmtudisc = IP_PMTUDISC_WANT;
            } else {
                std::cerr << LOG_MODULE_NAME << "pmtudisc_option config invalid. Reset to -1." << std::endl;
                ctx->pmtudisc = -1;
            }
        }

        if (config_items.get_child_optional(EXT_CONFIG_KEY_OF_EXT_PARAMS_USE_DEFAULT)) {
            ctx->use_default_header = static_cast<uint8_t>(config_items.get<bool>(EXT_CONFIG_KEY_OF_EXT_PARAMS_USE_DEFAULT));
            if (ctx->use_default_header) {
                return 0;
            }
        }


        if (config_items.get_child_optional(EXT_CONFIG_KEY_OF_EXT_PARAMS_ENABLE_SPANID)) {
            ctx->enable_spanid = static_cast<uint8_t>(config_items.get<bool>(EXT_CONFIG_KEY_OF_EXT_PARAMS_ENABLE_SPANID));
            if (ctx->enable_spanid) {
                if (config_items.get_child_optional(EXT_CONFIG_KEY_OF_EXT_PARAMS_SPANID)) {
                    ctx->spanid = config_items.get<uint16_t>(EXT_CONFIG_KEY_OF_EXT_PARAMS_SPANID);
                    if (ctx->spanid >= 0x0400) {
                        std::cerr << LOG_MODULE_NAME << "spanid value is out of bound(2 ^ 10). Reset to 0." << std::endl;
                        ctx->spanid = 0;
                    }
                }
            }
        }

        if (config_items.get_child_optional(EXT_CONFIG_KEY_OF_EXT_PARAMS_ENABLE_SEQ)) {
            ctx->enable_sequence = static_cast<uint8_t>(config_items.get<bool>(EXT_CONFIG_KEY_OF_EXT_PARAMS_ENABLE_SEQ));
            if (ctx->enable_sequence) {
                if (config_items.get_child_optional(EXT_CONFIG_KEY_OF_EXT_PARAMS_SEQ_INIT_VALUE)) {
                    ctx->sequence_begin = config_items.get<uint32_t>(EXT_CONFIG_KEY_OF_EXT_PARAMS_SEQ_INIT_VALUE);
                }                
            }
        }

        if (config_items.get_child_optional(EXT_CONFIG_KEY_OF_EXT_PARAMS_ENABLE_TIMESTAMP)) {
            ctx->enable_timestamp = static_cast<uint8_t>(config_items.get<bool>(EXT_CONFIG_KEY_OF_EXT_PARAMS_ENABLE_TIMESTAMP));
            if (ctx->enable_timestamp ) {
                ctx->time_begin = std::chrono::high_resolution_clock::now();
                if (config_items.get_child_optional(EXT_CONFIG_KEY_OF_EXT_PARAMS_TIMESTAMP_TYPE)) {
                    ctx->timestamp_type = static_cast<uint8_t>(config_items.get<bool>(EXT_CONFIG_KEY_OF_EXT_PARAMS_TIMESTAMP_TYPE));
                    if (ctx->timestamp_type != GRA_100_MICROSECONDS) {
                        std::cout << LOG_MODULE_NAME << "Now only support 100-microseconds granularity type. Reset type to it." << std::endl;
                        ctx->timestamp_type = GRA_100_MICROSECONDS;
                    }
                }
            }
        }

        if (config_items.get_child_optional(EXT_CONFIG_KEY_OF_EXT_PARAMS_ENABLE_SECURITY_GRP_TAG)) {
            ctx->enable_security_grp_tag = static_cast<uint8_t>(config_items.get<bool>(EXT_CONFIG_KEY_OF_EXT_PARAMS_ENABLE_SECURITY_GRP_TAG));
            if (ctx->enable_security_grp_tag) {
                if (config_items.get_child_optional(EXT_CONFIG_KEY_OF_EXT_PARAMS_SECURITY_GRP_TAG)) {
                    ctx->security_grp_tag = config_items.get<uint16_t>(EXT_CONFIG_KEY_OF_EXT_PARAMS_SECURITY_GRP_TAG);
                }
            }
        }

        if (config_items.get_child_optional(EXT_CONFIG_KEY_OF_EXT_PARAMS_ENABLE_HW_ID)) {
            ctx->enable_hw_id = static_cast<uint8_t>(config_items.get<bool>(EXT_CONFIG_KEY_OF_EXT_PARAMS_ENABLE_HW_ID));
            if (ctx->enable_hw_id) {
                if (config_items.get_child_optional(EXT_CONFIG_KEY_OF_EXT_PARAMS_HW_ID)) {
                    ctx->hw_id = config_items.get<uint16_t>(EXT_CONFIG_KEY_OF_EXT_PARAMS_HW_ID);
                    if (ctx->hw_id >= 0x40) {
                        std::cerr << LOG_MODULE_NAME << "hw_id value is out of bound(2 ^ 6). Reset to 0." << std::endl;
                        ctx->hw_id = 0;
                    }
                }
            }
        }
    }



    return 0;
}

int _init_context(extension_ctx_t* ctx, std::string& proto_config) {
    _reset_context(ctx);
    ctx->use_default_header = 1;

    if (_init_proto_config(ctx, proto_config)) {
        return 1;
    }
    std::cout << LOG_MODULE_NAME << "The context values:" << std::endl;
    std::cout << "remote ips: " << std::endl;
    for (auto& item : ctx->remoteips) {
        std::cout << "  " << item << std::endl;
    }
    std::cout << "bind_device " << ctx->bind_device <<std::endl;
    std::cout << "pmtudisc_option " << ctx->pmtudisc <<std::endl;
    std::cout << "use_default_header " << (uint32_t)ctx->use_default_header << std::endl;
    std::cout << "enable_sequence " << (uint32_t)ctx->enable_sequence << std::endl;
    std::cout << "enable_spanid " << (uint32_t)ctx->enable_spanid <<std::endl;
    std::cout << "enable_timestamp " << (uint32_t)ctx->enable_timestamp << std::endl;
    std::cout << "timestamp_type " << (uint32_t)ctx->timestamp_type << std::endl;
    std::cout << "sequence_begin " << (uint32_t)ctx->sequence_begin << std::endl;
    std::cout << "spanid " << (uint32_t)ctx->spanid << std::endl;
    std::cout << "enable_security_grp_tag " << (uint32_t)ctx->enable_security_grp_tag << std::endl;
    std::cout << "security_grp_tag " << (uint32_t)ctx->security_grp_tag << std::endl;
    std::cout << "enable_hw_id " << (uint32_t)ctx->enable_hw_id << std::endl;
    std::cout << "hw_id " << (uint32_t)ctx->hw_id << std::endl;  

    // allocate space for socket
    unsigned long remote_ips_size = ctx->remoteips.size();
    ctx->remote_addrs.resize(remote_ips_size);
    ctx->socketfds.resize(remote_ips_size);
    ctx->buffers.resize(remote_ips_size);
    for (size_t i = 0; i < remote_ips_size; ++i) {
        ctx->socketfds[i] = INVALIDE_SOCKET_FD;
        ctx->buffers[i].resize(65535 + sizeof(ERSpanType23GREHdr) + sizeof(ERSpanType3Hdr), '\0'); // original packet, plus tunnel header in outer L3 payload
    }

    // update header indicate
    if (ctx->use_default_header) {
        ctx->need_update_header = 0;
    } else {
        ctx->need_update_header = static_cast<uint8_t>(ctx->enable_sequence || ctx->enable_timestamp);
    }

    return 0;
}

int _init_proto_header(std::vector<char>& buffer, uint32_t seq_beg, uint16_t spanid, uint16_t secur_grp_tag, uint8_t hw_id) {

    ERSpanType23GREHdr *hdr = reinterpret_cast<ERSpanType23GREHdr*>(&(buffer[0]));
    hdr->flags = htons(0x1000);  // S bit
    hdr->protocol = htons(ETHERNET_TYPE_ERSPAN_TYPE3);
    hdr->sequence = htonl(seq_beg);

    ERSpanType3Hdr* erspan_hdr = reinterpret_cast<ERSpanType3Hdr*>(&(buffer[sizeof(ERSpanType23GREHdr)]));
    uint16_t ver_vlan = 0x2000; // ver = 2, vlan = 0
    erspan_hdr->ver_vlan = htons(ver_vlan);
    erspan_hdr->flags_spanId = htons(spanid); // COS = 0, BSO = 0, T = 0
    erspan_hdr->timestamp = 0;  
    erspan_hdr->security_grp_tag = htons(secur_grp_tag);
    erspan_hdr->hwid_flags = htons(hw_id << 4); // Gra = 00b
    return sizeof(ERSpanType23GREHdr) + sizeof(ERSpanType3Hdr);
}


int _init_sockets(std::string& remote_ip, AddressV4V6& remote_addr, int& socketfd,
                  std::string& bind_device, int pmtudisc) {
    if (socketfd == INVALIDE_SOCKET_FD) {
        int err = remote_addr.buildAddr(remote_ip.c_str());
        if (err != 0) {
            std::cerr << LOG_MODULE_NAME << "buildAddr failed, ip is " << remote_ip.c_str()
            << std::endl;
            return err;
        }

        int domain = remote_addr.getDomainAF_NET();
        if ((socketfd = socket(domain, SOCK_RAW, IPPROTO_GRE)) == INVALIDE_SOCKET_FD) {
            std::cerr << LOG_MODULE_NAME << "Create socket failed, error code is " << errno
            << ", error is " << strerror(errno) << "."
            << std::endl;
            return -1;
        }

        if (bind_device.length() > 0) {
            if (setsockopt(socketfd, SOL_SOCKET, SO_BINDTODEVICE,
                           bind_device.c_str(), static_cast<socklen_t>(bind_device.length())) < 0) {
                std::cerr << LOG_MODULE_NAME << "SO_BINDTODEVICE failed, error code is " << errno
                << ", error is " << strerror(errno) << "."
                << std::endl;
                return -1;
            }
        }

        if (pmtudisc >= 0) {
            if (setsockopt(socketfd, SOL_IP, IP_MTU_DISCOVER, &pmtudisc, sizeof(pmtudisc)) == -1) {
                std::cerr << LOG_MODULE_NAME << "IP_MTU_DISCOVER failed, error code is " << errno
                << ", error is " << strerror(errno) << "."
                << std::endl;
                return -1;
            }
        }
    }
    return 0;
}

int init_export(void* ext_handle, const char* ext_config) {
    if (!ext_handle) {
        return 1;
    }

    packet_agent_extension_itf_t* extension_itf = reinterpret_cast<packet_agent_extension_itf_t*>(ext_handle);
    if (!extension_itf->ctx) {
        return 1;
    }

    extension_ctx_t* ctx = reinterpret_cast<extension_ctx_t*>(extension_itf->ctx);

    std::string proto_config{};
    if (ext_config) {
        proto_config = ext_config;
    }
    std::cout << LOG_MODULE_NAME << "proto_config is " << proto_config <<  std::endl;

    _init_context(ctx, proto_config);


    for (size_t i = 0; i < ctx->remoteips.size(); ++i) {
        ctx->proto_header_len = static_cast<uint32_t>(_init_proto_header(ctx->buffers[i], ctx->sequence_begin,
                                                                         ctx->spanid, ctx->security_grp_tag, ctx->hw_id));
        int ret = _init_sockets(ctx->remoteips[i], ctx->remote_addrs[i], ctx->socketfds[i],
                                ctx->bind_device, ctx->pmtudisc);
        if (ret != 0) {
            std::cerr << LOG_MODULE_NAME << "Failed with index: " << i << std::endl;
            return ret;
        }
    }

    return 0;
}


int _export_packet_in_one_socket(AddressV4V6& remote_addr_v4v6, int& socketfd, std::vector<char>& buffer,
                                 uint32_t content_offset, const uint8_t *packet, uint32_t len) {
    struct sockaddr* remote_addr = remote_addr_v4v6.getSockAddr();
    size_t socklen = remote_addr_v4v6.getSockLen();
    size_t length = (size_t) (len <= 65535 ? len : 65535);


    std::memcpy(reinterpret_cast<void*>(&(buffer[content_offset])),
                reinterpret_cast<const void*>(packet), length);
    ssize_t nSend = sendto(socketfd, &(buffer[0]), length + content_offset, 0, remote_addr,
                           static_cast<socklen_t>(socklen));
    while (nSend == -1 && errno == ENOBUFS) {
        usleep(1000);
        nSend = static_cast<int>(sendto(socketfd, &(buffer[0]), length + content_offset, 0, remote_addr,
                                        static_cast<socklen_t>(socklen)));
    }
    if (nSend == -1) {
        std::cerr << LOG_MODULE_NAME << "Send to socket failed, error code is " << errno
        << ", error is " << strerror(errno) << "."
        << std::endl;
        return -1;
    }
    if (nSend < (ssize_t) (length + content_offset)) {
        std::cerr << LOG_MODULE_NAME << "Send socket " << length + content_offset
        << " bytes, but only " << nSend <<
        " bytes are sent success." << std::endl;
        return 1;
    }
    return 0;
}

int _export_packet_update_header(std::vector<char>& buffer, uint8_t enable_sequence, uint8_t enable_timestamp,
                                 std::chrono::high_resolution_clock::time_point time_begin) {
    if (enable_sequence) {
        ERSpanType23GREHdr *hdr = reinterpret_cast<ERSpanType23GREHdr*>(&(buffer[0]));
        uint32_t seq = ntohl(hdr->sequence);
        seq++;
        hdr->sequence = htonl(seq);
    }
    if (enable_timestamp) {
        ERSpanType3Hdr *erspan_hdr = reinterpret_cast<ERSpanType3Hdr *>(&(buffer[sizeof(ERSpanType23GREHdr)]));
        std::chrono::high_resolution_clock::time_point time_now = std::chrono::high_resolution_clock::now();
        std::chrono::microseconds micro_seconds =
                std::chrono::duration_cast<std::chrono::microseconds>(time_now - time_begin);
        erspan_hdr->timestamp = htonl(static_cast<uint32_t>(micro_seconds.count() / 100));
    }    
    return 0;
}


int export_packet(void* ext_handle, const void* pkthdr, const uint8_t *packet) {
    if (!ext_handle) {
        return 1;
    }
    packet_agent_extension_itf_t* extension_itf = reinterpret_cast<packet_agent_extension_itf_t*>(ext_handle);
    if (!extension_itf->ctx) {
        return 1;
    }
    extension_ctx_t* ctx = reinterpret_cast<extension_ctx_t*>(extension_itf->ctx);

    if (!pkthdr || !packet) {
        std::cerr << LOG_MODULE_NAME << "pkthdr or pkt is null. " << std::endl;
        return 1;
    }
    uint32_t len = (reinterpret_cast<const struct pcap_pkthdr*>(pkthdr))->caplen;

    int ret = 0;
    for (size_t i = 0; i < ctx->remoteips.size(); ++i) {
        if (ctx->need_update_header) {
           ret |=  _export_packet_update_header(ctx->buffers[i], ctx->enable_sequence, ctx->enable_timestamp,
                                                    ctx->time_begin);
        }

        ret |= _export_packet_in_one_socket(ctx->remote_addrs[i], ctx->socketfds[i], ctx->buffers[i],
                                            ctx->proto_header_len, packet, len);
    }
    return ret;
}



int close_export(void* ext_handle) {
    if (!ext_handle) {
        return 1;
    }
    packet_agent_extension_itf_t* extension_itf = reinterpret_cast<packet_agent_extension_itf_t*>(ext_handle);
    if (!extension_itf->ctx) {
        return 1;
    }
    extension_ctx_t* ctx = reinterpret_cast<extension_ctx_t*>(extension_itf->ctx);

    for (size_t i = 0; i < ctx->remoteips.size(); ++i) {
        if (ctx->socketfds[i] != INVALIDE_SOCKET_FD) {
            close(ctx->socketfds[i]);
            ctx->socketfds[i] = INVALIDE_SOCKET_FD;
        }
    }
    return 0;
}

// 
int terminate(void* ext_handle) {
    if (ext_handle) {
        packet_agent_extension_itf_t* handle = reinterpret_cast<packet_agent_extension_itf_t*>(ext_handle);
        if (handle->ctx) {
            delete reinterpret_cast<extension_ctx_t*>(handle->ctx);
        }
    }
    return 0;
}

int packet_agent_extension_entry(void* ext_handle) {
    if (!ext_handle) {
        std::cerr << LOG_MODULE_NAME << "The ext_handle is not ready!" << std::endl;
        return -1;
    }

    extension_ctx_t* ctx = new extension_ctx_t();
    if (!ctx) {
        std::cerr << LOG_MODULE_NAME << "malloc context failed!" << std::endl;
        return -1;
    }

    packet_agent_extension_itf_t* extension_itf = (packet_agent_extension_itf_t* )ext_handle;

    extension_itf->init_export_func = init_export;
    extension_itf->export_packet_func = export_packet;
    extension_itf->close_export_func = close_export;
    extension_itf->terminate_func = terminate;
    extension_itf->ctx = ctx;
    return 0;
}



#ifdef __cplusplus
}
#endif