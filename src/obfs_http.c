/*
 * obfs_http.c - Implementation of http obfuscating
 *
 * Copyright (C) 2013 - 2016, Max Lv <max.c.lv@gmail.com>
 *
 * This file is part of the simple-obfs.
 *
 * simple-obfs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * simple-obfs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with simple-obfs; see the file COPYING. If not, see
 * <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <strings.h>
#include <ctype.h> /* isblank() */

#include "base64.h"
#include "utils.h"
#include "obfs_http.h"

static const char *http_request_template =
    "%s %s HTTP/1.1\r\n"
    "Host: %s\r\n"
    "User-Agent: curl/7.%d.%d\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Key: %s\r\n"
    "Content-Length: %lu\r\n"
    "ServerRemotePort: %s\r\n"     //模板中添加分发端口字段
    "\r\n";

static const char *http_response_template =
    "HTTP/1.1 101 Switching Protocols\r\n"
    "Server: nginx/1.%d.%d\r\n"
    "Date: %s\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Accept: %s\r\n"
    "\r\n";

static int obfs_http_request(buffer_t *, size_t, obfs_t *);
static int obfs_http_response(buffer_t *, size_t, obfs_t *);
static int deobfs_http_header(buffer_t *, size_t, obfs_t *);
static int check_http_header(buffer_t *buf);
static void disable_http(obfs_t *obfs);
static int is_enable_http(obfs_t *obfs);
static void set_HttpHeader_ServerRemotePort(char* serverRemotePort );
static char* getServerRemotePort(void);

static int get_header(const char *, const char *, int, char **);
static int next_header(const char **, int *);

static obfs_para_t obfs_http_st = {
    .name            = "http",
    .port            = 80,
    .send_empty_response_upon_connection = true,

    .obfs_request    = &obfs_http_request,
    .obfs_response   = &obfs_http_response,
    .deobfs_request  = &deobfs_http_header,
    .deobfs_response = &deobfs_http_header,
    .check_obfs      = &check_http_header,
    .disable         = &disable_http,
    .is_enable       = &is_enable_http,
    .setObfsServerRemotePort = &set_HttpHeader_ServerRemotePort,     //设置指针指向
    .getObfsServerRemotePort =&getServerRemotePort,
};

obfs_para_t *obfs_http = &obfs_http_st;

char *chServerRemotePort=NULL;

//设置远程服务分发接口
static void set_HttpHeader_ServerRemotePort(char* serverRemotePort ){
    //设置服务分发端口
//    printf("set_HttpHeader_ServerRemotePort————设置服务分发端口%s--——————\n", serverRemotePort);
    chServerRemotePort=serverRemotePort;
}

static char* getServerRemotePort(){
    return chServerRemotePort;
}

static int
obfs_http_request(buffer_t *buf, size_t cap, obfs_t *obfs)
{

    if (obfs == NULL || obfs->obfs_stage != 0) return 0;
    obfs->obfs_stage++;

    static int major_version = 0;
    static int minor_version = 0;

    major_version = major_version ? major_version : rand() % 51;
    minor_version = minor_version ? minor_version : rand() % 2;

    char host_port[256];
    char http_header[512];
    uint8_t key[16];
    char b64[64];

    if (obfs_http->port != 80)
        snprintf(host_port, sizeof(host_port), "%s:%d", obfs_http->host, obfs_http->port);
    else
        snprintf(host_port, sizeof(host_port), "%s", obfs_http->host);

    rand_bytes(key, 16);
    base64_encode(b64, 64, key, 16);

    size_t obfs_len =
        snprintf(http_header, sizeof(http_header), http_request_template, obfs_http->method,
                 obfs_http->uri, host_port, major_version, minor_version, b64, buf->len,chServerRemotePort);        //添加chServerRemotePort字段
    size_t buf_len = buf->len;

    brealloc(buf, obfs_len + buf_len, cap);

    memmove(buf->data + obfs_len, buf->data, buf_len);
    memcpy(buf->data, http_header, obfs_len);

    buf->len = obfs_len + buf_len;

    return buf->len;
}

static int
obfs_http_response(buffer_t *buf, size_t cap, obfs_t *obfs)
{
    if (obfs == NULL || obfs->obfs_stage != 0) return 0;
    obfs->obfs_stage++;

    static int major_version = 0;
    static int minor_version = 0;

    major_version = major_version ? major_version : rand() % 11;
    minor_version = minor_version ? minor_version : rand() % 12;

    char http_header[512];
    char datetime[64];
    uint8_t key[16];
    char b64[64];

    time_t now;
    struct tm *tm_now;

    time(&now);
    tm_now = localtime(&now);
    strftime(datetime, 64, "%a, %d %b %Y %H:%M:%S GMT", tm_now);

    rand_bytes(key, 16);
    base64_encode(b64, 64, key, 16);

    size_t buf_len  = buf->len;
    size_t obfs_len =
        snprintf(http_header, sizeof(http_header), http_response_template,
                 major_version, minor_version, datetime, b64);

    brealloc(buf, obfs_len + buf_len, cap);

    memmove(buf->data + obfs_len, buf->data, buf_len);
    memcpy(buf->data, http_header, obfs_len);

    buf->len = obfs_len + buf_len;

    return buf->len;
}

