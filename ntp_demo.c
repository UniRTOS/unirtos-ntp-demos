/*****************************************************************/ /**
* @file ntp_demo.c
* @brief
* @author harry.li@quectel.com
* @date 2025-05-7
*
* @copyright Copyright (c) 2023 Quectel Wireless Solution, Co., Ltd.
* All Rights Reserved. Quectel Wireless Solution Proprietary and Confidential.
*
* @par EDIT HISTORY FOR MODULE
* <table>
* <tr><th>Date <th>Version <th>Author <th>Description
* <tr><td>2025-05-7 <td>1.0 <td>harry.li <td> Init
* </table>
**********************************************************************/
#include "qosa_sys.h"
#include "qosa_def.h"
#include "qosa_log.h"
#include "qosa_rtc.h"
#include "qcm_ntp_app.h"
#include "unirtos_app_init_registry.h"

/*===========================================================================
 *  Macro Definition
 ===========================================================================*/

#define QOS_LOG_TAG                   LOG_TAG

#define UNIR_NTP_DEMO_TASK_STACK_SIZE 4096

// NTP server address and port
#define UNIR_NTP_DEMO_SERVER          "ntp.aliyun.com"
#define UNIR_NTP_DEMO_PORT            123

// NTP configuration parameters
#define UNIR_NTP_DEMO_SIMID           0
#define UNIR_NTP_DEMO_PDPID           1
// NTP retry count and retry interval time
#define UNIR_NTP_DEMO_RETRY_CNT       3
#define UNIR_NTP_DEMO_RETRY_TIMEOUT   15

/**
 * @struct qapp_ntp_msg_t
 * @brief ntp callback function returns response event notification
 */
typedef struct
{
    qcm_ntp_client_id   client_id;  /*!< Corresponds to the currently executing NTP id */
    qcm_ntp_result_code result;     /*!< NTP APP execution result return */
    qosa_rtc_time_t     sync_time;  /*!< If successful, returns the time queried by NTP */
    void               *user_param; /*!< Corresponds to the user parameter carried during user startup */
} qapp_ntp_msg_t;

/*===========================================================================
 *  Variate
 ===========================================================================*/

static qosa_task_t g_ntp_demo_task = QOSA_NULL;
static qosa_msgq_t g_ntp_demo_msg = QOSA_NULL;

/*===========================================================================
 *  Static API Functions
 ===========================================================================*/

/**
 * NTP time query synchronization result callback function
 *
 * This function is used to handle the result callback after NTP client time query completion
 * and send it to the message queue for processing by other modules
 *
 * @param client_id NTP client identifier
 * @param result NTP query result code, indicating query success or failure
 * @param sync_time Pointer to time information structure after query
 * @param arg User-defined parameter pointer
 */
static void unir_ntp_sync_result_cb(qcm_ntp_client_id client_id, qcm_ntp_result_code result, qosa_rtc_time_t *sync_time, void *arg)
{
    qapp_ntp_msg_t msg = {0};

    /* Package NTP query result message */
    msg.client_id = client_id;
    msg.result = result;
    msg.user_param = arg;
    qosa_memcpy(&msg.sync_time, sync_time, sizeof(qosa_rtc_time_t));

    /* Send message */
    qosa_msgq_release(g_ntp_demo_msg, sizeof(qapp_ntp_msg_t), (qosa_uint8_t *)&msg, QOSA_NO_WAIT);
}

/**
 * @brief NTP result processing function
 * @param msg Pointer to NTP message structure, containing query results and time information
 */
