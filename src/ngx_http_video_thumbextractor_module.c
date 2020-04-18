/*
 * Copyright (C) 2011 Wandenberg Peixoto <wandenberg@gmail.com>
 *
 * This file is part of Nginx Video Thumb Extractor Module.
 *
 * Nginx Video Thumb Extractor Module is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Nginx Video Thumb Extractor Module is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Nginx Video Thumb Extractor Module.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * ngx_http_video_thumbextractor_module.c
 *
 * Created:  Nov 22, 2011
 * Author:   Wandenberg Peixoto <wandenberg@gmail.com>
 *
 */
#include <ngx_http_video_thumbextractor_module.h>
#include <ngx_http_video_thumbextractor_module_setup.c>
#include <ngx_http_video_thumbextractor_module_utils.c>
#include <ngx_http_video_thumbextractor_module_ipc.c>

ngx_int_t ngx_http_video_thumbextractor_extract_and_send_thumb(ngx_http_request_t *r);
ngx_int_t ngx_http_video_thumbextractor_set_request_context(ngx_http_request_t *r);
void      ngx_http_video_thumbextractor_cleanup_request_context(ngx_http_request_t *r);


ngx_int_t
ngx_http_video_thumbextractor_extract_and_send_thumb(ngx_http_request_t *r)
{
    ngx_http_video_thumbextractor_ctx_t       *ctx;

    ctx = ngx_http_get_module_ctx(r, ngx_http_video_thumbextractor_module);

    r->main->count++;

    ngx_queue_insert_tail(ngx_http_video_thumbextractor_module_extract_queue, &ctx->queue);

    ngx_http_video_thumbextractor_module_ensure_extractor_process();

    return NGX_DONE;
}


