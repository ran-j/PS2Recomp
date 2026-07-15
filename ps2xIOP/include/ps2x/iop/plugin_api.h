#ifndef PS2X_IOP_PLUGIN_API_H
#define PS2X_IOP_PLUGIN_API_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define PS2X_IOP_PLUGIN_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define PS2X_IOP_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define PS2X_IOP_PLUGIN_EXPORT
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#define PS2X_IOP_ABI_VERSION_V1 1u
#define PS2X_IOP_QUERY_SYMBOL_V1 "ps2x_iop_query_v1"

    enum ps2x_iop_status_v1
    {
        PS2X_IOP_STATUS_OK_V1 = 0,
        PS2X_IOP_STATUS_BUFFER_TOO_SMALL_V1 = 1,
        PS2X_IOP_STATUS_INVALID_ARGUMENT_V1 = -1,
        PS2X_IOP_STATUS_UNSUPPORTED_V1 = -2,
        PS2X_IOP_STATUS_FAILED_V1 = -3,
    };

    enum ps2x_iop_rpc_abi_v1
    {
        PS2X_IOP_RPC_ABI_DEFAULT_V1 = 0,
        PS2X_IOP_RPC_ABI_REGISTERS_V1 = 1,
        PS2X_IOP_RPC_ABI_STACK_V1 = 2,
    };

    enum ps2x_iop_callback_policy_v1
    {
        PS2X_IOP_CALLBACK_RUNTIME_DEFAULT_V1 = 0,
        PS2X_IOP_CALLBACK_SUPPRESS_V1 = 1,
    };

    enum ps2x_iop_server_dispatch_policy_v1
    {
        PS2X_IOP_SERVER_DISPATCH_RUNTIME_DEFAULT_V1 = 0,
        PS2X_IOP_SERVER_DISPATCH_SUPPRESS_V1 = 1,
    };

    enum ps2x_iop_transfer_kind_v1
    {
        PS2X_IOP_TRANSFER_SET_DMA_V1 = 0,
        PS2X_IOP_TRANSFER_GET_OTHER_DATA_V1 = 1,
    };

    enum ps2x_iop_transfer_phase_v1
    {
        PS2X_IOP_TRANSFER_BEFORE_COPY_V1 = 0,
        PS2X_IOP_TRANSFER_AFTER_COPY_V1 = 1,
    };

    enum ps2x_iop_host_path_kind_v1
    {
        PS2X_IOP_PATH_ELF_DIRECTORY_V1 = 0,
        PS2X_IOP_PATH_CD_ROOT_V1 = 1,
        PS2X_IOP_PATH_CD_IMAGE_V1 = 2,
        PS2X_IOP_PATH_HOST_ROOT_V1 = 3,
        PS2X_IOP_PATH_MEMORY_CARD_ROOT_V1 = 4,
    };

    enum ps2x_iop_handle_kind_v1
    {
        PS2X_IOP_HANDLE_RPC_SERVER_V1 = 0,
        PS2X_IOP_HANDLE_RPC_PACKET_V1 = 1,
    };

    enum ps2x_iop_log_level_v1
    {
        PS2X_IOP_LOG_DEBUG_V1 = 0,
        PS2X_IOP_LOG_INFO_V1 = 1,
        PS2X_IOP_LOG_WARNING_V1 = 2,
        PS2X_IOP_LOG_ERROR_V1 = 3,
    };

    enum ps2x_iop_memory_card_operation_v1
    {
        PS2X_IOP_MC_INIT_V1 = 0,
        PS2X_IOP_MC_GET_INFO_V1 = 1,
        PS2X_IOP_MC_OPEN_V1 = 2,
        PS2X_IOP_MC_CLOSE_V1 = 3,
        PS2X_IOP_MC_SEEK_V1 = 4,
        PS2X_IOP_MC_READ_V1 = 5,
        PS2X_IOP_MC_WRITE_V1 = 6,
        PS2X_IOP_MC_FLUSH_V1 = 7,
        PS2X_IOP_MC_CHDIR_V1 = 8,
        PS2X_IOP_MC_GET_DIR_V1 = 9,
        PS2X_IOP_MC_SET_FILE_INFO_V1 = 10,
        PS2X_IOP_MC_DELETE_V1 = 11,
        PS2X_IOP_MC_FORMAT_V1 = 12,
        PS2X_IOP_MC_UNFORMAT_V1 = 13,
        PS2X_IOP_MC_MKDIR_V1 = 14,
    };

    typedef struct ps2x_iop_string_view_v1
    {
        const char *data;
        size_t size;
    } ps2x_iop_string_view_v1;

    typedef struct ps2x_iop_guest_buffer_v1
    {
        uint32_t address;
        uint32_t size;
    } ps2x_iop_guest_buffer_v1;

    typedef struct ps2x_iop_game_identity_v1
    {
        uint32_t struct_size;
        ps2x_iop_string_view_v1 elf_name;
        uint32_t entry_point;
        uint32_t crc32;
    } ps2x_iop_game_identity_v1;

    typedef struct ps2x_iop_game_matcher_v1
    {
        uint32_t struct_size;
        ps2x_iop_string_view_v1 elf_name;
        uint32_t entry_point;
        uint32_t crc32;
    } ps2x_iop_game_matcher_v1;

    typedef struct ps2x_iop_rpc_candidate_v1
    {
        uint32_t send_size;
        uint32_t receive_address;
        uint32_t receive_size;
        uint32_t end_function;
        uint32_t end_parameter;
        uint32_t plausible;
    } ps2x_iop_rpc_candidate_v1;

    typedef struct ps2x_iop_rpc_abi_request_v1
    {
        uint32_t struct_size;
        uint32_t bound_sid;
        uint32_t function;
        ps2x_iop_rpc_candidate_v1 registers;
        ps2x_iop_rpc_candidate_v1 stack;
    } ps2x_iop_rpc_abi_request_v1;

    typedef struct ps2x_iop_rpc_request_v1
    {
        uint32_t struct_size;
        uint64_t call_token;
        uint32_t client_address;
        uint32_t server_address;
        uint32_t server_function;
        uint32_t server_buffer;
        uint32_t sid;
        uint32_t function;
        uint32_t mode;
        ps2x_iop_guest_buffer_v1 send;
        ps2x_iop_guest_buffer_v1 receive;
        uint32_t end_function;
        uint32_t end_parameter;
    } ps2x_iop_rpc_request_v1;

    typedef struct ps2x_iop_rpc_result_v1
    {
        uint32_t struct_size;
        uint32_t handled;
        uint32_t result_address;
        uint32_t signal_nowait_completion;
        uint32_t signal_completion;
        uint32_t callback_policy;
        uint32_t server_dispatch_policy;
    } ps2x_iop_rpc_result_v1;

    typedef struct ps2x_iop_sif_transfer_v1
    {
        uint32_t struct_size;
        uint32_t kind;
        uint32_t phase;
        uint32_t source_address;
        uint32_t destination_address;
        uint32_t size;
    } ps2x_iop_sif_transfer_v1;

    typedef struct ps2x_iop_debug_metric_v1
    {
        uint32_t struct_size;
        ps2x_iop_string_view_v1 name;
        uint64_t value;
        uint32_t hexadecimal;
    } ps2x_iop_debug_metric_v1;

    typedef struct ps2x_iop_memory_card_request_v1
    {
        uint32_t struct_size;
        uint32_t operation;
        uint32_t arguments[5];
    } ps2x_iop_memory_card_request_v1;

    typedef struct ps2x_iop_host_api_v1
    {
        uint32_t abi_version;
        uint32_t struct_size;
        void *userdata;

        int32_t (*read_guest)(void *userdata, uint32_t address, void *destination, size_t size);
        int32_t (*write_guest)(void *userdata, uint32_t address, const void *source, size_t size);
        int32_t (*zero_guest)(void *userdata, uint32_t address, size_t size);
        int32_t (*normalize_guest_address)(void *userdata, uint32_t address, uint32_t *normalized);
        uint32_t (*allocate_iop_handle)(void *userdata, uint32_t kind);
        uint32_t (*allocate_guest)(void *userdata, uint32_t size, uint32_t alignment);
        void (*free_guest)(void *userdata, uint32_t address);

        int32_t (*audio_command)(void *userdata,
                                 uint32_t sid,
                                 uint32_t function,
                                 ps2x_iop_guest_buffer_v1 send,
                                 ps2x_iop_guest_buffer_v1 receive);

        int32_t (*get_host_path)(void *userdata,
                                 uint32_t kind,
                                 char *destination,
                                 size_t capacity,
                                 size_t *required_size);
        int32_t (*translate_guest_path)(void *userdata,
                                        ps2x_iop_string_view_v1 path,
                                        char *destination,
                                        size_t capacity,
                                        size_t *required_size);
        uint64_t (*open_host_file)(void *userdata, ps2x_iop_string_view_v1 path);
        int32_t (*host_file_size)(void *userdata, uint64_t handle, uint64_t *size);
        int32_t (*read_host_file)(void *userdata,
                                  uint64_t handle,
                                  uint64_t offset,
                                  void *destination,
                                  size_t size,
                                  size_t *bytes_read);
        void (*close_host_file)(void *userdata, uint64_t handle);

        int32_t (*memory_card)(void *userdata, const ps2x_iop_memory_card_request_v1 *request, int32_t *result);

        int32_t (*has_guest_function)(void *userdata, uint32_t address);
        int32_t (*invoke_guest_function)(void *userdata,
                                         uint64_t call_token,
                                         uint32_t address,
                                         uint32_t a0,
                                         uint32_t a1,
                                         uint32_t a2,
                                         uint32_t a3,
                                         uint32_t *result_address);
        void (*log)(void *userdata, uint32_t level, ps2x_iop_string_view_v1 message);
    } ps2x_iop_host_api_v1;

    typedef void *(*ps2x_iop_profile_create_v1)(const ps2x_iop_host_api_v1 *host,
                                                const ps2x_iop_game_identity_v1 *identity);
    typedef void (*ps2x_iop_profile_destroy_v1)(void *instance);
    typedef int32_t (*ps2x_iop_profile_reset_v1)(void *instance);
    typedef uint32_t (*ps2x_iop_profile_select_rpc_abi_v1)(void *instance, const ps2x_iop_rpc_abi_request_v1 *request);
    typedef int32_t (*ps2x_iop_profile_handle_rpc_v1)(void *instance,
                                                      const ps2x_iop_rpc_request_v1 *request,
                                                      ps2x_iop_rpc_result_v1 *result);
    typedef int32_t (*ps2x_iop_profile_on_sif_transfer_v1)(void *instance, const ps2x_iop_sif_transfer_v1 *transfer);
    typedef size_t (*ps2x_iop_profile_debug_metric_count_v1)(void *instance);
    typedef int32_t (*ps2x_iop_profile_debug_metric_v1)(void *instance, size_t index, ps2x_iop_debug_metric_v1 *metric);

    typedef struct ps2x_iop_profile_api_v1
    {
        uint32_t abi_version;
        uint32_t struct_size;
        ps2x_iop_string_view_v1 id;
        ps2x_iop_game_matcher_v1 matcher;
        size_t sid_count;
        const uint32_t *sids;
        ps2x_iop_profile_create_v1 create;
        ps2x_iop_profile_destroy_v1 destroy;
        ps2x_iop_profile_reset_v1 reset;
        ps2x_iop_profile_select_rpc_abi_v1 select_rpc_abi;
        ps2x_iop_profile_handle_rpc_v1 handle_rpc;
        ps2x_iop_profile_on_sif_transfer_v1 on_sif_transfer;
        ps2x_iop_profile_debug_metric_count_v1 debug_metric_count;
        ps2x_iop_profile_debug_metric_v1 debug_metric;
    } ps2x_iop_profile_api_v1;

    typedef struct ps2x_iop_plugin_api_v1
    {
        uint32_t abi_version;
        uint32_t struct_size;
        ps2x_iop_string_view_v1 name;
        ps2x_iop_string_view_v1 version;
        size_t profile_count;
        const ps2x_iop_profile_api_v1 *profiles;
    } ps2x_iop_plugin_api_v1;

    typedef int32_t (*ps2x_iop_query_v1_fn)(uint32_t host_abi_version, ps2x_iop_plugin_api_v1 *plugin_api);

    PS2X_IOP_PLUGIN_EXPORT int32_t ps2x_iop_query_v1(uint32_t host_abi_version, ps2x_iop_plugin_api_v1 *plugin_api);

#ifdef __cplusplus
}
#endif

#endif