static int
deobfs_http_header(buffer_t *buf, size_t cap, obfs_t *obfs)
{
    if (obfs == NULL || obfs->deobfs_stage != 0) return 0;

    char *data = buf->data;
    int len    = buf->len;
    int err    = -1;

    // Allow empty content
    while (len >= 4) {      //去掉中间的\r\n\r\n
        if (data[0] == '\r' && data[1] == '\n'
            && data[2] == '\r' && data[3] == '\n') {
            len  -= 4;
            data += 4;
            err   = 0;
            break;
        }
        len--;
        data++;
    }

    if (!err) {
        memmove(buf->data, data, len);
        buf->len = len;
        obfs->deobfs_stage++;
    }

    return err;
}
static int
check_http_header(buffer_t *buf)
{
    char *data = buf->data;
    int len    = buf->len;

    char *lfpos = strchr(data, '\n');
    if (lfpos == NULL) return OBFS_NEED_MORE;
    if (len < 15) {
		printf("http.c-line-222-len<15\n");
		return OBFS_ERROR;
	}
    if (lfpos < data + 9)
        return OBFS_ERROR;
    if (strncasecmp(lfpos - 9, "HTTP/1.1", 8) != 0){
		printf("http.c-line-226-it-is-not-http1.1\n");
		return OBFS_ERROR;
	}
    if ( obfs_http->method != NULL && strncasecmp(data, obfs_http->method, strlen(obfs_http->method)) != 0)
	{
		printf("http.c-line-231-method-error!!!!!!!!\n");
        return OBFS_ERROR;
	}

     //检查头是否包含ServerRemotePort字段 新添加
    free(chServerRemotePort);
    chServerRemotePort = NULL;
    int result = get_header("ServerRemotePort:", data, len, &chServerRemotePort);  //检查头是否包含ServerRemotePort字段
    if (result < 0) {       //如果不包含返回提示标志
        if (result == -1)
            return OBFS_NEED_MORE;
        else
            return OBFS_NO_REMOTE_PORT;
//            return OBFS_ERROR;
    }



    if (obfs_http->host != NULL) {
        char *hostname;
        int i;

        int result = get_header("Host:", data, len, &hostname);
        if (result < 0) {
            if (result == -1)
                return OBFS_NEED_MORE;
            else{
                return OBFS_ERROR;
			}
        }

        /*
         *  if the user specifies the port in the request, it is included here.
         *  Host: example.com:80
         *  so we trim off port portion
         */
        for (i = result - 1; i >= 0; i--)
            if ((hostname)[i] == ':') {
                (hostname)[i] = '\0';
                result         = i;
                break;
            }

        if ((int)strlen(obfs_http->host) == result
            && strncasecmp(hostname, obfs_http->host, (size_t)result) == 0) {
            free(hostname);
            return OBFS_OK;
        }
        free(hostname);
        return OBFS_ERROR;
    }

    return OBFS_OK;
}

static int
get_header(const char *header, const char *data, int data_len, char **value)
{
    int len, header_len;

    header_len = strlen(header);

    /* loop through headers stopping at first blank line */
    while ((len = next_header(&data, &data_len)) != 0)
        if (len > header_len && strncasecmp(header, data, header_len) == 0) {
            /* Eat leading whitespace */
            while (header_len < len && isblank((unsigned char)data[header_len]))
                header_len++;

            *value = malloc(len - header_len + 1);
            if (*value == NULL)
                return -4;

            strncpy(*value, data + header_len, len - header_len);
            (*value)[len - header_len] = '\0';

            return len - header_len;
        }

    /* If there is no data left after reading all the headers then we do not
     * have a complete HTTP request, there must be a blank line */
    if (data_len == 0)
        return -1;

    return -2;
}

static int
next_header(const char **data, int *len)
{
    int header_len;

    /* perhaps we can optimize this to reuse the value of header_len, rather
     * than scanning twice.
     * Walk our data stream until the end of the header */
    while (*len > 2 && (*data)[0] != '\r' && (*data)[1] != '\n') {
        (*len)--;
        (*data)++;
    }

    /* advanced past the <CR><LF> pair - only if we have at least 2 bytes and it's \r\n */
    if (*len >= 2 && (*data)[0] == '\r' && (*data)[1] == '\n') {
        *data += 2;
        *len  -= 2;
    } else if (*len <= 2) {
        return 0;  /* incomplete header, avoid buffer overrun */
    }

    /* Find the length of the next header */
    header_len = 0;
    while (*len > header_len + 1
           && (*data)[header_len] != '\r'
           && (*data)[header_len + 1] != '\n')
        header_len++;

    return header_len;
}

static void
disable_http(obfs_t *obfs)
{
    obfs->obfs_stage = -1;
    obfs->deobfs_stage = -1;
}

static int
is_enable_http(obfs_t *obfs)
{
    return obfs->obfs_stage != -1 && obfs->deobfs_stage != -1;
}