/*
 * PCM parser for Ogg
 * Copyright (c) 2013 James Almer
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"
#include "oggdec.h"

struct oggpcm_private {
    int vorbis_comment;
    uint32_t extra_headers;
};

static const struct ogg_pcm_codec {
    uint32_t codec_id;
    uint32_t format_id;
} ogg_pcm_codecs[] = {
    { AV_CODEC_ID_PCM_S8,    0x00 },
    { AV_CODEC_ID_PCM_U8,    0x01 },
    { AV_CODEC_ID_PCM_S16LE, 0x02 },
    { AV_CODEC_ID_PCM_S16BE, 0x03 },
    { AV_CODEC_ID_PCM_S24LE, 0x04 },
    { AV_CODEC_ID_PCM_S24BE, 0x05 },
    { AV_CODEC_ID_PCM_S32LE, 0x06 },
    { AV_CODEC_ID_PCM_S32BE, 0x07 },
    { AV_CODEC_ID_PCM_F32LE, 0x20 },
    { AV_CODEC_ID_PCM_F32BE, 0x21 },
    { AV_CODEC_ID_PCM_F64LE, 0x22 },
    { AV_CODEC_ID_PCM_F64BE, 0x23 },
};

static const struct ogg_pcm_codec *ogg_get_pcm_codec_id(uint32_t format_id)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(ogg_pcm_codecs); i++)
        if (ogg_pcm_codecs[i].format_id == format_id)
            return &ogg_pcm_codecs[i];

    return NULL;
}

static int pcm_header(AVFormatContext *as, int idx)
{
    struct ogg *ogg = as->priv_data;
    struct ogg_stream *os = ogg->streams + idx;
    struct oggpcm_private *priv = os->private;
    const struct ogg_pcm_codec *pcm;
    AVStream *st = as->streams[idx];
    uint8_t *p = os->buf + os->pstart;
    uint16_t major, minor;
    uint32_t format_id;

    if (!priv) {
        priv = os->private = av_mallocz(sizeof(*priv));
        if (!priv)
            return AVERROR(ENOMEM);
    }

    if (os->flags & OGG_FLAG_BOS) {
        if (os->psize < 28) {
            av_log(as, AV_LOG_ERROR, "Invalid OggPCM header packet");
            return AVERROR_INVALIDDATA;
        }

        major = AV_RB16(p + 8);
        minor = AV_RB16(p + 10);
        if (major) {
            av_log(as, AV_LOG_ERROR, "Unsupported OggPCM version %u.%u\n", major, minor);
            return AVERROR_INVALIDDATA;
        }

        format_id = AV_RB32(p + 12);
        pcm = ogg_get_pcm_codec_id(format_id);
        if (!pcm) {
            av_log(as, AV_LOG_ERROR, "Unsupported PCM format ID 0x%X\n", format_id);
            return AVERROR_INVALIDDATA;
        }

        st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
        st->codecpar->codec_id    = pcm->codec_id;
        st->codecpar->sample_rate = AV_RB32(p + 16);
        st->codecpar->channels    = AV_RB8 (p + 21);
        priv->extra_headers    = AV_RB32(p + 24);
        priv->vorbis_comment   = 1;
        avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
    } else if (priv->vorbis_comment) {
        ff_vorbis_stream_comment(as, st, p, os->psize);
        priv->vorbis_comment   = 0;
    } else if (priv->extra_headers) {
        // TODO: Support for channel mapping and conversion headers.
        priv->extra_headers--;
    } else
        return 0;

    return 1;
}

const struct ogg_codec ff_pcm_codec = {
    .name      = "OggPCM",
    .magic     = "PCM     ",
    .magicsize = 8,
    .header    = pcm_header,
    .nb_header = 2,
};
