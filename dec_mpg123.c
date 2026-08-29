/*
 *			GPAC - Multimedia Framework C SDK
 *
 *  This file is part of GPAC / MP3 audio decoder filter
 *  based on mpg123 (https://mpg123.de/)
 *
 */

#include <gpac/filters.h>
#include <string.h>
#include <stdlib.h>

#include <mpg123.h>

typedef struct
{
	GF_FilterPid *ipid, *opid;

	Bool is_playing;
	u32 sample_rate, num_channels;
	Bool lib_inited;
} GF_Mpg123DecCtx;

static GF_Err mpg123dec_configure_pid(GF_Filter *filter, GF_FilterPid *pid, Bool is_remove)
{
	const GF_PropertyValue *prop;
	GF_Mpg123DecCtx *ctx = (GF_Mpg123DecCtx *)gf_filter_get_udta(filter);

	if (is_remove)
	{
		if (ctx->opid)
		{
			gf_filter_pid_remove(ctx->opid);
			ctx->opid = NULL;
		}
		ctx->ipid = NULL;
		return GF_OK;
	}
	if (!gf_filter_pid_check_caps(pid))
		return GF_NOT_SUPPORTED;

	prop = gf_filter_pid_get_property(pid, GF_PROP_PID_CODECID);
	if (!prop)
		return GF_NOT_SUPPORTED;
	ctx->ipid = pid;

	if (!ctx->opid)
	{
		ctx->opid = gf_filter_pid_new(filter);
	}

	if (!ctx->lib_inited)
	{
		mpg123_init();
		ctx->lib_inited = GF_TRUE;
	}

	gf_filter_pid_copy_properties(ctx->opid, ctx->ipid);
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_CODECID, &PROP_UINT(GF_CODECID_RAW));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_AUDIO_FORMAT, &PROP_UINT(GF_AUDIO_FMT_S16));

	return GF_OK;
}

static GF_Err mpg123dec_process(GF_Filter *filter)
{
	GF_FilterPacket *pck, *dst_pck;
	u8 *data, *output;
	u32 size;
	mpg123_handle *mh;
	int err, ret;
	long rate = 0;
	int channels = 0, encoding = 0;
	u8 *out_buf = NULL;
	u32 out_alloc = 0, out_pos = 0;
	u8 *chunk;
	const u32 CHUNK_SIZE = 65536;
	size_t done;
	Bool got_format = GF_FALSE;
	GF_Mpg123DecCtx *ctx = (GF_Mpg123DecCtx *)gf_filter_get_udta(filter);

	pck = gf_filter_pid_get_packet(ctx->ipid);
	if (!pck)
	{
		if (gf_filter_pid_is_eos(ctx->ipid))
		{
			gf_filter_pid_set_eos(ctx->opid);
			return GF_EOS;
		}
		return GF_OK;
	}
	data = (u8 *)gf_filter_pck_get_data(pck, &size);

	if (!data)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_IO_ERR;
	}

	chunk = (u8 *)gf_malloc(CHUNK_SIZE);
	if (!chunk)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_OUT_OF_MEM;
	}

	mh = mpg123_new(NULL, &err);
	if (!mh)
	{
		gf_free(chunk);
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_NOT_SUPPORTED;
	}
	if (mpg123_open_feed(mh) != MPG123_OK)
	{
		mpg123_delete(mh);
		gf_free(chunk);
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_NOT_SUPPORTED;
	}

	ret = mpg123_decode(mh, data, size, chunk, CHUNK_SIZE, &done);
	while (ret != MPG123_ERR && ret != MPG123_NEED_MORE)
	{
		if (ret == MPG123_NEW_FORMAT)
		{
			mpg123_getformat(mh, &rate, &channels, &encoding);
			got_format = GF_TRUE;
		}
		if (done)
		{
			if (out_pos + done > out_alloc)
			{
				out_alloc = (out_alloc ? out_alloc * 2 : 65536);
				if (out_alloc < out_pos + done) out_alloc = out_pos + (u32)done;
				out_buf = (u8 *)gf_realloc(out_buf, out_alloc);
			}
			memcpy(out_buf + out_pos, chunk, done);
			out_pos += (u32)done;
		}
		ret = mpg123_decode(mh, NULL, 0, chunk, CHUNK_SIZE, &done);
	}

	if (!got_format)
	{
		mpg123_getformat(mh, &rate, &channels, &encoding);
	}

	mpg123_close(mh);
	mpg123_delete(mh);
	gf_free(chunk);

	if (!out_pos || !rate || !channels || (encoding != MPG123_ENC_SIGNED_16))
	{
		if (out_buf) gf_free(out_buf);
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_NON_COMPLIANT_BITSTREAM;
	}

	ctx->sample_rate = (u32)rate;
	ctx->num_channels = (u32)channels;

	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_SAMPLE_RATE, &PROP_UINT(ctx->sample_rate));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_NUM_CHANNELS, &PROP_UINT(ctx->num_channels));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_CHANNEL_LAYOUT, &PROP_LONGUINT((ctx->num_channels == 1) ? GF_AUDIO_CH_FRONT_CENTER : GF_AUDIO_CH_FRONT_LEFT | GF_AUDIO_CH_FRONT_RIGHT));

	dst_pck = gf_filter_pck_new_alloc(ctx->opid, out_pos, &output);
	if (!dst_pck)
	{
		gf_free(out_buf);
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_OUT_OF_MEM;
	}
	memcpy(output, out_buf, out_pos);
	gf_free(out_buf);

	gf_filter_pck_merge_properties(pck, dst_pck);
	gf_filter_pck_set_dependency_flags(dst_pck, 0);
	gf_filter_pck_send(dst_pck);

	gf_filter_pid_drop_packet(ctx->ipid);
	return GF_EOS;
}

static const GF_FilterCapability Mpg123DecCaps[] =
	{
		CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_AUDIO),
		CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_CODECID, GF_CODECID_MPEG_AUDIO),
		CAP_BOOL(GF_CAPS_INPUT_EXCLUDED, GF_PROP_PID_UNFRAMED, GF_TRUE),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_AUDIO),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_CODECID, GF_CODECID_RAW),
};

GF_FilterRegister Mpg123DecoderRegister = {
	.name = "mpg123dec",
	GF_FS_SET_DESCRIPTION("MP3 decoder")
		GF_FS_SET_HELP("This filter decodes MPEG-1/2 audio using mpg123.")
			.private_size = sizeof(GF_Mpg123DecCtx),
	SETCAPS(Mpg123DecCaps),
	.configure_pid = mpg123dec_configure_pid,
	.process = mpg123dec_process,
};

const GF_FilterRegister * EMSCRIPTEN_KEEPALIVE dynCall_mpg123dec_register(GF_FilterSession *session)
{
	return &Mpg123DecoderRegister;
}

#include "filter_register.h"
__attribute__((constructor))
void register_mpg123dec(void) {
    gf_filter_auto_register("mpg123dec", dynCall_mpg123dec_register);
}