static void unir_ntp_result_handler(qapp_ntp_msg_t *msg)
{
    QOSA_UNUSED(msg);
    char             rsp_ptr[QOSA_ARRAY_BYTE_128] = {0};  // Used to store time information string
    qosa_int32_t     rsp_len = 0;                         // Used to record time information string length
    qosa_rtc_time_t *time = QOSA_NULL;                    // Used to store queried time information
    qosa_int8_t      timezone = qosa_rtc_get_timezone();  // Used to get current timezone

    /* Check if message pointer is null */
    if (msg == QOSA_NULL)
    {
        return;
    }

    /* Process accordingly based on NTP query result */
    if (msg->result == QCM_NTP_SUCCESS)
    {
        /* Query successful: format time information and log */
        time = &msg->sync_time;
        rsp_len += qosa_snprintf(
            rsp_ptr,
            QOSA_ARRAY_BYTE_128,
            "\"%04d/%02d/%02d,%02d:%02d:%02d%c%02d\"",
            time->tm_year + 1900,
            time->tm_mon + 1,
            time->tm_mday,
            time->tm_hour,
            time->tm_min,
            time->tm_sec,
            timezone >= 0 ? '+' : '-',
            QOSA_ABS(timezone)
        );
        QLOGV("NTP_TIME: [%s]", rsp_ptr);
    }
    else
    {
        /* Query failed: record error code */
        QLOGE("ntp errcode=%x", msg->result);
    }
}

/**
 * @brief NTP query demonstration processing function
 * @param ctx Task context pointer, unused
 *
 * This function demonstrates the complete process of NTP time query synchronization, including creating NTP client, configuring parameters,
 * starting asynchronous synchronization, and processing synchronization results.
 */
static void unir_ntp_demo_process(void *ctx)
{
    qcm_ntp_client_id   client_id = 0;
    qcm_ntp_config_t    ntp_options = {0};
    qapp_ntp_msg_t      msg = {0};
    qcm_ntp_result_code ret = 0;

    // Wait 10 seconds for log capture and network registration
    qosa_task_sleep_sec(10);

    // Apply for NTP client id
    client_id = qcm_ntp_client_new();
    if (client_id <= 0)
    {
        QLOGE("ntp err");
        return;
    }

    // Configure NTP parameters and start synchronization process
    ntp_options.sim_id = UNIR_NTP_DEMO_SIMID;
    ntp_options.pdp_id = UNIR_NTP_DEMO_PDPID;

    ntp_options.num_data_bytes = 48;
    ntp_options.retry_cnt = UNIR_NTP_DEMO_RETRY_CNT;
    // Set sync local time flag, here do not update local time, if set to QOSA_TRUE will sync to local time
    ntp_options.sync_local_time = QOSA_FALSE;
    ntp_options.retry_interval_tm = UNIR_NTP_DEMO_RETRY_TIMEOUT;

    // Start NTP asynchronous execution
    ret = qcm_ntp_sync_start(client_id, UNIR_NTP_DEMO_SERVER, UNIR_NTP_DEMO_PORT, &ntp_options, unir_ntp_sync_result_cb, QOSA_NULL);
    if (ret != QCM_NTP_SUCCESS)
    {
        QLOGE("ntp start err =%x", ret);
        qosa_msgq_delete(g_ntp_demo_msg);
        return;
    }

    // Wait for synchronization result message and process it
    qosa_msgq_wait(g_ntp_demo_msg, (qosa_uint8_t *)&msg, sizeof(qapp_ntp_msg_t), QOSA_WAIT_FOREVER);
    unir_ntp_result_handler(&msg);
    qosa_msgq_delete(g_ntp_demo_msg);
}

void unir_ntp_demo_init(void)
{
    QLOGV("enter UniRTOS NTP DEMO !!!");
    if (g_ntp_demo_msg == QOSA_NULL)
    {
        qosa_msgq_create(&g_ntp_demo_msg, sizeof(qapp_ntp_msg_t), 5);
    }
    if (g_ntp_demo_task == QOSA_NULL)
    {
        qosa_task_create(&g_ntp_demo_task, UNIR_NTP_DEMO_TASK_STACK_SIZE, QOSA_PRIORITY_NORMAL, "ntp_demo", unir_ntp_demo_process, QOSA_NULL);
    }
}
UNIRTOS_APP_EXPORT(320, "ntp_demo", unir_ntp_demo_init);
