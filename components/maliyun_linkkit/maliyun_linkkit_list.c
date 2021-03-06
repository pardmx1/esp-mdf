/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2019 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "mwifi.h"

#include "iot_export_linkkit.h"

static const char *TAG   = "maliyun_linkkit_list";

typedef struct mac_list_ {
    int device_id;
    char product[IOTX_PRODUCT_KEY_LEN];
    uint8_t mac[MWIFI_ADDR_LEN];
    SLIST_ENTRY(mac_list_) next;    //!< next command in the list
} mac_list_t;

/** linked list of command structures */

static SLIST_HEAD(test_list_, mac_list_) g_mac_list;

static xSemaphoreHandle g_list_lock = NULL;


static void maliyun_linkit_list_mutex_lock(void)
{
    while (xSemaphoreTake(g_list_lock, portMAX_DELAY) != pdPASS);
}

static void maliyun_linkit_list_mutex_unlock(void)
{
    xSemaphoreGive(g_list_lock);
}

esp_err_t maliyun_linkkit_list_create(void)
{
    if (g_list_lock == NULL) {
        g_list_lock = xSemaphoreCreateMutex();
        configASSERT(g_list_lock);
    }

    SLIST_INIT(&g_mac_list);
    return ESP_OK;
}

esp_err_t maliyun_linkkit_list_destroy(void)
{
    maliyun_linkit_list_mutex_lock();
    mac_list_t *it, *tmp;
    SLIST_FOREACH_SAFE(it, &g_mac_list, next, tmp) {
        SLIST_REMOVE(&g_mac_list, it, mac_list_, next);
        free(it);
    }
    maliyun_linkit_list_mutex_unlock();

    if (g_list_lock != NULL) {
        vQueueDelete(g_list_lock);
        g_list_lock = NULL;
    }

    return ESP_OK;
}

esp_err_t maliyun_linkkit_insert(int device_id, const uint8_t *mac, const char *product)
{
    MDF_PARAM_CHECK(device_id >= 0);
    MDF_PARAM_CHECK(mac);
    MDF_PARAM_CHECK(product);

    mac_list_t *item = (mac_list_t *) MDF_CALLOC(1, sizeof(mac_list_t));

    item->device_id = device_id;
    memcpy(item->mac, mac, MWIFI_ADDR_LEN);
    strncpy(item->product, product, IOTX_PRODUCT_KEY_LEN);

    maliyun_linkit_list_mutex_lock();

    mac_list_t *last = SLIST_FIRST(&g_mac_list);

    if (last == NULL) {
        SLIST_INSERT_HEAD(&g_mac_list, item, next);
    } else {
        mac_list_t *it;

        while ((it = SLIST_NEXT(last, next)) != NULL) {
            last = it;
        }

        SLIST_INSERT_AFTER(last, item, next);
    }

    maliyun_linkit_list_mutex_unlock();

    return ESP_OK;
}

esp_err_t maliyun_linkkit_list_get(int device_id)
{
    MDF_PARAM_CHECK(device_id >= 0);

    maliyun_linkit_list_mutex_lock();
    mac_list_t *it, *tmp;
    SLIST_FOREACH_SAFE(it, &g_mac_list, next, tmp) {
        if (device_id == it->device_id) {
            maliyun_linkit_list_mutex_unlock();
            return ESP_OK;
        }
    }
    maliyun_linkit_list_mutex_unlock();
    return ESP_FAIL;
}

esp_err_t maliyun_linkkit_list_get_mac(int device_id, uint8_t *mac)
{
    MDF_PARAM_CHECK(device_id >= 0);
    MDF_PARAM_CHECK(mac);

    maliyun_linkit_list_mutex_lock();
    mac_list_t *it, *tmp;
    SLIST_FOREACH_SAFE(it, &g_mac_list, next, tmp) {
        if (it->device_id == device_id) {
            memcpy(mac, it->mac, MWIFI_ADDR_LEN);
            maliyun_linkit_list_mutex_unlock();
            return ESP_OK;
        }
    }
    maliyun_linkit_list_mutex_unlock();
    return ESP_FAIL;
}

esp_err_t maliyun_linkkit_list_get_id(const uint8_t *mac, int *device_id)
{
    MDF_PARAM_CHECK(mac);
    MDF_PARAM_CHECK(device_id);

    maliyun_linkit_list_mutex_lock();
    mac_list_t *it, *tmp;
    SLIST_FOREACH_SAFE(it, &g_mac_list, next, tmp) {
        if (!memcmp(mac, it->mac, MWIFI_ADDR_LEN)) {
            *device_id = it->device_id;
            maliyun_linkit_list_mutex_unlock();
            return ESP_OK;
        }
    }
    maliyun_linkit_list_mutex_unlock();
    return ESP_FAIL;
}

esp_err_t maliyun_linkkit_list_delete_id(int device_id)
{
    MDF_PARAM_CHECK(device_id > 0);

    maliyun_linkit_list_mutex_lock();
    mac_list_t *it, *tmp;
    SLIST_FOREACH_SAFE(it, &g_mac_list, next, tmp) {
        if (it->device_id == device_id) {
            SLIST_REMOVE(&g_mac_list, it, mac_list_, next);
            free(it);
            maliyun_linkit_list_mutex_unlock();
            return ESP_OK;
        }
    }

    maliyun_linkit_list_mutex_unlock();
    return ESP_FAIL;
}

esp_err_t maliyun_linkkit_list_get_offline_id(uint8_t *mac_array, size_t array_num, int *device_id)
{
    MDF_PARAM_CHECK(mac_array);
    MDF_PARAM_CHECK(array_num > 0);
    MDF_PARAM_CHECK(device_id);

    bool offline_id = false;

    maliyun_linkit_list_mutex_lock();
    mac_list_t *it, *tmp;
    SLIST_FOREACH_SAFE(it, &g_mac_list, next, tmp) {
        for (int i = 0; i < array_num; i++) {
            if (!memcmp(mac_array + i * MWIFI_ADDR_LEN, it->mac, MWIFI_ADDR_LEN)) {
                offline_id = true;
                break;
            }
        }

        if (!offline_id) {
            *device_id = it->device_id;
            maliyun_linkit_list_mutex_unlock();
            return ESP_OK;
        }
    }
    maliyun_linkit_list_mutex_unlock();
    return ESP_FAIL;
}

int maliyun_linkkit_list_get_product_num(const char *product)
{
    MDF_PARAM_CHECK(product);

    uint8_t product_cnt = 0;

    maliyun_linkit_list_mutex_lock();

    mac_list_t *it, *tmp;
    SLIST_FOREACH_SAFE(it, &g_mac_list, next, tmp) {
        if (!strncmp(it->product, product, strlen(product))) {
            product_cnt ++;
        }
    }
    maliyun_linkit_list_mutex_unlock();
    return product_cnt;
}

int maliyun_linkkit_list_get_mac_array(const char *product, uint8_t *mac_table)
{
    MDF_PARAM_CHECK(product);
    MDF_PARAM_CHECK(mac_table);

    uint8_t product_cnt = 0;
    maliyun_linkit_list_mutex_lock();

    mac_list_t *it, *tmp;
    SLIST_FOREACH_SAFE(it, &g_mac_list, next, tmp) {
        if (!memcmp(it->product, product, strlen(product))) {
            memcpy(mac_table + product_cnt * MWIFI_ADDR_LEN, it->mac, MWIFI_ADDR_LEN);
            product_cnt ++;
        }
    }
    maliyun_linkit_list_mutex_unlock();
    return product_cnt;
}