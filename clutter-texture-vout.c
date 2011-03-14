/*****************************************************************************
 * clutter-texture-vout.c : video output into ckutter-toolkit texture with sar
 *****************************************************************************
 * Copyright (C) 2011
 * $Id$
 *
 * Authors: Dainius Ridzevicius <ridzevicius@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/

//#define DOMAIN  "vlc-myplugin"
//#define _(str)  dgettext(DOMAIN, str)
//#define N_(str) (str)

#include <assert.h>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_vout_display.h>
#include <vlc_picture_pool.h>
#include <vlc_fs.h>

#include <clutter/clutter.h>
#include <cogl/cogl.h>

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
#define TEXTURE_TEXT N_("clutter texture")
#define TEXTURE_LONGTEXT N_("pointer to clutter texture to write RGB data to")

#define NUM_TEXT N_("SAR num")
#define NUM_LONGTEXT N_("pointer to output SAR num")

#define DEN_TEXT N_("SAR den")
#define DEN_LONGTEXT N_("pointer to output SAR den")

//#define YUV4MPEG2_TEXT N_("YUV4MPEG2 header (default disabled)")
//#define YUV4MPEG2_LONGTEXT N_("The YUV4MPEG2 header is compatible " \
  //  "with mplayer yuv video ouput and requires YV12/I420 fourcc. By default "\
   // "vlc writes the fourcc of the picture frame into the output destination.")

//#define CFG_PREFIX "yuv-"
#define N_(s) (s)


static int  Open (vlc_object_t *);
static void Close(vlc_object_t *);

vlc_module_begin()
    set_shortname(N_("Clutter texture output"))
    set_description(N_("Clutter texture with sar video output"))
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VOUT)
    set_capability("vout display", 0)

    add_string("texture", NULL,
                TEXTURE_TEXT, TEXTURE_LONGTEXT, false)
    add_string("num", NULL,
                NUM_TEXT, NUM_LONGTEXT, true)
    add_string("den", NULL,
                DEN_TEXT, DEN_LONGTEXT, true)
//    add_bool  (CFG_PREFIX "yuv4mpeg2", false,
//                YUV4MPEG2_TEXT, YUV4MPEG2_LONGTEXT, true)

    set_callbacks(Open, Close)
vlc_module_end()

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
//static const char *const ppsz_vout_options[] = {
//    "file", "chroma", "yuv4mpeg2", NULL
//};

/* */
static picture_pool_t *Pool  (vout_display_t *, unsigned);
static void           Display(vout_display_t *, picture_t *, subpicture_t *subpicture);
static int            Control(vout_display_t *, int, va_list);

/*****************************************************************************
 * vout_display_sys_t: video output descriptor
 *****************************************************************************/
struct vout_display_sys_t{
  pthread_mutex_t mutex;
  bool destroy;

  guint ref_count;

  unsigned char* buffer;

  ClutterTexture* texture;
  unsigned int *num;
  unsigned int *den;

  picture_t *picture;

  guint texture_width;
  guint texture_height;
  guint texture_bpp;
  guint texture_rowstride;
  picture_pool_t *pool;
};

