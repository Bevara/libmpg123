/*
 *			GPAC - Multimedia Framework C SDK
 *
 *  This file is part of GPAC / MP3 audio reframer filter (mpg123 decode path)
 *  based on mpg123 (https://mpg123.de/)
 *
 */

#include <gpac/filters.h>
#include <string.h>

typedef struct
{
	GF_FilterPid *ipid;
	GF_FilterPid *opid;
	u32 src_timescale;
	Bool owns_timescale;
	u32 codec_id;

	Bool initial_play_done;
	Bool is_playing;
} GF_ReframeMpg123Ctx;

static GF_Err rfmpg123_configure_pid(GF_Filter *filter, GF_FilterPid *pid, Bool is_remove)
{
	GF_ReframeMpg123Ctx *ctx = gf_filter_get_udta(filter);
	const GF_PropertyValue *p;

	if (is_remove)
	{
		ctx->ipid = NULL;
		return GF_OK;
	}

	if (!gf_filter_pid_check_caps(pid))
		return GF_NOT_SUPPORTED;

	gf_filter_pid_set_framing_mode(pid, GF_TRUE);
	ctx->ipid = pid;
	ctx->codec_id = 0;

	p = gf_filter_pid_get_property(pid, GF_PROP_PID_TIMESCALE);
	if (p)
		ctx->src_timescale = p->value.uint;

	if (ctx->src_timescale && !ctx->opid)
	{
		ctx->opid = gf_filter_pid_new(filter);
		gf_filter_pid_copy_properties(ctx->opid, ctx->ipid);
		gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_UNFRAMED, NULL);
	}
	ctx->is_playing = GF_TRUE;
	return GF_OK;
}

static Bool rfmpg123_process_event(GF_Filter *filter, const GF_FilterEvent *evt)
{
	GF_FilterEvent fevt;
	GF_ReframeMpg123Ctx *ctx = gf_filter_get_udta(filter);
	if (evt->base.on_pid != ctx->opid)
		return GF_TRUE;
	switch (evt->base.type)
	{
	case GF_FEVT_PLAY:
		if (ctx->is_playing)
			return GF_TRUE;
		ctx->is_playing = GF_TRUE;
		if (!ctx->initial_play_done)
		{
			ctx->initial_play_done = GF_TRUE;
			return GF_TRUE;
		}
		GF_FEVT_INIT(fevt, GF_FEVT_SOURCE_SEEK, ctx->ipid);
		fevt.seek.start_offset = 0;
		gf_filter_pid_send_event(ctx->ipid, &fevt);
		return GF_TRUE;
	case GF_FEVT_STOP:
		ctx->is_playing = GF_FALSE;
		return GF_FALSE;
	default:
		break;
	}
	return GF_TRUE;
}

static GF_Err rfmpg123_process(GF_Filter *filter)
{
	GF_ReframeMpg123Ctx *ctx = gf_filter_get_udta(filter);
	GF_FilterPacket *pck, *dst_pck;
	GF_Err e;
	u8 *data;
	u32 size;

	pck = gf_filter_pid_get_packet(ctx->ipid);
	if (!pck)
	{
		if (gf_filter_pid_is_eos(ctx->ipid))
		{
			if (ctx->opid)
				gf_filter_pid_set_eos(ctx->opid);
			ctx->is_playing = GF_FALSE;
			return GF_EOS;
		}
		return GF_OK;
	}
	data = (u8 *)gf_filter_pck_get_data(pck, &size);

	if (!ctx->opid || !ctx->codec_id)
	{
		Bool has_id3 = (size >= 3) && !memcmp(data, "ID3", 3);
		Bool has_sync = (size >= 2) && (data[0] == 0xFF) && ((data[1] & 0xE0) == 0xE0);
		if (!has_id3 && !has_sync)
		{
			gf_filter_pid_drop_packet(ctx->ipid);
			return GF_NON_COMPLIANT_BITSTREAM;
		}

		ctx->codec_id = GF_CODECID_MPEG_AUDIO;
		ctx->opid = gf_filter_pid_new(filter);
		if (!ctx->opid)
		{
			gf_filter_pid_drop_packet(ctx->ipid);
			return GF_SERVICE_ERROR;
		}

		gf_filter_pid_copy_properties(ctx->opid, ctx->ipid);
		gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_STREAM_TYPE, &PROP_UINT(GF_STREAM_AUDIO));
		gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_CODECID, &PROP_UINT(ctx->codec_id));

		if (!gf_filter_pid_get_property(ctx->ipid, GF_PROP_PID_TIMESCALE))
		{
			gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_TIMESCALE, &PROP_UINT(1000));
			ctx->owns_timescale = GF_TRUE;
		}

		gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_NB_FRAMES, &PROP_UINT(1));
		gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PLAYBACK_MODE, &PROP_UINT(GF_PLAYBACK_MODE_FASTFORWARD));
	}

	e = GF_OK;

	dst_pck = gf_filter_pck_new_ref(ctx->opid, 0, size, pck);
	if (!dst_pck)
		return GF_OUT_OF_MEM;

	gf_filter_pck_merge_properties(pck, dst_pck);
	if (ctx->owns_timescale)
	{
		gf_filter_pck_set_cts(dst_pck, 0);
		gf_filter_pck_set_sap(dst_pck, GF_FILTER_SAP_1);
		gf_filter_pck_set_duration(dst_pck, 1000);
	}

	gf_filter_pck_send(dst_pck);
	gf_filter_pid_drop_packet(ctx->ipid);

	return e;
}

static const char *rfmpg123_probe_data(const u8 *data, u32 size, GF_FilterProbeScore *score)
{
	if (size < 3)
		return NULL;
	if (!memcmp(data, "ID3", 3))
	{
		*score = GF_FPROBE_MAYBE_SUPPORTED;
		return "audio/mp3";
	}
	if ((data[0] == 0xFF) && ((data[1] & 0xE0) == 0xE0))
	{
		*score = GF_FPROBE_MAYBE_SUPPORTED;
		return "audio/mp3";
	}
	return NULL;
}

static const GF_FilterCapability ReframeMpg123Caps[] =
	{
		CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_FILE),
		CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_FILE_EXT, "mp3|mp2|mp1"),
		CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_MIME, "audio/mp3|audio/x-mp3"),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_AUDIO),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_CODECID, GF_CODECID_MPEG_AUDIO),
};

GF_FilterRegister ReframeMpg123Register = {
	.name = "rfmpg123",
	GF_FS_SET_DESCRIPTION("MP3 audio reframer (mpg123 decode path)")
		GF_FS_SET_HELP("This filter parses MPEG-1/2 audio files/data as a whole and outputs a single audio PID/frame, meant to be decoded by the mpg123dec filter.\n")
			.private_size = sizeof(GF_ReframeMpg123Ctx),
	SETCAPS(ReframeMpg123Caps),
	.configure_pid = rfmpg123_configure_pid,
	.probe_data = rfmpg123_probe_data,
	.process = rfmpg123_process,
	.process_event = rfmpg123_process_event};

const GF_FilterRegister * EMSCRIPTEN_KEEPALIVE dynCall_mpg123_reframe_register(GF_FilterSession *session)
{
	return &ReframeMpg123Register;
}

#include "filter_register.h"
__attribute__((constructor))
void register_mpg123_reframe(void) {
    gf_filter_auto_register("mpg123_reframe", dynCall_mpg123_reframe_register);
}