ngx_int_t
ngx_http_video_thumbextractor_set_request_context(ngx_http_request_t *r)
{
    ngx_http_video_thumbextractor_loc_conf_t    *vtlcf;
    ngx_http_video_thumbextractor_ctx_t         *ctx;
    ngx_http_video_thumbextractor_thumb_ctx_t   *thumb_ctx;
    ngx_pool_cleanup_t                          *cln;
    ngx_str_t                                    vv_filename = ngx_null_string, vv_second = ngx_null_string;
    ngx_str_t                                    vv_value = ngx_null_string;

    vtlcf = ngx_http_get_module_loc_conf(r, ngx_http_video_thumbextractor_module);

    ctx = ngx_http_get_module_ctx(r, ngx_http_video_thumbextractor_module);

    if (ctx != NULL) {
        return NGX_OK;
    }

    if ((cln = ngx_pool_cleanup_add(r->pool, 0)) == NULL) {
        ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0, "video thumb extractor module: unable to allocate memory for cleanup");
        return NGX_ERROR;
    }

    // set a cleaner to request
    cln->handler = (ngx_pool_cleanup_pt) ngx_http_video_thumbextractor_cleanup_request_context;
    cln->data = r;

    if ((ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_video_thumbextractor_ctx_t))) == NULL) {
        return NGX_ERROR;
    }

    ngx_http_set_ctx(r, ctx, ngx_http_video_thumbextractor_module);

    ctx->slot = -1;
    ctx->request = r;
    ngx_queue_init(&ctx->queue);

    thumb_ctx = &ctx->thumb_ctx;
    thumb_ctx->file_info.offset = 0;

    // check if received a filename
    ngx_http_complex_value(r, vtlcf->video_filename, &vv_filename);
    NGX_HTTP_VIDEO_THUMBEXTRACTOR_VARIABLE_REQUIRED(vv_filename, r->connection->log, "filename variable is empty");

    ngx_http_complex_value(r, vtlcf->video_second, &vv_second);
    NGX_HTTP_VIDEO_THUMBEXTRACTOR_VARIABLE_REQUIRED(vv_second, r->connection->log, "second variable is empty");

    thumb_ctx->second = ngx_atoi(vv_second.data, vv_second.len);
    if (thumb_ctx->second == NGX_ERROR) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "video thumb extractor module: Invalid second %V", &vv_second);
        return NGX_HTTP_BAD_REQUEST;
    }

    NGX_HTTP_VIDEO_THUMBEXTRACTOR_PARSE_VARIABLE_VALUE_INT(vtlcf->image_width, vv_value, thumb_ctx->width, 0);
    NGX_HTTP_VIDEO_THUMBEXTRACTOR_PARSE_VARIABLE_VALUE_INT(vtlcf->image_height, vv_value, thumb_ctx->height, 0);
    NGX_HTTP_VIDEO_THUMBEXTRACTOR_PARSE_VARIABLE_VALUE_INT(vtlcf->tile_sample_interval, vv_value, thumb_ctx->tile_sample_interval, 5);
    NGX_HTTP_VIDEO_THUMBEXTRACTOR_PARSE_VARIABLE_VALUE_INT(vtlcf->tile_rows, vv_value, thumb_ctx->tile_rows, NGX_CONF_UNSET);
    NGX_HTTP_VIDEO_THUMBEXTRACTOR_PARSE_VARIABLE_VALUE_INT(vtlcf->tile_max_rows, vv_value, thumb_ctx->tile_max_rows, NGX_CONF_UNSET);
    NGX_HTTP_VIDEO_THUMBEXTRACTOR_PARSE_VARIABLE_VALUE_INT(vtlcf->tile_cols, vv_value, thumb_ctx->tile_cols, NGX_CONF_UNSET);
    NGX_HTTP_VIDEO_THUMBEXTRACTOR_PARSE_VARIABLE_VALUE_INT(vtlcf->tile_max_cols, vv_value, thumb_ctx->tile_max_cols, NGX_CONF_UNSET);
    NGX_HTTP_VIDEO_THUMBEXTRACTOR_PARSE_VARIABLE_VALUE_INT(vtlcf->tile_margin, vv_value, thumb_ctx->tile_margin, 0);
    NGX_HTTP_VIDEO_THUMBEXTRACTOR_PARSE_VARIABLE_VALUE_INT(vtlcf->tile_padding, vv_value, thumb_ctx->tile_padding, 0);
    thumb_ctx->tile_color = vtlcf->tile_color;

    if (((thumb_ctx->width > 0) && (thumb_ctx->width < 16)) || ((thumb_ctx->height > 0) && (thumb_ctx->height < 16))) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "video thumb extractor module: Very small size requested, %d x %d", thumb_ctx->width, thumb_ctx->height);
        return NGX_HTTP_BAD_REQUEST;
    }

    if ((thumb_ctx->filename.data = ngx_pcalloc(r->pool, vv_filename.len + 1)) == NULL) {
        ngx_log_error(NGX_LOG_CRIT, r->connection->log, 0, "video thumb extractor module: unable to allocate memory to store full filename");
        return NGX_ERROR;
    }
    ngx_memcpy(thumb_ctx->filename.data, vv_filename.data, vv_filename.len);
    thumb_ctx->filename.len = vv_filename.len;
    thumb_ctx->filename.data[thumb_ctx->filename.len] = '\0';

    return NGX_OK;
}


void
ngx_http_video_thumbextractor_cleanup_request_context(ngx_http_request_t *r)
{
    ngx_http_video_thumbextractor_ctx_t       *ctx = ngx_http_get_module_ctx(r, ngx_http_video_thumbextractor_module);

    r->read_event_handler = ngx_http_request_empty_handler;

    if (ctx != NULL) {
        if (ctx->slot >= 0) {
            ngx_http_video_thumbextractor_module_ipc_ctxs[ctx->slot].request = NULL;
        }

        if (!ngx_queue_empty(&ctx->queue)) {
            ngx_queue_remove(&ctx->queue);
            ngx_queue_init(&ctx->queue);
        }

        ngx_http_set_ctx(r, NULL, ngx_http_video_thumbextractor_module);
    }

    ngx_http_video_thumbextractor_module_ensure_extractor_process();
}


static ngx_int_t
ngx_http_video_thumbextractor_handler(ngx_http_request_t *r)
{
    ngx_int_t                                 rc;

    if ((rc = ngx_http_video_thumbextractor_set_request_context(r)) != NGX_OK) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "video thumb extractor module: unable to setup context");
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    ngx_http_clear_content_length(r);
    ngx_http_clear_accept_ranges(r);
    ngx_http_clear_last_modified(r);

    return ngx_http_video_thumbextractor_extract_and_send_thumb(r);
}