/* */
static int Open(vlc_object_t *object)
{


    vout_display_t *vd = (vout_display_t *)object;



    vout_display_sys_t *sys;

    /* Allocate instance and initialize some members */
    vd->sys = sys = malloc(sizeof(*sys));
    if (!sys)
        return VLC_ENOMEM;


    char *psz_texture = var_InheritString(vd, "texture");
    if (psz_texture == NULL) {
        msg_Err(vd, "clutter-texture is missing.");
        return VLC_EGENERIC;
    }
    sys->texture = (ClutterTexture *)(intptr_t)atoll(psz_texture);
    free(psz_texture);
    if (sys->texture == NULL)
    {
      msg_Err(vd, "clutter-texture is missing.");
      free(sys);
      return VLC_EGENERIC;
    }
    g_object_ref(sys->texture);

    char *psz_num = var_InheritString(vd, "num");
    if (psz_num == NULL) {
        msg_Err(vd, "pointer to SAR num is missing.");
        return VLC_EGENERIC;
    }
    sys->num = (unsigned int *)(intptr_t)atoll(psz_num);
    free(psz_num);

    char *psz_den = var_InheritString(vd, "den");
    sys->den = (unsigned int *)(intptr_t)atoll(psz_den);
    if (psz_den == NULL) {
        msg_Err(vd, "pointer to SAR den is missing.");
        return VLC_EGENERIC;
    }
    free(psz_den);

    if (sys->num == NULL || sys->den == NULL)
    {
      msg_Err(vd, "num/den is missing.");
      free(sys);
      return VLC_EGENERIC;
    }

    pthread_mutex_init(&sys->mutex, NULL);

    sys->pool = NULL;

    /* */

    const vlc_fourcc_t chroma = VLC_CODEC_RGB24;
    msg_Dbg(vd, "Using chroma %4.4s", (char *)&chroma);
    video_format_t fmt = vd->fmt;
    fmt.i_chroma = chroma;
    video_format_FixRgb(&fmt);

    /* */
    vout_display_info_t info = vd->info;
    info.has_hide_mouse = true;

    /* */
    vd->fmt     = fmt;
    vd->info    = info;
    vd->pool    = Pool;
    vd->prepare = NULL;
    vd->display = Display;
    vd->control = Control;
    vd->manage  = NULL;

    vout_display_SendEventFullscreen(vd, false);
    return VLC_SUCCESS;
}

/* */
static void Close(vlc_object_t *object)
{
    vout_display_t *vd = (vout_display_t *)object;
    vout_display_sys_t *sys = vd->sys;
    sys->destroy = TRUE;
    pthread_mutex_lock(&sys->mutex);
    g_object_unref(sys->texture);

    if (sys->pool)
        picture_pool_Delete(sys->pool);
    free(sys);
    //pthread_mutex_unlock(&sys->mutex);
    pthread_mutex_destroy(&sys->mutex);

}

/*****************************************************************************
 *
 *****************************************************************************/
static picture_pool_t *Pool(vout_display_t *vd, unsigned count)
{
    vout_display_sys_t *sys = vd->sys;
    if (!sys->pool)
        sys->pool = picture_pool_NewFromFormat(&vd->fmt, count);
    return sys->pool;
}

static
gboolean clutter_update_frame(gpointer data)
{
    vout_display_sys_t* sys = data;
    if (pthread_mutex_trylock(&sys->mutex) == 0){
    const plane_t *plane = &sys->picture->p[0];

    clutter_texture_set_from_rgb_data(sys->texture,
				    (const guchar *)plane->p_pixels,
				    FALSE,
				    plane->i_pitch/24,
				    plane->i_lines,
				    plane->i_pitch,
				    24,
				    CLUTTER_TEXTURE_RGB_FLAG_BGR,
				    NULL);
    picture_Release(sys->picture);
    pthread_mutex_unlock(&sys->mutex);
    }
    return FALSE;

}

static void Display(vout_display_t *vd, picture_t *picture, subpicture_t *subpicture)
{
    vout_display_sys_t *sys = vd->sys;

    /* */
    //video_format_t fmt = vd->fmt;
    *sys->num = vd->source.i_sar_num;
    *sys->den = vd->source.i_sar_den;

    /* */
    char type;
    if (picture->b_progressive)
        type = 'p';
    else if (picture->b_top_field_first)
        type = 't';
    else
        type = 'b';

    if (type != 'p') {
        msg_Warn(vd, "Received a non progressive frame, "
                     "it will be written as progressive.");
        type = 'p';
    }


    if (pthread_mutex_trylock(&sys->mutex) == 0) {

    sys->picture = picture;
    pthread_mutex_unlock(&sys->mutex);
    clutter_threads_add_idle_full(G_PRIORITY_HIGH_IDLE,
            clutter_update_frame,
            sys,
            NULL);
    }
    else {
    picture_Release(picture);
    }
    VLC_UNUSED(subpicture);
}

static int Control(vout_display_t *vd, int query, va_list args)
{
    VLC_UNUSED(vd);
    switch (query) {
    case VOUT_DISPLAY_CHANGE_FULLSCREEN: {
        const vout_display_cfg_t *cfg = va_arg(args, const vout_display_cfg_t *);
        if (cfg->is_fullscreen)
            return VLC_EGENERIC;
        return VLC_SUCCESS;
    }
    default:
        return VLC_EGENERIC;
    }
}


